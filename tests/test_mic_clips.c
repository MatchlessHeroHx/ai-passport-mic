#include "mic_clips.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Checks the real pack written by tools/generate_mic_clips.py, then a few
   malformed packs, then playback timing including millisecond rollover. */
static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    assert(data && fread(data, 1, *size, f) == *size);
    fclose(f);
    return data;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    size_t size;
    uint8_t *data = load(argv[1], &size);
    mic_pack_t pack;
    assert(mic_pack_open(&pack, data, size));
    assert(pack.size == 240 && pack.tile == 16 && pack.grid == 15 && pack.fps > 0);
    for (int c = 0; c < MIC_CLIP_COUNT; ++c) assert(pack.frames[c] > 0);
    assert(pack.loop[MIC_CLIP_IDLE] && pack.loop[MIC_CLIP_LISTEN]);
    assert(!pack.loop[MIC_CLIP_NOD] && !pack.loop[MIC_CLIP_LISTEN_IN] && !pack.loop[MIC_CLIP_LISTEN_OUT]);

    /* Truncation, bad magic and an out-of-range tile index are all rejected. */
    assert(!mic_pack_open(&pack, data, size - 1));
    data[0] ^= 1;
    assert(!mic_pack_open(&pack, data, size));
    data[0] ^= 1;
    size_t first_map = 16 + MIC_CLIP_COUNT * 8;
    uint8_t lo = data[first_map], hi = data[first_map + 1];
    data[first_map] = data[first_map + 1] = 0xFF;
    assert(!mic_pack_open(&pack, data, size));
    data[first_map] = lo;
    data[first_map + 1] = hi;
    assert(mic_pack_open(&pack, data, size));

    /* One-shot clips hold their last frame; loops ping-pong and never jump. */
    mic_player_t p;
    mic_player_play(&p, MIC_CLIP_NOD, 1000);
    assert(mic_player_frame(&pack, &p, 1000) == pack.first[MIC_CLIP_NOD]);
    assert(!mic_player_done(&pack, &p, 1000));
    uint32_t end = 1000 + (uint32_t)pack.frames[MIC_CLIP_NOD] * 1000 / pack.fps + 1;
    assert(mic_player_done(&pack, &p, end));
    assert(mic_player_frame(&pack, &p, end + 60000) == pack.first[MIC_CLIP_NOD] + pack.frames[MIC_CLIP_NOD] - 1);
    mic_player_resume_idle(&pack, &p, end);
    assert(p.clip == MIC_CLIP_IDLE);
    assert(mic_player_frame(&pack, &p, end) == pack.first[MIC_CLIP_IDLE] + pack.exit_frame[MIC_CLIP_NOD]);

    mic_player_play(&p, MIC_CLIP_IDLE, UINT32_MAX - 5000);  /* runs across rollover */
    int prev = -1;
    for (uint32_t t = 0; t < 120000; t += 1000 / pack.fps) {
        int f = mic_player_frame(&pack, &p, UINT32_MAX - 5000 + t) - pack.first[MIC_CLIP_IDLE];
        assert(f >= 0 && f < pack.frames[MIC_CLIP_IDLE]);
        if (prev >= 0) assert(abs(f - prev) <= 1);
        prev = f;
    }
    free(data);
    puts("Microphone clips: PASS (pack validation, one-shot, ping-pong, rollover)");
}
