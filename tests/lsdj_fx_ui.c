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
static void check_bars_of(const lsdj_fx_t *f, const lsdj_fx_ui_t *uu) {
    char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
    for (int hot = -1; hot < 4; hot++) {
        lsdj_fx_bars(f, uu, hot, title, big, label, value, caption, level);
        fits(title, 17); fits(big, 17);   /* 17 x 12 px + margins inside the box */
    }
    for (int k = 0; k < 4; k++) { fits(label[k], 5); fits(value[k], 5); fits(caption[k], 2); assert(level[k] <= 127); }
}
static void check_bars(void) { check_bars_of(&fx, &u); }

int main(int argc, char **argv) {
    lsdj_fx_init(&fx, echo); lsdj_fx_ui_init(&u); lsdj_fx_update(&fx);
    assert(!fx.active && u.target == LSDJ_FX_CURSOR && u.page == FX_PAGE_FILTER && u.mpage == FX_PAGE_MASTER && !u.held);
    /* Every effect once; unused knobs are blank and inert. */
    { int seen[FXK_COUNT] = {0};
      for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) for (unsigned k = 0; k < 4; k++) {
          unsigned e = lsdj_fx_page_knob[p][k];
          if (e != FXK_NONE) { assert(e < FXK_COUNT); seen[e]++; continue; }
          u.page = (uint8_t)p;
          lsdj_fx_params_t before = fx.p;
          assert(turn(k, 10) == -1 && !memcmp(&before, &fx.p, sizeof before));
          char names[4][7], title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
          lsdj_fx_names(&u, names); assert(!names[k][0]);
          lsdj_fx_bars(&fx, &u, (int)k, title, big, label, value, caption, level);
          assert(!label[k][0] && !value[k][0] && !caption[k][0] && !level[k]);
      }
      for (unsigned k = 0; k < FXK_COUNT; k++) assert(seen[k] == 1);
      assert(sizeof lsdj_fx_page_letters == LSDJ_FX_PAGES + 1); }

    /* MASTER: the switch is independent of the cursor, preserves channel bypass and all values. */
    u.page = FX_PAGE_MASTER;
    for (unsigned c = 0; c <= 4; c++) {
        u.cursor_ch = c == 4 ? 0xFF : (uint8_t)c;
        lsdj_fx_params_t before = fx.p;
        turn(0, -1); assert(fx.p.bypass == 1);
        before.bypass = 1; assert(!memcmp(&before, &fx.p, sizeof before));
        turn(0, 1); assert(!fx.p.bypass);
    }
    turn(1, 30); assert(fx.p.comp == 60 && fx.active);
    check_bars(); fx.p.comp = 0; lsdj_fx_update(&fx);
    /* FILTER: Channel FX, echo send (979: next to FX on/off), cutoff, resonance. */
    u.page = FX_PAGE_FILTER; u.cursor_ch = 2;
    assert(lsdj_fx_page_knob[FX_PAGE_FILTER][0] == FXK_FXON && lsdj_fx_page_knob[FX_PAGE_FILTER][1] == FXK_SEND);
    assert(turn(2, 5) == 2);
    assert(fx.p.ch[2][FXP_CUTOFF] == 10 && fx.p.ch[2][FXP_TYPE] == 1 && fx.p.ch[0][FXP_CUTOFF] == 0 && fx.active);
    /* LO-FI K3 = filter type, one per click, LP -> HP -> BP -> NOTCH -> LP24, stops at the ends. */
    u.page = FX_PAGE_LOFI; assert(lsdj_fx_page_knob[FX_PAGE_LOFI][2] == FXK_TYPE);
    turn(2, 1); assert(fx.p.ch[2][FXP_TYPE] == 2);
    turn(2, 9); assert(fx.p.ch[2][FXP_TYPE] == LSDJ_FX_FILTERS - 1);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_TYPE, v); assert(!strcmp(v, "LP24")); }
    turn(2, -9); assert(fx.p.ch[2][FXP_TYPE] == 0);
    turn(2, 1);
    u.page = FX_PAGE_FILTER;
    /* Cursor unknown: ALL. */
    u.cursor_ch = 0xFF; lsdj_fxk_turn(&fx, &u, FXK_DRIVE, 4); for (int c = 0; c < 4; c++) assert(fx.p.ch[c][FXP_DRIVE] == 8);
    u.cursor_ch = 2;
    /* Values clamp to 0..127. */
    turn(2, 200); assert(fx.p.ch[2][FXP_CUTOFF] == 127); turn(2, -300); assert(fx.p.ch[2][FXP_CUTOFF] == 0);
    check_bars();

    /* SELECT knob: the channel pages stop at the first and last (979); PAGE WRAP on: they go
       round both ways; with FX held, the master pages. */
    u.page = FX_PAGE_FILTER;
    lsdj_fx_page_turn(&u, -1, 0); assert(u.page == FX_PAGE_FILTER);                 /* back to FILTER: stays */
    lsdj_fx_page_turn(&u, 1, 0); assert(u.page == FX_PAGE_DRIVE);
    lsdj_fx_page_turn(&u, 99, 0); assert(u.page == FX_PAGE_ECHO_MOD);
    lsdj_fx_page_turn(&u, 1, 0); assert(u.page == FX_PAGE_ECHO_MOD);
    lsdj_fx_page_turn(&u, -99, 0); assert(u.page == FX_PAGE_FILTER);
    lsdj_fx_page_turn(&u, 1, 1); assert(u.page == FX_PAGE_DRIVE);
    lsdj_fx_page_turn(&u, -2, 1); assert(u.page == FX_PAGE_ECHO_MOD);
    lsdj_fx_page_turn(&u, 1, 1); assert(u.page == FX_PAGE_FILTER);
    lsdj_fx_page_turn(&u, 6 + 2, 1); assert(u.page == FX_PAGE_LFO && u.mpage == FX_PAGE_MASTER);
    lsdj_fx_page_turn(&u, 2, 1); assert(u.page == FX_PAGE_ECHO); lsdj_fx_page_turn(&u, -2, 1); assert(u.page == FX_PAGE_LFO);
    u.held = 1;
    assert(lsdj_fx_cur(&u) == FX_PAGE_MASTER && lsdj_fx_pos(&u) == 0);
    lsdj_fx_page_turn(&u, 1, 0); assert(u.mpage == FX_PAGE_MASTER && u.page == FX_PAGE_LFO);   /* one master page */
    lsdj_fx_page_turn(&u, -3, 1); assert(u.mpage == FX_PAGE_MASTER && lsdj_fx_cur(&u) == FX_PAGE_MASTER);
    { char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
      lsdj_fx_bars(&fx, &u, -1, title, big, label, value, caption, level);
      assert(!strcmp(title, "MASTER 1/1") && !strcmp(big, "C4-F4: MUTE")); }
    u.held = 0; u.page = FX_PAGE_FILTER; u.mpage = FX_PAGE_MASTER;
    for (unsigned g = 0; g < 2; g++) for (unsigned i = 0; i < lsdj_fx_group_n[g]; i++)
        assert(lsdj_fx_is_master(lsdj_fx_group[g][i]) == (int)g);   /* every page in its own group once */
    assert(lsdj_fx_group_n[0] + lsdj_fx_group_n[1] == LSDJ_FX_PAGES && strlen(lsdj_fx_group_letters[0]) == 6 && strlen(lsdj_fx_group_letters[1]) == 1);

    /* DRIVE: drive, distortion, warmth. Warm with no filter: low-pass on. */
    u.page = FX_PAGE_DRIVE;
    turn(1, 2); assert(fx.p.q[2][FXQ_DIST] == 2 && fx.p.q[0][FXQ_DIST] == 0);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_DIST, v); assert(!strcmp(v, "FOLD")); }
    turn(1, 9); assert(fx.p.q[2][FXQ_DIST] == LSDJ_FX_DISTS - 1);
    fx.p.ch[2][FXP_TYPE] = 0; turn(2, 10); assert(fx.p.q[2][FXQ_WARM] == 20 && fx.p.ch[2][FXP_TYPE] == 1);
    assert(turn(3, 30) == -1 && !fx.p.comp);
    check_bars();
    /* Page 3, LFO: rate in Hz then synced notes; depth on the cutoff switches a filter on; shape, destination. */
    u.page = FX_PAGE_LFO;
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

    /* FILTER K2 send starts echo; FILTER K1 switches the cursor channel's FX off and on. */
    u.page = FX_PAGE_FILTER;
    turn(1, 3); assert(fx.p.ch[2][FXP_SEND] == 6 && fx.p.m[FXM_MIX] == 80 && fx.p.m[FXM_TIME] && fx.p.m[FXM_FEEDBACK]);
    turn(0, -1); assert(fx.p.fx_off == 4 && u.target == LSDJ_FX_CURSOR);   /* WAV dry */
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_FXON, v); assert(!strcmp(v, "OFF")); }
    turn(0, -3); assert(fx.p.fx_off == 4);
    turn(0, 1); assert(fx.p.fx_off == 0);
    u.page = FX_PAGE_LOFI;
    u.cursor_ch = 0; turn(1, 2); assert(fx.p.ch[0][FXP_DECIM] == 4 && fx.p.ch[2][FXP_DECIM] == 0); u.cursor_ch = 2;   /* follows the cursor */
    check_bars();

    /* ECHO MOD: sync on starts at 1/8, then ECHO time steps by notes. */
    u.page = FX_PAGE_ECHO_MOD;
    turn(1, 1); assert(fx.p.m[FXM_SYNC] == 1 && lsdj_fx_note_of(fx.p.m[FXM_TIME]) == 5);
    turn(2, 1); assert(fx.p.m[FXM_MODE] == 1);
    turn(3, 1); assert(fx.freeze); turn(3, -1); assert(!fx.freeze);
    turn(0, 10); assert(fx.p.m[FXM_WOBBLE] == 20);
    check_bars();
    u.page = FX_PAGE_ECHO;
    turn(0, 1); assert(lsdj_fx_note_of(fx.p.m[FXM_TIME]) == 6);
    turn(3, -10); assert(fx.p.m[FXM_TONE] == 44);
    {   char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
        lsdj_fx_bars(&fx, &u, 0, title, big, label, value, caption, level);
        assert(!strcmp(big, "K1 TIME 1/4T"));
        assert(!strcmp(title, "ECHO 5/6"));              /* a channel page, but shared: no channel named */
        assert(!strcmp(label[0], "TM") && strlen(value[0]) == 2 && !strcmp(caption[0], "K1"));
        assert(!strcmp(value[3], "2C") && level[3] == 44);
        assert(level[0] == fx.p.m[FXM_TIME]);
        u.page = FX_PAGE_FILTER; lsdj_fx_bars(&fx, &u, -1, title, big, label, value, caption, level);
        assert(!strcmp(title, "FILTER WAV 1/6") && !strcmp(value[1], "06") && !strcmp(label[1], "SN"));
        assert(!strcmp(big, "SELECT: PAGE"));
        assert(level[1] == 6);
        u.page = FX_PAGE_LOFI; lsdj_fx_bars(&fx, &u, -1, title, big, label, value, caption, level);
        assert(!strcmp(value[2], "LP") && !strcmp(label[2], "TY") && level[2] == 127u / (LSDJ_FX_FILTERS - 1u));
    }
    u.page = FX_PAGE_ECHO_MOD; turn(1, -1); assert(!fx.p.m[FXM_SYNC]);
    { char v[6]; lsdj_fxk_value(&fx, &u, FXK_ECHO_TIME, v); assert(v[0] >= '1' && v[0] <= '9'); }   /* ms */
    for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) { u.page = (uint8_t)p; check_bars(); }
    u.page = FX_PAGE_LFO;

    /* KNOBS = CHANNELS: one effect on K1-K4 = PU1 PU2 WAV NOI, whatever the cursor; SELECT goes
       through the 14 per-channel effects in page order, then ECHO and ECHO MOD as pages. */
    {
        lsdj_fx_t c; lsdj_fx_ui_t cu; lsdj_fx_init(&c, echo); lsdj_fx_ui_init(&cu); lsdj_fx_update(&c);
        cu.channels = 1; cu.cursor_ch = 2;
        { int seen[FXK_COUNT] = {0};   /* exactly the per-channel effects, once each */
          for (unsigned i = 0; i < LSDJ_FX_CHPARAMS; i++) { assert(lsdj_fxk_on_channel(lsdj_fx_chparam[i])); seen[lsdj_fx_chparam[i]]++; }
          for (unsigned k = 0; k < FXK_COUNT; k++) assert(seen[k] == (lsdj_fxk_on_channel(k) ? 1 : 0)); }
        assert(lsdj_fx_chmode(&cu) && lsdj_fx_count(&cu) == LSDJ_FX_CHPARAMS + 2u && lsdj_fx_pos(&cu) == 0);
        int32_t st[4] = {-1, 0, -1, 0};                     /* FX ON: PU1 and WAV off */
        assert(lsdj_fx_knobs(&c, &cu, st) == 2 && c.p.fx_off == 5);
        lsdj_fx_page_turn(&cu, 1, 0); assert(cu.param == 1 && cu.page == FX_PAGE_FILTER);   /* SEND */
        lsdj_fx_page_turn(&cu, 1, 0); assert(cu.param == 2 && cu.page == FX_PAGE_FILTER);   /* CUTOFF */
        { int32_t s2[4] = {1, 2, 3, 4}; lsdj_fx_knobs(&c, &cu, s2); }
        for (unsigned ch = 0; ch < 4; ch++) assert(c.p.ch[ch][FXP_CUTOFF] == 2u * (ch + 1u) && c.p.ch[ch][FXP_TYPE] == 1);
        {   char title[20], big[20], label[4][7], value[4][6], caption[4][4], names[4][7]; uint8_t level[4];
            lsdj_fx_bars(&c, &cu, 3, title, big, label, value, caption, level);
            assert(!strcmp(title, "CUTOFF 3/16") && !strcmp(big, "K4 NOI 08"));
            assert(!strcmp(label[0], "PU1") && !strcmp(label[3], "NOI") && !strcmp(value[1], "04") && level[2] == 6);
            lsdj_fx_names(&cu, names); assert(!strcmp(names[0], "PU1") && !strcmp(names[3], "NOI"));
            check_bars_of(&c, &cu); }
        lsdj_fx_page_turn(&cu, 2, 0); assert(cu.param == 4 && cu.page == FX_PAGE_DRIVE);   /* DRIVE */
        lsdj_fx_page_turn(&cu, 1, 0);                                                         /* DISTORT: choices */
        { uint8_t nc[4], pick[4]; char chs[4][6][3]; c.p.q[3][FXQ_DIST] = 2;
          lsdj_fx_bar_choices(&c, &cu, nc, pick, chs); assert(nc[0] == 4 && nc[3] == 4 && pick[3] == 2 && pick[0] == 0); }
        lsdj_fx_page_turn(&cu, 99, 0); assert(cu.param == LSDJ_FX_CHPARAMS + 1u && cu.page == FX_PAGE_ECHO_MOD && !lsdj_fx_chmode(&cu));
        lsdj_fx_page_turn(&cu, -1, 0); assert(cu.page == FX_PAGE_ECHO && lsdj_fx_knob(&cu, 0) == FXK_ECHO_TIME);   /* a page again */
        lsdj_fx_page_turn(&cu, -1, 0); assert(cu.param == LSDJ_FX_CHPARAMS - 1u && cu.page == FX_PAGE_LOFI && lsdj_fx_knob(&cu, 2) == FXK_TYPE);
        lsdj_fx_page_turn(&cu, 3, 1); assert(cu.param == 0 && cu.page == FX_PAGE_FILTER);   /* PAGE WRAP: round */
        cu.held = 1; assert(!lsdj_fx_chmode(&cu) && lsdj_fx_cur(&cu) == FX_PAGE_MASTER && lsdj_fx_count(&cu) == 1);   /* hold FX: as ever */
        cu.held = 0;
        for (unsigned i = 0; i < LSDJ_FX_CHPARAMS + 2u; i++) { cu.param = (uint8_t)i; check_bars_of(&c, &cu); }
    }

    /* Hold FX: C4..F4 mute PU1..NOI; their lights are lit while the channel plays; other keys do nothing. */
    {
        lsdj_fx_t h; lsdj_fx_init(&h, echo); lsdj_fx_update(&h);
        assert(lsdj_fx_hold_lights(&h) == ((1u << 7) | (1u << 9) | (1u << 11) | (1u << 12)));
        assert(lsdj_fx_hold_keys(&h, 1u << 11) && h.mute == 4 && h.active);
        assert(!(lsdj_fx_hold_lights(&h) & (1u << 11)));
        assert(lsdj_fx_hold_keys(&h, (1u << 7) | (1u << 9) | (1u << 12)) && h.mute == 15 && !lsdj_fx_hold_lights(&h));
        assert(!lsdj_fx_hold_keys(&h, (1u << 14) | (1u << 21) | 1u) && h.mute == 15 && !h.p.fx_off && !h.freeze);
        assert(lsdj_fx_hold_keys(&h, 1u << 11) && h.mute == 11);
    }

    /* Saving: round trip with page and target; mutes and freeze not kept. */
    fx.p.bypass = 1;
    uint32_t w[LSDJ_FX_WORDS]; lsdj_fx_pack(&fx, &u, w);
    lsdj_fx_t fx2; lsdj_fx_ui_t u2; lsdj_fx_init(&fx2, echo); lsdj_fx_ui_init(&u2);
    assert(lsdj_fx_unpack(&fx2, &u2, w) && !memcmp(&fx2.p, &fx.p, sizeof fx.p) && u2.page == FX_PAGE_LFO && u2.target == u.target);
    fx.mute = 3; fx.freeze = 1; { uint32_t w2[LSDJ_FX_WORDS]; lsdj_fx_pack(&fx, &u, w2); assert(!memcmp(w, w2, sizeof w)); } fx.mute = 0; fx.freeze = 0;
    /* MASTER saved by 975: opens on FILTER (MASTER is the hold-FX page now); ECHO stays a channel page. */
    { lsdj_fx_ui_t m = u; m.page = FX_PAGE_MASTER; uint32_t mw[LSDJ_FX_WORDS]; lsdj_fx_pack(&fx, &m, mw);
      lsdj_fx_ui_init(&u2); assert(lsdj_fx_unpack(&fx2, &u2, mw) && u2.page == FX_PAGE_FILTER);
      m.page = FX_PAGE_ECHO; lsdj_fx_pack(&fx, &m, mw);
      lsdj_fx_ui_init(&u2); assert(lsdj_fx_unpack(&fx2, &u2, mw) && u2.page == FX_PAGE_ECHO); }
    /* Alphas 30-33: knob assignments in bytes 38-41 -> page 1. */
    {
        uint32_t ow[LSDJ_FX_WORDS]; memcpy(ow, w, sizeof ow);
        ow[9] = 3u | 1u << 8 | 0u << 16 | 5u << 24;        /* target 3, live 1, knobs CUTOFF RESO */
        ow[10] = 2u | 5u << 8 | (uint32_t)LSDJ_FX_MARK << 16;   /* knobs DRIVE SEND, mark */
        lsdj_fx_ui_init(&u2); assert(lsdj_fx_unpack(&fx2, &u2, ow) && u2.page == FX_PAGE_FILTER && u2.target == LSDJ_FX_CURSOR && !fx2.p.bypass);
    }
    /* Alphas 27-29 (10 words, mark F1). */
    {
        uint8_t ob[44] = {0}; ob[0] = 1; ob[1] = 77; ob[28] = 33; ob[29] = 44; ob[30] = 55; ob[31] = 2; ob[32] = 1;
        ob[33] = 0; ob[34] = 1; ob[35] = 2; ob[36] = 8; ob[37] = 0xF1;
        uint32_t ow[LSDJ_FX_WORDS_OLD]; for (unsigned i = 0; i < LSDJ_FX_WORDS_OLD; i++) ow[i] = ob[i*4] | ob[i*4+1] << 8 | ob[i*4+2] << 16 | (uint32_t)ob[i*4+3] << 24;
        lsdj_fx_init(&fx2, echo); lsdj_fx_ui_init(&u2);
        assert(lsdj_fx_unpack_n(&fx2, &u2, ow, LSDJ_FX_WORDS_OLD));
        assert(fx2.p.ch[0][FXP_CUTOFF] == 77 && fx2.p.m[FXM_MIX] == 55 && fx2.p.m[FXM_TONE] == LSDJ_FX_TONE_FLAT && u2.target == LSDJ_FX_CURSOR && u2.page == FX_PAGE_FILTER && !fx2.p.bypass);
    }
    /* Alpha 66's effects: saved and read back; an 11-word setup (alpha 65 and before) leaves them off */
    {
        lsdj_fx_t a; lsdj_fx_ui_t au; lsdj_fx_init(&a, echo); lsdj_fx_ui_init(&au);
        a.p.q[1][FXQ_LFO_RATE] = 99; a.p.q[3][FXQ_DIST] = 3; a.p.q[0][FXQ_LFO_DEST] = 2; a.p.comp = 77; a.p.ch[2][FXP_TYPE] = 5; lsdj_fx_update(&a);
        uint32_t aw[LSDJ_FX_WORDS]; lsdj_fx_pack(&a, &au, aw);
        lsdj_fx_t b; lsdj_fx_ui_t bu; lsdj_fx_init(&b, echo); lsdj_fx_ui_init(&bu);
        assert(lsdj_fx_unpack(&b, &bu, aw) && !memcmp(&a.p, &b.p, sizeof a.p));
        lsdj_fx_init(&b, echo);
        assert(!lsdj_fx_unpack_n(&b, &bu, aw, LSDJ_FX_WORDS_OLD));
        aw[9] = LSDJ_FX_CURSOR | 1u << 8;
        assert(lsdj_fx_unpack_n(&b, &bu, aw, LSDJ_FX_WORDS_OLD) && b.p.ch[2][FXP_TYPE] == 5 && b.p.comp == 77 && b.p.q[1][FXQ_LFO_RATE] == 0);
        aw[12] = 0xFFu; assert(!lsdj_fx_unpack(&b, &bu, aw));   /* a bad new value: refused */
        assert(!lsdj_fx_unpack_n(&b, &bu, aw, 12));
    }
    /* Every saved legacy page migrates; legacy live clients keep newer effects. */
    {
        static const unsigned old4[] = {FX_PAGE_FILTER, FX_PAGE_LOFI, FX_PAGE_ECHO, FX_PAGE_ECHO_MOD};
        for (unsigned count = 11; count <= 17; count += 6) {
            unsigned pages = count == 11 ? 4 : 6;
            for (unsigned p = 0; p < pages; p++) {
                uint32_t ow[LSDJ_FX_WORDS]; memcpy(ow, w, sizeof ow);
                ow[9] = LSDJ_FX_CURSOR | 1u << 8 | p << 16;
                unsigned expected = count == 11 ? old4[p] : p + 1;
                if (lsdj_fx_is_master(expected)) expected = FX_PAGE_FILTER;   /* master pages: hold FX now */
                fx2.p.bypass = 1;
                assert(lsdj_fx_unpack_n(&fx2, &u2, ow, count) && u2.page == expected && !fx2.p.bypass);
                fx2.p.bypass = 1; fx2.p.comp = 93; fx2.p.q[2][FXQ_WARM] = 47;
                assert(lsdj_fx_set_words(&fx2, &u2, ow, count) && u2.page == expected && fx2.p.bypass);
                if (count == 11) assert(fx2.p.comp == 93 && fx2.p.q[2][FXQ_WARM] == 47);
            }
        }
        for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) for (unsigned bypass = 0; bypass < 2; bypass++) {
            fx.p.bypass = (uint8_t)bypass; u.page = (uint8_t)p;
            uint32_t nw[LSDJ_FX_WORDS]; lsdj_fx_pack(&fx, &u, nw);
            assert(lsdj_fx_unpack(&fx2, &u2, nw) && u2.page == (lsdj_fx_is_master(p) ? FX_PAGE_FILTER : p) && fx2.p.bypass == bypass);
            lsdj_fx_params_t before = fx2.p;
            nw[9] = LSDJ_FX_CURSOR | 2u << 8 | 7u << 16;
            assert(!lsdj_fx_set_words(&fx2, &u2, nw, LSDJ_FX_WORDS) && !memcmp(&before, &fx2.p, sizeof before));
            nw[9] = LSDJ_FX_CURSOR | 2u << 8 | 2u << 24;
            assert(!lsdj_fx_unpack(&fx2, &u2, nw) && !memcmp(&before, &fx2.p, sizeof before));
        }
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
    /* Every discrete parameter is a choice list; amount scales are raw settings. */
    {   uint8_t nc[4], pick[4]; char ch[4][6][3];
        lsdj_fx_t f; lsdj_fx_ui_t fu; lsdj_fx_init(&f, echo); lsdj_fx_ui_init(&fu); fu.cursor_ch = 1;
        f.p.q[1][FXQ_LFO_DEST] = 2; f.p.m[FXM_MODE] = 1; f.p.fx_off = 2; lsdj_fx_update(&f);
        fu.page = FX_PAGE_LFO; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
        assert(nc[0] == 0 && nc[1] == 0 && nc[2] == 5 && nc[3] == 3 && pick[3] == 2 && !strcmp(ch[3][0], "CT") && !strcmp(ch[3][2], "PN"));
        assert(!strcmp(ch[2][4], "RN"));
        fu.page = FX_PAGE_FILTER; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
        assert(nc[0] == 2 && pick[0] == 0 && !strcmp(ch[0][1], "ON"));
        fu.page = FX_PAGE_ECHO_MOD; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
        assert(nc[0] == 0 && nc[1] == 2 && nc[2] == 2 && pick[2] == 1 && !strcmp(ch[2][1], "PP") && nc[3] == 2);
        fu.page = FX_PAGE_FILTER; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
        assert(nc[0] == 2 && !nc[1] && !nc[2] && !nc[3]);
        fu.page = FX_PAGE_LOFI; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
        assert(!nc[0] && !nc[1] && nc[2] == 6 && !nc[3] && !strcmp(ch[2][5], "L4"));
        fu.page = FX_PAGE_DRIVE; f.p.q[1][FXQ_DIST] = 3; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
        assert(nc[1] == 4 && pick[1] == 3 && !strcmp(ch[1][3], "FZ"));
        for (unsigned p = 0; p < LSDJ_FX_PAGES; p++) { fu.page = (uint8_t)p; lsdj_fx_bar_choices(&f, &fu, nc, pick, ch);
            for (int k = 0; k < 4; k++) {
                unsigned e = lsdj_fx_page_knob[p][k];
                for (unsigned i = 0; i < nc[k]; i++) {
                    fits(ch[k][i], 2);
                    lsdj_fx_t chosen = f;
                    if (lsdj_fxk_is_q(e)) chosen.p.q[1][lsdj_fxk_q(e)] = (uint8_t)i;
                    else if (e == FXK_TYPE) chosen.p.ch[1][FXP_TYPE] = (uint8_t)i;
                    else if (e == FXK_FXON) chosen.p.fx_off = i ? 0 : 2;
                    else if (e == FXK_ALLON) chosen.p.bypass = !i;
                    else if (e == FXK_SYNC) chosen.p.m[FXM_SYNC] = (uint8_t)i;
                    else if (e == FXK_MODE) chosen.p.m[FXM_MODE] = (uint8_t)i;
                    else chosen.freeze = (uint8_t)i;
                    char val[6]; lsdj_fxk_value_short(&chosen, &fu, e, val);
                    assert(!strcmp(val, ch[k][i]));
                }
            }
        }
        for (unsigned v = 0; v < 128u; v++) {
            f.p.m[FXM_TIME] = (uint8_t)v;
            f.p.q[1][FXQ_LFO_RATE] = (uint8_t)v;
            assert(lsdj_fxk_level(&f, &fu, FXK_ECHO_TIME) == v);
            assert(lsdj_fxk_level(&f, &fu, FXK_LFO_RATE) == v);
        }
    }
    puts("PASS: effects controls: pages, knobs, shortcuts, save words, all 2-6 choices as selectors, raw amount scales");
    return 0;
}
