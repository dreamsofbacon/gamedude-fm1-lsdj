/* SPDX-License-Identifier: GPL-3.0-only */
/* On-screen messages for the FM-1's own controls (HOME, OCT, SAVE...), so a
   user learns what a press did. A box at the bottom of the game picture shows
   up to two lines of 19 characters for LSDJ_OV_MS milliseconds.

   board.c calls lsdj_ov_draw() on the 240x216 screen buffer just before it is
   sent to the LCD; the next game frame is scaled into the buffer from scratch,
   so the box disappears by itself once it is no longer drawn. Pure logic (no
   hardware calls): tests/lsdj_overlay.c renders every message to an image. */
#pragma once
#include <stdint.h>
/* No string.h: the firmware's freestanding libc has only mem* functions. */

#define LSDJ_OV_W      240u
#define LSDJ_OV_H      216u
#define LSDJ_OV_COLS   19u
static uint32_t lsdj_ov_ms = 1500u;   /* how long pop-ups stay: the POPUP TIME setting */
#define LSDJ_OV_MS     lsdj_ov_ms
#define LSDJ_OV_SCALE  2u          /* 5x7 glyphs drawn 10x14 */
#define LSDJ_OV_PITCH  12u         /* 10 px glyph + 2 px gap */
#define LSDJ_OV_LINE_H 18u
/* The screen buffer holds RGB565 byte-swapped (it goes to the LCD as is). */
#define LSDJ_OV_SWAP(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))
/* The overlay's colours come from the theme (firmware/lsdj_theme.h sets them):
   text, the dark of boxes and outlines, the accent (what is active), the empty
   part of a bar, captions. Stored byte-swapped, as the LCD takes them. */
typedef struct { uint16_t fg, bg, accent, dim, dim2; } lsdj_ov_colors_t;
static lsdj_ov_colors_t lsdj_ov_col = {
    LSDJ_OV_SWAP(0xFFFFu), LSDJ_OV_SWAP(0x18E3u), LSDJ_OV_SWAP(0xFD20u), LSDJ_OV_SWAP(0x4208u), LSDJ_OV_SWAP(0x9CD3u) };
#define LSDJ_OV_FG     (lsdj_ov_col.fg)
#define LSDJ_OV_BG     (lsdj_ov_col.bg)
#define LSDJ_OV_EDGE   (lsdj_ov_col.accent)

/* 5x7 glyphs: one byte per row, bit 4 = the leftmost column. Characters not
   listed draw as blank. (Bytes, not "#...#" strings: 357 bytes instead of 3.5 KB.) */
static const char lsdj_ov_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-+!?/()=',#_";
static const uint8_t lsdj_ov_font[][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},   /* space */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},   /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},   /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},   /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},   /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},   /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},   /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},   /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},   /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},   /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},   /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},   /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},   /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},   /* M */
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11},   /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},   /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},   /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},   /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},   /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},   /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},   /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},   /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},   /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},   /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},   /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},   /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},   /* Z */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},   /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},   /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},   /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},   /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},   /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},   /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},   /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},   /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},   /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},   /* 9 */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},   /* . */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},   /* : */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},   /* - */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},   /* + */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},   /* ! */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},   /* ? */
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00},   /* / */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02},   /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},   /* ) */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},   /* = */
    {0x04,0x04,0x08,0x00,0x00,0x00,0x00},   /* ' */
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08},   /* , */
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},   /* # */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},   /* _ */
};

typedef struct {
    char line[2][LSDJ_OV_COLS + 1];
    uint32_t until_ms;              /* 0 = nothing shown */
    uint32_t lift;                  /* px above the bottom (board: the effects pop-up's line is there) */
} lsdj_ov_t;

/* Show a message (second line may be NULL) for LSDJ_OV_MS from now_ms. */
static uint32_t lsdj_ov_len(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static void lsdj_ov_show(lsdj_ov_t *o, const char *l1, const char *l2, uint32_t now_ms) {
    const char *src[2] = {l1, l2};
    for (unsigned i = 0; i < 2; i++) {
        uint32_t n = 0;
        while (src[i] && src[i][n] && n < LSDJ_OV_COLS) { o->line[i][n] = src[i][n]; n++; }
        o->line[i][n] = 0;
    }
    o->until_ms = (now_ms + LSDJ_OV_MS) | 1u;
}

static int lsdj_ov_active(const lsdj_ov_t *o, uint32_t now_ms) {
    return o->until_ms && (int32_t)(o->until_ms - now_ms) > 0;
}

/* See-through boxes: the picture under a box is darkened to a quarter, so LSDj
   stays visible around and behind every text. */
static void lsdj_ov_dim(uint16_t *scr, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)LSDJ_OV_W) x1 = (int)LSDJ_OV_W;
    if (y1 > (int)LSDJ_OV_H) y1 = (int)LSDJ_OV_H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint16_t c = LSDJ_OV_SWAP(scr[y * (int)LSDJ_OV_W + x]);
            scr[y * (int)LSDJ_OV_W + x] = LSDJ_OV_SWAP((uint16_t)((c >> 2) & 0x39E7u));
        }
}
static void lsdj_ov_glyph_s(uint16_t *scr, uint32_t x0, uint32_t y0, char c, uint16_t fg, uint32_t s);
/* Text at (x, y) in its own see-through box (2 px around), scale s. Returns its width. */
static uint32_t lsdj_ov_label(uint16_t *scr, uint32_t x, uint32_t y, const char *t, uint16_t fg, uint32_t s) {
    uint32_t n = 0; while (t && t[n]) n++;
    if (!n) return 0;
    uint32_t w = n * 6u * s - s;
    lsdj_ov_dim(scr, (int)x - 2, (int)y - 2, (int)(x + w) + 2, (int)(y + 7u * s) + 2);
    for (uint32_t k = 0; k < n; k++) lsdj_ov_glyph_s(scr, x + k * 6u * s, y, t[k], fg, s);
    return w;
}
/* Draw the message, if one is showing: each line in its own see-through box,
   centred, at the bottom (o->lift px higher, above the effects pop-up's line). */
static void lsdj_ov_draw(lsdj_ov_t *o, uint16_t *scr, uint32_t now_ms) {
    if (!lsdj_ov_active(o, now_ms)) { o->until_ms = 0; return; }
    uint32_t lines = o->line[1][0] ? 2u : 1u;
    for (uint32_t i = 0; i < lines; i++) {
        uint32_t n = lsdj_ov_len(o->line[i]), w = n * LSDJ_OV_PITCH - 2u;
        uint32_t y = LSDJ_OV_H - 16u - o->lift - (lines - 1u - i) * LSDJ_OV_LINE_H;
        lsdj_ov_label(scr, (LSDJ_OV_W - w) / 2u, y, o->line[i], LSDJ_OV_FG, LSDJ_OV_SCALE);
    }
}

/* Every message, in one place (the test renders them all). Max 19 characters. */
enum { LSDJ_MSG_NOTE_MODE, LSDJ_MSG_BUTTON_MODE, LSDJ_MSG_OCT_DOWN, LSDJ_MSG_OCT_UP, LSDJ_MSG_OCT_BUTTONS,
       LSDJ_MSG_SAVING, LSDJ_MSG_SAVED, LSDJ_MSG_NO_CHANGES, LSDJ_MSG_SAVE_PLAYING, LSDJ_MSG_SAVE_FAILED,
       LSDJ_MSG_LOADED, LSDJ_MSG_SETTINGS_SAVED, LSDJ_MSG_SETTINGS_FAILED, LSDJ_MSG_SAVING_PLAYING,
       LSDJ_MSG_COUNT };
static const char *const lsdj_ov_msg[LSDJ_MSG_COUNT][2] = {
    {"NOTE MODE",        "SET LSDJ SYNC=KEYBD"},
    {"BUTTON MODE",      "KEYS = GAME BOY PAD"},
    {"NOTE OCTAVE DOWN", 0},
    {"NOTE OCTAVE UP",   0},
    {"OCT: NOTE MODE",   "PRESS HOME FIRST"},
    {"SAVING...",        "DO NOT SWITCH OFF"},
    {"SAVED",            0},
    {"NOTHING TO SAVE",  "ALREADY SAVED"},
    {"STOP THE SONG",    "THEN PRESS SAVE"},
    {"SAVE FAILED",      "LAST SAVE IS KEPT"},
    {"SONGS LOADED",     0},
    {"SETTINGS SAVED",   0},
    {"SETTINGS NOT SAVED", "FLASH ERROR"},
    {"SAVING...",        "MUSIC KEEPS PLAYING"},
};
static void lsdj_ov_say(lsdj_ov_t *o, unsigned msg, uint32_t now_ms) {
    if (msg < LSDJ_MSG_COUNT) lsdj_ov_show(o, lsdj_ov_msg[msg][0], lsdj_ov_msg[msg][1], now_ms);
}

/* OCT- / OCT+ in note mode: "NOTE OCTAVE UP" + "OCTAVE NOW +1" (or 0, -2...). */
static void lsdj_ov_octave(lsdj_ov_t *o, int went_down, int octave, uint32_t now_ms) {
    char now_at[16] = "OCTAVE NOW ";
    uint32_t n = 11;
    if (octave) now_at[n++] = octave < 0 ? '-' : '+';
    now_at[n++] = (char)('0' + (octave < 0 ? -octave : octave));
    now_at[n] = 0;
    lsdj_ov_show(o, lsdj_ov_msg[went_down ? LSDJ_MSG_OCT_DOWN : LSDJ_MSG_OCT_UP][0], now_at, now_ms);
}

/* A full panel (the settings menu): n lines from the top, line `hi` highlighted. */
#define LSDJ_OV_HI_BG  (lsdj_ov_col.accent)
static void lsdj_ov_panel(uint16_t *scr, const char lines[][20], uint32_t n, int hi) {
    /* Each line in its own see-through box, the highlighted one amber. */
    if (n * LSDJ_OV_LINE_H + 4u > LSDJ_OV_H) n = (LSDJ_OV_H - 4u) / LSDJ_OV_LINE_H;
    uint32_t y0 = (LSDJ_OV_H - n * LSDJ_OV_LINE_H) / 2u + 2u;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t y = y0 + i * LSDJ_OV_LINE_H, len = lsdj_ov_len(lines[i]);
        if (!len) continue;
        if ((int)i == hi) {
            uint32_t w = len * LSDJ_OV_PITCH - 2u;
            for (uint32_t yy = y - 2u; yy < y + 16u; yy++)
                for (uint32_t x = 5u; x < 9u + w; x++) scr[yy * LSDJ_OV_W + x] = LSDJ_OV_HI_BG;
            for (uint32_t k = 0; k < len; k++) lsdj_ov_glyph_s(scr, 7u + k * LSDJ_OV_PITCH, y, lines[i][k], LSDJ_OV_BG, LSDJ_OV_SCALE);
        } else lsdj_ov_label(scr, 7u, y, lines[i], LSDJ_OV_FG, LSDJ_OV_SCALE);
    }
}

/* ---------- four columns of bars, one under each LSDj channel column ----------
   The title at the top, per column a see-through box with a bar (level 0..127),
   its value, a label and a caption (e.g. which knob), a big line at the bottom. LSDj's SONG screen has its
   channels at Game Boy x = 32, 56, 80, 104 (3 characters apart): scaled 1.5x,
   x = 48, 84, 120, 156 here. Reused for anything with four values. */
#define LSDJ_OV_BAR_X(c)  (48u + 36u * (c))
#define LSDJ_OV_BAR_W     12u
#define LSDJ_OV_BAR_TOP   52u   /* bars from y 52, values above, names below */
#define LSDJ_OV_BAR_LEN   44u
#define LSDJ_OV_DIM       (lsdj_ov_col.dim)    /* empty part of a bar */
#define LSDJ_OV_DIM2      (lsdj_ov_col.dim2)   /* captions */
typedef struct {
    char title[20];
    char label[4][7];        /* under the bar, small: 5 characters */
    char value[4][6];        /* above the label, small: 5 characters */
    char caption[4][4];      /* under the label, small: "K1" */
    char big[20];            /* bottom line, large: what just changed ("CUTOFF 64") */
    uint8_t level[4];        /* 0..127 */
    int8_t hot;              /* column drawn in amber (the knob just turned), -1 none */
    int8_t chan;             /* LSDj channel column framed (the one the effects act on), -1 none */
    /* A knob with 2..6 choices is drawn as its choices, not a bar (lsdj_nat_bars): how many
       (0: a bar), the one set, and their 2-character names, lowest first */
    uint8_t nchoice[4], pick[4];
    char choice[4][6][3];
    uint8_t amount_scale;    /* effects: eight 0x10 steps, with a shared raw-setting ruler */
    uint8_t strip;           /* hold FX: 0x80 | the muted channels (bit per channel); 0 none */
    uint8_t grey;            /* muted (GREY MUTED): bit k = knob k's column grey, 0x80 = the whole panel */
    uint32_t until_ms;
} lsdj_ov_bars_t;

static void lsdj_ov_bars_keep(lsdj_ov_bars_t *b, uint32_t now_ms) { b->until_ms = (now_ms + LSDJ_OV_MS) | 1u; }
static int lsdj_ov_bars_active(const lsdj_ov_bars_t *b, uint32_t now_ms) {
    return b->until_ms && (int32_t)(b->until_ms - now_ms) > 0;
}
/* A glyph at any scale (1 = 5x7). */
static void lsdj_ov_glyph_s(uint16_t *scr, uint32_t x0, uint32_t y0, char c, uint16_t fg, uint32_t s) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    uint32_t idx = 0;
    while (lsdj_ov_chars[idx] && lsdj_ov_chars[idx] != c) idx++;
    if (!c || !lsdj_ov_chars[idx]) return;
    const uint8_t *g = lsdj_ov_font[idx];
    for (uint32_t r = 0; r < 7; r++)
        for (uint32_t col = 0; col < 5; col++)
            if (g[r] & (0x10u >> col))
                for (uint32_t dy = 0; dy < s; dy++)
                    for (uint32_t dx = 0; dx < s; dx++) {
                        uint32_t x = x0 + col * s + dx, y = y0 + r * s + dy;
                        if (x < LSDJ_OV_W && y < LSDJ_OV_H) scr[y * LSDJ_OV_W + x] = fg;
                    }
}
/* Text centred on x at scale s (pitch 6 s). */
static void lsdj_ov_text_c(uint16_t *scr, uint32_t xc, uint32_t y, const char *t, uint16_t fg, uint32_t s) {
    uint32_t n = lsdj_ov_len(t), w = n * 6u * s - s, x = xc > w / 2u ? xc - w / 2u : 0;
    for (uint32_t k = 0; k < n; k++) lsdj_ov_glyph_s(scr, x + k * 6u * s, y, t[k], fg, s);
}
static void lsdj_ov_bars_draw(lsdj_ov_bars_t *b, uint16_t *scr, uint32_t now_ms) {
    if (!lsdj_ov_bars_active(b, now_ms)) { b->until_ms = 0; return; }
    if (b->chan >= 0 && b->chan < 4) {                                         /* the channel's whole column */
        /* a light tint of the accent (about 1/5) over LSDj's column: it shows which
           channel without pulling the eye from the bars */
        uint16_t acc = LSDJ_OV_SWAP(LSDJ_OV_EDGE);
        int ar = acc >> 11, ag = (acc >> 5) & 63, ab = acc & 31;
        int x0 = (int)LSDJ_OV_BAR_X((uint32_t)b->chan) - 18, x1 = (int)LSDJ_OV_BAR_X((uint32_t)b->chan) + 18;
        for (int y = 21; y < (int)LSDJ_OV_H - 19; y++)
            for (int x = x0; x < x1; x++) {
                uint16_t c = LSDJ_OV_SWAP(scr[y * (int)LSDJ_OV_W + x]);
                int r = c >> 11, g = (c >> 5) & 63, bl = c & 31;
                r += (ar - r) / 5; g += (ag - g) / 5; bl += (ab - bl) / 5;
                scr[y * (int)LSDJ_OV_W + x] = LSDJ_OV_SWAP((uint16_t)(r << 11 | g << 5 | bl));
            }
    }
    lsdj_ov_label(scr, 4u, 3u, b->title, LSDJ_OV_FG, 2u);                       /* top */
    lsdj_ov_label(scr, 4u, LSDJ_OV_H - 16u, b->big, LSDJ_OV_EDGE, 2u);         /* bottom */
    for (uint32_t c = 0; c < 4; c++) {                                          /* one box per column */
        if (!b->label[c][0]) continue;                                          /* unused knob */
        uint32_t xc = LSDJ_OV_BAR_X(c), bx = xc - LSDJ_OV_BAR_W / 2u, top = LSDJ_OV_BAR_TOP;
        uint16_t ink = (int)c == b->hot ? LSDJ_OV_EDGE : LSDJ_OV_FG;
        lsdj_ov_dim(scr, (int)xc - 17, (int)top - 13, (int)xc + 17, (int)(top + LSDJ_OV_BAR_LEN) + 32);
        uint32_t lv = b->level[c] > 127u ? 127u : b->level[c];
        uint32_t fill = (lv * LSDJ_OV_BAR_LEN + 63u) / 127u;
        for (uint32_t y = top; y < top + LSDJ_OV_BAR_LEN; y++)
            for (uint32_t x = bx; x < bx + LSDJ_OV_BAR_W; x++)
                scr[y * LSDJ_OV_W + x] = y >= top + LSDJ_OV_BAR_LEN - fill ? ink : LSDJ_OV_DIM;
        lsdj_ov_text_c(scr, xc, top - 10u, b->value[c], ink, 1u);
        lsdj_ov_text_c(scr, xc, top + LSDJ_OV_BAR_LEN + 4u, b->label[c], ink, 1u);
        lsdj_ov_text_c(scr, xc, top + LSDJ_OV_BAR_LEN + 14u, b->caption[c], LSDJ_OV_DIM2, 1u);
    }
}

/* ---------- a list (settings): large type, see-through ----------
   lines[0] the title (top edge), lines[1..n-2] the entries, lines[n-1] the
   bottom line (bottom edge, amber). The selected entry `hi` (0-based) is drawn
   amber with dark text; the others in their own see-through boxes. */
#define LSDJ_OV_LIST_Y0   30u
#define LSDJ_OV_LIST_STEP 22u
static void lsdj_ov_list(uint16_t *scr, const char lines[][20], uint32_t n, int hi) {
    if (n < 2u) return;
    lsdj_ov_label(scr, 4u, 3u, lines[0], LSDJ_OV_FG, 2u);
    for (uint32_t i = 1; i + 1u < n; i++) {
        uint32_t y = LSDJ_OV_LIST_Y0 + (i - 1u) * LSDJ_OV_LIST_STEP, len = lsdj_ov_len(lines[i]);
        if (!len || y + 16u > LSDJ_OV_H - 20u) continue;
        if ((int)(i - 1u) == hi) {
            uint32_t w = len * LSDJ_OV_PITCH - 2u;
            for (uint32_t yy = y - 2u; yy < y + 16u; yy++)
                for (uint32_t x = 5u; x < 9u + w && x < LSDJ_OV_W; x++) scr[yy * LSDJ_OV_W + x] = LSDJ_OV_HI_BG;
            for (uint32_t k = 0; k < len; k++) lsdj_ov_glyph_s(scr, 7u + k * LSDJ_OV_PITCH, y, lines[i][k], LSDJ_OV_BG, 2u);
        } else lsdj_ov_label(scr, 7u, y, lines[i], LSDJ_OV_FG, 2u);
    }
    lsdj_ov_label(scr, 4u, LSDJ_OV_H - 16u, lines[n - 1u], LSDJ_OV_EDGE, 2u);
}

/* ---------- LSDj's mode display, covered while it is out of date ----------
   SEQ switches LSDj's live / song mode, but LSDj redraws its mode display only
   when the screen changes. Until then: mode 1 (song) / 2 (live) draws the right
   letter over the screen map's S / L (Game Boy cell 15,16: x 180..192, y 192..204
   here) and, on the SONG screen, the right word over its SONG / LIVE title
   (cells 0..3 of row 0). Amber, so it reads as the firmware's. */
static void lsdj_ov_mode_cover(uint16_t *scr, unsigned mode, int song_screen) {
    if (!mode) return;
    const char letter[2] = {mode == 2u ? 'L' : 'S', 0};
    for (uint32_t y = 190; y < 206; y++) for (uint32_t x = 179; x < 193; x++) scr[y * LSDJ_OV_W + x] = LSDJ_OV_HI_BG;
    lsdj_ov_glyph_s(scr, 181u, 191u, letter[0], LSDJ_OV_BG, 2u);
    if (song_screen) {
        for (uint32_t y = 0; y < 15; y++) for (uint32_t x = 0; x < 50; x++) scr[y * LSDJ_OV_W + x] = LSDJ_OV_HI_BG;
        const char *w = mode == 2u ? "LIVE" : "SONG";
        for (uint32_t k = 0; k < 4; k++) lsdj_ov_glyph_s(scr, 2u + k * 12u, 0u, w[k], LSDJ_OV_BG, 2u);
    }
}
