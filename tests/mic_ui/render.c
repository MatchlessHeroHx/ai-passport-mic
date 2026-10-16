#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include "bsp_display_rounding.h"
#include "mic_ui.h"
#include "mic_lines.h"

LV_FONT_DECLARE(mic_font_16);
LV_FONT_DECLARE(mic_font_20);
static uint16_t s_frame[320][240];
static uint16_t s_draw[20 * 240];

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    uint16_t *source = (uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            s_frame[y][x] = bsp_display_pixel_outside_rounded_rect(x, y, 240, 320, 30)
                            ? 0 : *source;
            source++;
        }
    }
    lv_display_flush_ready(display);
}

static void check_labels(lv_obj_t *parent) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t *obj = lv_obj_get_child(parent, i);
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_check_type(obj, &lv_label_class)) {
            const char *text = lv_label_get_text(obj);
            const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
            for (uint32_t offset = 0; text[offset];) {
                uint32_t cp = lv_text_encoded_next(text, &offset);
                lv_font_glyph_dsc_t glyph = {0};
                bool found = lv_font_get_glyph_dsc(font, &glyph, cp, 0);
                if (!found || glyph.is_placeholder) fprintf(stderr, "Missing U+%04X in %s\n", cp, text);
                assert(found && !glyph.is_placeholder);
            }
            lv_area_t area;
            lv_obj_get_coords(obj, &area);
            assert(area.x1 >= 0 && area.x2 < 240 && area.y1 >= 0 && area.y2 < 320);
            // A wrapped/truncated label must not grow beyond its single-line slot.
            assert(lv_obj_get_height(obj) <= font->line_height);
        }
        check_labels(obj);
    }
}

static void snapshot(const char *name) {
    lv_obj_update_layout(lv_screen_active());
    check_labels(lv_screen_active());
    lv_refr_now(NULL);
    FILE *out = fopen(name, "wb");
    assert(out);
    fprintf(out, "P6\n240 320\n255\n");
    for (int y = 0; y < 320; y++) {
        for (int x = 0; x < 240; x++) {
            uint16_t pixel = s_frame[y][x];
            uint8_t rgb[] = {(uint8_t)((pixel >> 11) * 255 / 31),
                             (uint8_t)(((pixel >> 5) & 63) * 255 / 63),
                             (uint8_t)((pixel & 31) * 255 / 31)};
            fwrite(rgb, 1, sizeof(rgb), out);
        }
    }
    fclose(out);
}


/* Every built-in bubble line must render with the bundled font in one row of the bubble. */
static void check_lines(void) {
    unsigned total = 0;
    for (int mood = 0; mood < MIC_MOOD_COUNT; ++mood) {
        assert(mic_line_count(mood) > 0);
        for (unsigned i = 0; i < mic_line_count(mood); ++i, ++total) {
            const char *text = mic_line_at(mood, i);
            for (uint32_t offset = 0; text[offset];) {
                uint32_t cp = lv_text_encoded_next(text, &offset);
                lv_font_glyph_dsc_t glyph = {0};
                bool found = lv_font_get_glyph_dsc(&mic_font_16, &glyph, cp, 0);
                if (!found || glyph.is_placeholder) fprintf(stderr, "Missing U+%04X in %s\n", cp, text);
                assert(found && !glyph.is_placeholder);
            }
            lv_point_t size;
            lv_text_get_size(&size, text, &mic_font_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > 196) fprintf(stderr, "Too wide (%d px): %s\n", (int)size.x, text);
            assert(size.x <= 196);
        }
        /* Back-to-back picks for the same mood never repeat. */
        const char *prev = mic_line(mood, 1);
        for (uint32_t seed = 2; seed < 200; ++seed) {
            const char *next = mic_line(mood, seed);
            assert(mic_line_count(mood) == 1 || next != prev);
            prev = next;
        }
    }
    printf("Bubble lines: %u checked\n", total);
}

int main(void) {
    setbuf(stdout, NULL);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, s_draw, NULL, sizeof(s_draw), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    check_lines();
    mic_model_t model = {0};
    mic_ui_create();
    mic_ui_update(&model, 1000, 0, true, -1);
    snapshot("mic-offline.ppm");
    model.state = MIC_READY;
    mic_ui_update(&model, 1100, 0, true, 85);
    snapshot("mic-ready.ppm");
    mic_ui_press(1200);
    model.state = MIC_STARTING;
    mic_ui_update(&model, 1200, 0, true, 85);
    snapshot("mic-starting.ppm");
    model.state = MIC_RECORDING;
    model.started = 1300;
    for (int i = 0; i < 32; ++i)
        mic_ui_update(&model, 42000 + i * 60, (i * 5119) % 32769, true, 85);
    snapshot("mic-recording.ppm");
    mic_ui_update(&model, 3601300, 32768, true, 100);
    snapshot("mic-long-recording.ppm");
    for (int i = 0; i < 32; ++i)
        mic_ui_update(&model, 3601400 + i * 60, 0, true, 100);
    snapshot("mic-silence.ppm");
    model.state = MIC_READY;
    mic_ui_finished(3604000, 43000);
    mic_ui_update(&model, 3604000, 0, true, 85);
    snapshot("mic-done.ppm");
    mic_ui_update(&model, 3605600, 0, true, 85);
    snapshot("mic-ready-again.ppm");
    mic_ui_update(&model, 3605700, 0, false, -1);
    snapshot("mic-error.ppm");
    if (getenv("MIC_RENDER_ANIMATION")) {
        for (int i = 0; i < 80; ++i) {
            uint32_t now = 500000 + (uint32_t)i * 100;
            model.state = i < 20 ? MIC_READY : i < 25 ? MIC_STARTING : i < 62 ? MIC_RECORDING : MIC_READY;
            model.started = 502500;
            if (i == 20 || i == 62) mic_ui_press(now);
            if (i == 62) mic_ui_finished(now, 3700);
            mic_ui_update(&model, now, (i * 5119) % 32769, true, 85);
            char path[40];
            snprintf(path, sizeof(path), "motion-%03d.ppm", i);
            snapshot(path);
        }
    }
    mic_ui_link(true, -1);
    model.state = MIC_READY;
    mic_ui_action(4000000, MIC_UNDO);
    mic_ui_update(&model, 4000600, 0, true, 85);
    snapshot("mic-undo.ppm");
    mic_ui_action(4001200, MIC_CONFIRM);
    mic_ui_update(&model, 4001800, 0, true, 85);
    snapshot("mic-confirm.ppm");
    mic_ui_link(false, 123456);
    model.state = MIC_OFFLINE;
    mic_ui_update(&model, 4002400, 0, true, 85);
    snapshot("mic-pairing.ppm");
    mic_ui_link(true, -1);
    /* Exercise rollover and repeated transitions without allocating more objects. */
    for (int i = 0; i < 1000; ++i) {
        model.state = i % 4;
        mic_ui_update(&model, UINT32_MAX - 100 + (uint32_t)i * 50, i % 32769, true, 100);
    }
    snapshot("mic-soak.ppm");
    lv_font_glyph_dsc_t missing = {0};
    bool found = lv_font_get_glyph_dsc(&mic_font_16, &missing, 0x9F98, 0);
    assert(!found || missing.is_placeholder);
    puts("Microphone UI: PASS (13 frames, glyph coverage, label bounds, bubble lines, 1000 updates)");
    lv_deinit();
    return 0;
}
