#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Idle screen stages: full brightness, dimmed after 15 s, panel off after 45 s.
   Pure timing logic so the host tests can drive it. */
#define MIC_SCREEN_DIM_MS 15000
#define MIC_SCREEN_OFF_MS 45000

typedef enum { MIC_SCREEN_ON, MIC_SCREEN_DIM, MIC_SCREEN_OFF } mic_screen_level_t;
typedef struct {
    mic_screen_level_t level;
    uint32_t active_at;
} mic_screen_t;

void mic_screen_wake(mic_screen_t *screen, uint32_t now);
/* Any key wakes the screen. Returns whether the key should still do its job:
   from off only OK does (it starts dictation), UP/DOWN just light the screen. */
bool mic_screen_key(mic_screen_t *screen, uint32_t now, bool ok);
/* busy holds the screen on (starting, recording, pairing). */
mic_screen_level_t mic_screen_tick(mic_screen_t *screen, uint32_t now, bool busy);
