/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_theme.h: every preset readable (the accent stands
   out from each screen shade, text from the dark), names match the settings menu.
   With a folder holding song.ppm (an LSDj screenshot in the CLASSIC palette):
   the effects pop-up in every theme, theme_N.ppm. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_theme.h"
#include "lsdj_settings.h"

static uint16_t scr[LSDJ_OV_W * LSDJ_OV_H];
static uint8_t bg[160 * 144 * 3];

int main(int argc, char **argv) {
    assert(LSDJ_THEMES == LSDJ_SET_THEMES);
    for (unsigned t = 0; t < LSDJ_THEMES; t++) assert(!strcmp(lsdj_theme_name[t], lsdj_set_theme_name[t]));
    for (unsigned t = 0; t < LSDJ_THEME_PRESETS; t++) {
        const lsdj_theme_t *th = &lsdj_themes[t];
        assert(!strcmp(lsdj_theme_name[t], lsdj_set_theme_name[t]));
        for (unsigned s = 0; s < 4; s++) {
            unsigned d = lsdj_theme_dist(th->accent, th->gb[s]);
            if (d < 150) { printf("theme %s: accent too close to shade %u (%u)\n", lsdj_theme_name[t], s, d); assert(0); }
        }
        assert(lsdj_theme_dist(th->fg, th->bg) >= 400);
        assert(lsdj_theme_dist(th->accent, th->bg) >= 150);   /* amber text on dark boxes */
        for (unsigned s = 0; s + 1 < 4; s++)                  /* shades go from light to dark */
            assert(((th->gb[s] >> 11) + ((th->gb[s] >> 5) & 63u) / 2u + (th->gb[s] & 31u)) >=
                   ((th->gb[s + 1] >> 11) + ((th->gb[s + 1] >> 5) & 63u) / 2u + (th->gb[s + 1] & 31u)));
    }
    /* Settings keep the theme. */
    lsdj_settings_t a = {0, 0, 0, 0, 0, 0, 6, 0}, b;
    assert(lsdj_set_unpack(lsdj_set_pack(&a), &b) && b.theme == 6);
    for (unsigned t = 0; t < LSDJ_THEMES; t++) {   /* every theme round-trips; 0..7 keep their old packed form */
        a.theme = (uint8_t)t; uint32_t v = lsdj_set_pack(&a);
        assert(lsdj_set_unpack(v, &b) && b.theme == t);
        assert(((v >> 12) & 1u) == ((t >> 3) & 1u) && ((v >> 15) & 7u) == (t >> 4) && (v & 0x00FC0000u) == 0);
        char line[20] = "THEME: "; assert(strlen(line) + strlen(lsdj_theme_name[t]) <= 19u);
    }
    assert(!lsdj_set_unpack(0x5A000000u | 7u << 7 | 1u << 12 | 7u << 15, &b));   /* 127: no such theme */
    { a.theme = 7; assert(lsdj_set_pack(&a) == (0x5A000000u | 7u << 7)); }   /* themes 0..7 keep their old packed form */
    assert(lsdj_theme_of(LSDJ_THEME_CUSTOM) == &lsdj_theme_custom);
    { uint16_t c[8], d[8]; lsdj_theme_to8(&lsdj_themes[4], c); lsdj_theme_from8(&lsdj_theme_custom, c); lsdj_theme_to8(lsdj_theme_of(LSDJ_THEME_CUSTOM), d); assert(!memcmp(c, d, sizeof c)); }   /* CUSTOM round trip */
    if (argc > 1) {
        char p[512]; snprintf(p, sizeof p, "%s/song.ppm", argv[1]); FILE *f = fopen(p, "rb");
        if (f) {
            char h[32]; int ok = fgets(h, sizeof h, f) && fread(bg, 1, sizeof bg, f) == sizeof bg; fclose(f);
            static const uint16_t classic[4] = {0xE7DA, 0x9D50, 0x5308, 0x18E3};
            for (unsigned t = 0; ok && t < LSDJ_THEME_PRESETS; t++) {
                const lsdj_theme_t *th = &lsdj_themes[t];
                lsdj_theme_overlay(th);
                for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
                    const uint8_t *q = &bg[((i / LSDJ_OV_W * 2u / 3u) * 160u + i % LSDJ_OV_W * 2u / 3u) * 3u];
                    uint16_t c = (uint16_t)((q[0] >> 3) << 11 | (q[1] >> 2) << 5 | q[2] >> 3);
                    unsigned best = 0, bd = 9999; for (unsigned s = 0; s < 4; s++) { unsigned d = lsdj_theme_dist(c, classic[s]); if (d < bd) { bd = d; best = s; } }
                    scr[i] = LSDJ_OV_SWAP(th->gb[best]);
                }
                lsdj_ov_bars_t bars; memset(&bars, 0, sizeof bars);
                strcpy(bars.title, "FILTER WAV 1/4"); strcpy(bars.big, lsdj_theme_name[t]);
                const char *lab[4] = {"DRIVE", "TYPE", "FREQ", "RESO"}, *val[4] = {"8", "LP", "64", "0"};
                for (int k = 0; k < 4; k++) { strcpy(bars.label[k], lab[k]); strcpy(bars.value[k], val[k]); bars.caption[k][0] = 'K'; bars.caption[k][1] = (char)('1' + k); bars.level[k] = (uint8_t)(30 * k + 20); }
                bars.hot = 0; bars.chan = 2; lsdj_ov_bars_keep(&bars, 1000u); lsdj_ov_bars_draw(&bars, scr, 1000u);
                lsdj_ov_mode_cover(scr, 2, 0);
                snprintf(p, sizeof p, "%s/theme_%u.ppm", argv[1], t);
                f = fopen(p, "wb"); fprintf(f, "P6 %u %u 255\n", LSDJ_OV_W, LSDJ_OV_H);
                for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
                    uint16_t c = LSDJ_OV_SWAP(scr[i]);
                    uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
            }
        }
    }
    puts("PASS: themes: every preset readable (accent vs every shade, text vs dark), shades light to dark, saved");
    return 0;
}
