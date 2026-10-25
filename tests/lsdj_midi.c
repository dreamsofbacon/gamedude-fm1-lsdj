/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_midi.h: packing (every combination round-trips,
   junk refused, defaults = alpha 56 behaviour), routing by source and channel,
   CC -> effects, clock out Start / ticks / Stop, the list lines. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_midi.h"

static uint32_t pk(uint8_t st, uint8_t d1, uint8_t d2) { return (uint32_t)(st >> 4) | (uint32_t)st << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24; }
static void fits(const char *s) { assert(strlen(s) <= 19); for (; *s; s++) assert((*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr(" :+-", *s)); }

int main(void) {
    lsdj_midi_t m, t;
    /* defaults and packing */
    assert(!lsdj_midi_unpack(0, &m) && m.clock_src == LSDJ_CLK_ALL && m.transport && m.delay == 0 && !m.note && !m.cc && !m.clock_out && !m.pc_cue);
    assert(lsdj_midi_delay_samples(&m) == 4410u);   /* = alpha 56's SYNC_DELAY */
    unsigned combos = 0;
    for (unsigned a = 0; a < LSDJ_CLK_COUNT; a++) for (unsigned d = 0; d < LSDJ_MIDI_DELAYS; d++)
    for (unsigned n = 0; n < LSDJ_MIDI_NOTE_CHOICES; n++) for (unsigned c = 0; c < LSDJ_MIDI_CC_CHOICES; c++)
    for (unsigned o = 0; o < LSDJ_OUT_COUNT; o++) for (unsigned x = 0; x < 4; x++) {
        lsdj_midi_t v = {(uint8_t)a, (uint8_t)(x & 1), (uint8_t)d, (uint8_t)n, (uint8_t)c, (uint8_t)o, (uint8_t)(x >> 1)};
        assert(lsdj_midi_unpack(lsdj_midi_pack(&v), &t) && !memcmp(&v, &t, sizeof v));
        if (!(combos++ % 97)) for (unsigned i = 0; i < LSDJ_MIDI_ITEMS; i++) { char l[20]; lsdj_midi_line(&v, i, l); fits(l); }
    }
    assert(!lsdj_midi_unpack(0x4D000005u, &t));                 /* clock source 5 */
    assert(!lsdj_midi_unpack(0x4D000030u, &t));                 /* delay 3 */
    assert(!lsdj_midi_unpack(0x5A000000u, &t));                 /* a settings word, not MIDI */
    assert(!lsdj_midi_unpack(0x4D040000u, &t));                 /* unused bits */

    /* routing */
    lsdj_midi_defaults(&m);
    assert(lsdj_midi_route(&m, pk(0xF8, 0, 0), LSDJ_MIDI_SRC_TRS) == LSDJ_MIDI_CLOCK);
    assert(lsdj_midi_route(&m, pk(0xFA, 0, 0), LSDJ_MIDI_SRC_BT) == LSDJ_MIDI_TRANSPORT);
    assert(lsdj_midi_route(&m, pk(0x90, 60, 100), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_IGNORE);   /* notes off by default */
    m.clock_src = LSDJ_CLK_USB;
    assert(lsdj_midi_route(&m, pk(0xF8, 0, 0), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_CLOCK);
    assert(lsdj_midi_route(&m, pk(0xF8, 0, 0), LSDJ_MIDI_SRC_TRS) == LSDJ_MIDI_IGNORE);
    assert(lsdj_midi_route(&m, pk(0xFC, 0, 0), LSDJ_MIDI_SRC_BT) == LSDJ_MIDI_IGNORE);
    m.transport = 0;
    assert(lsdj_midi_route(&m, pk(0xFA, 0, 0), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_IGNORE);   /* hand transport */
    assert(lsdj_midi_route(&m, pk(0xF8, 0, 0), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_CLOCK);     /* the clock still */
    m.clock_src = LSDJ_CLK_OFF;
    assert(lsdj_midi_route(&m, pk(0xF8, 0, 0), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_IGNORE);
    m.note = 1;
    assert(lsdj_midi_route(&m, pk(0x93, 60, 100), LSDJ_MIDI_SRC_TRS) == LSDJ_MIDI_NOTE);
    assert(lsdj_midi_route(&m, pk(0x93, 60, 0), LSDJ_MIDI_SRC_TRS) == LSDJ_MIDI_IGNORE);   /* velocity 0 = off */
    m.note = 2 + 9;   /* channel 10 */
    assert(lsdj_midi_route(&m, pk(0x99, 36, 90), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_NOTE);
    assert(lsdj_midi_route(&m, pk(0x90, 36, 90), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_IGNORE);
    m.cc = 5;         /* channels 5..8 */
    assert(lsdj_midi_route(&m, pk(0xB4, 74, 10), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_CC);
    assert(lsdj_midi_route(&m, pk(0xB7, 74, 10), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_CC);
    assert(lsdj_midi_route(&m, pk(0xB8, 74, 10), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_IGNORE);
    assert(lsdj_midi_route(&m, pk(0xB3, 74, 10), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_IGNORE);
    assert(lsdj_midi_route(&m, pk(0xC0, 12, 0), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_IGNORE);
    m.pc_cue = 1;
    assert(lsdj_midi_route(&m, pk(0xC5, 12, 0), LSDJ_MIDI_SRC_USB) == LSDJ_MIDI_PC);

    /* CC -> effects */
    static uint8_t echo[LSDJ_FX_ECHO_LEN]; lsdj_fx_t fx; lsdj_fx_init(&fx, echo);
    assert(lsdj_midi_cc(&m, &fx, pk(0xB6, 74, 99)) && fx.p.ch[2][FXP_CUTOFF] == 99 && fx.p.ch[2][FXP_TYPE] == 1);   /* WAV, LP on */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 70, 50)) && fx.p.ch[0][FXP_TYPE] == 2);   /* PU1 high-pass (6 bands of ~21) */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 70, 127)) && fx.p.ch[0][FXP_TYPE] == 5);  /* LP24 */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 70, 0)) && fx.p.ch[0][FXP_TYPE] == 0);
    /* alpha 66: LFO, warm, distortion per channel; the compressor from any channel */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 72, 100)) && fx.p.q[0][FXQ_LFO_RATE] == 100 && fx.c[0].lfo_inc == lsdj_fx_lfo_step(100, fx.bpm));
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 73, 64)) && fx.p.q[0][FXQ_LFO_DEPTH] == 64 && fx.p.ch[0][FXP_TYPE] == 1);   /* to the cutoff: LP on */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 83, 127)) && fx.p.q[0][FXQ_LFO_DEST] == 2);   /* PAN */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 82, 64)) && fx.p.q[0][FXQ_LFO_SHAPE] == 2);   /* SQUARE */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB5, 78, 30)) && fx.p.q[1][FXQ_WARM] == 30 && fx.p.ch[1][FXP_TYPE] == 1);   /* PU2 warm: LP on */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB7, 79, 96)) && fx.p.q[3][FXQ_DIST] == 3);   /* NOI fuzz */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB6, 20, 40)) && fx.p.comp == 40 && fx.active);
    assert(lsdj_midi_cc(&m, &fx, pk(0xB7, 91, 50)) && fx.p.ch[3][FXP_SEND] == 50);
    assert(lsdj_midi_cc(&m, &fx, pk(0xB5, 80, 0)) && (fx.p.fx_off & 2));          /* PU2 dry */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB5, 80, 127)) && !(fx.p.fx_off & 2));
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 81, 127)) && fx.mute == 1);
    assert(lsdj_midi_cc(&m, &fx, pk(0xB7, 14, 77)) && fx.p.m[FXM_MIX] == 77);      /* echo from any of the four */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 17, 127)) && fx.p.m[FXM_SYNC] == 1 && lsdj_fx_note_of(fx.p.m[FXM_TIME]) == 5);
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 12, 127)) && lsdj_fx_note_of(fx.p.m[FXM_TIME]) == LSDJ_FX_NOTES - 1);   /* synced: notes */
    assert(lsdj_midi_cc(&m, &fx, pk(0xB4, 19, 100)) && fx.freeze == 1);
    assert(!lsdj_midi_cc(&m, &fx, pk(0xB4, 1, 100)));                              /* not mapped */
    for (unsigned i = 0; i < LSDJ_MIDI_CCS; i++) { assert(strlen(lsdj_midi_cc_name[i]) <= 11); assert(lsdj_midi_cc_apply(&fx, 1, lsdj_midi_cc_num[i], 64)); }

    /* clock out: Start then ticks, Stop after the ticks stop, Start again */
    { lsdj_mco_t o = {0}; uint8_t b[2]; unsigned n;
      n = lsdj_mco_tick(&o, 1000, b); assert(n == 2 && b[0] == 0xFA && b[1] == 0xF8);
      for (unsigned k = 1; k < 48; k++) { n = lsdj_mco_tick(&o, 1000 + 21 * k, b); assert(n == 1 && b[0] == 0xF8); assert(!lsdj_mco_poll(&o, 1000 + 21 * k + 10)); }
      assert(!lsdj_mco_poll(&o, 1000 + 21 * 47 + LSDJ_MCO_STOP_MS) && lsdj_mco_poll(&o, 1000 + 21 * 47 + LSDJ_MCO_STOP_MS + 1));
      assert(!lsdj_mco_poll(&o, 99999));
      n = lsdj_mco_tick(&o, 100000, b); assert(n == 2 && b[0] == 0xFA); }

    /* the list: one choice per click, ends hold */
    lsdj_midi_defaults(&m);
    assert(lsdj_midi_turn(&m, LSDJ_MIDI_ITEM_DELAY, 5) && m.delay == LSDJ_MIDI_DELAYS - 1 && !lsdj_midi_turn(&m, LSDJ_MIDI_ITEM_DELAY, 1));
    assert(lsdj_midi_turn(&m, LSDJ_MIDI_ITEM_NOTE, 11) && m.note == 11);
    { char l[20]; lsdj_midi_line(&m, LSDJ_MIDI_ITEM_NOTE, l); assert(!strcmp(l, "NOTE IN: CH 10"));
      lsdj_midi_line(&m, LSDJ_MIDI_ITEM_DELAY, l); assert(!strcmp(l, "SYNC DELAY: 50 MS"));
      m.cc = 13; lsdj_midi_line(&m, LSDJ_MIDI_ITEM_CC, l); assert(!strcmp(l, "CC FX: CH 13-16"));
      m.clock_out = LSDJ_OUT_BOTH; lsdj_midi_line(&m, LSDJ_MIDI_ITEM_OUT, l); assert(!strcmp(l, "CLOCK OUT: USB+BT")); }
    printf("PASS lsdj_midi: packing (%u combinations), routing by source / channel, CC -> effects, clock out, list lines\n", combos);
    return 0;
}
