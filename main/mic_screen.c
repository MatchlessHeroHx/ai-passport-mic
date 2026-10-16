#include "mic_screen.h"

void mic_screen_wake(mic_screen_t *s, uint32_t now) {
    s->active_at = now;
    s->level = MIC_SCREEN_ON;
}

bool mic_screen_key(mic_screen_t *s, uint32_t now, bool ok) {
    bool act = ok || s->level != MIC_SCREEN_OFF;
    mic_screen_wake(s, now);
    return act;
}

mic_screen_level_t mic_screen_tick(mic_screen_t *s, uint32_t now, bool busy) {
    if (busy) mic_screen_wake(s, now);
    uint32_t idle = now - s->active_at;
    s->level = idle >= MIC_SCREEN_OFF_MS ? MIC_SCREEN_OFF
             : idle >= MIC_SCREEN_DIM_MS ? MIC_SCREEN_DIM : MIC_SCREEN_ON;
    return s->level;
}
