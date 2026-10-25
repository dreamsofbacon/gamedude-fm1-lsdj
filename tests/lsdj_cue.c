/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_cue.h. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_cue.h"
#include "lsdj_overlay.h"

static void fits(const char *t, unsigned max) { assert(strlen(t) <= max); for (const char *c = t; *c; c++) assert(strchr(lsdj_ov_chars, *c)); }

int main(void) {
    lsdj_cue_t q; memset(&q, 0, sizeof q);
    uint8_t all[4] = {1, 1, 1, 1}, none[4] = {0, 0, 0, 0}, rows[4] = {3, 3, 3, 3};
    uint16_t a[8]; uint8_t v[8];
    /* Opens on the next row; a press alone cues nothing. */
    lsdj_cue_open(&q, all, rows); assert(q.held && q.row == 4 && lsdj_cue_lit(&q));
    assert(!lsdj_cue_release(&q) && !q.pending && !lsdj_cue_lit(&q));
    assert(lsdj_cue_frame(&q, all, rows, 0xC200, 0xC17C, a, v) == 0);
    /* Stopped: opens on row 00. */
    lsdj_cue_open(&q, none, rows); assert(q.row == 0); lsdj_cue_release(&q);
    /* K1 +5, K2 -1 (16 back, wraps): row 4 + 5 - 16 = F9; let go cues. */
    lsdj_cue_open(&q, all, rows);
    lsdj_cue_turn(&q, 5, 0); lsdj_cue_turn(&q, 0, -1); assert(q.row == 0xF9 && q.turned);
    assert(lsdj_cue_release(&q) && q.pending && lsdj_cue_lit(&q));
    /* Playing: one batch, each channel row-1 and chain place 15. */
    unsigned n = lsdj_cue_frame(&q, all, rows, 0xC200, 0xC17C, a, v);
    assert(n == 8 && !q.pending && q.waiting);
    for (unsigned c = 0; c < 4; c++) {
        assert(a[2 * c] == 0xC200 + c && v[2 * c] == 0xF8);
        assert(a[2 * c + 1] == 0xC17C + c && v[2 * c + 1] == 15);
    }
    /* Lit until a channel is on the row. */
    assert(lsdj_cue_frame(&q, all, rows, 0xC200, 0xC17C, a, v) == 0 && q.waiting);
    rows[0] = 0xF9; lsdj_cue_frame(&q, all, rows, 0xC200, 0xC17C, a, v); assert(!q.waiting && !lsdj_cue_lit(&q));
    /* Row 00: written as FF (LSDj wraps to 00). Live mode: only playing channels. */
    uint8_t wav[4] = {0, 0, 1, 0};
    lsdj_cue_open(&q, wav, rows); q.row = 0xFF; lsdj_cue_turn(&q, 1, 0); assert(q.row == 0); lsdj_cue_release(&q);
    n = lsdj_cue_frame(&q, wav, rows, 0xC200, 0xC17C, a, v);
    assert(n == 2 && a[0] == 0xC202 && v[0] == 0xFF && a[1] == 0xC17E && v[1] == 15);
    /* Pending while stopped: written when playing starts. */
    lsdj_cue_open(&q, none, rows); lsdj_cue_turn(&q, 2, 0); lsdj_cue_release(&q);
    assert(lsdj_cue_frame(&q, none, rows, 0xC200, 0xC17C, a, v) == 0 && q.pending);
    assert(lsdj_cue_frame(&q, all, rows, 0xC200, 0xC17C, a, v) == 8 && v[0] == 1);
    /* Gives up waiting after ~4 s. */
    for (int i = 0; i < 300; i++) lsdj_cue_frame(&q, all, rows, 0xC200, 0xC17C, a, v);
    assert(!q.waiting);
    /* The pop-up: chains on the row, "--" for none. */
    {
        static uint8_t song[1024]; memset(song, 0xFF, sizeof song);
        song[8 * 4 + 0] = 0x42; song[8 * 4 + 2] = 0x19;
        char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
        lsdj_cue_open(&q, all, rows); q.row = 8;
        lsdj_cue_bars(&q, song, title, big, label, value, caption, level);
        assert(!strcmp(title, "CUE ROW 08") && !strcmp(value[0], "42") && !strcmp(value[1], "--") && !strcmp(value[2], "19"));
        assert(level[0] == 127 && level[1] == 0 && !strcmp(label[3], "NS"));
        fits(title, 17); fits(big, 19); for (int c = 0; c < 4; c++) { fits(label[c], 5); fits(value[c], 5); }
        lsdj_cue_turn(&q, 1, 0); lsdj_cue_bars(&q, song, title, big, label, value, caption, level); fits(big, 19);
    }
    puts("PASS: song row cue: choose (K1, K2 by 16), let go to cue, one batch per playing channel, waits for play");
    return 0;
}
