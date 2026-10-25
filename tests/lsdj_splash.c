/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_splash.h: every frame of the drop stays inside
   the screen buffer (canaries around it), the title rests where it should, the
   alpha number comes from the identity. With a folder: frames as PPMs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_splash.h"

static uint16_t buf[LSDJ_OV_W * LSDJ_OV_H + 2 * 4096];
#define CANARY 0x1234u
static int y_of(uint32_t ms) {   /* GameDude's drop (boot_brand.h gd_splash_y), at rest 84 */
    if (ms < 500u) return -36 + (int)(120u * ms * ms / 250000u);
    if (ms < 660u) { uint32_t u = ms - 500u; return 84 - (int)(48u * u * (160u - u) / 25600u); }
    if (ms < 800u) { uint32_t u = ms - 660u; return 84 - (int)(16u * u * (140u - u) / 19600u); }
    return 84;
}

int main(int argc, char **argv) {
    uint16_t *scr = buf + 4096;
    for (uint32_t ms = 0; ms <= 1600u; ms += 5u) {
        for (unsigned i = 0; i < 4096; i++) buf[i] = buf[4096 + LSDJ_OV_W * LSDJ_OV_H + i] = CANARY;
        lsdj_splash_draw(scr, ms, y_of(ms) - 84, "FM-1_937", &lsdj_splash_default);
        for (unsigned i = 0; i < 4096; i++) assert(buf[i] == CANARY && buf[4096 + LSDJ_OV_W * LSDJ_OV_H + i] == CANARY);
    }
    /* At rest: "LSDJ" amber in the middle band, credit and version grey at the bottom. */
    lsdj_splash_draw(scr, 1600u, 0, "FM-1_937", &lsdj_splash_default);
    unsigned amber = 0, grey = 0;
    for (unsigned y = 0; y < LSDJ_OV_H; y++)
        for (unsigned x = 0; x < LSDJ_OV_W; x++) {
            uint16_t c = scr[y * LSDJ_OV_W + x];
            if (c == LSDJ_SPLASH_AMBER) { amber++; assert(y >= 66 && y < 66 + 42); }
            if (c == LSDJ_SPLASH_GREY) { grey++; assert(y >= 182); }
        }
    assert(amber > 500 && grey > 100);
    /* In a theme: its darkest shade behind, its accent for LSDJ, its second shade for the small lines. */
    {
        static const uint16_t gb[4] = {0xEF5C, 0xAEB9, 0x7657, 0x4B8D};
        lsdj_splash_colors_t c = lsdj_splash_of_theme(gb, 0xDA63);
        lsdj_splash_draw(scr, 1600u, 0, "FM-1_937", &c);
        unsigned acc = 0, dim = 0;
        assert(scr[0] == LSDJ_OV_SWAP(0x4B8D));
        for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) { acc += scr[i] == c.accent; dim += scr[i] == c.dim && i / LSDJ_OV_W >= 182u; }
        assert(acc > 500 && dim > 100);
    }
    const char *lines[] = {"DREAMY_BACON'S", "LSDJ", "BURRITO", "ALPHA 37", "BASED ON GAMEDUDE - GPL-3.0"};
    for (unsigned i = 0; i < 5; i++) for (const char *c = lines[i]; *c; c++) assert(strchr(lsdj_ov_chars, *c));
    if (argc > 1) {
        const uint32_t at[4] = {200, 520, 700, 1600};
        for (unsigned k = 0; k < 4; k++) {
            lsdj_splash_draw(scr, at[k], y_of(at[k]) - 84, "FM-1_937", &lsdj_splash_default);
            char p[512]; snprintf(p, sizeof p, "%s/splash_%u.ppm", argv[1], k);
            FILE *f = fopen(p, "wb"); fprintf(f, "P6 %u %u 255\n", LSDJ_OV_W, LSDJ_OV_H);
            for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
                uint16_t c = LSDJ_OV_SWAP(scr[i]);
                uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        }
    }
    puts("PASS: splash: every frame inside the screen, title at rest, credit and alpha number");
    return 0;
}
