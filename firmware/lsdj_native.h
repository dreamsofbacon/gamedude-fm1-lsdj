/* SPDX-License-Identifier: GPL-3.0-only */
/* Drawing in LSDj's own style: into the 160x144 Game Boy picture (before it is
   scaled to the LCD), on LSDj's 8x8 character grid (20 x 18), with LSDj's own
   font (its tiles in video RAM) and its shades. The firmware's panels, lists and
   messages look like part of LSDj and follow the theme with it.
   LSDj 9.3.9's font (harness/lsdj_drive.c fontsheet): A-Z 0x0E.., 0-9 0x04..,
   space 0x03, '-' 0x28, '#' 0x29, '?' 0x2A, '!' 0x2B, '.' 0x2F, ':' 0x30,
   '=' 0x31, '+' 0x3C, '/' 0x47; the same characters as LSDj's value fields
   (dark on light) at +0x45. Other characters draw as space.
   Pure logic: tests/lsdj_native.c; on real LSDj: harness/native_shots.sh. */
#pragma once
#include <stdint.h>
#include "lsdj_overlay.h"

#define LSDJ_NAT_W 160u
#define LSDJ_NAT_COLS 20u
#define LSDJ_NAT_ROWS 18u

typedef struct {
    uint16_t *px;                  /* the 160x144 picture */
    const uint8_t *vram;
    uint8_t lcdc, bgp;
    const uint16_t *shade;         /* the four shades in use (lightest first) */
} lsdj_nat_t;

static int lsdj_nat_tile(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return 0x0E + (ch - 'A');
    if (ch >= '0' && ch <= '9') return 0x04 + (ch - '0');
    switch (ch) {
    case '-': return 0x28; case '#': return 0x29; case '?': return 0x2A; case '!': return 0x2B;
    case '.': return 0x2F; case ':': return 0x30; case '=': return 0x31; case '+': return 0x3C; case '/': return 0x47;
    }
    return 0x03;
}
/* The tile of ch as one of LSDj's value fields (dark on light): +0x45 for the
   characters that have one (space .. '!'), else the plain tile. */
static int lsdj_nat_field_tile(char ch) {
    int t = lsdj_nat_tile(ch);
    return t <= 0x2B ? t + 0x45 : t;
}
static uint16_t lsdj_nat_col(const lsdj_nat_t *n, unsigned i) { return n->shade[(n->bgp >> (2u * i)) & 3u]; }

static void lsdj_nat_tile_at(const lsdj_nat_t *n, unsigned col, unsigned row, int t) {
    if (col >= LSDJ_NAT_COLS || row >= LSDJ_NAT_ROWS) return;
    unsigned base = (n->lcdc & 0x10u) ? (unsigned)t * 16u : (t < 128 ? 0x1000u + (unsigned)t * 16u : 0x0800u + (unsigned)(t - 128) * 16u);
    for (unsigned y = 0; y < 8u; y++) {
        uint8_t lo = n->vram[base + y * 2u], hi = n->vram[base + y * 2u + 1u];
        for (unsigned x = 0; x < 8u; x++) {
            unsigned i = ((lo >> (7u - x)) & 1u) | (((hi >> (7u - x)) & 1u) << 1);
            n->px[(row * 8u + y) * LSDJ_NAT_W + col * 8u + x] = lsdj_nat_col(n, i);
        }
    }
}
/* Text at (col, row), at most max characters; field: as LSDj's value fields. */
static void lsdj_nat_text(const lsdj_nat_t *n, unsigned col, unsigned row, const char *s, unsigned max, int field) {
    for (unsigned k = 0; s && s[k] && k < max; k++)
        lsdj_nat_tile_at(n, col + k, row, field ? lsdj_nat_field_tile(s[k]) : lsdj_nat_tile(s[k]));
}
/* Character cells cleared to LSDj's background (colour index 0). */
static void lsdj_nat_clear(const lsdj_nat_t *n, unsigned col, unsigned row, unsigned w, unsigned h) {
    uint16_t c = lsdj_nat_col(n, 0);
    for (unsigned y = row * 8u; y < (row + h) * 8u && y < 144u; y++)
        for (unsigned x = col * 8u; x < (col + w) * 8u && x < LSDJ_NAT_W; x++) n->px[y * LSDJ_NAT_W + x] = c;
}
/* ---------- the effects / cue / tempo panel: in the song table's format ----------
   It covers the song area (columns 0..14) down to row 15 and is laid out like a
   mixer, knob k in channel k's 2-wide data column (column 3+3k): row 0 the
   title, row 1 the turned knob ("K1 DRIVE 08") or the hint as a value field
   across the panel, rows 2..13 the controls; row 15 their values.
   Effects amounts have eight segments of 0x10 raw settings, partial fill and
   a hex ruler. Discrete modes have named choices, lowest at the bottom, with
   only the selected choice inverted. Cue/tempo keep their song-cell bars.
   Rows 16..17 are the knob names (lsdj_nat_names), drawn with the
   panel (board.c native_draw). Channel identity stays in the text, not a
   column tint. LSDj's right panel stays. */
#define LSDJ_NAT_BAR_TOP 2u
#define LSDJ_NAT_QUAD 4u        /* LSDj's rows come in groups of four */
#define LSDJ_NAT_BAR_CELLS 12u  /* three groups: rows 2..13 = song rows 00..0B */
#define LSDJ_NAT_PANEL 15u   /* columns 0..14: LSDj's right panel and screen map (15..19) stay */
#define LSDJ_NAT_VALUE_ROW 15u
#define LSDJ_NAT_NAME_ROW 16u   /* rows 16..17 */
#define LSDJ_NAT_AMOUNT_STEPS 8u
#define LSDJ_NAT_AMOUNT_HEIGHT 12u  /* 96 pixels / eight 0x10 steps */
static unsigned lsdj_nat_amount_fill(unsigned value, unsigned step) {
    if (value > 127u) value = 127u;
    unsigned base = step * 16u, part = value > base ? value - base : 0u;
    if (part > 16u) part = 16u;
    return (part * (LSDJ_NAT_AMOUNT_HEIGHT - 1u) + 15u) / 16u;
}
static void lsdj_nat_amount(const lsdj_nat_t *n, unsigned col, unsigned value, uint16_t accent) {
    uint16_t bg = lsdj_nat_col(n, 0), ink = lsdj_nat_col(n, 3);
    for (unsigned step = 0; step < LSDJ_NAT_AMOUNT_STEPS; step++) {
        unsigned top = LSDJ_NAT_BAR_TOP * 8u + (LSDJ_NAT_AMOUNT_STEPS - 1u - step) * LSDJ_NAT_AMOUNT_HEIGHT;
        unsigned fill = lsdj_nat_amount_fill(value, step);
        for (unsigned y = 0; y < LSDJ_NAT_AMOUNT_HEIGHT; y++) {
            uint16_t color = y >= LSDJ_NAT_AMOUNT_HEIGHT - fill ? accent : bg;
            for (unsigned x = 0; x < 16u; x++) n->px[(top + y) * LSDJ_NAT_W + col * 8u + x] = color;
        }
        if (!fill) {
            for (unsigned x = 2; x < 6u; x++) n->px[(top + 6u) * LSDJ_NAT_W + col * 8u + x] = ink;
            for (unsigned x = 10; x < 14u; x++) n->px[(top + 6u) * LSDJ_NAT_W + col * 8u + x] = ink;
        }
    }
}
static void lsdj_nat_bars(const lsdj_nat_t *n, const lsdj_ov_bars_t *b, uint16_t accent) {
    lsdj_nat_clear(n, 0, 0, LSDJ_NAT_PANEL, LSDJ_NAT_NAME_ROW);
    lsdj_nat_text(n, 0, 0, b->title, LSDJ_NAT_PANEL, 0);
    lsdj_nat_text(n, 0, 1, "               ", LSDJ_NAT_PANEL, 1);
    lsdj_nat_text(n, 0, 1, b->big, LSDJ_NAT_PANEL, 1);
    if (b->amount_scale) {
        static const char *const ticks[5] = {"7F", "60", "40", "20", "00"};
        for (unsigned i = 0; i < 5u; i++) lsdj_nat_text(n, 0, LSDJ_NAT_BAR_TOP + i * 3u, ticks[i], 2, 0);
    }
    for (unsigned k = 0; k < 4u; k++) {
        if (!b->label[k][0]) continue;
        unsigned c = 3u + 3u * k, hot = (int)k == b->hot;
        lsdj_nat_text(n, c, LSDJ_NAT_VALUE_ROW, b->value[k], 2, (int)hot);
        if (b->nchoice[k] >= 2u && b->nchoice[k] <= 6u) {
            unsigned rows = b->nchoice[k] <= 3u ? LSDJ_NAT_QUAD : 2u;
            for (unsigned i = 0; i < b->nchoice[k]; i++) {
                unsigned top = LSDJ_NAT_BAR_TOP + LSDJ_NAT_BAR_CELLS - (i + 1u) * rows;
                lsdj_nat_text(n, c, top + (rows - 1u) / 2u, b->choice[k][i], 2, i == b->pick[k]);
            }
            continue;
        }
        if (b->amount_scale) { lsdj_nat_amount(n, c, b->level[k], accent); continue; }
        unsigned lv = b->level[k] > 127u ? 127u : b->level[k];
        unsigned fill = (lv * LSDJ_NAT_BAR_CELLS + 63u) / 127u;
        if (lv && !fill) fill = 1u;
        for (unsigned i = 0; i < LSDJ_NAT_BAR_CELLS; i++) {
            unsigned row = LSDJ_NAT_BAR_TOP + LSDJ_NAT_BAR_CELLS - 1u - i;
            lsdj_nat_text(n, c, row, i < fill ? "  " : "--", 2, i < fill);
        }
    }
    if (!b->amount_scale) {   /* cue/tempo retain the song-row grouping */
        uint16_t bg = lsdj_nat_col(n, 0);
        for (unsigned g = 1; g < LSDJ_NAT_BAR_CELLS / LSDJ_NAT_QUAD; g++) {
            unsigned y = (LSDJ_NAT_BAR_TOP + g * LSDJ_NAT_QUAD) * 8u;
            for (unsigned k = 0; k < 4u; k++) {
                if (!b->label[k][0]) continue;
                unsigned x0 = (3u + 3u * k) * 8u;
                for (unsigned yy = y - 1u; yy <= y; yy++)
                    for (unsigned x = x0; x < x0 + 16u; x++) n->px[yy * LSDJ_NAT_W + x] = bg;
            }
        }
    }
}

/* ---------- the knobs' names, with the panel (LSDj's bottom two rows) ----------
   Rows 16..17, columns 0..14, two knobs a row: the knob's number as a value
   field, then its name (up to 6 characters): "1DRIVE 2FILTER" / "3CUTOFF4RESO".
   The knob just turned (hot, -1 none) has its name as a value field too. */
static void lsdj_nat_names(const lsdj_nat_t *n, const char names[4][7], int hot) {
    lsdj_nat_clear(n, 0, LSDJ_NAT_NAME_ROW, LSDJ_NAT_PANEL, 2);
    for (unsigned k = 0; k < 4u; k++) {
        unsigned row = LSDJ_NAT_NAME_ROW + k / 2u, col = (k % 2u) * 7u;   /* column 14 stays blank before LSDj's map */
        char d[2] = {(char)('1' + k), 0};
        lsdj_nat_text(n, col, row, d, 1, 1);
        lsdj_nat_text(n, col + 1u, row, names[k], 6, (int)k == hot);
    }
}

/* ---------- the pages, in LSDj's screen map (columns 15..19, rows 15..17) ----------
   While the effects panel is open the map shows the pages the way LSDj shows its
   screens (its map letters are the value-field tiles; the screen you are on is
   that tile +0x45 again, e.g. S 0x65 / 0xAA): "FX" over a row of page letters. */
static void lsdj_nat_pagemap(const lsdj_nat_t *n, const char *letters, unsigned page) {
    lsdj_nat_clear(n, LSDJ_NAT_PANEL, 15, LSDJ_NAT_COLS - LSDJ_NAT_PANEL, 3);
    lsdj_nat_text(n, LSDJ_NAT_PANEL, 15, "FX", 2, 0);
    for (unsigned i = 0; letters[i] && LSDJ_NAT_PANEL + i < LSDJ_NAT_COLS; i++) {
        int t = lsdj_nat_field_tile(letters[i]);
        lsdj_nat_tile_at(n, LSDJ_NAT_PANEL + i, 16, i == page ? t + 0x45 : t);
    }
}

/* ---------- a list (settings), like an LSDj screen ----------
   lines[0] the title (row 0), lines[1..count-2] the entries (rows 2..), the
   selected one as a value field; lines[count-1] the bottom row (17). The whole
   screen but LSDj's right panel's column 19 stays usable: 20 columns. */
static void lsdj_nat_list(const lsdj_nat_t *n, const char lines[][20], unsigned count, int hi) {
    lsdj_nat_clear(n, 0, 0, LSDJ_NAT_COLS, LSDJ_NAT_ROWS);
    if (count < 2u) return;
    lsdj_nat_text(n, 0, 0, lines[0], LSDJ_NAT_COLS, 0);
    /* rows 2..16 hold 15 entries; a longer list scrolls to keep the selected one in view */
    unsigned entries = count - 2u, rows = LSDJ_NAT_ROWS - 3u, first = 0;
    if (entries > rows && hi >= (int)rows) first = (unsigned)hi - rows + 1u;
    for (unsigned r = 0; r < rows && first + r < entries; r++) {
        unsigned e = first + r; int sel = (int)e == hi;
        if (sel) lsdj_nat_text(n, 0, r + 2u, "                    ", LSDJ_NAT_COLS, 1);
        lsdj_nat_text(n, 0, r + 2u, lines[1u + e], LSDJ_NAT_COLS, sel);
    }
    lsdj_nat_text(n, 0, LSDJ_NAT_ROWS - 1u, lines[count - 1u], LSDJ_NAT_COLS, 0);
}

/* ---------- a message: LSDj's bottom two rows ---------- */
static void lsdj_nat_message(const lsdj_nat_t *n, const char *l1, const char *l2) {
    unsigned rows = l2 && l2[0] ? 2u : 1u;
    lsdj_nat_clear(n, 0, LSDJ_NAT_ROWS - rows, LSDJ_NAT_COLS, rows);
    lsdj_nat_text(n, 0, LSDJ_NAT_ROWS - rows, l1, LSDJ_NAT_COLS, 0);
    if (rows == 2u) lsdj_nat_text(n, 0, LSDJ_NAT_ROWS - 1u, l2, LSDJ_NAT_COLS, 0);
}

/* ---------- LSDj's mode display, while it is out of date (SEQ) ----------
   mode 1 song / 2 live: the screen map's S / L (cell 15,16) and, on the SONG
   screen, its title (row 0) - in LSDj's own font, as LSDj would draw them. */
static void lsdj_nat_mode(const lsdj_nat_t *n, unsigned mode, int song_screen) {
    if (!mode) return;
    lsdj_nat_tile_at(n, 15, 16, lsdj_nat_tile(mode == 2u ? 'L' : 'S'));
    if (song_screen) lsdj_nat_text(n, 0, 0, mode == 2u ? "LIVE" : "SONG", 4, 0);
}
