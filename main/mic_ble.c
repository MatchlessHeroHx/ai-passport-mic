#include "mic_ble.h"
#include "mic_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/nimble_port.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <string.h>

/* Audio must never take the last host buffers: the Mac's control writes need one
   for their ATT response, or CoreBluetooth reports "Resources are insufficient".
   Pool 2 (larger blocks) is unused by these small packets, so subtract it. */
#define MIC_BLE_RESERVED_BLOCKS 6

/* The host task owns GAP; atomics publish its state to the audio task.
   Commands are idempotent, so a bitmask coalesces them and never fills up. */
static atomic_uint commands;
static atomic_uint connection = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint mtu = 23;
static atomic_bool subscribed, encrypted, lost;
static atomic_int passkey = -1;
static uint16_t audio_handle;
static uint8_t addr_type;
static uint16_t frame_id;
void ble_store_config_init(void);

static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0xF0,0xDE,0xBC,0x9A,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12);
static const ble_uuid128_t audio_uuid = BLE_UUID128_INIT(
    0xF1,0xDE,0xBC,0x9A,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12);
static const ble_uuid128_t control_uuid = BLE_UUID128_INIT(
    0xF2,0xDE,0xBC,0x9A,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12);

static int access_value(uint16_t conn, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, "FPM1", 4) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (!len || len > 8) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t data[8];
    if (os_mbuf_copydata(ctxt->om, 0, len, data)) return BLE_ATT_ERR_UNLIKELY;
    unsigned bits = 0;
    for (unsigned i = 0; i < len; ++i) {
        /* A reset supersedes everything written before it, so "PX" ends offline. */
        if (data[i] == 'X') bits = MIC_BLE_CMD_RESET;
        else if (data[i] == 'P') bits |= MIC_BLE_CMD_PING;
        else if (data[i] == 'A') bits |= MIC_BLE_CMD_ACK;
        else return BLE_ATT_ERR_UNLIKELY;
    }
    /* Only this host task writes; the app task only takes the whole mask. */
    if (bits & MIC_BLE_CMD_RESET) atomic_store(&commands, bits);
    else atomic_fetch_or(&commands, bits);
    return 0;
}

static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid = &audio_uuid.u, .access_cb = access_value,
         .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &audio_handle},
        {.uuid = &control_uuid.u, .access_cb = access_value,
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN |
                  BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN},
        {0}
    }
}, {0}};

static int gap_event(struct ble_gap_event *event, void *arg);
static int advertise(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) return rc;
    /* Name goes in scan response: flags + 128-bit UUID + name exceed 31 bytes. */
    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)MIC_BLE_DEVICE_NAME;
    response.name_len = strlen(MIC_BLE_DEVICE_NAME);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc) return rc;
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static void reset_link(void) {
    encrypted = false;
    subscribed = false;
    connection = BLE_HS_CONN_HANDLE_NONE;
    mtu = 23;
    passkey = -1;
    lost = true;
    commands = 0;
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            connection = event->connect.conn_handle;
            struct ble_gap_upd_params params = {
                .itvl_min = 12, .itvl_max = 24, .latency = 0, .supervision_timeout = 300
            };
            (void)ble_gap_update_params(event->connect.conn_handle, &params);
            (void)ble_gap_set_data_len(event->connect.conn_handle, 251, 2120);
        } else (void)advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        reset_link();
        (void)advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == audio_handle) {
            subscribed = event->subscribe.cur_notify != 0;
            if (!subscribed) lost = true;
        }
        break;
    case BLE_GAP_EVENT_MTU:
        mtu = event->mtu.value;
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;
        encrypted = event->enc_change.status == 0 &&
            ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
            desc.sec_state.encrypted && desc.sec_state.authenticated;
        passkey = -1;
        if (!encrypted) (void)ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP};
            io.passkey = 100000 + esp_random() % 900000;
            passkey = (int)io.passkey;
            (void)ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Do not silently discard an existing bond. Forget the device on Mac first. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: break;
    }
    return 0;
}

static void on_reset(int reason) { (void)reason; reset_link(); }
static void on_sync(void) {
    if (!ble_hs_util_ensure_addr(0) && !ble_hs_id_infer_auto(0, &addr_type)) (void)advertise();
}
static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    vTaskDelete(NULL);
}

esp_err_t mic_ble_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    if (ble_svc_gap_device_name_set(MIC_BLE_DEVICE_NAME) ||
        ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) {
        err = ESP_FAIL;
    } else if (xTaskCreate(host_task, "mic_ble", NIMBLE_HS_STACK_SIZE, NULL,
                           configMAX_PRIORITIES - 4, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    } else return ESP_OK;
    (void)nimble_port_deinit();
    return err;
}

static bool host_buffers_available(void) {
    return os_msys_num_free() >= CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT + MIC_BLE_RESERVED_BLOCKS;
}

esp_err_t mic_ble_send(const uint8_t *data, size_t size, bool droppable) {
    if (!mic_ble_connected()) return ESP_ERR_INVALID_STATE;
    unsigned conn = connection;
    size_t payload = mic_ble_payload_size(mtu);
    if (!payload || !data || !size || size > MIC_HEADER_SIZE + MIC_PAYLOAD_MAX) return ESP_ERR_INVALID_SIZE;
    /* Drop 20 ms of audio before its first fragment rather than queueing a backlog;
       once a frame has started it must finish, or the receiver sees a broken frame. */
    if (droppable && !host_buffers_available()) return ESP_ERR_NO_MEM;
    uint8_t chunk[244];
    uint16_t id = frame_id++;
    size_t total = (size + payload - 1) / payload;
    chunk[0] = 0xB5; chunk[1] = 0x1A;
    chunk[2] = id; chunk[3] = id >> 8;
    chunk[5] = total;
    /* Bounded retry for BLE congestion; never accumulate seconds of speech.
       Control frames (keys, state) get longer because losing one desyncs the Mac. */
    int64_t deadline = esp_timer_get_time() + (droppable ? 20000 : 150000);
    for (size_t offset = 0; offset < size;) {
        size_t part = size - offset;
        if (part > payload) part = payload;
        chunk[4] = offset / payload;
        memcpy(chunk + 6, data + offset, part);
        if (!mic_ble_connected() || connection != conn) return ESP_ERR_INVALID_STATE;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(chunk, 6 + part);
        int rc = om ? ble_gatts_notify_custom(conn, audio_handle, om) : BLE_HS_ENOMEM;
        /* notify_custom consumes om even on failure. */
        if (rc) {
            if ((rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY && rc != BLE_HS_EAGAIN) ||
                esp_timer_get_time() >= deadline) return ESP_FAIL;
            vTaskDelay(pdMS_TO_TICKS(1));
        } else offset += part;
    }
    return ESP_OK;
}

unsigned mic_ble_take_commands(void) { return atomic_exchange(&commands, 0); }
bool mic_ble_connected(void) { return encrypted && subscribed && connection != BLE_HS_CONN_HANDLE_NONE; }
bool mic_ble_lost(void) { return atomic_exchange(&lost, false); }
int mic_ble_passkey(void) { return passkey; }
