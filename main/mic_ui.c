#include "mic_ui.h"
#include "mic_clips.h"
#include "mic_lines.h"
#include "lvgl.h"
#include <string.h>

LV_FONT_DECLARE(mic_font_16);
LV_FONT_DECLARE(mic_font_20);

/* "Xiao Sheng": a companion who listens. The video clips carry her expressions;
   the speech bubble carries her personality; the bottom card says what to do. */
#define PAPER 0xEEDCCB   /* the clips' background */
#define CARD 0xFBF4EC
#define INK 0x3A2620
#define MUTED 0x9C8478
#define ROSE 0xD65B49
#define ROSE_SOFT 0xF3C4B6
#define SAGE 0x6F9A6A
#define LINE 0xE2CDBB

#define BARS 21
#define BAR_Y 276
#define BUBBLE_MS 4200      /* how long a line stays up */
#define CHATTER_MS 9000     /* gap before she says something new on her own */

/* Defined by main/CMakeLists.txt EMBED_FILES (or the host renderer). */
extern const uint8_t mic_clips_start[] asm("_binary_mic_clips_bin_start");
extern const uint8_t mic_clips_end[] asm("_binary_mic_clips_bin_end");

static mic_pack_t pack;
static bool pack_ok;
static mic_player_t player;
static uint16_t shown_frame = 0xFFFF;

static lv_obj_t *screen, *video, *link_pill, *link_icon, *link_text, *battery_pill, *battery_icon, *battery_label;
static lv_obj_t *bubble, *bubble_text, *title, *caption, *clock_label, *hint;
static lv_obj_t *wave[BARS];
static uint8_t history[BARS];
static uint32_t pressed_at, finished_at, finished_ms, wave_at, action_at, said_at, greet_at;
static bool pressed, finished, wireless, connected, greeting, waves_shown, bubble_shown, shown_clock;
static int pairing_code = -1, shown_battery = -2, shown_mood = -1;
static uint8_t edit_action;
static const char *shown_title, *shown_caption, *shown_hint, *shown_line, *shown_link;
static uint32_t shown_seconds = 0xFFFFFFFF, shown_link_color = 0xFFFFFFFF;

/* Draw only the tiles that intersect the area LVGL is refreshing, straight from
   Flash. No frame buffer: a changed tile costs 512 bytes of SPI, nothing in RAM. */
static void video_draw(lv_event_t *e) {
    if (!pack_ok || shown_frame == 0xFFFF) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(video, &box);
    const lv_area_t *clip = &layer->_clip_area;
    int tile = pack.tile;
    int tx0 = LV_MAX(0, (clip->x1 - box.x1) / tile), tx1 = LV_MIN(pack.grid - 1, (clip->x2 - box.x1) / tile);
    int ty0 = LV_MAX(0, (clip->y1 - box.y1) / tile), ty1 = LV_MIN(pack.grid - 1, (clip->y2 - box.y1) / tile);
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    /* One descriptor per tile: draw tasks may still reference earlier ones. */
    static lv_image_dsc_t imgs[15 * 15];
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            lv_image_dsc_t *img = &imgs[ty * pack.grid + tx];
            img->header.magic = LV_IMAGE_HEADER_MAGIC;
            img->header.cf = LV_COLOR_FORMAT_RGB565;
            img->header.w = img->header.h = (uint32_t)tile;
            img->header.stride = (uint32_t)tile * 2;
            img->data_size = (uint32_t)tile * tile * 2;
            img->data = (const uint8_t *)mic_pack_tile_pixels(&pack, mic_pack_tile_at(&pack, shown_frame, tx, ty));
            dsc.src = img;
            lv_area_t at = {box.x1 + tx * tile, box.y1 + ty * tile,
                            box.x1 + tx * tile + tile - 1, box.y1 + ty * tile + tile - 1};
            lv_draw_image(layer, &dsc, &at);
        }
    }
}

/* Show a frame by invalidating only the tiles that differ from the last one. */
static void show_frame(uint16_t frame) {
    if (!pack_ok || frame == shown_frame) return;
    uint16_t old = shown_frame;
    shown_frame = frame;
    if (old == 0xFFFF) {
        lv_obj_invalidate(video);
        return;
    }
    lv_area_t box;
    lv_obj_get_coords(video, &box);
    for (unsigned ty = 0; ty < pack.grid; ++ty) {
        int run = -1;
        for (unsigned tx = 0; tx <= pack.grid; ++tx) {
            bool changed = tx < pack.grid &&
                mic_pack_tile_at(&pack, old, tx, ty) != mic_pack_tile_at(&pack, frame, tx, ty);
            if (changed && run < 0) run = (int)tx;
            if (!changed && run >= 0) {  /* merge a horizontal run into one area */
                lv_area_t a = {box.x1 + run * pack.tile, box.y1 + (int)ty * pack.tile,
                               box.x1 + (int)tx * pack.tile - 1, box.y1 + (int)(ty + 1) * pack.tile - 1};
                lv_obj_invalidate_area(video, &a);
                run = -1;
            }
        }
    }
}

static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color) {
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, s);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    return o;
}

/* Every LVGL setter invalidates its widget, so only touch what changed. */
static void set_text(lv_obj_t *label, const char **shown, const char *s) {
    if (*shown == s) return;
    *shown = s;
    lv_label_set_text(label, s);
}

static void set_hidden(lv_obj_t *o, bool hidden) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void mic_ui_create(void) {
    pack_ok = mic_pack_open(&pack, mic_clips_start, (size_t)(mic_clips_end - mic_clips_start));
    mic_player_play(&player, MIC_CLIP_IDLE, 0);
    shown_frame = 0xFFFF;
    pressed = finished = connected = greeting = false;
    waves_shown = bubble_shown = shown_clock = false;
    shown_battery = -2;
    shown_mood = -1;
    wave_at = said_at = 0;
    shown_title = shown_caption = shown_hint = shown_line = shown_link = NULL;
    shown_seconds = shown_link_color = 0xFFFFFFFF;
    memset(history, 0, sizeof(history));

    screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 240, 320);
    lv_obj_set_style_bg_color(screen, lv_color_hex(PAPER), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    video = lv_obj_create(screen);
    lv_obj_remove_style_all(video);
    lv_obj_set_pos(video, 0, 0);
    lv_obj_set_size(video, 240, 240);
    lv_obj_remove_flag(video, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(video, video_draw, LV_EVENT_DRAW_MAIN, NULL);
    if (pack_ok) show_frame(mic_player_frame(&pack, &player, 0));

    /* Status pills: readable over her hair, inset from the rounded corners. */
    link_pill = shape(screen, 22, 6, 10, 24, CARD, 12);
    lv_obj_set_style_bg_opa(link_pill, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(link_pill, 9, 0);
    lv_obj_set_width(link_pill, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(link_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(link_pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(link_pill, 4, 0);
    link_icon = text(link_pill, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_14, MUTED);
    link_text = text(link_pill, "", &mic_font_16, MUTED);
    battery_pill = shape(screen, 0, 6, 10, 24, CARD, 12);
    lv_obj_set_style_bg_opa(battery_pill, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(battery_pill, 9, 0);
    lv_obj_set_width(battery_pill, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(battery_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(battery_pill, 4, 0);
    lv_obj_align(battery_pill, LV_ALIGN_TOP_RIGHT, -22, 6);
    battery_label = text(battery_pill, "", &lv_font_montserrat_14, MUTED);
    battery_icon = text(battery_pill, LV_SYMBOL_BATTERY_FULL, &lv_font_montserrat_14, MUTED);
    set_hidden(battery_pill, true);

    /* Speech bubble across her shoulders. */
    bubble = lv_obj_create(screen);
    lv_obj_remove_style_all(bubble);
    lv_obj_set_size(bubble, 240, 240);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *cloud = shape(bubble, 16, 198, 208, 32, CARD, 16);
    lv_obj_set_style_border_width(cloud, 1, 0);
    lv_obj_set_style_border_color(cloud, lv_color_hex(LINE), 0);
    bubble_text = text(cloud, "", &mic_font_16, INK);
    lv_obj_align(bubble_text, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(bubble, LV_OBJ_FLAG_HIDDEN);

    /* Bottom card: state, detail and the one thing to do next. */
    lv_obj_t *card = shape(screen, 0, 240, 240, 80, CARD, 0);
    shape(card, 0, 0, 240, 1, LINE, 0);
    title = text(screen, "", &mic_font_20, INK);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 244);
    clock_label = text(screen, "00:00", &lv_font_montserrat_20, ROSE);
    lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 246);
    lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    caption = text(screen, "", &mic_font_16, MUTED);
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 268);
    for (int i = 0; i < BARS; ++i) {
        wave[i] = shape(screen, 38 + i * 8, BAR_Y - 1, 4, 2, ROSE_SOFT, 2);
        lv_obj_add_flag(wave[i], LV_OBJ_FLAG_HIDDEN);
    }
    hint = text(screen, "", &mic_font_16, INK);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 292);
    lv_screen_load(screen);
}

void mic_ui_link(bool is_wireless, int code) {
    wireless = is_wireless;
    pairing_code = code;
}

void mic_ui_action(uint32_t now, uint8_t action) {
    action_at = now;
    edit_action = action;
    if (action == MIC_UNDO || action == MIC_CONFIRM) mic_player_play(&player, MIC_CLIP_NOD, now);
}

void mic_ui_press(uint32_t now) { pressed_at = now; pressed = true; }

void mic_ui_finished(uint32_t now, uint32_t duration_ms) {
    finished_at = now;
    finished_ms = duration_ms;
    finished = true;
}

static void show_clock(uint32_t value, bool code) {
    if (!shown_clock) {
        shown_clock = true;
        shown_seconds = 0xFFFFFFFF;
        set_hidden(title, true);
        set_hidden(clock_label, false);
    }
    if (value == shown_seconds) return;
    shown_seconds = value;
    lv_obj_set_style_text_color(clock_label, lv_color_hex(code ? INK : ROSE), 0);
    if (code) lv_label_set_text_fmt(clock_label, "%06lu", (unsigned long)value);
    else if (value >= 3600)
        lv_label_set_text_fmt(clock_label, "%lu:%02lu:%02lu", (unsigned long)(value / 3600),
                              (unsigned long)(value / 60 % 60), (unsigned long)(value % 60));
    else lv_label_set_text_fmt(clock_label, "%02lu:%02lu", (unsigned long)(value / 60), (unsigned long)(value % 60));
}

static void show_title(const char *s) {
    if (shown_clock) {
        shown_clock = false;
        set_hidden(clock_label, true);
        set_hidden(title, false);
    }
    set_text(title, &shown_title, s);
}

/* Pick the clip for the state; one-shot clips finish before the loop resumes. */
static void drive_clip(bool listening, uint32_t now) {
    if (!pack_ok) return;
    mic_clip_t c = player.clip;
    if (listening) {
        if (c != MIC_CLIP_LISTEN_IN && c != MIC_CLIP_LISTEN) mic_player_play(&player, MIC_CLIP_LISTEN_IN, now);
        else if (c == MIC_CLIP_LISTEN_IN && mic_player_done(&pack, &player, now))
            mic_player_play(&player, MIC_CLIP_LISTEN, now);
    } else if (c == MIC_CLIP_LISTEN_IN || c == MIC_CLIP_LISTEN) {
        mic_player_play(&player, MIC_CLIP_LISTEN_OUT, now);
    } else if ((c == MIC_CLIP_LISTEN_OUT || c == MIC_CLIP_NOD) && mic_player_done(&pack, &player, now)) {
        mic_player_resume_idle(&pack, &player, now);
    }
    show_frame(mic_player_frame(&pack, &player, now));
}

/* Say a new line when the mood changes, and now and then on her own. */
static void speak(mic_mood_t mood, uint32_t now, bool chatter, const char *head, const char *sub) {
    bool changed = (int)mood != shown_mood && !(shown_mood == MIC_MOOD_HELLO && mood == MIC_MOOD_IDLE);
    if (shown_mood == MIC_MOOD_HELLO && mood == MIC_MOOD_IDLE) shown_mood = (int)mood;
    if (changed || (chatter && (uint32_t)(now - said_at) >= CHATTER_MS)) {
        shown_mood = (int)mood;
        said_at = now;
        const char *line = mic_line(mood, now);
        for (unsigned tries = mic_line_count(mood); tries && (!strcmp(line, head) || !strcmp(line, sub)); --tries)
            line = mic_line(mood, now + tries);
        if (line != shown_line) {
            shown_line = line;
            lv_label_set_text(bubble_text, line);
        }
    }
    /* Lines fade after a while so she is not talking all the time, except in
       states where the line explains what is going on. */
    bool keep = mood == MIC_MOOD_PAIR || mood == MIC_MOOD_ERROR || mood == MIC_MOOD_WAITING;
    bool show = keep || (uint32_t)(now - said_at) < BUBBLE_MS;
    if (show != bubble_shown) {
        bubble_shown = show;
        set_hidden(bubble, !show);
    }
}

static void show_link(const char *label, uint32_t color, const char *symbol) {
    if (label != shown_link) {
        shown_link = label;
        lv_label_set_text(link_text, label);
        lv_label_set_text(link_icon, symbol);
    }
    if (color != shown_link_color) {
        shown_link_color = color;
        lv_obj_set_style_text_color(link_icon, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(link_text, lv_color_hex(color), 0);
    }
}

static void show_battery(int battery) {
    if (battery == shown_battery) return;
    shown_battery = battery;
    set_hidden(battery_pill, battery < 0);
    if (battery < 0) return;
    lv_label_set_text(battery_icon, battery > 80 ? LV_SYMBOL_BATTERY_FULL : battery > 55 ? LV_SYMBOL_BATTERY_3
                                    : battery > 30 ? LV_SYMBOL_BATTERY_2 : battery > 10 ? LV_SYMBOL_BATTERY_1
                                    : LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_color(battery_icon, lv_color_hex(battery <= 10 ? ROSE : MUTED), 0);
    lv_label_set_text_fmt(battery_label, "%d%%", battery);
}

void mic_ui_update(const mic_model_t *m, uint32_t now, int peak, bool audio_ok, int battery) {
    bool live = audio_ok && m->state == MIC_RECORDING;
    bool ready = audio_ok && m->state == MIC_READY;
    bool waiting = audio_ok && m->state == MIC_STARTING;
    bool pairing = pairing_code >= 0;
    if (!ready || (uint32_t)(now - finished_at) >= 2000) finished = false;
    if ((uint32_t)(now - pressed_at) >= 180) pressed = false;
    if (!ready || (uint32_t)(now - action_at) >= 1600) edit_action = 0;
    bool done = ready && finished;
    if (ready && !connected) {
        greeting = true;
        greet_at = now;
    }
    if (!ready || (uint32_t)(now - greet_at) >= BUBBLE_MS) greeting = false;
    connected = ready || live || waiting;
    if (peak < 0) peak = 0;
    if (peak > 32768) peak = 32768;

    drive_clip(live || waiting, now);

    const char *head = "等待连接", *sub = "在 Mac 打开 Passport Mic", *next = "";
    mic_mood_t mood = MIC_MOOD_WAITING;
    bool chatter = false;
    if (!audio_ok) {
        head = "麦克风暂不可用"; sub = "长按电源键关机"; next = "再长按开机"; mood = MIC_MOOD_ERROR;
    } else if (pairing) {
        sub = "请在 Mac 输入配对码"; next = "等待确认中…"; mood = MIC_MOOD_PAIR;
    } else if (live) {
        sub = ""; next = "说完按确定";
        mood = (uint32_t)(now - m->started) > 60000 ? MIC_MOOD_LONG : MIC_MOOD_LISTEN;
        chatter = true;
    } else if (waiting) {
        head = "正在准备…"; sub = "正在开启听写"; next = "稍候"; mood = MIC_MOOD_LISTEN;
    } else if (done) {
        sub = "内容已同步到 Mac"; next = "按确定 · 继续说"; mood = MIC_MOOD_DONE;
    } else if (ready) {
        head = "随时在听"; sub = "上 撤回 · 下 发送"; next = "按确定 · 开始说话";
        mood = greeting ? MIC_MOOD_HELLO : MIC_MOOD_IDLE;
        chatter = true;
        if (edit_action) {
            mood = edit_action == MIC_UNDO ? MIC_MOOD_UNDO : MIC_MOOD_SEND;
            sub = edit_action == MIC_UNDO ? "已撤回" : "已发送";
        }
    }
    speak(mood, now, chatter, head, sub);

    if (pairing) show_clock((uint32_t)pairing_code, true);
    else if (live) show_clock((now - m->started) / 1000, false);
    else if (done) show_clock(finished_ms / 1000, false);
    else show_title(head);
    set_text(caption, &shown_caption, sub);
    set_text(hint, &shown_hint, next);
    /* Plain words with the Bluetooth or USB icon, instead of "BLE". */
    if (!audio_ok) show_link("异常", ROSE, LV_SYMBOL_WARNING);
    else if (pairing) show_link("配对中", SAGE, LV_SYMBOL_BLUETOOTH);
    else if (live) show_link("录音中", ROSE, wireless ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_USB);
    else if (connected) show_link(wireless ? "蓝牙已连接" : "USB 已连接", SAGE,
                                  wireless ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_USB);
    else show_link("未连接", MUTED, LV_SYMBOL_BLUETOOTH);
    show_battery(battery);

    /* The waveform replaces the caption row and only shows real input. */
    if (waves_shown != live) {
        waves_shown = live;
        set_hidden(caption, live);
        memset(history, 0, sizeof(history));
        for (int i = 0; i < BARS; ++i) {
            lv_obj_set_height(wave[i], 2);
            lv_obj_set_y(wave[i], BAR_Y - 1);
            set_hidden(wave[i], !live);
        }
    }
    if (!live || (uint32_t)(now - wave_at) < 50) return;
    wave_at = now;
    memmove(history, history + 1, BARS - 1);
    history[BARS - 1] = (uint8_t)(peak * 8 / 32768);
    for (int i = 0; i < BARS; ++i) {
        int h = 2 + 2 * history[i];
        if (lv_obj_get_height(wave[i]) == h) continue;
        lv_obj_set_height(wave[i], h);
        lv_obj_set_y(wave[i], BAR_Y - h / 2);
        lv_obj_set_style_bg_color(wave[i], lv_color_hex(history[i] ? ROSE : ROSE_SOFT), 0);
    }
}
