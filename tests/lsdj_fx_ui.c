/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_fx_ui.h (pages, knobs, bars, shortcuts, saving).
   With a folder: writes the pop-up of every page as PPMs, drawn over a
   screenshot of LSDj's SONG screen if FOLDER/song.ppm (160x144) is there. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_fx_ui.h"
#include "lsdj_overlay.h"

static uint8_t echo[LSDJ_FX_ECHO_LEN];
static lsdj_fx_t fx; static lsdj_fx_ui_t u;
static uint16_t scr[LSDJ_OV_W * LSDJ_OV_H];
static int turn(unsigned knob, int clicks) { int32_t s[4] = {0, 0, 0, 0}; s[knob] = clicks; return lsdj_fx_knobs(&fx, &u, s); }
static void fits(const char *s, unsigned max) {
    assert(strlen(s) <= max);
    for (const char *c = s; *c; c++) assert(strchr(lsdj_ov_chars, *c));
}
static void check_bars(void) {
    char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
    for (int hot = -1; hot < 4; hot++) {
        lsdj_fx_bars(&fx, &u, hot, title, big, label, value, caption, level);
        fits(title, 17); fits(big, 17);   /* 17 x 12 px + margins inside the box */
    }
    for (int k = 0; k < 4; k++) { fits(label[k], 5); fits(value[k], 5); fits(caption[k], 2); assert(level[k] <= 127); }
}

int main(int argc, char **argv) {
    lsdj_fx_init(&fx, echo); lsdj_fx_ui_init(&u); lsdj_fx_update(&fx);
    assert(!fx.active && u.target == LSDJ_FX_CURSOR && u.page == 0);
    /* Every page: four different effects, every effect on some page once (DRIVE twice: FILTER and TONE). */
    { int seen[FXK_COUNT] = {0};
      for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) for (unsigned k = 0; k < 4; k++) seen[lsdj_fx_page_knob[p][k]]++;
      for (unsigned k = 0; k < FXK_COUNT; k++) assert(seen[k] == (k == FXK_DRIVE ? 2 : 1));
      assert(sizeof lsdj_fx_page_letters == LSDJ_FX_PAGES + 1); }

    /* Page 1 (K1 drive, K2 type, K3 cutoff, K4 reso), CURSOR on WAV: K3 = cutoff of WAV only; filter switches on. */
    u.cursor_ch = 2;
    assert(turn(2, 5) == 2);
    assert(fx.p.ch[2][FXP_CUTOFF] == 10 && fx.p.ch[2][FXP_TYPE] == 1 && fx.p.ch[0][FXP_CUTOFF] == 0 && fx.active);
    /* K2 = filter type, one per click, LP -> HP -> BP -> NOTCH -> LP24, stops at the ends. */
    turn(1, 1); assert(fx.p.ch[2][FXP_TYPE] == 2);
    turn(1, 9); assert(fx.p.ch[2][FXP_TYPE] == LSDJ_FX_FILTERS - 1);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_TYPE, v); assert(!strcmp(v, "LP24")); }
    turn(1, -9); assert(fx.p.ch[2][FXP_TYPE] == 0);
    turn(1, 1);
    /* Cursor unknown: ALL. */
    u.cursor_ch = 0xFF; turn(0, 4); for (int c = 0; c < 4; c++) assert(fx.p.ch[c][FXP_DRIVE] == 8);
    u.cursor_ch = 2;
    /* Values clamp to 0..127. */
    turn(2, 200); assert(fx.p.ch[2][FXP_CUTOFF] == 127); turn(2, -300); assert(fx.p.ch[2][FXP_CUTOFF] == 0);
    check_bars();

    /* SELECT knob: pages wrap both ways. */
    lsdj_fx_page_turn(&u, 1); assert(u.page == 1);
    lsdj_fx_page_turn(&u, -2); assert(u.page == LSDJ_FX_PAGES - 1);
    lsdj_fx_page_turn(&u, 1); assert(u.page == 0);
    lsdj_fx_page_turn(&u, LSDJ_FX_PAGES + 2); assert(u.page == 2);

    /* Page 2, TONE (K1 drive, K2 distortion, K3 warm, K4 compressor). Warm with no filter: low-pass on. */
    u.page = 1;
    turn(1, 2); assert(fx.p.q[2][FXQ_DIST] == 2 && fx.p.q[0][FXQ_DIST] == 0);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_DIST, v); assert(!strcmp(v, "FOLD")); }
    turn(1, 9); assert(fx.p.q[2][FXQ_DIST] == LSDJ_FX_DISTS - 1);
    fx.p.ch[2][FXP_TYPE] = 0; turn(2, 10); assert(fx.p.q[2][FXQ_WARM] == 20 && fx.p.ch[2][FXP_TYPE] == 1);
    turn(3, 30); assert(fx.p.comp == 60 && fx.active);   /* master: no channel */
    check_bars();
    /* Page 3, LFO: rate in Hz then synced notes; depth on the cutoff switches a filter on; shape, destination. */
    u.page = 2;
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_LFO_RATE, v); assert(!strcmp(v, "0.05")); }
    turn(0, 31); assert(fx.p.q[2][FXQ_LFO_RATE] == 62);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_LFO_RATE, v); assert(!strcmp(v, "11.2")); }   /* (4870 << 7) x 14 / 8 steps */
    turn(0, 19); { char v[6]; lsdj_fxk_value(&fx, &u, FXK_LFO_RATE, v); assert(!strcmp(v, "1/4")); }   /* 100: synced, note 8 */
    fx.p.ch[2][FXP_TYPE] = 0; turn(1, 5); assert(fx.p.q[2][FXQ_LFO_DEPTH] == 10 && fx.p.ch[2][FXP_TYPE] == 1);
    turn(2, 4); assert(fx.p.q[2][FXQ_LFO_SHAPE] == 4); turn(3, 9); assert(fx.p.q[2][FXQ_LFO_DEST] == 2);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_LFO_DEST, v); assert(!strcmp(v, "PAN")); lsdj_fxk_value(&fx, &u, FXK_LFO_SHAPE, v); assert(!strcmp(v, "RND")); }
    check_bars();
    { char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
      lsdj_fx_bars(&fx, &u, 0, title, big, label, value, caption, level);
      assert(!strcmp(title, "LFO WAV 3/6") && !strcmp(big, "K1 LFO RATE 1/4") && !strcmp(value[3], "PN")); }
    /* restore what the older checks below expect */
    for (unsigned q = 0; q < FXQ_COUNT; q++) fx.p.q[2][q] = 0;
    fx.p.comp = 0; fx.p.ch[2][FXP_TYPE] = 1; lsdj_fx_update(&fx);

    /* Page 4, LO-FI: send with the echo silent sets a mix; K4 switches the cursor channel's FX off and on. */
    u.page = 3;
    turn(2, 3); assert(fx.p.ch[2][FXP_SEND] == 6 && fx.p.m[FXM_MIX] == 80 && fx.p.m[FXM_TIME] && fx.p.m[FXM_FEEDBACK]);
    turn(3, -1); assert(fx.p.fx_off == 4 && u.target == LSDJ_FX_CURSOR);   /* WAV dry */
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_FXON, v); assert(!strcmp(v, "OFF")); }
    turn(3, -3); assert(fx.p.fx_off == 4);
    turn(3, 1); assert(fx.p.fx_off == 0);
    u.cursor_ch = 0; turn(1, 2); assert(fx.p.ch[0][FXP_DECIM] == 4 && fx.p.ch[2][FXP_DECIM] == 0); u.cursor_ch = 2;   /* follows the cursor */
    check_bars();

    /* Page 6, ECHO MORE: sync on starts at 1/8, then page 5's time steps by notes. */
    u.page = 5;
    turn(1, 1); assert(fx.p.m[FXM_SYNC] == 1 && lsdj_fx_note_of(fx.p.m[FXM_TIME]) == 5);
    turn(2, 1); assert(fx.p.m[FXM_MODE] == 1);
    turn(3, 1); assert(fx.freeze); turn(3, -1); assert(!fx.freeze);
    turn(0, 10); assert(fx.p.m[FXM_WOBBLE] == 20);
    check_bars();
    u.page = 4;
    turn(0, 1); assert(lsdj_fx_note_of(fx.p.m[FXM_TIME]) == 6);
    turn(3, -10); assert(fx.p.m[FXM_TONE] == 44);
    {   char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
        lsdj_fx_bars(&fx, &u, 0, title, big, label, value, caption, level);
        assert(!strcmp(big, "K1 TIME 1/4T"));
        assert(!strcmp(title, "ECHO 5/6"));              /* echo only: no channel */
        assert(!strcmp(label[0], "TM") && strlen(value[0]) == 2 && !strcmp(caption[0], "K1"));
        assert(!strcmp(value[3], "2C") && level[3] == 44);
        assert(level[0] == 6u * 127u / 11u);
        u.page = 0; lsdj_fx_bars(&fx, &u, -1, title, big, label, value, caption, level);
        assert(!strcmp(title, "FILTER WAV 1/6") && !strcmp(value[1], "LP") && !strcmp(label[1], "TY"));
        assert(!strcmp(big, "SELECT: PAGE"));
        assert(level[1] == 127u / (LSDJ_FX_FILTERS - 1u));
    }
    u.page = 5; turn(1, -1); assert(!fx.p.m[FXM_SYNC]);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_ECHO_TIME, v); assert(v[0] >= '1' && v[0] <= '9'); }   /* ms */
    for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) { u.page = (uint8_t)p; check_bars(); }
    u.page = 2;

    /* Hold FX: C4..F4 mute, G4..C5 FX off, D5 freeze; lights show it. */
    {
        lsdj_fx_t h; lsdj_fx_init(&h, echo); lsdj_fx_update(&h);
        char l1[20], l2[20];
        assert(lsdj_fx_hold_lights(&h) == ((1u << 7) | (1u << 9) | (1u << 11) | (1u << 12) | (1u << 14) | (1u << 16) | (1u << 18) | (1u << 19)));
        assert(lsdj_fx_hold_keys(&h, 1u << 11, l1, l2) && h.mute == 4 && !strcmp(l1, "WAV MUTED") && !strcmp(l2, "MUTED: WAV"));
        assert(!(lsdj_fx_hold_lights(&h) & (1u << 11)) && h.active);
        assert(lsdj_fx_hold_keys(&h, 1u << 14, l1, l2) && h.p.fx_off == 1 && !strcmp(l1, "PU1 FX OFF"));
        assert(lsdj_fx_hold_keys(&h, 1u << 21, l1, l2) && h.freeze && (lsdj_fx_hold_lights(&h) & (1u << 21)));
        assert(lsdj_fx_hold_keys(&h, (1u << 7) | (1u << 9) | (1u << 12), l1, l2) && h.mute == 15 && !strcmp(l2, "MUTED: ALL"));
        assert(!lsdj_fx_hold_keys(&h, 1u << 0, l1, l2));
        fits(l1, 19); fits(l2, 19);
    }

    /* Saving: round trip with page and target; mutes and freeze not kept. */
    uint32_t w[LSDJ_FX_WORDS]; lsdj_fx_pack(&fx, &u, w);
    lsdj_fx_t fx2; lsdj_fx_ui_t u2; lsdj_fx_init(&fx2, echo); lsdj_fx_ui_init(&u2);
    assert(lsdj_fx_unpack(&fx2, &u2, w) && !memcmp(&fx2.p, &fx.p, sizeof fx.p) && u2.page == 2 && u2.target == u.target);
    fx.mute = 3; fx.freeze = 1; { uint32_t w2[LSDJ_FX_WORDS]; lsdj_fx_pack(&fx, &u, w2); assert(!memcmp(w, w2, sizeof w)); } fx.mute = 0; fx.freeze = 0;
    /* Alphas 30-33: knob assignments in bytes 38-41 -> page 1. */
    {
        uint32_t ow[LSDJ_FX_WORDS]; memcpy(ow, w, sizeof ow);
        ow[9] = 3u | 1u << 8 | 0u << 16 | 5u << 24;        /* target 3, live 1, knobs CUTOFF RESO */
        ow[10] = 2u | 5u << 8 | (uint32_t)LSDJ_FX_MARK << 16;   /* knobs DRIVE SEND, mark */
        lsdj_fx_ui_init(&u2); assert(lsdj_fx_unpack(&fx2, &u2, ow) && u2.page == 0 && u2.target == LSDJ_FX_CURSOR);   /* a saved lock is dropped */
    }
    /* Alphas 27-29 (10 words, mark F1). */
    {
        uint8_t ob[44] = {0}; ob[0] = 1; ob[1] = 77; ob[28] = 33; ob[29] = 44; ob[30] = 55; ob[31] = 2; ob[32] = 1;
        ob[33] = 0; ob[34] = 1; ob[35] = 2; ob[36] = 8; ob[37] = 0xF1;
        uint32_t ow[LSDJ_FX_WORDS_OLD]; for (unsigned i = 0; i < LSDJ_FX_WORDS_OLD; i++) ow[i] = ob[i*4] | ob[i*4+1] << 8 | ob[i*4+2] << 16 | (uint32_t)ob[i*4+3] << 24;
        lsdj_fx_init(&fx2, echo); lsdj_fx_ui_init(&u2);
        assert(lsdj_fx_unpack_n(&fx2, &u2, ow, LSDJ_FX_WORDS_OLD));
        assert(fx2.p.ch[0][FXP_CUTOFF] == 77 && fx2.p.m[FXM_MIX] == 55 && fx2.p.m[FXM_TONE] == LSDJ_FX_TONE_FLAT && u2.target == LSDJ_FX_CURSOR && u2.page == 0);
    }
    /* Alpha 66's effects: saved and read back; an 11-word setup (alpha 65 and before) leaves them off */
    {
        lsdj_fx_t a; lsdj_fx_ui_t au; lsdj_fx_init(&a, echo); lsdj_fx_ui_init(&au);
        a.p.q[1][FXQ_LFO_RATE] = 99; a.p.q[3][FXQ_DIST] = 3; a.p.q[0][FXQ_LFO_DEST] = 2; a.p.comp = 77; a.p.ch[2][FXP_TYPE] = 5; lsdj_fx_update(&a);
        uint32_t aw[LSDJ_FX_WORDS]; lsdj_fx_pack(&a, &au, aw);
        lsdj_fx_t b; lsdj_fx_ui_t bu; lsdj_fx_init(&b, echo); lsdj_fx_ui_init(&bu);
        assert(lsdj_fx_unpack(&b, &bu, aw) && !memcmp(&a.p, &b.p, sizeof a.p));
        lsdj_fx_init(&b, echo);
        assert(lsdj_fx_unpack_n(&b, &bu, aw, LSDJ_FX_WORDS_OLD) && b.p.ch[2][FXP_TYPE] == 5 && b.p.comp == 77 && b.p.q[1][FXQ_LFO_RATE] == 0);
        aw[12] = 0xFFu; assert(!lsdj_fx_unpack(&b, &bu, aw));   /* a bad new value: refused */
        assert(!lsdj_fx_unpack_n(&b, &bu, aw, 12));
    }
    uint32_t bad[LSDJ_FX_WORDS]; memcpy(bad, w, sizeof bad); bad[10] = 0;
    lsdj_fx_init(&fx2, echo); lsdj_fx_ui_init(&u2);
    assert(!lsdj_fx_unpack(&fx2, &u2, bad) && u2.target == LSDJ_FX_CURSOR && !fx2.active);
    memcpy(bad, w, sizeof bad); bad[0] |= 0xFFu; assert(!lsdj_fx_unpack(&fx2, &u2, bad));

    /* The bars under LSDj's channel columns (Game Boy x 32 56 80 104, x 1.5). */
    for (unsigned c = 0; c < 4; c++) assert(LSDJ_OV_BAR_X(c) * 2u == (32u + 24u * c) * 3u);

    if (argc > 1) {
        static uint8_t bg[160 * 144 * 3]; int have_bg = 0;
        { char p[512]; snprintf(p, sizeof p, "%s/song.ppm", argv[1]); FILE *f = fopen(p, "rb");
          if (f) { char hdr[32]; if (fgets(hdr, sizeof hdr, f) && fread(bg, 1, sizeof bg, f) == sizeof bg) have_bg = 1; fclose(f); } }
        for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) {
            u.page = (uint8_t)p;
            for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
                uint32_t x = i % LSDJ_OV_W * 2u / 3u, y = i / LSDJ_OV_W * 2u / 3u;
                const uint8_t *q = &bg[(y * 160u + x) * 3u];
                scr[i] = have_bg ? LSDJ_OV_SWAP((uint16_t)((q[0] >> 3) << 11 | (q[1] >> 2) << 5 | q[2] >> 3)) : 0x5555;
            }
            lsdj_ov_bars_t b = {0};
            b.hot = (int8_t)(p % 4u); b.chan = (int8_t)lsdj_fx_eff(&u);
            lsdj_fx_bars(&fx, &u, b.hot, b.title, b.big, b.label, b.value, b.caption, b.level); lsdj_ov_bars_keep(&b, 1000u); lsdj_ov_bars_draw(&b, scr, 1000u);
            lsdj_ov_t o = {0}; lsdj_ov_show(&o, "MIDI START", 0, 1000u); o.lift = 20u; lsdj_ov_draw(&o, scr, 1000u);
            char path[512]; snprintf(path, sizeof path, "%s/fxbars_%u.ppm", argv[1], p);
            FILE *f = fopen(path, "wb"); fprintf(f, "P6 %u %u 255\n", LSDJ_OV_W, LSDJ_OV_H);
            for (unsigned i = 0; i < LSDJ_OV_W * LSDJ_OV_H; i++) {
                uint16_t c = LSDJ_OV_SWAP(scr[i]);
                uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        }
    }
    /* 2 or 3 choices: drawn as the choices, not a bar; everything else (and 4+ choices) a bar */
    {   uint8_t nc[4], pick[4]; char ch[4][3][3];
        lsdj_fx_t f; lsdj_fx_ui_t fu; lsdj_fx_init(&f, echo); lsdj_fx_ui_init(&fu); fu.cursor_ch = 1;
        f.p.q[1][FXQ_LFO_DEST] = 2; f.p.m[FXM_MODE] = 1; f.p.fx_off = 2; lsdj_fx_update(&f);
        fu.page = 2; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);   /* LFO: rate, depth, shape (5) bars; LFO TO 3 */
        assert(nc[0] == 0 && nc[1] == 0 && nc[2] == 0 && nc[3] == 3 && pick[3] == 2 && !strcmp(ch[3][0], "CT") && !strcmp(ch[3][2], "PN"));
        fu.page = 3; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);   /* LO-FI: FX ON, off on PU2 */
        assert(nc[3] == 2 && pick[3] == 0 && !strcmp(ch[3][1], "ON"));
        fu.page = 5; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);   /* ECHO MORE: wobble bar; sync, mode, freeze */
        assert(nc[0] == 0 && nc[1] == 2 && nc[2] == 2 && pick[2] == 1 && !strcmp(ch[2][1], "PP") && nc[3] == 2);
        fu.page = 0; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);   /* FILTER: the type has 6 choices: a bar */
        assert(!nc[0] && !nc[1] && !nc[2] && !nc[3]);
        for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) { fu.page = (uint8_t)p; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
            for (int k = 0; k < 4; k++) for (unsigned i = 0; i < nc[k]; i++) fits(ch[k][i], 2); }
    }
    puts("PASS: effects controls: pages, SELECT turns pages, knobs per page, choices, bars text fits, shortcuts, save words, 2-3 choices as choices");
    return 0;
}
