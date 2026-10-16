#include "mic_clips.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool mic_pack_open(mic_pack_t *pack, const uint8_t *data, size_t size) {
    memset(pack, 0, sizeof(*pack));
    if (!data || size < 16 || memcmp(data, "MCV1", 4)) return false;
    pack->size = u16(data + 4);
    pack->tile = u16(data + 6);
    pack->fps = u16(data + 8);
    unsigned clips = u16(data + 10);
    pack->frame_count = u16(data + 12);
    pack->tile_count = u16(data + 14);
    if (!pack->tile || pack->size % pack->tile || !pack->fps || clips != MIC_CLIP_COUNT ||
        !pack->frame_count || !pack->tile_count) return false;
    pack->grid = pack->size / pack->tile;
    size_t cells = (size_t)pack->grid * pack->grid;
    size_t maps_at = 16 + (size_t)clips * 8;
    size_t tiles_at = maps_at + (size_t)pack->frame_count * cells * 2;
    size_t tile_bytes = (size_t)pack->tile * pack->tile * 2;
    if (size != tiles_at + (size_t)pack->tile_count * tile_bytes) return false;
    /* Tiles are read as uint16_t; the embedded blob must keep them aligned. */
    if (((uintptr_t)(data + tiles_at)) % 2) return false;
    for (unsigned c = 0; c < clips; ++c) {
        const uint8_t *row = data + 16 + c * 8;
        pack->first[c] = u16(row);
        pack->frames[c] = u16(row + 2);
        pack->loop[c] = row[4] != 0;
        pack->exit_frame[c] = u16(row + 6);
        if (!pack->frames[c] || (uint32_t)pack->first[c] + pack->frames[c] > pack->frame_count) return false;
    }
    for (unsigned c = 0; c < clips; ++c) {
        if (pack->exit_frame[c] >= pack->frames[MIC_CLIP_IDLE]) return false;
    }
    pack->maps = data + maps_at;
    pack->tiles = data + tiles_at;
    for (size_t i = 0; i < (size_t)pack->frame_count * cells; ++i)
        if (u16(pack->maps + i * 2) >= pack->tile_count) return false;
    return true;
}

uint16_t mic_pack_tile_at(const mic_pack_t *pack, uint16_t frame, unsigned tx, unsigned ty) {
    size_t cell = ((size_t)frame * pack->grid + ty) * pack->grid + tx;
    return u16(pack->maps + cell * 2);
}

const uint16_t *mic_pack_tile_pixels(const mic_pack_t *pack, uint16_t tile) {
    return (const uint16_t *)(pack->tiles + (size_t)tile * pack->tile * pack->tile * 2);
}

void mic_player_play(mic_player_t *player, mic_clip_t clip, uint32_t now) {
    player->clip = clip;
    player->started = now;
    player->offset = 0;
}

void mic_player_resume_idle(const mic_pack_t *pack, mic_player_t *player, uint32_t now) {
    uint16_t exit_frame = pack->exit_frame[player->clip];
    mic_player_play(player, MIC_CLIP_IDLE, now);
    player->offset = exit_frame;
}

/* Split into whole seconds and remainder so elapsed * fps never overflows. */
static uint32_t step_of(const mic_pack_t *pack, const mic_player_t *player, uint32_t now) {
    uint32_t elapsed = now - player->started;
    return elapsed / 1000u * pack->fps + elapsed % 1000u * pack->fps / 1000u;
}

uint16_t mic_player_frame(const mic_pack_t *pack, const mic_player_t *player, uint32_t now) {
    uint32_t step = step_of(pack, player, now);
    uint16_t count = pack->frames[player->clip];
    uint32_t k;
    if (!pack->loop[player->clip]) k = step < count ? step : count - 1u;
    else if (count == 1) k = 0;
    else {
        uint32_t period = 2u * (count - 1u);
        uint32_t p = (step + player->offset) % period;
        k = p < count ? p : period - p;
    }
    return (uint16_t)(pack->first[player->clip] + k);
}

bool mic_player_done(const mic_pack_t *pack, const mic_player_t *player, uint32_t now) {
    if (pack->loop[player->clip]) return false;
    return step_of(pack, player, now) >= pack->frames[player->clip];
}
