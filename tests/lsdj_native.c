/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_native.h: the panel stays inside the 160x144 picture
   and LSDj's right panel (columns 16..19), uses LSDj's font tiles for its
   characters, inverts the turned knob's value. Real LSDj: harness/native_shots.sh. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_native.h"
#include "lsdj_fx_ui.h"
#include "lsdj_theme.h"

static uint16_t buf[160 * 144 + 2 * 1024];
static uint8_t vram[0x2000];
static const uint16_t shades[4] = {0x1111, 0x2222, 0x3333, 0x4444};
#define CANARY 0x5A5Au

int main(void) {
    uint16_t *px = buf + 1024;
    for (unsigned i = 0; i < sizeof vram; i++) vram[i] = (uint8_t)(i * 7u + 3u);   /* any "font" */
    lsdj_nat_t n = {px, vram, 0xC3, 0xE4, shades};
    /* tiles: letters, digits, the few symbols */
    assert(lsdj_nat_tile('A') == 0x0E && lsdj_nat_tile('Z') == 0x27 && lsdj_nat_tile('0') == 0x04 && lsdj_nat_tile('9') == 0x0D);
    assert(lsdj_nat_tile('-') == 0x28 && lsdj_nat_tile('/') == 0x47 && lsdj_nat_tile(' ') == 0x03 && lsdj_nat_tile('+') == 0x3C && lsdj_nat_tile('#') == 0x29);
    assert(lsdj_nat_field_tile('0') == 0x49 && lsdj_nat_field_tile('A') == 0x53 && lsdj_nat_field_tile(' ') == 0x48 && lsdj_nat_field_tile('/') == 0x47);
    assert(lsdj_nat_tile('(') == 0x03);
    static uint8_t echo[LSDJ_FX_ECHO_LEN]; lsdj_fx_t fx; lsdj_fx_ui_t u; lsdj_fx_init(&fx, echo); lsdj_fx_ui_init(&u); u.cursor_ch = 3;
    for (unsigned pg = 0; pg < LSDJ_FX_PAGES; pg++)
        for (int hot = -1; hot < 4; hot++) {
            for (unsigned i = 0; i < 1024; i++) buf[i] = buf[1024 + 160 * 144 + i] = CANARY;
            for (unsigned i = 0; i < 160 * 144; i++) px[i] = 0x7777;
            lsdj_ov_bars_t b; memset(&b, 0, sizeof b);
            u.page = (uint8_t)pg; b.hot = (int8_t)hot; b.chan = 3;
            lsdj_fx_bars(&fx, &u, hot, b.title, b.big, b.label, b.value, b.caption, b.level);
            lsdj_fx_bar_choices(&fx, &u, b.nchoice, b.pick, b.choice);
            b.amount_scale = 1;
            lsdj_nat_bars(&n, &b, 0xFD20);
            for (unsigned i = 0; i < 1024; i++) assert(buf[i] == CANARY && buf[1024 + 160 * 144 + i] == CANARY);
            for (unsigned y = 0; y < 144; y++) for (unsigned x = 120; x < 160; x++) assert(px[y * 160 + x] == 0x7777);   /* right panel and screen map kept */
            assert(strlen(b.title) <= LSDJ_NAT_COLS && strlen(b.big) <= LSDJ_NAT_COLS);
            for (unsigned k = 0; k < 4; k++) assert(strlen(b.label[k]) <= 5 && strlen(b.value[k]) <= 5);
        }
    /* A list and a message stay inside the picture too. */
    {
        char lines[9][20] = {"SETTINGS", "PRESETS: UP/DOWN", "A", "B", "C", "D", "E", "F", "SELECT=PICK K1=SET"};
        for (unsigned i = 0; i < 1024; i++) buf[i] = buf[1024 + 160 * 144 + i] = CANARY;
        lsdj_nat_list(&n, (const char (*)[20])lines, 9, 2);
        lsdj_nat_message(&n, "SETTINGS NOT SAVED", "FLASH ERROR");
        lsdj_nat_mode(&n, 2, 1);
        for (unsigned i = 0; i < 1024; i++) assert(buf[i] == CANARY && buf[1024 + 160 * 144 + i] == CANARY);
    }
    /* A list longer than rows 2..16 scrolls: with the last of 20 entries selected it
       shows entries 5..19, exactly as a 15-entry list of those with the last selected. */
    {
        static char big[22][20], part[17][20];
        static uint16_t a[160 * 144];
        for (unsigned i = 0; i < 22; i++) snprintf(big[i], 20, "LINE %u", i);
        memcpy(part[0], big[0], 20); memcpy(part[16], big[21], 20);
        for (unsigned i = 0; i < 15; i++) memcpy(part[1 + i], big[1 + 5 + i], 20);
        lsdj_nat_list(&n, (const char (*)[20])big, 22, 19); memcpy(a, px, sizeof a);
        lsdj_nat_list(&n, (const char (*)[20])part, 17, 14);
        assert(!memcmp(a, px, sizeof a));
        lsdj_nat_list(&n, (const char (*)[20])big, 22, 3); memcpy(a, px, sizeof a);   /* near the top: no scroll */
        memcpy(part[1], big[1], 20); for (unsigned i = 0; i < 15; i++) memcpy(part[1 + i], big[1 + i], 20);
        lsdj_nat_list(&n, (const char (*)[20])part, 17, 3);
        assert(!memcmp(a, px, sizeof a));
    }
    /* The always-shown names: only the bottom two rows, columns 0..14; the page map:
       only LSDj's screen map (columns 15..19, rows 15..17). */
    for (unsigned pg = 0; pg < LSDJ_FX_PAGES; pg++) {
        for (unsigned i = 0; i < 160 * 144; i++) px[i] = 0x7777;
        char names[4][7]; u.page = (uint8_t)pg; lsdj_fx_names(&u, names);
        for (unsigned k = 0; k < 4; k++) assert(strlen(names[k]) >= 3 && strlen(names[k]) <= 6);   /* more than 2 letters, fits */
        lsdj_nat_names(&n, (const char (*)[7])names, (int)pg - 1);
        for (unsigned y = 0; y < 144; y++) for (unsigned x = 0; x < 160; x++)
            assert((px[y * 160 + x] != 0x7777) == (y >= 128 && x < 120));
        for (unsigned i = 0; i < 160 * 144; i++) px[i] = 0x7777;
        lsdj_nat_pagemap(&n, lsdj_fx_page_letters, pg);
        for (unsigned y = 0; y < 144; y++) for (unsigned x = 0; x < 160; x++)
            if (!(y >= 120 && x >= 120)) assert(px[y * 160 + x] == 0x7777);
    }
    assert(strlen(lsdj_fx_page_letters) == LSDJ_FX_PAGES);
    /* 0..127 values show as two hex digits, as LSDj's. */
    { char v[6]; fx.p.ch[3][FXP_CUTOFF] = 0x4F; u.page = 0; lsdj_fxk_value(&fx, &u, FXK_CUTOFF, v); assert(!strcmp(v, "4F")); }
    /* Eight 0x10 segments: exact boundaries, partial fill and full-scale 7F. */
    n.lcdc |= 0x10;
    for (unsigned v = 0; v < 128u; v++) {
        lsdj_ov_bars_t b = {0}; b.amount_scale = 1; b.hot = -1; b.chan = -1;
        strcpy(b.label[0], "CT"); b.level[0] = (uint8_t)v;
        lsdj_nat_bars(&n, &b, 0xFD20);
        for (unsigned step = 0; step < 8u; step++) {
            unsigned part = v > step * 16u ? v - step * 16u : 0u;
            if (part > 16u) part = 16u;
            unsigned expected = (part * 11u + 15u) / 16u;
            unsigned fill = lsdj_nat_amount_fill(v, step);
            assert(fill == expected);
            if (v) assert(fill >= lsdj_nat_amount_fill(v - 1u, step));
            if (v == step * 16u) assert(fill == 0);
            if (v >= (step + 1u) * 16u || v == 127u) assert(fill == 11);
            unsigned top = 16u + (7u - step) * 12u, counted = 0;
            for (unsigned y = 0; y < 12u; y++) {
                uint16_t pixel = px[(top + y) * 160u + 24u];
                assert(pixel == (y >= 12u - expected ? 0xFD20 : shades[0]));
                counted += pixel == 0xFD20;
            }
            assert(counted == expected);
        }
    }
    assert(lsdj_nat_amount_fill(255, 7) == 11);
    /* Every preset's accent remains distinct from every background shade.
       Channel selection must not tint any pixels of the native panel. */
    unsigned min_distance = 9999u;
    static uint16_t no_tint[160 * 144];
    for (unsigned t = 0; t < LSDJ_THEME_PRESETS; t++) for (unsigned s = 0; s < 4u; s++)
        for (int target = -1; target < 4; target++) {
            const lsdj_theme_t *theme = &lsdj_themes[t];
            lsdj_nat_t themed = n; themed.shade = theme->gb; themed.bgp = (uint8_t)s;
            lsdj_ov_bars_t b = {0}; b.amount_scale = 1; b.hot = -1; b.chan = (int8_t)target;
            strcpy(b.label[0], "CT"); b.level[0] = 64;
            lsdj_nat_bars(&themed, &b, theme->accent);
            uint16_t fill = px[110u * 160u + 24u], gap = px[100u * 160u + 24u];
            assert(fill == theme->accent && gap == theme->gb[s]);
            unsigned distance = lsdj_theme_dist(fill, gap);
            assert(distance >= 150u);
            if (distance < min_distance) min_distance = distance;
            if (target < 0) memcpy(no_tint, px, sizeof no_tint);
            else assert(!memcmp(no_tint, px, sizeof no_tint));
        }
    printf("PASS: accent fill across %u theme entries/all shades: RGB distance min %u; no channel tint\n",
           LSDJ_THEME_PRESETS, min_distance);
    /* Each mode is drawn once; only its selected row uses inverted font tiles. */
    for (unsigned count = 2; count <= 6u; count++) for (unsigned pick = 0; pick < count; pick++) {
        static uint16_t expected[160 * 144];
        lsdj_ov_bars_t b = {0}; b.amount_scale = 1; b.hot = -1; b.chan = -1;
        strcpy(b.label[0], "TY"); b.nchoice[0] = (uint8_t)count; b.pick[0] = (uint8_t)pick;
        for (unsigned i = 0; i < count; i++) { b.choice[0][i][0] = 'A' + (char)i; b.choice[0][i][1] = '0' + (char)i; }
        lsdj_nat_bars(&n, &b, 0xFD20);
        lsdj_nat_t ref = n; ref.px = expected;
        unsigned rows = count <= 3u ? 4u : 2u;
        for (unsigned i = 0; i < count; i++) {
            unsigned row = 14u - (i + 1u) * rows + (rows - 1u) / 2u;
            lsdj_nat_text(&ref, 3, row, b.choice[0][i], 2, i == pick);
            for (unsigned y = row * 8u; y < row * 8u + 8u; y++)
                assert(!memcmp(&px[y * 160u + 24u], &expected[y * 160u + 24u], 16u * sizeof(uint16_t)));
        }
    }
    puts("PASS: LSDj-style drawing: bounds, right panel, font, theme-accent segments at all 128 values, 2-6 choice selections");
    return 0;
}
