/* SPDX-License-Identifier: GPL-3.0-only */
/* Feature switches (SETTINGS / FX+LIGHTS, kept in save-store record 23, the web
   page's Settings tab). One table: each switch's name, bits in the packed word,
   number of choices and default. Adding a switch = one line here (and its use).

   PAGE WRAP    OFF / ON         SELECT past the last FX page goes back to the first
   GREY MUTED   OFF / ON         a muted channel's FX are drawn grey (still adjustable)
   MUTE POPUP   OFF / ON         turning a muted channel's FX says why it is silent
   SHOW CC      OFF / ON         an FX change by MIDI CC shows like a knob turn
   CC OUT       OFF / ON         FX knobs also send their MIDI CC (lsdj_midi.h numbers)
   KNOBS        PARAMS / CHANNELS  K1-K4 = 4 effects of one channel, or PU1 PU2 WAV NOI
   PAGE LIGHTS  OFF / ON         black keys light up for the FX page / LSDj screen
   MODE LIGHTS  OFF / ON         a light sweep on entering note / button mode
   BATTERY      OFF / ON         battery level in the FX panel and settings
   FX THEME     MATCH / a theme  the FX panel's colours (MATCH: LSDj's theme)

   Packed: 'F' (0x46) in bits 24-31. Pure logic: tests/lsdj_feat.c. */
#pragma once
#include <stdint.h>
#include "lsdj_theme.h"

enum { LSDJ_FEAT_WRAP, LSDJ_FEAT_GREY, LSDJ_FEAT_MUTEPOP, LSDJ_FEAT_CCSHOW, LSDJ_FEAT_CCOUT, LSDJ_FEAT_CHANNELS,
       LSDJ_FEAT_KEYLIGHTS, LSDJ_FEAT_ANIM, LSDJ_FEAT_BATTERY, LSDJ_FEAT_THEME, LSDJ_FEAT_ITEMS };
#define LSDJ_FEAT_MAGIC 0x46u
static const struct { const char *name; uint8_t shift, bits, n, def; } lsdj_feat_info[LSDJ_FEAT_ITEMS] = {
    {"PAGE WRAP", 0, 1, 2, 0}, {"GREY MUTED", 1, 1, 2, 1}, {"MUTE POPUP", 2, 1, 2, 1}, {"SHOW CC", 3, 1, 2, 1},
    {"CC OUT", 4, 1, 2, 0}, {"KNOBS", 5, 1, 2, 0}, {"PAGE LIGHTS", 6, 1, 2, 1}, {"MODE LIGHTS", 7, 1, 2, 1},
    {"BATTERY", 8, 1, 2, 1}, {"FX THEME", 9, 7, LSDJ_THEMES + 1u, 0},
};
typedef struct { uint8_t v[LSDJ_FEAT_ITEMS]; } lsdj_feat_t;

static void lsdj_feat_defaults(lsdj_feat_t *f) { for (unsigned i = 0; i < LSDJ_FEAT_ITEMS; i++) f->v[i] = lsdj_feat_info[i].def; }
static uint32_t lsdj_feat_pack(const lsdj_feat_t *f) {
    uint32_t w = (uint32_t)LSDJ_FEAT_MAGIC << 24;
    for (unsigned i = 0; i < LSDJ_FEAT_ITEMS; i++) w |= (uint32_t)f->v[i] << lsdj_feat_info[i].shift;
    return w;
}
/* 0 if w is not a packed features word (the defaults are set then). Unused bits must be 0. */
static int lsdj_feat_unpack(uint32_t w, lsdj_feat_t *f) {
    lsdj_feat_t t; uint32_t used = 0xFF000000u;
    lsdj_feat_defaults(f);
    if (w >> 24 != LSDJ_FEAT_MAGIC) return 0;
    for (unsigned i = 0; i < LSDJ_FEAT_ITEMS; i++) {
        uint32_t mask = (1u << lsdj_feat_info[i].bits) - 1u;
        t.v[i] = (uint8_t)((w >> lsdj_feat_info[i].shift) & mask);
        used |= mask << lsdj_feat_info[i].shift;
        if (t.v[i] >= lsdj_feat_info[i].n) return 0;
    }
    if (w & ~used) return 0;
    *f = t;
    return 1;
}
/* One choice per click, stopping at the ends; 1 if it changed. */
static int lsdj_feat_turn(lsdj_feat_t *f, unsigned item, int steps) {
    if (item >= LSDJ_FEAT_ITEMS || !steps) return 0;
    int v = (int)f->v[item] + steps, n = (int)lsdj_feat_info[item].n;
    uint8_t to = (uint8_t)(v < 0 ? 0 : v >= n ? n - 1 : v);
    if (to == f->v[item]) return 0;
    f->v[item] = to;
    return 1;
}
/* "PAGE WRAP: OFF", "KNOBS: CHANNELS", "FX THEME: MATCH" (up to 19 characters). */
static void lsdj_feat_line(const lsdj_feat_t *f, unsigned item, char line[20]) {
    const char *t[3] = {lsdj_feat_info[item % LSDJ_FEAT_ITEMS].name, ": ", 0};
    unsigned v = f->v[item % LSDJ_FEAT_ITEMS];
    t[2] = item == LSDJ_FEAT_THEME ? (v ? lsdj_theme_name[(v - 1u) % LSDJ_THEMES] : "MATCH")
         : item == LSDJ_FEAT_CHANNELS ? (v ? "CHANNELS" : "PARAMS") : v ? "ON" : "OFF";
    uint32_t n = 0;
    for (unsigned k = 0; k < 3; k++) for (const char *c = t[k]; *c && n < 19u; c++) line[n++] = *c;
    line[n] = 0;
}
/* The theme the FX panel draws in: its own, or LSDj's (`lsdj_theme`) with MATCH. */
static unsigned lsdj_feat_theme(const lsdj_feat_t *f, unsigned lsdj_theme) {
    return f->v[LSDJ_FEAT_THEME] ? (f->v[LSDJ_FEAT_THEME] - 1u) % LSDJ_THEMES : lsdj_theme;
}
