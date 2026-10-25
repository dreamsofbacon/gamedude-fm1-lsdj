/* SPDX-License-Identifier: GPL-3.0-only */
/* Settings (GLO opens and closes the menu; LSDj keeps playing). The menu is
   knob driven like the effects pages: see lsdj_menu_knobs below.
   The settings are packed into 32 bits and kept in the save store
   (firmware/lsdj_ftl.h, a settings record). Pure logic: tests/lsdj_settings.c. */
#pragma once
#include <stdint.h>
#include "lsdj_midi.h"   /* the MIDI lines follow the settings in the list */
#include "lsdj_theme.h"  /* the theme names (tools/themes.py) */

typedef struct {
    uint8_t knobs_swapped;   /* 0: PRESETS = up/down, ALGORITHM = left/right; 1: the other way */
    uint8_t presets_rev;     /* reverse the knob in the PRESETS position */
    uint8_t algo_rev;        /* reverse the knob in the ALGORITHM position */
    uint8_t play_is_select;  /* 0: PLAY = Start, REC = Select; 1: swapped */
    uint8_t select_key;      /* extra Select in button mode: 0 off, 1 F#5, 2 D#5 */
    uint8_t meter;           /* CPU meter on screen */
    uint8_t theme;           /* colour theme, 0..LSDJ_SET_THEMES-1 (firmware/lsdj_theme.h) */
    uint8_t bluetooth;       /* 0 off, 1 DEVICE (a BLE-MIDI device: firmware/lsdj_ble.h) */
    uint8_t popup;           /* how long pop-ups (effects panel, messages) stay: lsdj_set_popup_ms */
} lsdj_settings_t;

#define LSDJ_SET_MAGIC 0x5Au
#define LSDJ_SET_THEMES LSDJ_THEMES   /* the index: bits 7-9, bit 12 (bit 3), bits 15-17 (bits 4-6) */
#define LSDJ_SET_BT_MODES 2u
static const char *const lsdj_set_bt_name[LSDJ_SET_BT_MODES] = {"OFF", "DEVICE"};
#define LSDJ_SET_POPUPS 4u   /* bits 13-14 */
static const char *const lsdj_set_popup_name[LSDJ_SET_POPUPS] = {"1.5S", "3S", "5S", "10S"};
static const uint32_t lsdj_set_popup_ms[LSDJ_SET_POPUPS] = {1500u, 3000u, 5000u, 10000u};
#define lsdj_set_theme_name lsdj_theme_name   /* top byte of the packed form */
static unsigned lsdj_set_theme_of(uint32_t v) { return ((v >> 7) & 7u) | ((v >> 9) & 8u) | ((v >> 11) & 0x70u); }
static uint32_t lsdj_set_pack(const lsdj_settings_t *s) {
    return (uint32_t)LSDJ_SET_MAGIC << 24 | (uint32_t)(s->knobs_swapped & 1u) | (uint32_t)(s->presets_rev & 1u) << 1 |
           (uint32_t)(s->algo_rev & 1u) << 2 | (uint32_t)(s->play_is_select & 1u) << 3 |
           (uint32_t)(s->select_key & 3u) << 4 | (uint32_t)(s->meter & 1u) << 6 | (uint32_t)(s->theme & 7u) << 7 | (uint32_t)((s->theme >> 3) & 1u) << 12 | (uint32_t)((s->theme >> 4) & 7u) << 15 |
           (uint32_t)(s->bluetooth & 3u) << 10 | (uint32_t)(s->popup & 3u) << 13;
}
/* 0 if v is not a packed settings word (the defaults are set then). */
static int lsdj_set_unpack(uint32_t v, lsdj_settings_t *s) {
    lsdj_settings_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    *s = d;
    if (v >> 24 != LSDJ_SET_MAGIC || (v & 0x00FC0000u) || ((v >> 4) & 3u) > 2u || lsdj_set_theme_of(v) >= LSDJ_SET_THEMES ||
        ((v >> 10) & 3u) >= LSDJ_SET_BT_MODES) return 0;
    s->knobs_swapped = v & 1u; s->presets_rev = (v >> 1) & 1u; s->algo_rev = (v >> 2) & 1u;
    s->play_is_select = (v >> 3) & 1u; s->select_key = (v >> 4) & 3u; s->meter = (v >> 6) & 1u; s->theme = (uint8_t)lsdj_set_theme_of(v);
    s->bluetooth = (v >> 10) & 3u; s->popup = (v >> 13) & 3u;
    return 1;
}

/* Piano key indices (0 = F3) used by the Select key option. */
#define LSDJ_KEY_FS5 25u   /* first black key from the right */
#define LSDJ_KEY_DS5 22u   /* second black key from the right */

/* The settings menu: a list, one setting per line in large type. The SELECT
   knob moves between the lines, KNOB 1-4 change the selected one, GLO closes
   (and keeps the changes). Drawn with lsdj_ov_list (lsdj_overlay.h). */
enum { LSDJ_SET_KNOBS, LSDJ_SET_PRESETS_DIR, LSDJ_SET_ALGO_DIR, LSDJ_SET_PLAYREC, LSDJ_SET_SELKEY,
       LSDJ_SET_METER, LSDJ_SET_THEME, LSDJ_SET_POPUP, LSDJ_SET_BT, LSDJ_SET_ITEMS };

typedef struct {
    uint8_t open, cursor, changed, page;   /* page: LSDJ_PAGE_* (the top, or a category) */
} lsdj_menu_t;

static void lsdj_menu_open(lsdj_menu_t *m) { m->open = 1; m->changed = 0; m->page = 0; m->cursor = 0; }

static unsigned lsdj_set_get(const lsdj_settings_t *s, unsigned item) {
    switch (item) {
    case LSDJ_SET_KNOBS: return s->knobs_swapped;
    case LSDJ_SET_PRESETS_DIR: return s->presets_rev;
    case LSDJ_SET_ALGO_DIR: return s->algo_rev;
    case LSDJ_SET_PLAYREC: return s->play_is_select;
    case LSDJ_SET_SELKEY: return s->select_key;
    case LSDJ_SET_THEME: return s->theme;
    case LSDJ_SET_BT: return s->bluetooth;
    case LSDJ_SET_POPUP: return s->popup;
    default: return s->meter;
    }
}
static unsigned lsdj_set_choices(unsigned item) {
    return item == LSDJ_SET_SELKEY ? 3u : item == LSDJ_SET_THEME ? LSDJ_SET_THEMES : item == LSDJ_SET_BT ? LSDJ_SET_BT_MODES : item == LSDJ_SET_POPUP ? LSDJ_SET_POPUPS : 2u;
}
/* A setting moves one choice per click, clockwise up, and stops at the ends. */
static int lsdj_set_turn(lsdj_settings_t *s, unsigned item, int steps) {
    if (item >= LSDJ_SET_ITEMS || !steps) return 0;
    int v = (int)lsdj_set_get(s, item) + steps, n = (int)lsdj_set_choices(item);
    uint8_t to = (uint8_t)(v < 0 ? 0 : v >= n ? n - 1 : v);
    if (to == lsdj_set_get(s, item)) return 0;
    switch (item) {
    case LSDJ_SET_KNOBS: s->knobs_swapped = to; break;
    case LSDJ_SET_PRESETS_DIR: s->presets_rev = to; break;
    case LSDJ_SET_ALGO_DIR: s->algo_rev = to; break;
    case LSDJ_SET_PLAYREC: s->play_is_select = to; break;
    case LSDJ_SET_SELKEY: s->select_key = to; break;
    case LSDJ_SET_THEME: s->theme = to; break;
    case LSDJ_SET_BT: s->bluetooth = to; break;
    case LSDJ_SET_POPUP: s->popup = to; break;
    default: s->meter = to; break;
    }
    return 1;
}
static uint32_t lsdj_set_put(char *d, uint32_t n, const char *t) { while (*t && n < 19u) d[n++] = *t++; d[n] = 0; return n; }
/* The full line for a setting (up to 19 characters), the short value (up to 5). */
static void lsdj_set_line(const lsdj_settings_t *s, unsigned item, char line[20]) {
    static const char *const sel[3] = {"OFF", "F#5", "D#5"};
    uint32_t n = 0;
    switch (item) {
    case LSDJ_SET_KNOBS: lsdj_set_put(line, 0, s->knobs_swapped ? "PRESETS: LEFT/RIGHT" : "PRESETS: UP/DOWN"); break;
    case LSDJ_SET_PRESETS_DIR: n = lsdj_set_put(line, 0, "PRESETS DIR: "); lsdj_set_put(line, n, s->presets_rev ? "REV" : "NORMAL"); break;
    case LSDJ_SET_ALGO_DIR: n = lsdj_set_put(line, 0, "ALGO DIR: "); lsdj_set_put(line, n, s->algo_rev ? "REV" : "NORMAL"); break;
    case LSDJ_SET_PLAYREC: lsdj_set_put(line, 0, s->play_is_select ? "PLAY=SEL REC=START" : "PLAY=START REC=SEL"); break;
    case LSDJ_SET_SELKEY: n = lsdj_set_put(line, 0, "SELECT KEY: "); lsdj_set_put(line, n, sel[s->select_key % 3u]); break;
    case LSDJ_SET_THEME: n = lsdj_set_put(line, 0, "THEME: "); lsdj_set_put(line, n, lsdj_set_theme_name[s->theme % LSDJ_SET_THEMES]); break;
    case LSDJ_SET_POPUP: n = lsdj_set_put(line, 0, "POPUP TIME: "); lsdj_set_put(line, n, lsdj_set_popup_name[s->popup % LSDJ_SET_POPUPS]); break;
    case LSDJ_SET_BT: n = lsdj_set_put(line, 0, "BLUETOOTH: "); lsdj_set_put(line, n, lsdj_set_bt_name[s->bluetooth % LSDJ_SET_BT_MODES]); break;
    default: n = lsdj_set_put(line, 0, "CPU METER: "); lsdj_set_put(line, n, s->meter ? "ON" : "OFF"); break;
    }
}
/* The menu in categories: the top page lists them (CONTROLS, DISPLAY, MIDI);
   each opens its own page, which starts with a BACK line. Knobs only: the
   SELECT knob moves between the lines, KNOB 1-4 change the selected setting,
   open the selected category or, on BACK, go back. GLO closes (and keeps the
   changes). A page entry: a setting (< 0x40), a MIDI option (0x40 + item,
   lsdj_midi.h), a category (0x80 + page), BACK (0xFF). */
enum { LSDJ_PAGE_TOP, LSDJ_PAGE_CONTROLS, LSDJ_PAGE_DISPLAY, LSDJ_PAGE_MIDI, LSDJ_PAGES };
#define LSDJ_MENU_MIDI(i) (0x40u + (i))
#define LSDJ_MENU_CAT(p) (0x80u + (p))
#define LSDJ_MENU_BACK 0xFFu
#define LSDJ_MENU_MAX (LSDJ_MIDI_ITEMS + 2u)   /* the longest page: BACK, the MIDI options, BLUETOOTH */
static const char *const lsdj_page_name[LSDJ_PAGES] = {"SETTINGS", "CONTROLS", "DISPLAY", "MIDI"};
static unsigned lsdj_menu_entries(unsigned page, uint8_t e[LSDJ_MENU_MAX]) {
    static const uint8_t top[] = {LSDJ_MENU_CAT(LSDJ_PAGE_CONTROLS), LSDJ_MENU_CAT(LSDJ_PAGE_DISPLAY), LSDJ_MENU_CAT(LSDJ_PAGE_MIDI)};
    static const uint8_t controls[] = {LSDJ_MENU_BACK, LSDJ_SET_KNOBS, LSDJ_SET_PRESETS_DIR, LSDJ_SET_ALGO_DIR, LSDJ_SET_PLAYREC, LSDJ_SET_SELKEY};
    static const uint8_t display[] = {LSDJ_MENU_BACK, LSDJ_SET_THEME, LSDJ_SET_POPUP, LSDJ_SET_METER};
    unsigned n = 0;
    switch (page) {
    case LSDJ_PAGE_CONTROLS: for (unsigned i = 0; i < sizeof controls; i++) e[n++] = controls[i]; break;
    case LSDJ_PAGE_DISPLAY: for (unsigned i = 0; i < sizeof display; i++) e[n++] = display[i]; break;
    case LSDJ_PAGE_MIDI:
        e[n++] = LSDJ_MENU_BACK;
        for (unsigned i = 0; i < LSDJ_MIDI_ITEMS; i++) e[n++] = (uint8_t)LSDJ_MENU_MIDI(i);
        e[n++] = LSDJ_SET_BT;
        break;
    default: for (unsigned i = 0; i < sizeof top; i++) e[n++] = top[i]; break;
    }
    return n;
}
static uint8_t lsdj_menu_entry(const lsdj_menu_t *m) {
    uint8_t e[LSDJ_MENU_MAX]; unsigned n = lsdj_menu_entries(m->page, e);
    return e[m->cursor < n ? m->cursor : n - 1u];
}
/* SELECT knob clicks move the selection (stopping at the ends); KNOB 1-4 clicks
   change the selected setting, open a category, or go BACK. Returns 1 if a
   setting changed, 2 if a MIDI option did, 0 otherwise. */
static int lsdj_menu_knobs(lsdj_menu_t *m, lsdj_settings_t *s, lsdj_midi_t *mi, const int32_t steps[4], int32_t select) {
    uint8_t e[LSDJ_MENU_MAX]; unsigned n = lsdj_menu_entries(m->page, e);
    if (select) {
        int c = (int)m->cursor + (int)select;
        m->cursor = (uint8_t)(c < 0 ? 0 : c >= (int)n ? (int)n - 1 : c);
    }
    if (m->cursor >= n) m->cursor = (uint8_t)(n - 1u);
    uint8_t it = e[m->cursor];
    /* on THEME, K2 jumps 8 themes a click (there are many) */
    int st = (int)steps[0] + (int)steps[1] * (it == LSDJ_SET_THEME ? 8 : 1) + (int)steps[2] + (int)steps[3];
    if (!st) return 0;
    if (it == LSDJ_MENU_BACK) {   /* back to the top, on the category we came from */
        m->cursor = (uint8_t)(m->page - 1u); m->page = LSDJ_PAGE_TOP; return 0;
    }
    if (it >= LSDJ_MENU_CAT(0)) { m->page = (uint8_t)(it - LSDJ_MENU_CAT(0)); m->cursor = 1; return 0; }   /* open: on its first setting */
    if (it >= LSDJ_MENU_MIDI(0)) { if (lsdj_midi_turn(mi, it - LSDJ_MENU_MIDI(0), st)) { m->changed = 1; return 2; } return 0; }
    if (lsdj_set_turn(s, it, st)) { m->changed = 1; return 1; }
    return 0;
}
/* The open page as lines: its title, a line per entry (lines[1 + entry]), and
   the bottom line (hint, or how to use the selected line). Up to 19 characters
   each. Returns the number of lines. */
#define LSDJ_MENU_LINES (LSDJ_MENU_MAX + 2u)
static unsigned lsdj_menu_lines(const lsdj_menu_t *m, const lsdj_settings_t *s, const lsdj_midi_t *mi, const char *hint,
                                char lines[LSDJ_MENU_LINES][20]) {
    uint8_t e[LSDJ_MENU_MAX]; unsigned n = lsdj_menu_entries(m->page, e);
    uint32_t k = lsdj_set_put(lines[0], 0, "SETTINGS");
    if (m->page != LSDJ_PAGE_TOP) { k = lsdj_set_put(lines[0], k, "/"); lsdj_set_put(lines[0], k, lsdj_page_name[m->page % LSDJ_PAGES]); }
    for (unsigned i = 0; i < n; i++) {
        uint8_t it = e[i];
        if (it == LSDJ_MENU_BACK) lsdj_set_put(lines[1 + i], 0, "BACK");
        else if (it >= LSDJ_MENU_CAT(0)) lsdj_set_put(lines[1 + i], 0, lsdj_page_name[(it - LSDJ_MENU_CAT(0)) % LSDJ_PAGES]);
        else if (it >= LSDJ_MENU_MIDI(0)) lsdj_midi_line(mi, it - LSDJ_MENU_MIDI(0), lines[1 + i]);
        else lsdj_set_line(s, it, lines[1 + i]);
    }
    uint8_t sel = e[m->cursor < n ? m->cursor : n - 1u];
    lsdj_set_put(lines[n + 1u], 0, hint ? hint : sel == LSDJ_SET_THEME ? "K1 NEXT  K2 JUMP 8" : sel == LSDJ_MENU_BACK ? "K1=BACK  GLO=CLOSE" :
                 sel >= LSDJ_MENU_CAT(0) && sel != LSDJ_MENU_BACK ? "SELECT=PICK K1=OPEN" : "SELECT=PICK K1=SET");
    return n + 2u;
}
