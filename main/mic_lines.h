#pragma once
#include <stdint.h>

/* Built-in speech-bubble lines for the companion. Pure data and selection, no
   LVGL, so the host tests check every line's glyphs and width. */

typedef enum {
    MIC_MOOD_WAITING,     /* no Mac bridge yet */
    MIC_MOOD_HELLO,       /* just connected */
    MIC_MOOD_IDLE,        /* ready, nothing happening */
    MIC_MOOD_LISTEN,      /* recording */
    MIC_MOOD_LONG,        /* recording for a while */
    MIC_MOOD_DONE,        /* recording finished */
    MIC_MOOD_SEND,        /* DOWN: Return sent */
    MIC_MOOD_UNDO,        /* UP: Command-Z sent */
    MIC_MOOD_PAIR,        /* pairing code on screen */
    MIC_MOOD_ERROR,       /* microphone failed */
    MIC_MOOD_COUNT
} mic_mood_t;

/* A line for the mood. Picks vary with `seed` but never repeat the previous
   pick for the same mood back to back. */
const char *mic_line(mic_mood_t mood, uint32_t seed);
unsigned mic_line_count(mic_mood_t mood);
const char *mic_line_at(mic_mood_t mood, unsigned index);
