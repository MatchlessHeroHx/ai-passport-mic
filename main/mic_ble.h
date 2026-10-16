#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIC_BLE_DEVICE_NAME "Passport Mic"
#define MIC_BLE_SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define MIC_BLE_AUDIO_UUID   "12345678-1234-5678-1234-56789abcdef1"
#define MIC_BLE_CONTROL_UUID "12345678-1234-5678-1234-56789abcdef2"

/* Pending host commands, applied in this order: reset, ping, start acknowledgement. */
#define MIC_BLE_CMD_RESET (1u << 0)
#define MIC_BLE_CMD_PING  (1u << 1)
#define MIC_BLE_CMD_ACK   (1u << 2)

esp_err_t mic_ble_init(void);
/* ESP_ERR_NO_MEM: a droppable frame was skipped because host buffers are low. */
esp_err_t mic_ble_send(const uint8_t *data, size_t size, bool droppable);
unsigned mic_ble_take_commands(void);
bool mic_ble_connected(void);

bool mic_ble_lost(void);
int mic_ble_passkey(void);
