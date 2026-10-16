#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tiled RGB565 animation pack written by tools/generate_mic_clips.py.
   Pure parsing and playback timing, no LVGL, so it is covered by host tests. */

/* LISTEN_IN leads into the LISTEN loop; LISTEN_OUT returns from it to rest. */
typedef enum {
    MIC_CLIP_IDLE, MIC_CLIP_LISTEN_IN, MIC_CLIP_LISTEN, MIC_CLIP_LISTEN_OUT, MIC_CLIP_NOD,
    MIC_CLIP_COUNT
} mic_clip_t;

typedef struct {
    const uint8_t *maps;      /* frame_count x grid x grid tile indices, u16 LE */
    const uint8_t *tiles;     /* tile_count x tile x tile RGB565 pixels, u16 LE */
    uint16_t size, tile, grid, fps;
    uint16_t frame_count, tile_count;
    uint16_t first[MIC_CLIP_COUNT], frames[MIC_CLIP_COUNT];
    uint16_t exit_frame[MIC_CLIP_COUNT]; /* idle frame a one-shot clip hands over to */
    bool loop[MIC_CLIP_COUNT];
} mic_pack_t;

/* Returns false if the pack is malformed, truncated or has out-of-range tiles. */
bool mic_pack_open(mic_pack_t *pack, const uint8_t *data, size_t size);
uint16_t mic_pack_tile_at(const mic_pack_t *pack, uint16_t frame, unsigned tx, unsigned ty);
const uint16_t *mic_pack_tile_pixels(const mic_pack_t *pack, uint16_t tile);

/* Playback: looping clips play forward then backward (ping-pong) so they never
   jump at the wrap. A one-shot clip plays once and reports done; idle then
   resumes from the clip's exit frame, which matches its last frame. */
typedef struct {
    mic_clip_t clip;
    uint32_t started;
    uint16_t offset;   /* loop step to start from */
} mic_player_t;

void mic_player_play(mic_player_t *player, mic_clip_t clip, uint32_t now);
/* Start idle where the finished one-shot clip left off. */
void mic_player_resume_idle(const mic_pack_t *pack, mic_player_t *player, uint32_t now);
uint16_t mic_player_frame(const mic_pack_t *pack, const mic_player_t *player, uint32_t now);
bool mic_player_done(const mic_pack_t *pack, const mic_player_t *player, uint32_t now);
