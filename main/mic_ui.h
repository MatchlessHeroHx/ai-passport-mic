#pragma once
#include "mic_protocol.h"

/* Called only with the BSP LVGL lock held. One persistent screen, no UI tasks. */
void mic_ui_create(void);
void mic_ui_press(uint32_t now);
void mic_ui_finished(uint32_t now, uint32_t duration_ms);
void mic_ui_update(const mic_model_t *model, uint32_t now, int peak,
                   bool audio_ok, int battery);

void mic_ui_link(bool wireless, int pairing_code);
void mic_ui_action(uint32_t now, uint8_t action);
