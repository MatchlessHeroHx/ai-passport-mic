#include "mic_protocol.h"
#include <string.h>

uint32_t mic_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static void put32(uint8_t *p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

size_t mic_packet(uint8_t *out, uint8_t type, uint32_t sequence,
                  const void *payload, uint16_t size) {
    if (size > MIC_PAYLOAD_MAX) return 0;
    memcpy(out, "FPM1", 4);
    out[4] = type;
    out[5] = 0;
    out[6] = (uint8_t)size;
    out[7] = (uint8_t)(size >> 8);
    put32(out + 8, sequence);
    if (size) memcpy(out + MIC_HEADER_SIZE, payload, size);
    /* Protect metadata as well as PCM; CRC storage is excluded. */
    uint8_t checked[8 + MIC_PAYLOAD_MAX];
    memcpy(checked, out + 4, 8);
    if (size) memcpy(checked + 8, payload, size);
    put32(out + 12, mic_crc32(checked, 8 + size));
    return MIC_HEADER_SIZE + size;
}

void mic_ping(mic_model_t *m, uint32_t now) {
    m->last_ping = now;
    if (m->state == MIC_OFFLINE) m->state = MIC_READY;
}

bool mic_expire(mic_model_t *m, uint32_t now) {
    if (m->state != MIC_OFFLINE &&
        (uint32_t)(now - m->last_ping) >= MIC_LINK_TIMEOUT_MS) {
        m->state = MIC_OFFLINE;
        return true;
    }
    if (m->state == MIC_STARTING && (uint32_t)(now - m->started) >= 2000) {
        m->state = MIC_READY;
        return true;
    }
    return false;
}

uint8_t mic_press(mic_model_t *m, uint32_t now) {
    if (m->state == MIC_READY) {
        m->state = MIC_STARTING;
        m->started = now;
        return MIC_START;
    }
    if (m->state == MIC_RECORDING) {
        m->state = MIC_READY;
        return MIC_STOP;
    }
    return 0;
}

bool mic_ack(mic_model_t *m, uint32_t now) {
    if (m->state != MIC_STARTING) return false;
    m->state = MIC_RECORDING;
    m->started = now;
    return true;
}

uint8_t mic_edit_action(const mic_model_t *m, bool undo) {
    return m->state == MIC_READY ? (undo ? MIC_UNDO : MIC_CONFIRM) : 0;
}

size_t mic_ble_payload_size(unsigned mtu) {
    if (mtu < 23) return 0;
    if (mtu > 247) mtu = 247;
    return mtu - 3 - 6; /* ATT header and chunk envelope. */
}
