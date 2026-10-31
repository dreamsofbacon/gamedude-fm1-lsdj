/* SPDX-License-Identifier: GPL-3.0-only */
/* Startup screen of Dreamy_bacon's LSDJ Burrito: the title drops in and
   bounces (GameDude's timing, boot_brand.h gd_splash_y), a burst of pixels
   where it lands; a credit line and the alpha number at the bottom. Drawn with
   the overlay font (lsdj_overlay.h), in the theme's colours as LSDj shows them
   (lsdj_splash_colors_t). Pure logic: tests/lsdj_splash.c. */
#pragma once
#include <stdint.h>
#include "lsdj_overlay.h"

#define LSDJ_SPLASH_BG     LSDJ_OV_SWAP(0x0843u)   /* near black, as GameDude's */
#define LSDJ_SPLASH_AMBER  LSDJ_OV_SWAP(0xFD20u)
#define LSDJ_SPLASH_CREAM  LSDJ_OV_SWAP(0xEF5Eu)
#define LSDJ_SPLASH_GREY   LSDJ_OV_SWAP(0x8C51u)

/* The screen's colours (byte-swapped for the LCD): the background, the title,
   "LSDJ" and the burst's accent, the small lines. From a theme: its darkest
   shade, lightest shade, accent and second shade (lsdj_splash_of_theme). */
typedef struct { uint16_t bg, text, accent, dim; } lsdj_splash_colors_t;
static const lsdj_splash_colors_t lsdj_splash_default = {LSDJ_SPLASH_BG, LSDJ_SPLASH_CREAM, LSDJ_SPLASH_AMBER, LSDJ_SPLASH_GREY};
/* gb: the theme's four shades, lightest first; accent (both plain RGB565). */
static lsdj_splash_colors_t lsdj_splash_of_theme(const uint16_t gb[4], uint16_t accent) {
    lsdj_splash_colors_t c = {LSDJ_OV_SWAP(gb[3]), LSDJ_OV_SWAP(gb[0]), LSDJ_OV_SWAP(accent), LSDJ_OV_SWAP(gb[1])};
    return c;
}

/* Title lines at rest (y), moved by dy (negative while dropping in: rows above
   the screen wrap to huge unsigned values and are clipped by lsdj_ov_glyph_s). */
static void lsdj_splash_title(uint16_t *scr, int dy, const lsdj_splash_colors_t *col) {
    static const struct { const char *t; int y; uint32_t s; int amber; } line[3] = {
        {"DREAMY_BACON'S", 44, 2u, 0}, {"LSDJ", 66, 6u, 1}, {"BURRITO", 116, 4u, 0} };
    for (unsigned i = 0; i < 3; i++) {
        int y = line[i].y + dy;
        lsdj_ov_text_c(scr, LSDJ_OV_W / 2u, (uint32_t)y, line[i].t, line[i].amber ? col->accent : col->text, line[i].s);
    }
}
/* id: the firmware identity "FM-1_9NN": alpha NN is shown. */
static void lsdj_splash_draw(uint16_t *scr, uint32_t ms, int y_drop, const char *id, const lsdj_splash_colors_t *col) {
    for (uint32_t i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) scr[i] = col->bg;
    if (ms >= 500u && ms < 1100u) {          /* landing burst */
        static const int8_t dx[12] = {16, 14, 8, 0, -8, -14, -16, -14, -8, 0, 8, 14};
        static const int8_t dy[12] = {0, 8, 14, 16, 14, 8, 0, -8, -14, -16, -14, -8};
        uint32_t t = ms - 500u, reach = 30u + t * 70u / 600u, sz = t < 350u ? 3u : 1u;
        for (unsigned i = 0; i < 12; i++) {
            int x = 120 + dx[i] * (int)reach / 10, y = 100 + dy[i] * (int)reach / 14;
            for (uint32_t a = 0; a < sz; a++)
                for (uint32_t b = 0; b < sz; b++) {
                    int px = x + (int)a, py = y + (int)b;
                    if (px >= 0 && px < (int)LSDJ_OV_W && py >= 0 && py < (int)LSDJ_OV_H)
                        scr[(uint32_t)py * LSDJ_OV_W + (uint32_t)px] = i & 1u ? col->accent : col->text;
                }
        }
    }
    lsdj_splash_title(scr, y_drop, col);                /* over the burst */
    /* a release version ("0.9.96": "VERSION 0.9.96"), or an identity "FM-1_9NN": "ALPHA NN" */
    char ver[24] = "ALPHA ";
    uint32_t n = 6, len = lsdj_ov_len(id), dotted = 0;
    for (uint32_t i = 0; i < len; i++) if (id[i] == '.') dotted = 1;
    if (dotted && len <= 15u) { const char *v = "VERSION "; n = 0; while (v[n]) { ver[n] = v[n]; n++; }
        for (uint32_t i = 0; i < len; i++) ver[n++] = id[i]; }
    else if (len >= 2u) { ver[n++] = id[len - 2u]; ver[n++] = id[len - 1u]; }
    ver[n] = 0;
    lsdj_ov_text_c(scr, LSDJ_OV_W / 2u, 182u, ver, col->dim, 1u);
    lsdj_ov_text_c(scr, LSDJ_OV_W / 2u, 196u, "BASED ON GAMEDUDE - GPL-3.0", col->dim, 1u);
}
