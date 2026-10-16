#include "mic_screen.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    mic_screen_t s;
    mic_screen_wake(&s, 1000);
    assert(mic_screen_tick(&s, 1000, false) == MIC_SCREEN_ON);
    assert(mic_screen_tick(&s, 1000 + MIC_SCREEN_DIM_MS - 1, false) == MIC_SCREEN_ON);
    assert(mic_screen_tick(&s, 1000 + MIC_SCREEN_DIM_MS, false) == MIC_SCREEN_DIM);
    assert(mic_screen_tick(&s, 1000 + MIC_SCREEN_OFF_MS - 1, false) == MIC_SCREEN_DIM);
    assert(mic_screen_tick(&s, 1000 + MIC_SCREEN_OFF_MS, false) == MIC_SCREEN_OFF);

    /* Off: UP/DOWN only light the screen, OK lights it and still acts. */
    uint32_t t = 1000 + MIC_SCREEN_OFF_MS;
    assert(!mic_screen_key(&s, t, false));
    assert(s.level == MIC_SCREEN_ON);
    assert(mic_screen_tick(&s, t + 1, false) == MIC_SCREEN_ON);
    t += MIC_SCREEN_OFF_MS;
    assert(mic_screen_tick(&s, t, false) == MIC_SCREEN_OFF);
    assert(mic_screen_key(&s, t, true));
    assert(mic_screen_tick(&s, t + MIC_SCREEN_DIM_MS - 1, false) == MIC_SCREEN_ON);

    /* Dimmed: the screen is still readable, so every key acts and restores it. */
    t += MIC_SCREEN_DIM_MS;
    assert(mic_screen_tick(&s, t, false) == MIC_SCREEN_DIM);
    assert(mic_screen_key(&s, t, false));
    assert(mic_screen_tick(&s, t, false) == MIC_SCREEN_ON);

    /* Busy holds the screen on however long it lasts; the idle clock restarts after. */
    for (uint32_t i = 0; i <= 10 * MIC_SCREEN_OFF_MS; i += 20)
        assert(mic_screen_tick(&s, t + i, true) == MIC_SCREEN_ON);
    t += 10 * MIC_SCREEN_OFF_MS;
    assert(mic_screen_tick(&s, t + MIC_SCREEN_DIM_MS - 1, false) == MIC_SCREEN_ON);
    assert(mic_screen_tick(&s, t + MIC_SCREEN_OFF_MS, false) == MIC_SCREEN_OFF);

    /* The millisecond clock wraps after about 49 days. */
    mic_screen_wake(&s, UINT32_MAX - 10);
    assert(mic_screen_tick(&s, 100, false) == MIC_SCREEN_ON);
    assert(mic_screen_tick(&s, MIC_SCREEN_OFF_MS - 11, false) == MIC_SCREEN_OFF);
    puts("mic screen tests passed");
    return 0;
}
