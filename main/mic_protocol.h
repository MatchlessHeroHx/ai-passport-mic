#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIC_SAMPLES 320
#define MIC_PAYLOAD_MAX (MIC_SAMPLES * 2)
#define MIC_HEADER_SIZE 16
#define MIC_LINK_TIMEOUT_MS 3000

enum { MIC_HELLO = 1, MIC_START, MIC_AUDIO, MIC_STOP, MIC_ERROR,
       MIC_UNDO, MIC_CONFIRM, MIC_AUDIO_ADPCM };
typedef enum { MIC_OFFLINE, MIC_READY, MIC_STARTING, MIC_RECORDING } mic_state_t;
typedef struct {
    mic_state_t state;
    uint32_t last_ping;
    uint32_t started;
} mic_model_t;

uint32_t mic_crc32(const uint8_t *data, size_t size);
size_t mic_packet(uint8_t *out, uint8_t type, uint32_t sequence,
                  const void *payload, uint16_t size);
void mic_ping(mic_model_t *model, uint32_t now);
bool mic_expire(mic_model_t *model, uint32_t now);
uint8_t mic_press(mic_model_t *model, uint32_t now);
bool mic_ack(mic_model_t *model, uint32_t now);

uint8_t mic_edit_action(const mic_model_t *model, bool undo);
size_t mic_ble_payload_size(unsigned mtu);
