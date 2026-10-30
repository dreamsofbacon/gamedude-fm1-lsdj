/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_feat.h: the feature switches' defaults, packing,
   refusals, turning, and the menu lines (every theme name fits). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_feat.h"
#include "lsdj_overlay.h"   /* lsdj_ov_chars: what the menu font can draw */

static void fits(const char *t) {
    assert(strlen(t) <= 19u);
    for (const char *c = t; *c; c++) assert(strchr(lsdj_ov_chars, *c));
}

int main(void) {
    lsdj_feat_t f, g;
    /* No record (or not ours): the defaults. Page wrap and CC out off, knobs on
       parameters, FX theme matching LSDj's; the rest on. */
    assert(!lsdj_feat_unpack(0, &f));
    assert(!f.v[LSDJ_FEAT_WRAP] && f.v[LSDJ_FEAT_GREY] && f.v[LSDJ_FEAT_MUTEPOP] && f.v[LSDJ_FEAT_CCSHOW]);
    assert(!f.v[LSDJ_FEAT_CCOUT] && !f.v[LSDJ_FEAT_CHANNELS] && f.v[LSDJ_FEAT_KEYLIGHTS] && f.v[LSDJ_FEAT_ANIM]);
    assert(f.v[LSDJ_FEAT_BATTERY] && !f.v[LSDJ_FEAT_THEME] && lsdj_feat_theme(&f, 5) == 5 && f.v[LSDJ_FEAT_AUTOSAVE] == 1);
    assert(!lsdj_feat_unpack(0x4D000000u, &f));            /* the MIDI word's magic */
    /* The fields do not overlap and fit in bits 0..23. */
    uint32_t used = 0;
    for (unsigned i = 0; i < LSDJ_FEAT_ITEMS; i++) {
        uint32_t m = ((1u << lsdj_feat_info[i].bits) - 1u) << lsdj_feat_info[i].shift;
        assert(!(used & m) && !(m & 0xFF000000u) && lsdj_feat_info[i].n <= (1u << lsdj_feat_info[i].bits));
        assert(lsdj_feat_info[i].def < lsdj_feat_info[i].n);
        used |= m;
    }
    /* Every value of every switch round-trips, alone and all at once. */
    for (unsigned i = 0; i < LSDJ_FEAT_ITEMS; i++)
        for (unsigned v = 0; v < lsdj_feat_info[i].n; v++) {
            lsdj_feat_defaults(&f); f.v[i] = (uint8_t)v;
            uint32_t w = lsdj_feat_pack(&f);
            assert(w >> 24 == LSDJ_FEAT_MAGIC && lsdj_feat_unpack(w, &g) && !memcmp(&f, &g, sizeof f));
            char line[20]; lsdj_feat_line(&f, i, line); fits(line);
        }
    for (unsigned i = 0; i < LSDJ_FEAT_ITEMS; i++) f.v[i] = (uint8_t)(lsdj_feat_info[i].n - 1u);
    assert(lsdj_feat_unpack(lsdj_feat_pack(&f), &g) && !memcmp(&f, &g, sizeof f));
    assert(lsdj_feat_theme(&f, 0) == LSDJ_THEMES - 1u);
    /* Refused (defaults instead): a theme past the last, any unused bit. */
    lsdj_feat_defaults(&f);
    uint32_t w = lsdj_feat_pack(&f);
    assert(!lsdj_feat_unpack(w | (uint32_t)(LSDJ_THEMES + 1u) << lsdj_feat_info[LSDJ_FEAT_THEME].shift, &g));
    for (unsigned b = 0; b < 24; b++) if (!((used >> b) & 1u)) assert(!lsdj_feat_unpack(w | 1u << b, &g));
    /* Turning: one choice a click, stopping at the ends. */
    assert(lsdj_feat_turn(&f, LSDJ_FEAT_WRAP, 1) && f.v[LSDJ_FEAT_WRAP] == 1 && !lsdj_feat_turn(&f, LSDJ_FEAT_WRAP, 5));
    assert(lsdj_feat_turn(&f, LSDJ_FEAT_THEME, 999) && f.v[LSDJ_FEAT_THEME] == LSDJ_THEMES);
    assert(lsdj_feat_turn(&f, LSDJ_FEAT_THEME, -999) && f.v[LSDJ_FEAT_THEME] == 0 && !lsdj_feat_turn(&f, LSDJ_FEAT_THEME, -1));
    assert(!lsdj_feat_turn(&f, LSDJ_FEAT_ITEMS, 1) && !lsdj_feat_turn(&f, LSDJ_FEAT_GREY, 0));
    /* Lines. */
    char l[20];
    lsdj_feat_defaults(&f);
    lsdj_feat_line(&f, LSDJ_FEAT_WRAP, l); assert(!strcmp(l, "PAGE WRAP: OFF"));
    lsdj_feat_line(&f, LSDJ_FEAT_GREY, l); assert(!strcmp(l, "GREY MUTED: ON"));
    lsdj_feat_line(&f, LSDJ_FEAT_CHANNELS, l); assert(!strcmp(l, "KNOBS: PARAMS"));
    f.v[LSDJ_FEAT_CHANNELS] = 1; lsdj_feat_line(&f, LSDJ_FEAT_CHANNELS, l); assert(!strcmp(l, "KNOBS: CHANNELS"));
    lsdj_feat_line(&f, LSDJ_FEAT_THEME, l); assert(!strcmp(l, "FX THEME: MATCH"));
    f.v[LSDJ_FEAT_THEME] = 1; lsdj_feat_line(&f, LSDJ_FEAT_THEME, l);
    assert(!strncmp(l, "FX THEME: ", 10) && !strcmp(l + 10, lsdj_theme_name[0]) && lsdj_feat_theme(&f, 9) == 0);
    /* FM-1_981 AUTOSAVE: default 1 MIN, also for a word saved by 979 / 980 (no AUTOSAVE bits);
       OFF, 5 and 15 MIN round-trip; stored as value XOR 1. */
    lsdj_feat_defaults(&f); assert(f.v[LSDJ_FEAT_AUTOSAVE] == 1);
    assert(lsdj_feat_unpack(0x460001CEu, &g) && g.v[LSDJ_FEAT_AUTOSAVE] == 1 && !g.v[LSDJ_FEAT_WRAP]);
    assert(lsdj_feat_pack(&f) == 0x460001CEu);                  /* the defaults' word is unchanged */
    f.v[LSDJ_FEAT_AUTOSAVE] = 0; assert(lsdj_feat_pack(&f) == (0x460001CEu | 1u << 16));
    lsdj_feat_line(&f, LSDJ_FEAT_AUTOSAVE, l); assert(!strcmp(l, "AUTOSAVE: OFF"));
    f.v[LSDJ_FEAT_AUTOSAVE] = 3; lsdj_feat_line(&f, LSDJ_FEAT_AUTOSAVE, l); assert(!strcmp(l, "AUTOSAVE: 15 MIN"));
    assert(lsdj_feat_unpack(lsdj_feat_pack(&f), &g) && g.v[LSDJ_FEAT_AUTOSAVE] == 3);
    f.v[LSDJ_FEAT_AUTOSAVE] = 1; lsdj_feat_line(&f, LSDJ_FEAT_AUTOSAVE, l); assert(!strcmp(l, "AUTOSAVE: 1 MIN"));
    /* FM-1_989 SCREEN RATE: default 10/S (every 6th frame), the defaults' word unchanged */
    lsdj_feat_defaults(&f); assert(!f.v[LSDJ_FEAT_RATE] && lsdj_feat_rate_every[0] == 6u && lsdj_feat_pack(&f) == 0x460001CEu);
    lsdj_feat_line(&f, LSDJ_FEAT_RATE, l); assert(!strcmp(l, "SCREEN RATE: 10/S"));
    f.v[LSDJ_FEAT_RATE] = 3; lsdj_feat_line(&f, LSDJ_FEAT_RATE, l); assert(!strcmp(l, "SCREEN RATE: 30/S") && lsdj_feat_rate_every[3] == 2u);
    assert(lsdj_feat_pack(&f) == (0x460001CEu | 3u << 18) && lsdj_feat_unpack(lsdj_feat_pack(&f), &g) && g.v[LSDJ_FEAT_RATE] == 3);
    puts("PASS: feature switches: defaults, every value packs, refusals, turning, lines fit for every theme, autosave, screen rate");
    return 0;
}
