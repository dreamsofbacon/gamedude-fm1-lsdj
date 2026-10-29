/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI options (the MIDI part of the settings list, kept in save-store record 16):

   CLOCK IN   OFF / ALL / USB / TRS / BT   where clock and Start/Stop are taken from
   TRANSPORT  ON / OFF                     Start / Continue / Stop press LSDj's START
                                            (off: start and stop by hand; the clock still
                                            drives LSDj while it plays)
   SYNC DELAY 100 / 75 / 50 MS             a tick is heard this long after it arrived
                                            (lower = closer to the source; 50 is the least
                                            the sound buffer allows without jitter)
   NOTE IN    OFF / ALL / CH 1..16          note-ons play LSDj like the FM-1's keys in note
                                            mode (LSDj SYNC = KEYBD)
   CC FX      OFF / CH 1..13                CCs move the effects: channel N = PU1, N+1 PU2,
                                            N+2 WAV, N+3 NOI (lsdj_midi_cc_map)
   CLOCK OUT  OFF / USB / BT / USB+BT      LSDj leading (SYNC = LSDJ, playing): MIDI clock,
                                            Start and Stop out, each tick when it is heard
   PC CUE     OFF / ON                      a program change n (any channel) cues song row n

   Defaults are alpha 56's behaviour: clock from everywhere, transport on, 100 ms.
   Pure logic: tests/lsdj_midi.c. */
#pragma once
#include <stdint.h>
#include "lsdj_fx_ui.h"   /* the knobs' effects (FXK_*), for CC display and CC out */

enum { LSDJ_MIDI_SRC_USB = 1, LSDJ_MIDI_SRC_TRS = 2, LSDJ_MIDI_SRC_BT = 3 };   /* where a message came from */
enum { LSDJ_CLK_OFF, LSDJ_CLK_ALL, LSDJ_CLK_USB, LSDJ_CLK_TRS, LSDJ_CLK_BT, LSDJ_CLK_COUNT };
enum { LSDJ_OUT_OFF, LSDJ_OUT_USB, LSDJ_OUT_BT, LSDJ_OUT_BOTH, LSDJ_OUT_COUNT };
#define LSDJ_MIDI_DELAYS 3u
static const uint8_t lsdj_midi_delay_ms[LSDJ_MIDI_DELAYS] = {100, 75, 50};
#define LSDJ_MIDI_NOTE_CHOICES 18u   /* OFF, ALL, CH 1..16 */
#define LSDJ_MIDI_CC_CHOICES 14u     /* OFF, CH 1..13 (four channels from it) */

typedef struct {
    uint8_t clock_src;   /* LSDJ_CLK_* */
    uint8_t transport;   /* 1 on */
    uint8_t delay;       /* index into lsdj_midi_delay_ms */
    uint8_t note;        /* 0 off, 1 all, 2..17 = channel 1..16 */
    uint8_t cc;          /* 0 off, 1..13 = PU1's channel */
    uint8_t clock_out;   /* LSDJ_OUT_* */
    uint8_t pc_cue;      /* 1 on */
} lsdj_midi_t;

#define LSDJ_MIDI_MAGIC 0x4Du   /* top byte of the packed form ('M') */
static void lsdj_midi_defaults(lsdj_midi_t *m) {
    m->clock_src = LSDJ_CLK_ALL; m->transport = 1; m->delay = 0; m->note = 0; m->cc = 0; m->clock_out = 0; m->pc_cue = 0;
}
static uint32_t lsdj_midi_pack(const lsdj_midi_t *m) {
    return (uint32_t)LSDJ_MIDI_MAGIC << 24 | (uint32_t)(m->clock_src & 7u) | (uint32_t)(m->transport & 1u) << 3 |
           (uint32_t)(m->delay & 3u) << 4 | (uint32_t)(m->note & 31u) << 6 | (uint32_t)(m->cc & 15u) << 11 |
           (uint32_t)(m->clock_out & 3u) << 15 | (uint32_t)(m->pc_cue & 1u) << 17;
}
/* 0 if v is not a packed MIDI word (the defaults are set then). */
static int lsdj_midi_unpack(uint32_t v, lsdj_midi_t *m) {
    lsdj_midi_defaults(m);
    if (v >> 24 != LSDJ_MIDI_MAGIC || (v & 0x00FC0000u)) return 0;
    lsdj_midi_t t = {(uint8_t)(v & 7u), (uint8_t)((v >> 3) & 1u), (uint8_t)((v >> 4) & 3u), (uint8_t)((v >> 6) & 31u),
                     (uint8_t)((v >> 11) & 15u), (uint8_t)((v >> 15) & 3u), (uint8_t)((v >> 17) & 1u)};
    if (t.clock_src >= LSDJ_CLK_COUNT || t.delay >= LSDJ_MIDI_DELAYS || t.note >= LSDJ_MIDI_NOTE_CHOICES ||
        t.cc >= LSDJ_MIDI_CC_CHOICES) return 0;
    *m = t;
    return 1;
}

/* ---- in: what to do with one message (a USB-MIDI style packet: CIN low nibble, status, data 1, data 2) ---- */
enum { LSDJ_MIDI_IGNORE, LSDJ_MIDI_CLOCK, LSDJ_MIDI_TRANSPORT, LSDJ_MIDI_NOTE, LSDJ_MIDI_CC, LSDJ_MIDI_PC };
static int lsdj_midi_clock_from(const lsdj_midi_t *m, unsigned src) {
    switch (m->clock_src) {
    case LSDJ_CLK_ALL: return 1;
    case LSDJ_CLK_USB: return src == LSDJ_MIDI_SRC_USB;
    case LSDJ_CLK_TRS: return src == LSDJ_MIDI_SRC_TRS;
    case LSDJ_CLK_BT: return src == LSDJ_MIDI_SRC_BT;
    default: return 0;
    }
}
static int lsdj_midi_route(const lsdj_midi_t *m, uint32_t pkt, unsigned src) {
    uint8_t st = (uint8_t)(pkt >> 8), d2 = (uint8_t)(pkt >> 24), ch = st & 15u;
    if (st >= 0xF8u) {
        if (!lsdj_midi_clock_from(m, src)) return LSDJ_MIDI_IGNORE;
        if (st == 0xF8u) return LSDJ_MIDI_CLOCK;
        if (st == 0xFAu || st == 0xFBu || st == 0xFCu) return m->transport ? LSDJ_MIDI_TRANSPORT : LSDJ_MIDI_IGNORE;
        return LSDJ_MIDI_IGNORE;
    }
    switch (st & 0xF0u) {
    case 0x90: return d2 && m->note && (m->note == 1u || ch == m->note - 2u) ? LSDJ_MIDI_NOTE : LSDJ_MIDI_IGNORE;
    case 0xB0: return m->cc && ch >= m->cc - 1u && ch < m->cc + 3u ? LSDJ_MIDI_CC : LSDJ_MIDI_IGNORE;
    case 0xC0: return m->pc_cue ? LSDJ_MIDI_PC : LSDJ_MIDI_IGNORE;
    }
    return LSDJ_MIDI_IGNORE;
}
/* the sync delay in sound samples (44.1 kHz) */
static uint32_t lsdj_midi_delay_samples(const lsdj_midi_t *m) { return lsdj_midi_delay_ms[m->delay % LSDJ_MIDI_DELAYS] * 441u / 10u; }

/* ---- CC -> effects. Channel N + c is LSDj channel c (0 PU1 .. 3 NOI); the echo and compressor CCs work on any of them.
   Per channel: 70 FILTER (6 bands of ~21: OFF LP HP BP NOTCH LP24), 71 RESO, 74 CUTOFF, 75 DRIVE, 76 CRUSH, 77 DECIMATE,
                91 ECHO SEND, 80 CHANNEL FX (64+ on), 81 MUTE (64+ muted)
                alpha 66: 72 LFO RATE, 73 LFO DEPTH, 78 WARM, 79 DISTORT (4 bands of 32: SOFT HARD FOLD FUZZ),
                82 LFO SHAPE (5 bands: SINE TRI SQUARE SAW RND), 83 LFO TO (3 bands: CUTOFF VOLUME PAN)
   Echo:        12 TIME (synced: 12 note values), 13 FEEDBACK, 14 MIX, 15 TONE, 16 WOBBLE, 17 SYNC (64+),
                18 MODE (64+ ping-pong), 19 FREEZE (64+); 20 COMPRESSOR (alpha 66)
   Returns 1 if it was one of these. A cutoff, resonance, warmth or cutoff LFO on a channel with no filter turns
   low-pass on, as the knobs do. */
#define LSDJ_MIDI_CCS 24u
static const uint8_t lsdj_midi_cc_num[LSDJ_MIDI_CCS] = {70, 71, 74, 75, 76, 77, 91, 80, 81, 12, 13, 14, 15, 16, 17, 18, 19,
                                                        72, 73, 78, 79, 82, 83, 20};
static const char *const lsdj_midi_cc_name[LSDJ_MIDI_CCS] = {"FILTER", "RESO", "CUTOFF", "DRIVE", "CRUSH", "DECIMATE",
    "ECHO SEND", "CHANNEL FX", "MUTE", "ECHO TIME", "ECHO FDBK", "ECHO MIX", "ECHO TONE", "ECHO WOBBLE", "ECHO SYNC",
    "ECHO MODE", "FREEZE", "LFO RATE", "LFO DEPTH", "WARM", "DISTORT", "LFO SHAPE", "LFO TO", "COMPRESSOR"};
static int lsdj_midi_cc_apply(lsdj_fx_t *fx, unsigned c, unsigned cc, unsigned v) {
    if (c > 3u || v > 127u) return 0;
    uint8_t *p = fx->p.ch[c], *q = fx->p.q[c], on = v >= 64u;
    switch (cc) {
    case 70: p[FXP_TYPE] = (uint8_t)(v * LSDJ_FX_FILTERS / 128u); break;
    case 71: p[FXP_RESO] = (uint8_t)v; if (!p[FXP_TYPE]) p[FXP_TYPE] = 1; break;
    case 74: p[FXP_CUTOFF] = (uint8_t)v; if (!p[FXP_TYPE]) p[FXP_TYPE] = 1; break;
    case 75: p[FXP_DRIVE] = (uint8_t)v; break;
    case 76: p[FXP_CRUSH] = (uint8_t)v; break;
    case 77: p[FXP_DECIM] = (uint8_t)v; break;
    case 91: p[FXP_SEND] = (uint8_t)v; break;
    case 80: fx->p.fx_off = (uint8_t)(on ? fx->p.fx_off & ~(1u << c) : fx->p.fx_off | (1u << c)); break;
    case 81: fx->mute = (uint8_t)(on ? fx->mute | (1u << c) : fx->mute & ~(1u << c)); break;
    case 72: q[FXQ_LFO_RATE] = (uint8_t)v; break;
    case 73: q[FXQ_LFO_DEPTH] = (uint8_t)v; if (v && q[FXQ_LFO_DEST] == 0u && !p[FXP_TYPE]) p[FXP_TYPE] = 1; break;
    case 78: q[FXQ_WARM] = (uint8_t)v; if (v && !p[FXP_TYPE]) p[FXP_TYPE] = 1; break;
    case 79: q[FXQ_DIST] = (uint8_t)(v * LSDJ_FX_DISTS / 128u); break;
    case 82: q[FXQ_LFO_SHAPE] = (uint8_t)(v * LSDJ_FX_SHAPES / 128u); break;
    case 83: q[FXQ_LFO_DEST] = (uint8_t)(v * LSDJ_FX_DESTS / 128u); break;
    case 12: fx->p.m[FXM_TIME] = fx->p.m[FXM_SYNC] ? (uint8_t)lsdj_fx_time_of(v * LSDJ_FX_NOTES / 128u) : (uint8_t)v; break;
    case 13: fx->p.m[FXM_FEEDBACK] = (uint8_t)v; break;
    case 14: fx->p.m[FXM_MIX] = (uint8_t)v; break;
    case 15: fx->p.m[FXM_TONE] = (uint8_t)v; break;
    case 16: fx->p.m[FXM_WOBBLE] = (uint8_t)v; break;
    case 17: if (on && !fx->p.m[FXM_SYNC]) fx->p.m[FXM_TIME] = (uint8_t)lsdj_fx_time_of(5); fx->p.m[FXM_SYNC] = on; break;
    case 18: fx->p.m[FXM_MODE] = on; break;
    case 19: fx->freeze = on; break;
    case 20: fx->p.comp = (uint8_t)v; break;
    default: return 0;
    }
    lsdj_fx_update(fx);
    return 1;
}
/* Returns 1 + the LSDj channel (1..4) the CC was for, 0 if it was none of ours. */
static int lsdj_midi_cc(const lsdj_midi_t *m, lsdj_fx_t *fx, uint32_t pkt) {
    unsigned ch = (pkt >> 8) & 15u, cc = (pkt >> 16) & 127u, v = (pkt >> 24) & 127u;
    if (!m->cc || ch < m->cc - 1u) return 0;
    return lsdj_midi_cc_apply(fx, ch - (m->cc - 1u), cc, v) ? (int)(ch - (m->cc - 1u)) + 1 : 0;
}
/* The knob effect (FXK_*) of each CC above, in lsdj_midi_cc_num's order (MUTE has none). */
static const uint8_t lsdj_midi_cc_fxk[LSDJ_MIDI_CCS] = {FXK_TYPE, FXK_RESO, FXK_CUTOFF, FXK_DRIVE, FXK_CRUSH, FXK_DECIM,
    FXK_SEND, FXK_FXON, FXK_NONE, FXK_ECHO_TIME, FXK_ECHO_FB, FXK_ECHO_MIX, FXK_ECHO_TONE, FXK_ECHO_WOBBLE, FXK_SYNC,
    FXK_MODE, FXK_FREEZE, FXK_LFO_RATE, FXK_LFO_DEPTH, FXK_WARM, FXK_DIST, FXK_LFO_SHAPE, FXK_LFO_DEST, FXK_COMP};
/* CC number -> its knob effect, or FXK_NONE. */
static unsigned lsdj_midi_fxk_of_cc(unsigned cc) {
    for (unsigned i = 0; i < LSDJ_MIDI_CCS; i++) if (lsdj_midi_cc_num[i] == cc) return lsdj_midi_cc_fxk[i];
    return FXK_NONE;
}
/* ---- CC out (CC OUT): a knob turned on the FM-1 sends its CC, so a DAW can record it.
   Knob effect k -> its CC number (0: none, e.g. ALL FX) and the CC value that sets it
   back (choices: the middle of their band; a synced echo time: its note). ---- */
static unsigned lsdj_midi_cc_of_fxk(unsigned k) {
    for (unsigned i = 0; i < LSDJ_MIDI_CCS; i++) if (lsdj_midi_cc_fxk[i] == k && k != FXK_NONE) return lsdj_midi_cc_num[i];
    return 0;
}
static unsigned lsdj_midi_cc_value(const lsdj_fx_t *fx, unsigned k, unsigned raw) {
    unsigned n = lsdj_fxk_choices(k);
    if (k == FXK_ECHO_TIME && fx->p.m[FXM_SYNC]) { raw = lsdj_fx_note_of(raw); n = LSDJ_FX_NOTES; }
    return n ? (2u * raw + 1u) * 64u / n : raw;
}

/* ---- clock out: LSDj leading sends one link byte per MIDI tick (24 a beat), as an Arduinoboy reads it in its
   LSDj master mode. The first tick after a pause also sends Start; no tick for LSDJ_MCO_STOP_MS sends Stop. ---- */
#define LSDJ_MCO_STOP_MS 300u    /* longer than a tick at 20 BPM (125 ms) and LSDj's own gaps */
typedef struct { uint8_t running; uint32_t last_ms, ticks; } lsdj_mco_t;
/* a tick heard now: the bytes to send (Start first after a pause), their count */
static unsigned lsdj_mco_tick(lsdj_mco_t *o, uint32_t now_ms, uint8_t out[2]) {
    unsigned n = 0;
    if (!o->running) { out[n++] = 0xFAu; o->running = 1; }
    out[n++] = 0xF8u; o->last_ms = now_ms; o->ticks++;
    return n;
}
/* every main-loop pass: 1 (send Stop) when the ticks stopped */
static int lsdj_mco_poll(lsdj_mco_t *o, uint32_t now_ms) {
    if (o->running && now_ms - o->last_ms > LSDJ_MCO_STOP_MS) { o->running = 0; return 1; }
    return 0;
}

/* ---- the settings list's MIDI lines ---- */
enum { LSDJ_MIDI_ITEM_CLOCK, LSDJ_MIDI_ITEM_TRANSPORT, LSDJ_MIDI_ITEM_DELAY, LSDJ_MIDI_ITEM_NOTE, LSDJ_MIDI_ITEM_CC,
       LSDJ_MIDI_ITEM_OUT, LSDJ_MIDI_ITEM_PC, LSDJ_MIDI_ITEMS };
static unsigned lsdj_midi_choices(unsigned item) {
    static const uint8_t n[LSDJ_MIDI_ITEMS] = {LSDJ_CLK_COUNT, 2, LSDJ_MIDI_DELAYS, LSDJ_MIDI_NOTE_CHOICES,
                                               LSDJ_MIDI_CC_CHOICES, LSDJ_OUT_COUNT, 2};
    return item < LSDJ_MIDI_ITEMS ? n[item] : 0u;
}
static uint8_t *lsdj_midi_field(lsdj_midi_t *m, unsigned item) {
    switch (item) {
    case LSDJ_MIDI_ITEM_CLOCK: return &m->clock_src;
    case LSDJ_MIDI_ITEM_TRANSPORT: return &m->transport;
    case LSDJ_MIDI_ITEM_DELAY: return &m->delay;
    case LSDJ_MIDI_ITEM_NOTE: return &m->note;
    case LSDJ_MIDI_ITEM_CC: return &m->cc;
    case LSDJ_MIDI_ITEM_OUT: return &m->clock_out;
    default: return &m->pc_cue;
    }
}
/* one choice per click, stopping at the ends; 1 if it changed. TRANSPORT and PC CUE: clockwise = on. */
static int lsdj_midi_turn(lsdj_midi_t *m, unsigned item, int steps) {
    if (item >= LSDJ_MIDI_ITEMS || !steps) return 0;
    uint8_t *f = lsdj_midi_field(m, item);
    int v = (int)*f + steps, n = (int)lsdj_midi_choices(item);
    uint8_t to = (uint8_t)(v < 0 ? 0 : v >= n ? n - 1 : v);
    if (to == *f) return 0;
    *f = to;
    return 1;
}
static uint32_t lsdj_midi_put(char *d, uint32_t n, const char *t) { while (*t && n < 19u) d[n++] = *t++; d[n] = 0; return n; }
static uint32_t lsdj_midi_num(char *d, uint32_t n, unsigned v) {
    if (v >= 10u && n < 19u) d[n++] = (char)('0' + v / 10u);
    if (n < 19u) d[n++] = (char)('0' + v % 10u);
    d[n] = 0; return n;
}
static void lsdj_midi_line(const lsdj_midi_t *m, unsigned item, char line[20]) {
    static const char *const clk[LSDJ_CLK_COUNT] = {"OFF", "ALL", "USB", "TRS", "BT"};
    static const char *const out[LSDJ_OUT_COUNT] = {"OFF", "USB", "BT", "USB+BT"};
    uint32_t n = 0;
    switch (item) {
    case LSDJ_MIDI_ITEM_CLOCK: n = lsdj_midi_put(line, 0, "CLOCK IN: "); lsdj_midi_put(line, n, clk[m->clock_src % LSDJ_CLK_COUNT]); break;
    case LSDJ_MIDI_ITEM_TRANSPORT: n = lsdj_midi_put(line, 0, "TRANSPORT: "); lsdj_midi_put(line, n, m->transport ? "ON" : "OFF"); break;
    case LSDJ_MIDI_ITEM_DELAY: { static const char *const d[LSDJ_MIDI_DELAYS] = {"100 MS", "75 MS", "50 MS"};
        n = lsdj_midi_put(line, 0, "SYNC DELAY: "); lsdj_midi_put(line, n, d[m->delay % LSDJ_MIDI_DELAYS]); break; }
    case LSDJ_MIDI_ITEM_NOTE: n = lsdj_midi_put(line, 0, "NOTE IN: ");
        if (m->note < 2u) lsdj_midi_put(line, n, m->note ? "ALL" : "OFF"); else { n = lsdj_midi_put(line, n, "CH "); lsdj_midi_num(line, n, m->note - 1u); } break;
    case LSDJ_MIDI_ITEM_CC: n = lsdj_midi_put(line, 0, "CC FX: ");
        if (!m->cc) lsdj_midi_put(line, n, "OFF"); else { n = lsdj_midi_put(line, n, "CH "); n = lsdj_midi_num(line, n, m->cc); n = lsdj_midi_put(line, n, "-"); lsdj_midi_num(line, n, m->cc + 3u); } break;
    case LSDJ_MIDI_ITEM_OUT: n = lsdj_midi_put(line, 0, "CLOCK OUT: "); lsdj_midi_put(line, n, out[m->clock_out % LSDJ_OUT_COUNT]); break;
    default: n = lsdj_midi_put(line, 0, "PC CUE: "); lsdj_midi_put(line, n, m->pc_cue ? "ON" : "OFF"); break;
    }
}
