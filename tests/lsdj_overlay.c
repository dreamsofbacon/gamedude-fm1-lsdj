/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_overlay.h: every message fits and uses only
   characters the font has; the box stays inside the picture and goes away
   on time. With an output folder, writes each message over a grey picture
   as a PPM: lsdj_overlay [DIR] */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lsdj_overlay.h"

static uint16_t scr[LSDJ_OV_W * LSDJ_OV_H];

int main(int argc, char **argv) {
    for (unsigned m = 0; m < LSDJ_MSG_COUNT; m++)
        for (unsigned l = 0; l < 2; l++) {
            const char *s = lsdj_ov_msg[m][l];
            if (!s) continue;
            assert(strlen(s) <= LSDJ_OV_COLS);
            for (const char *c = s; *c; c++) assert(strchr(lsdj_ov_chars, *c));
        }
    assert(sizeof lsdj_ov_font / sizeof lsdj_ov_font[0] == strlen(lsdj_ov_chars));
    for (unsigned g = 0; g < strlen(lsdj_ov_chars); g++) {   /* 5 columns; every glyph but space has ink */
        unsigned ink = 0;
        for (unsigned r = 0; r < 7; r++) { assert(lsdj_ov_font[g][r] < 0x20u); ink |= lsdj_ov_font[g][r]; }
        assert(g == 0 ? ink == 0 : ink != 0);
    }

    lsdj_ov_t o; memset(&o, 0, sizeof o);
    const uint16_t grey = 0x5555;
    for (unsigned m = 0; m < LSDJ_MSG_COUNT; m++) {
        for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) scr[i] = grey;
        lsdj_ov_say(&o, m, 1000);
        assert(lsdj_ov_active(&o, 1000) && lsdj_ov_active(&o, 1000 + LSDJ_OV_MS - 1));
        lsdj_ov_draw(&o, scr, 1200);
        unsigned changed = 0, top = LSDJ_OV_H;
        for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++)
            if (scr[i] != grey) { changed++; if (i / LSDJ_OV_W < top) top = i / LSDJ_OV_W; }
        assert(changed > 0 && top > LSDJ_OV_H / 2);           /* a box in the lower half */
        for (unsigned y = 0; y < LSDJ_OV_H; y++)               /* 2 px margin left and right */
            assert(scr[y * LSDJ_OV_W] == grey && scr[y * LSDJ_OV_W + LSDJ_OV_W - 1] == grey);
        if (argc > 1) {
            char path[512]; snprintf(path, sizeof path, "%s/msg%02u.ppm", argv[1], m);
            FILE *f = fopen(path, "wb"); assert(f);
            fprintf(f, "P6 %u %u 255\n", LSDJ_OV_W, LSDJ_OV_H);
            for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
                uint16_t c = LSDJ_OV_SWAP(scr[i]);
                uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        }
    }
    /* Octave messages. */
    lsdj_ov_octave(&o, 0, 1, 0);  assert(!strcmp(o.line[0], "NOTE OCTAVE UP") && !strcmp(o.line[1], "OCTAVE NOW +1"));
    lsdj_ov_octave(&o, 1, 0, 0);  assert(!strcmp(o.line[0], "NOTE OCTAVE DOWN") && !strcmp(o.line[1], "OCTAVE NOW 0"));
    lsdj_ov_octave(&o, 1, -2, 0); assert(!strcmp(o.line[1], "OCTAVE NOW -2"));
    lsdj_ov_show(&o, "THIS LINE IS FAR TOO LONG TO FIT", 0, 0);
    assert(strlen(o.line[0]) == LSDJ_OV_COLS && o.line[1][0] == 0);
    /* Expiry: not drawn after LSDJ_OV_MS, also across the 32-bit ms wrap. */
    lsdj_ov_say(&o, LSDJ_MSG_SAVED, 0xFFFFFF00u);
    assert(lsdj_ov_active(&o, 0xFFFFFFF0u) && lsdj_ov_active(&o, 100u) && !lsdj_ov_active(&o, 0xFFFFFF00u + LSDJ_OV_MS + 2u));
    for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) scr[i] = grey;
    lsdj_ov_draw(&o, scr, 0xFFFFFF00u + LSDJ_OV_MS + 2u);
    for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) assert(scr[i] == grey);
    puts("PASS: overlay messages fit, font complete, box in the lower picture, expires on time");
    return 0;
}
