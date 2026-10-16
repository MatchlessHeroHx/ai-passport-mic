#include "mic_protocol.h"
#include "mic_ui.h"
#include "mic_ble.h"
#include "mic_adpcm.h"
#include "mic_screen.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

static QueueHandle_t buttons;
static mic_model_t model;
static bool ble_owner;
static uint32_t sequence;
static int16_t pcm[MIC_SAMPLES];
static uint8_t packet[MIC_HEADER_SIZE + MIC_PAYLOAD_MAX];
static mic_screen_t screen;

#define BACKLIGHT_ON 70
#define BACKLIGHT_DIM 5

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool send_frame(uint8_t type, const void *data, uint16_t size) {
    uint8_t compressed[MIC_ADPCM_BYTES];
    if (ble_owner && type == MIC_AUDIO) {
        mic_adpcm_encode(data, compressed);
        type = MIC_AUDIO_ADPCM;
        data = compressed;
        size = sizeof(compressed);
    }
    size_t count = mic_packet(packet, type, sequence++, data, size);
    if (ble_owner) {
        /* Audio and the periodic state report may be skipped under radio congestion:
           the consumed sequence number tells the Mac to fill 20 ms of silence, and the
           next HELLO repeats the state. Only a lost link takes the device offline. */
        bool droppable = type == MIC_AUDIO_ADPCM || type == MIC_HELLO;
        esp_err_t err = mic_ble_send(packet, count, droppable);
        return err == ESP_OK || (droppable && err != ESP_ERR_INVALID_STATE);
    }
    if (!usb_serial_jtag_is_connected()) return false;
    size_t sent = 0;
    while (sent < count) {
        int n = usb_serial_jtag_write_bytes(packet + sent, count - sent, pdMS_TO_TICKS(100));
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

/* Called with the LVGL lock held. Off means DISPOFF as well as no backlight, and
   no UI updates, so LVGL has nothing to redraw. Waking renders the current frame
   into the panel first, then turns the panel on, then the backlight. */
static void apply_screen(mic_screen_level_t level, mic_screen_level_t shown) {
    if (level == shown) return;
    if (level == MIC_SCREEN_OFF) {
        bsp_display_backlight(0);
        (void)bsp_display_set_on(false);
        return;
    }
    if (shown == MIC_SCREEN_OFF) {
        lv_refr_now(NULL);
        (void)bsp_display_set_on(true);
    }
    bsp_display_backlight(level == MIC_SCREEN_DIM ? BACKLIGHT_DIM : BACKLIGHT_ON);
}

static void key_event(bsp_btn_t key, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (event == BSP_BTN_PRESS) {
        uint8_t code = (uint8_t)key;
        xQueueSend(buttons, &code, 0);
    }
}

void app_main(void) {
    buttons = xQueueCreate(8, sizeof(uint8_t));
    ESP_ERROR_CHECK(buttons ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(bsp_lvgl_init() ? ESP_OK : ESP_ERR_NO_MEM);
    if (bsp_lvgl_lock(1000)) { mic_ui_create(); bsp_lvgl_unlock(); }
    bsp_display_backlight(BACKLIGHT_ON);
    bool audio_ok = bsp_audio_init() == ESP_OK;
    if (audio_ok) audio_ok = bsp_audio_set_format(16000, 16, 1) == ESP_OK;
    bsp_audio_set_volume(0);
    (void)bsp_battery_init();
    ESP_ERROR_CHECK(bsp_button_init(key_event, NULL));
    /* The serial stream belongs exclusively to the framed protocol. */
    esp_log_level_set("*", ESP_LOG_NONE);
    usb_serial_jtag_driver_config_t cfg = { .tx_buffer_size = 2048, .rx_buffer_size = 256 };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    ESP_ERROR_CHECK(mic_ble_init());
    uint32_t last_hello = 0, last_ui = 0, last_battery = 0;
    int battery = -1, ui_peak = 0;
    mic_screen_wake(&screen, now_ms());
    mic_screen_level_t shown_screen = MIC_SCREEN_ON;
    mic_state_t last_state = model.state;
    for (;;) {
        uint32_t now = now_ms();
        uint8_t commands[32];
        if (mic_ble_lost()) {
            if (ble_owner) model.state = MIC_OFFLINE;
            ble_owner = false;
        }
        if (mic_ble_connected() != ble_owner) {
            model.state = MIC_OFFLINE;
            ble_owner = mic_ble_connected();
        }
        int count = usb_serial_jtag_read_bytes(commands, sizeof(commands), 0);
        if (ble_owner) count = 0; /* Never mix two hosts' control commands. */
        for (int i = 0; i < count; ++i) {
            if (commands[i] == 'P') mic_ping(&model, now);
            if (commands[i] == 'A') (void)mic_ack(&model, now);
            if (commands[i] == 'X') model.state = MIC_OFFLINE;
        }
        unsigned ble_commands = mic_ble_take_commands();
        if (ble_owner) {
            if (ble_commands & MIC_BLE_CMD_RESET) model.state = MIC_OFFLINE;
            if (ble_commands & MIC_BLE_CMD_PING) mic_ping(&model, now);
            if (ble_commands & MIC_BLE_CMD_ACK) (void)mic_ack(&model, now);
        }
        if (mic_expire(&model, now)) (void)send_frame(MIC_STOP, NULL, 0);
        uint8_t pressed;
        while (xQueueReceive(buttons, &pressed, 0) == pdTRUE) {
            /* From off, OK lights the screen and starts dictation; UP/DOWN only light it. */
            bool act = mic_screen_key(&screen, now, pressed == BSP_BTN_OK);
            uint8_t event = 0;
            uint32_t duration = now - model.started;
            if (!act) event = 0;
            else if (pressed == BSP_BTN_OK) event = audio_ok ? mic_press(&model, now) : MIC_ERROR;
            else if (pressed == BSP_BTN_UP) event = mic_edit_action(&model, true);
            else if (pressed == BSP_BTN_DOWN) event = mic_edit_action(&model, false);
            bool sent = !event || send_frame(event, NULL, 0);
            if (!sent) model.state = MIC_OFFLINE;
            if (bsp_lvgl_lock(10)) {
                if (act && pressed == BSP_BTN_OK) mic_ui_press(now);
                if (sent && (event == MIC_UNDO || event == MIC_CONFIRM)) mic_ui_action(now, event);
                if (sent && event == MIC_STOP) mic_ui_finished(now, duration);
                mic_ui_update(&model, now, ui_peak, audio_ok, battery);
                apply_screen(screen.level, shown_screen);
                shown_screen = screen.level;
                bsp_lvgl_unlock();
            }
        }
        int peak = 0;
        /* Drain I2S continuously so a new recording cannot start with stale DMA audio.
           Idle PCM is discarded locally and never sent to the host. */
        if (audio_ok) {
            if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
                audio_ok = false;
                model.state = MIC_OFFLINE;
                (void)send_frame(MIC_ERROR, NULL, 0);
            } else if (model.state == MIC_RECORDING) {
                for (int i = 0; i < MIC_SAMPLES; ++i) {
                    int value = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
                    if (value > peak) peak = value;
                }
                if (!send_frame(MIC_AUDIO, pcm, sizeof(pcm))) model.state = MIC_OFFLINE;
            }
        } else vTaskDelay(pdMS_TO_TICKS(20));
        if ((uint32_t)(now - last_hello) >= 500) {
            uint8_t state = audio_ok ? model.state : 255;
            if (!send_frame(MIC_HELLO, &state, 1)) model.state = MIC_OFFLINE;
            last_hello = now;
        }
        if ((uint32_t)(now - last_battery) >= 5000) {
            battery = bsp_battery_soc();
            last_battery = now;
        }
        if (peak > ui_peak) ui_peak = peak;
        if ((uint32_t)(now - last_ui) >= 50 && bsp_lvgl_lock(10)) {
            /* Recording, starting and pairing hold the screen on; any state change
               (connect, disconnect, finish) lights it again. */
            if (model.state != last_state) mic_screen_wake(&screen, now);
            last_state = model.state;
            bool busy = model.state == MIC_STARTING || model.state == MIC_RECORDING ||
                        mic_ble_passkey() >= 0;
            mic_screen_level_t level = mic_screen_tick(&screen, now, busy);
            mic_ui_link(ble_owner, mic_ble_passkey());
            if (level != MIC_SCREEN_OFF) mic_ui_update(&model, now, ui_peak, audio_ok, battery);
            apply_screen(level, shown_screen);
            shown_screen = level;
            bsp_lvgl_unlock();
            ui_peak = 0;
            last_ui = now;
        }
    }
}
