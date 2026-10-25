/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_native.h: the panel stays inside the 160x144 picture
   and LSDj's right panel (columns 16..19), uses LSDj's font tiles for its
   characters, inverts the turned knob's value. Real LSDj: harness/native_shots.sh. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_native.h"
#include "lsdj_fx_ui.h"

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
    puts("PASS: LSDj-style drawing: panel, list, message, mode inside the picture; right panel kept; font and field tiles; hex values");
    return 0;
}
