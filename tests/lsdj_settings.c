/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_settings.h (+ the menu panel in lsdj_overlay.h):
   packing, defaults, key navigation, and the menu text. With a folder, writes
   the menu as PPMs: lsdj_settings [DIR] */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_settings.h"
#include "lsdj_overlay.h"
static lsdj_midi_t mi = {LSDJ_CLK_ALL, 1, 0, 0, 0, 0, 0};
static lsdj_feat_t fe;   /* the feature switches (FX+LIGHTS): defaults, set in main */
static void still(lsdj_menu_t *m) {   /* knobs left still after opening a page / BACK */
    lsdj_settings_t s0; const int32_t no[4] = {0, 0, 0, 0};
    while (m->settle) assert(lsdj_menu_knobs(m, &s0, &mi, &fe, no, 0) == 0);
}

static uint16_t scr[LSDJ_OV_W * LSDJ_OV_H];
static void ppm(const char *dir, const char *name) {
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6 %u %u 255\n", LSDJ_OV_W, LSDJ_OV_H);
    for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
        uint16_t c = LSDJ_OV_SWAP(scr[i]);
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void fits(const char *t, unsigned max) {
    assert(strlen(t) <= max);
    for (const char *c = t; *c; c++) assert(strchr(lsdj_ov_chars, *c));
}
static void check_text(const lsdj_settings_t *s) {   /* every page, every line selected */
    for (unsigned p = 0; p < LSDJ_PAGES; p++) {
        uint8_t e[LSDJ_MENU_MAX]; unsigned n = lsdj_menu_entries(p, e);
        for (unsigned c = 0; c < n; c++) {
            lsdj_menu_t m; memset(&m, 0, sizeof m); m.page = (uint8_t)p; m.cursor = (uint8_t)c;
            char lines[LSDJ_MENU_LINES][20];
            unsigned cnt = lsdj_menu_lines(&m, s, &mi, &fe, 0, lines);
            assert(cnt == n + 2u && cnt <= LSDJ_MENU_LINES);
            for (unsigned i = 0; i < cnt; i++) { fits(lines[i], 19); assert(strlen(lines[i]) > 0); }
        }
    }
}

int main(int argc, char **argv) {
    lsdj_settings_t s, t;
    lsdj_feat_defaults(&fe);
    /* Defaults = today's behaviour; packing round-trips every combination. */
    assert(!lsdj_set_unpack(0xFFFFFFFFu, &s) && !s.knobs_swapped && !s.presets_rev && !s.meter && !s.select_key);
    assert(!lsdj_set_unpack(0, &s));
    for (unsigned v = 0; v < 192u * 8u * LSDJ_SET_BT_MODES; v++) {
        lsdj_settings_t a = {v & 1u, (v >> 1) & 1u, (v >> 2) & 1u, (v >> 3) & 1u, (uint8_t)((v >> 4) % 3u), (v >> 6) & 1u,
                             (uint8_t)((v / 192u) % 8u), (uint8_t)(v / (192u * 8u))};
        assert(lsdj_set_unpack(lsdj_set_pack(&a), &t) == 1 && !memcmp(&a, &t, sizeof a));
        check_text(&a);
    }
    assert(!lsdj_set_unpack(0x5A000030u, &s));   /* select key 3: invalid */
    assert(!lsdj_set_unpack(0x5A000000u | (uint32_t)LSDJ_SET_BT_MODES << 10, &s));   /* no such Bluetooth mode */
    assert(lsdj_set_unpack(0x5A000400u, &s) && s.bluetooth == 1);                     /* DEVICE */

    /* The categories: the top lists CONTROLS, DISPLAY, MIDI; a knob turn opens
       the selected one on its first setting; BACK (its first line) returns to
       the top on that category. Inside, SELECT moves (stopping at the ends) and
       any of K1-K4 changes the selected setting one choice per click. */
    lsdj_menu_t m; memset(&m, 0, sizeof m); memset(&s, 0, sizeof s);
    const int32_t no[4] = {0, 0, 0, 0}, k1[4] = {1, 0, 0, 0};
    lsdj_menu_open(&m); assert(m.open && !m.changed && m.cursor == 0 && m.page == LSDJ_PAGE_TOP);
    {   char l[LSDJ_MENU_LINES][20]; unsigned cnt = lsdj_menu_lines(&m, &s, &mi, &fe, 0, l);
        assert(cnt == 6 && !strcmp(l[0], "SETTINGS") && !strcmp(l[1], "CONTROLS") && !strcmp(l[2], "DISPLAY") && !strcmp(l[3], "MIDI"));
        assert(!strcmp(l[4], "FX+LIGHTS") && !strcmp(l[5], "SELECT=PICK K1=OPEN")); }
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 99); assert(m.cursor == 3);                       /* stops at FX+LIGHTS */
    /* FX+LIGHTS: BACK, then the feature switches in lsdj_feat.h's order; a change returns 3 */
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 0 && m.page == LSDJ_PAGE_FEAT && m.cursor == 1);
    assert(lsdj_menu_entry(&m) == LSDJ_MENU_FEAT(LSDJ_FEAT_WRAP));
    {   char l[LSDJ_MENU_LINES][20]; unsigned cnt = lsdj_menu_lines(&m, &s, &mi, &fe, 0, l);
        /* FM-1_980: FX THEME is on DISPLAY (under THEME), not here */
        assert(cnt == LSDJ_FEAT_ITEMS + 2u && !strcmp(l[0], "SETTINGS/FX+LIGHTS") && !strcmp(l[2], "PAGE WRAP: OFF"));
        assert(!strcmp(l[3], "GREY MUTED: ON") && !strcmp(l[7], "KNOBS: PARAMS") && !strcmp(l[10], "BATTERY: ON")); }
    /* FM-1_980: the rest of the turn that opened the page changes nothing (it switched PAGE WRAP on);
       a click while waiting starts the wait again */
    for (unsigned i = 0; i + 1u < LSDJ_MENU_SETTLE; i++) assert(lsdj_menu_knobs(&m, &s, &mi, &fe, no, 0) == 0);
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 0 && fe.v[LSDJ_FEAT_WRAP] == 0 && !m.changed);
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 0 && fe.v[LSDJ_FEAT_WRAP] == 0 && m.settle == LSDJ_MENU_SETTLE);
    still(&m);
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 3 && fe.v[LSDJ_FEAT_WRAP] == 1 && m.changed);
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 0);                                   /* ON: at the end */
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 99); assert(lsdj_menu_entry(&m) == LSDJ_MENU_FEAT(LSDJ_FEAT_BATTERY));   /* the last line */
    lsdj_feat_defaults(&fe); m.changed = 0;
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -99); lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0);  /* BACK: on FX+LIGHTS */
    assert(m.page == LSDJ_PAGE_TOP && m.cursor == 3);
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 0 && m.page == LSDJ_PAGE_TOP);   /* the same turn does not reopen it */
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -1); assert(m.cursor == 2);                       /* MIDI */
    still(&m);
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 0 && m.page == LSDJ_PAGE_MIDI && m.cursor == 1 && !m.changed);
    still(&m);
    {   char l[LSDJ_MENU_LINES][20]; lsdj_menu_lines(&m, &s, &mi, &fe, 0, l); assert(!strcmp(l[0], "SETTINGS/MIDI") && !strcmp(l[1], "BACK")); }
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 99); assert(lsdj_menu_entry(&m) == LSDJ_SET_BT);   /* the last line: BLUETOOTH */
    { int32_t s2[4] = {5, 0, 0, 0}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0) == 1 && s.bluetooth == LSDJ_SET_BT_MODES - 1u); }
    { char l[20]; lsdj_set_line(&s, LSDJ_SET_BT, l); assert(!strcmp(l, "BLUETOOTH: DEVICE")); }
    { int32_t s2[4] = {-5, 0, 0, 0}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0) && s.bluetooth == 0); }
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -1); assert(lsdj_menu_entry(&m) == LSDJ_MENU_MIDI(LSDJ_MIDI_ITEMS - 1u));   /* PC CUE */
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) == 2 && mi.pc_cue == 1);
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -99); assert(m.cursor == 0 && lsdj_menu_entry(&m) == LSDJ_MENU_BACK);
    {   char l[LSDJ_MENU_LINES][20]; unsigned cnt = lsdj_menu_lines(&m, &s, &mi, &fe, 0, l); assert(!strcmp(l[cnt - 1], "K1=BACK  GLO=CLOSE")); }
    { int32_t s2[4] = {0, 0, -1, 0}; lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0); }            /* any knob, either way */
    assert(m.page == LSDJ_PAGE_TOP && m.cursor == 2);                                  /* back on MIDI */
    still(&m);
    /* DISPLAY: THEME (K2 jumps 8), FX THEME (FM-1_980: right under THEME), POPUP TIME, CPU METER */
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -1); lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0);
    assert(m.page == LSDJ_PAGE_DISPLAY && lsdj_menu_entry(&m) == LSDJ_SET_THEME);
    {   char l[LSDJ_MENU_LINES][20]; unsigned cnt = lsdj_menu_lines(&m, &s, &mi, &fe, 0, l);
        assert(cnt == 7u && !strcmp(l[0], "SETTINGS/DISPLAY") && !strncmp(l[2], "THEME: ", 7) && !strcmp(l[3], "FX THEME: MATCH")); }
    still(&m);
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 1); assert(lsdj_menu_entry(&m) == LSDJ_MENU_FEAT(LSDJ_FEAT_THEME));
    { int32_t s2[4] = {0, 1, 0, 0}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0) == 3 && fe.v[LSDJ_FEAT_THEME] == 8); }   /* K2: 8 */
    {   char l[LSDJ_MENU_LINES][20]; unsigned cnt = lsdj_menu_lines(&m, &s, &mi, &fe, 0, l); assert(!strcmp(l[cnt - 1], "K1 NEXT  K2 JUMP 8")); }
    { int32_t s2[4] = {-99, 0, 0, 0}; lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0); assert(fe.v[LSDJ_FEAT_THEME] == 0); }
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -1);
    { int32_t s2[4] = {3, 0, 0, 0}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0) && s.theme == 3); }   /* THEME: one per click */
    { int32_t s2[4] = {0, 2, 0, 0}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0) && s.theme == 19); }   /* K2: 8 a click */
    { int32_t s2[4] = {0, 99, 0, 0}; lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0); assert(s.theme == LSDJ_SET_THEMES - 1u); }   /* stops at the end */
    {   char l[LSDJ_MENU_LINES][20]; unsigned cnt = lsdj_menu_lines(&m, &s, &mi, &fe, 0, l); assert(!strcmp(l[cnt - 1], "K1 NEXT  K2 JUMP 8")); }
    { int32_t s2[4] = {-99, 0, 0, 0}; lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0); s2[0] = 3; lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0); assert(s.theme == 3); }
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 2); assert(lsdj_menu_entry(&m) == LSDJ_SET_POPUP);   /* past FX THEME */   /* POPUP TIME: 1.5 3 5 10 s */
    { int32_t s2[4] = {9, 0, 0, 0}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0) && s.popup == LSDJ_SET_POPUPS - 1u); }
    { char l[20]; lsdj_set_line(&s, LSDJ_SET_POPUP, l); assert(!strcmp(l, "POPUP TIME: 10S")); }
    { lsdj_settings_t r; assert(lsdj_set_unpack(lsdj_set_pack(&s), &r) && r.popup == 3 && lsdj_set_popup_ms[r.popup] == 10000u); }
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 1); assert(lsdj_menu_entry(&m) == LSDJ_SET_METER);
    { int32_t s2[4] = {0, 1, 0, 0}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, s2, 0) && s.meter == 1); }
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -9); lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0); assert(m.page == LSDJ_PAGE_TOP && m.cursor == 1);
    still(&m);
    /* CONTROLS */
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, -1); lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0);
    assert(m.page == LSDJ_PAGE_CONTROLS && lsdj_menu_entry(&m) == LSDJ_SET_KNOBS);
    still(&m);
    assert(lsdj_menu_knobs(&m, &s, &mi, &fe, k1, 0) && s.knobs_swapped == 1 && m.changed);
    { int32_t st[4] = {0, 0, 3, 0}; assert(!lsdj_menu_knobs(&m, &s, &mi, &fe, st, 0) && s.knobs_swapped == 1); }   /* at the end */
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 1);
    { int32_t st[4] = {0, 0, 0, 1}; assert(lsdj_menu_knobs(&m, &s, &mi, &fe, st, 0) && s.presets_rev && !s.algo_rev); }   /* K4 too */
    lsdj_menu_knobs(&m, &s, &mi, &fe, no, 99); assert(lsdj_menu_entry(&m) == LSDJ_SET_SELKEY);
    { int32_t st[4] = {2, 0, 0, 0}; lsdj_menu_knobs(&m, &s, &mi, &fe, st, 0); assert(s.select_key == 2); }
    { int32_t st[4] = {-1, 0, 0, 0}; lsdj_menu_knobs(&m, &s, &mi, &fe, st, 0); assert(s.select_key == 1); }
    {   char lines[LSDJ_MENU_LINES][20];
        unsigned cnt = lsdj_menu_lines(&m, &s, &mi, &fe, 0, lines);
        assert(!strcmp(lines[0], "SETTINGS/CONTROLS") && !strcmp(lines[1 + 5], "SELECT KEY: F#5"));
        assert(!strcmp(lines[cnt - 1], "SELECT=PICK K1=SET"));
        lsdj_menu_lines(&m, &s, &mi, &fe, "PRESETS: UP", lines); assert(!strcmp(lines[cnt - 1], "PRESETS: UP"));
    }
    assert(m.changed);
    lsdj_menu_open(&m); assert(m.page == LSDJ_PAGE_TOP && m.cursor == 0);   /* opens on the top again */
    assert(lsdj_set_unpack(0x5A000000u, &s) && s.popup == 0 && lsdj_set_popup_ms[0] == 1500u);   /* older saves: as before */

    if (argc > 1) {
        static uint8_t bg[160 * 144 * 3]; int have_bg = 0;   /* LSDj screenshot behind, if DIR/song.ppm */
        { char p[512]; snprintf(p, sizeof p, "%s/song.ppm", argv[1]); FILE *f = fopen(p, "rb");
          if (f) { char hdr[32]; if (fgets(hdr, sizeof hdr, f) && fread(bg, 1, sizeof bg, f) == sizeof bg) have_bg = 1; fclose(f); } }
        lsdj_settings_t shown[2] = {{0, 0, 0, 0, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 1, 4, 1}};
        for (int k = 0; k < 2; k++) {
            for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
                const uint8_t *q = &bg[((i / LSDJ_OV_W * 2u / 3u) * 160u + i % LSDJ_OV_W * 2u / 3u) * 3u];
                scr[i] = have_bg ? LSDJ_OV_SWAP((uint16_t)((q[0] >> 3) << 11 | (q[1] >> 2) << 5 | q[2] >> 3)) : 0x5555;
            }
            lsdj_menu_t mm; memset(&mm, 0, sizeof mm); mm.open = 1; mm.cursor = (uint8_t)(k ? 4 : 1);
            mm.page = LSDJ_PAGE_CONTROLS;
            char lines[LSDJ_MENU_LINES][20];
            unsigned cnt = lsdj_menu_lines(&mm, &shown[k], &mi, &fe, 0, lines);
            lsdj_ov_list(scr, (const char (*)[20])lines, cnt, mm.cursor);
            ppm(argv[1], k ? "menu_b" : "menu_a");
        }
    }
    puts("PASS: settings pack/unpack, defaults, knob list (SELECT picks, K1-K4 change, ends), text fits the font");
    return 0;
}
