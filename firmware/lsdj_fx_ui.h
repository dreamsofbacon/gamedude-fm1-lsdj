/* SPDX-License-Identifier: GPL-3.0-only */
/* Effects controls. KNOB 1-4 always play the four effects of the current page;
   the SELECT knob turns the pages. Each change pops up four bars (one per knob,
   lined up under LSDj's four channel columns: firmware/lsdj_overlay.h).
   Two areas (FM-1_984). Turning a knob or SELECT pops up the channel pages FILTER, DRIVE,
   LFO, LO-FI (each channel sends to the shared echo from FILTER K2, next to its FX on/off)
   for a moment; the FX button opens the master pages MASTER (ALL FX, COMP, echo MIX and
   TIME), ECHO, ECHO MOD, with a channel strip: C4-F4 mute PU1 PU2 WAV NOI, until FX again.
   SELECT turns the pages of the area shown. (982: FX = channel pages, master, closed.)
   SELECT stops at the first and last page (PAGE WRAP on: it goes round).
   Per-channel effects act on the target: the channel under LSDj's cursor by
   default. Packs the setup into 17 32-bit words for the save store (11 before
   alpha 66: still read).
   Pure logic: tests/lsdj_fx_ui.c. */
#pragma once
#include <stdint.h>
#include "lsdj_fx.h"

/* Everything a knob can play. */
enum { FXK_CUTOFF, FXK_RESO, FXK_DRIVE, FXK_CRUSH, FXK_DECIM, FXK_SEND,              /* per channel, 0..127 */
       FXK_ECHO_TIME, FXK_ECHO_FB, FXK_ECHO_MIX, FXK_ECHO_TONE, FXK_ECHO_WOBBLE,     /* echo, 0..127 */
       FXK_TYPE, FXK_FXON, FXK_SYNC, FXK_MODE, FXK_FREEZE,                         /* choices */
       /* alpha 66: per channel (warm, LFO rate and depth 0..127; distortion, shape, destination choices),
          and the master compressor (0..127) */
       FXK_WARM, FXK_LFO_RATE, FXK_LFO_DEPTH, FXK_DIST, FXK_LFO_SHAPE, FXK_LFO_DEST, FXK_COMP, FXK_ALLON, FXK_COUNT };
#define FXK_NONE FXK_COUNT
static const char *const lsdj_fxk_name[FXK_COUNT + 1] = {
    "CUTOFF", "RESO", "DRIVE", "CRUSH", "DECIMATE", "ECHO SEND", "ECHO TIME", "ECHO FDBK", "ECHO MIX",
    "ECHO TONE", "ECHO WOBBLE", "FILTER", "CHANNEL FX", "ECHO SYNC", "ECHO MODE", "FREEZE",
    "WARM", "LFO RATE", "LFO DEPTH", "DISTORT", "SHAPE", "LFO TO", "COMPRESSOR", "ALL FX", "" };
static const char *const lsdj_fxk_short[FXK_COUNT + 1] = {   /* 2 characters, over the bars (LSDj's 2-wide columns) */
    "CT", "RS", "DR", "CR", "DC", "SN", "TM", "FB", "MX", "TN", "WB", "TY", "FX",
    "SY", "MD", "FZ", "WM", "RT", "DP", "DS", "SH", "TO", "CP", "FX", "" };

static const char *const lsdj_fxk_label[FXK_COUNT + 1] = {   /* up to 6 characters: the knob names on LSDj's bottom rows */
    "CUTOFF", "RESO", "DRIVE", "CRUSH", "DECIM", "SEND", "TIME", "FDBK", "MIX", "TONE", "WOBBLE", "FILTER", "FX ON",
    "SYNC", "MODE", "FREEZE", "WARM", "RATE", "DEPTH", "DIST", "SHAPE", "LFO TO", "COMP", "ALL FX", "" };

/* The pages: four effects each, KNOB 1..4 in order. */
#define LSDJ_FX_PAGES 7u
enum { FX_PAGE_MASTER, FX_PAGE_FILTER, FX_PAGE_DRIVE, FX_PAGE_LFO, FX_PAGE_LOFI, FX_PAGE_ECHO, FX_PAGE_ECHO_MOD };
static const uint8_t lsdj_fx_page_knob[LSDJ_FX_PAGES][4] = {
    {FXK_ALLON, FXK_COMP, FXK_ECHO_MIX, FXK_ECHO_TIME},   /* FM-1_980: the echo mix and time here too (hold FX) */
    {FXK_FXON, FXK_SEND, FXK_CUTOFF, FXK_RESO},   /* FM-1_979: the echo send next to FX on/off */
    {FXK_DRIVE, FXK_DIST, FXK_WARM, FXK_NONE},
    {FXK_LFO_RATE, FXK_LFO_DEPTH, FXK_LFO_SHAPE, FXK_LFO_DEST},
    {FXK_CRUSH, FXK_DECIM, FXK_TYPE, FXK_NONE},   /* ... and the filter type here (before 979: swapped) */
    {FXK_ECHO_TIME, FXK_ECHO_FB, FXK_ECHO_MIX, FXK_ECHO_TONE},
    {FXK_ECHO_WOBBLE, FXK_SYNC, FXK_MODE, FXK_FREEZE},
};
static const char *const lsdj_fx_page_name[LSDJ_FX_PAGES] = {"MASTER", "FILTER", "DRIVE", "LFO", "LO-FI", "ECHO", "ECHO MOD"};
static const char lsdj_fx_page_letters[LSDJ_FX_PAGES + 1] = "1234567";   /* matches the page counter */

#define LSDJ_FX_ALL 4u      /* target: every channel */
#define LSDJ_FX_CURSOR 5u   /* target: the channel under LSDj's cursor (default) */
static const char *const lsdj_fx_target_name[6] = {"PU1", "PU2", "WAV", "NOI", "ALL", "CUR"};
static const char *const lsdj_fx_filter_name[LSDJ_FX_FILTERS] = {"OFF", "LP", "HP", "BP", "NOTCH", "LP24"};
static const char *const lsdj_fx_dist_name[LSDJ_FX_DISTS] = {"SOFT", "HARD", "FOLD", "FUZZ"};
static const char *const lsdj_fx_shape_name[LSDJ_FX_SHAPES] = {"SINE", "TRI", "SQUAR", "SAW", "RND"};
static const char *const lsdj_fx_dest_name[LSDJ_FX_DESTS] = {"CUT", "VOL", "PAN"};

typedef struct {
    uint8_t target;          /* 0..3 one channel, LSDJ_FX_ALL, LSDJ_FX_CURSOR */
    uint8_t cursor_ch;       /* board: LSDj's cursor channel 0..3, else (unknown) 0xFF */
    uint8_t page;            /* the page shown (FX_PAGE_*) */
    uint8_t chpage;          /* the channel page last shown (back to it from the master pages) */
    uint8_t channels;        /* KNOBS = CHANNELS (lsdj_feat.h): K1-K4 = PU1 PU2 WAV NOI, see below */
    uint8_t param;           /* ... the effect on them (0..LSDJ_FX_CHPARAMS-1) */
} lsdj_fx_ui_t;

static void lsdj_fx_ui_init(lsdj_fx_ui_t *u) {
    u->target = LSDJ_FX_CURSOR; u->cursor_ch = 0xFF; u->page = u->chpage = FX_PAGE_FILTER; u->channels = 0; u->param = 0;
}
/* The pages in order: the channel pages (the first four), then the master pages (MASTER and
   the shared echo). FM-1_982: two lists, SELECT turns the pages of the one shown (981: one
   list of all seven; 977-980: hold FX for the master pages). */
#define LSDJ_FX_ORDER 7u
static const uint8_t lsdj_fx_order[LSDJ_FX_ORDER] = {FX_PAGE_FILTER, FX_PAGE_DRIVE, FX_PAGE_LFO, FX_PAGE_LOFI,
                                                    FX_PAGE_MASTER, FX_PAGE_ECHO, FX_PAGE_ECHO_MOD};
#define LSDJ_FX_CHANNEL_PAGES 4u   /* the first four of them */
static const char *const lsdj_fx_list_letters[2] = {"1234", "123"};   /* the screen map, per list */
static int lsdj_fx_is_master(unsigned p) { return p == FX_PAGE_MASTER || p == FX_PAGE_ECHO || p == FX_PAGE_ECHO_MOD; }
static unsigned lsdj_fx_index(unsigned p) {   /* its place in the list */
    for (unsigned i = 0; i < LSDJ_FX_ORDER; i++) if (lsdj_fx_order[i] == p) return i;
    return 0;
}
/* The page on the knobs. */
static unsigned lsdj_fx_cur(const lsdj_fx_ui_t *u) { return u->page % LSDJ_FX_PAGES; }
/* A master page (MASTER, ECHO, ECHO MOD): the channel strip, C4-F4 mute. */
static int lsdj_fx_on_master(const lsdj_fx_ui_t *u) { return lsdj_fx_is_master(lsdj_fx_cur(u)); }
/* ---------- KNOBS = CHANNELS ----------
   The channel pages become one effect at a time on all four channels: K1 PU1, K2 PU2,
   K3 WAV, K4 NOI. SELECT goes through the per-channel effects in page order (the master
   pages as usual). u->page follows: the page the effect is on (the screen map and the web
   page show it). */
#define LSDJ_FX_CHPARAMS 14u
static const uint8_t lsdj_fx_chparam[LSDJ_FX_CHPARAMS] = {FXK_FXON, FXK_SEND, FXK_CUTOFF, FXK_RESO, FXK_DRIVE, FXK_DIST, FXK_WARM,
    FXK_LFO_RATE, FXK_LFO_DEPTH, FXK_LFO_SHAPE, FXK_LFO_DEST, FXK_CRUSH, FXK_DECIM, FXK_TYPE};
static int lsdj_fx_chmode(const lsdj_fx_ui_t *u) { return u->channels && !lsdj_fx_on_master(u); }
/* Its place in its list (0..), and the list's length. */
static unsigned lsdj_fx_pos(const lsdj_fx_ui_t *u) {
    if (lsdj_fx_on_master(u)) return lsdj_fx_index(lsdj_fx_cur(u)) - LSDJ_FX_CHANNEL_PAGES;
    return u->channels ? u->param % LSDJ_FX_CHPARAMS : lsdj_fx_index(lsdj_fx_cur(u));
}
static unsigned lsdj_fx_count(const lsdj_fx_ui_t *u) {
    return lsdj_fx_on_master(u) ? LSDJ_FX_ORDER - LSDJ_FX_CHANNEL_PAGES : u->channels ? LSDJ_FX_CHPARAMS : LSDJ_FX_CHANNEL_PAGES;
}
/* FX: the master pages (from MASTER), or back to the channel page last shown. */
static void lsdj_fx_master(lsdj_fx_ui_t *u, int on) {
    if (on == lsdj_fx_on_master(u)) return;
    if (on) { u->chpage = u->page; u->page = FX_PAGE_MASTER; }
    else u->page = lsdj_fx_is_master(u->chpage) ? FX_PAGE_FILTER : u->chpage;
}
/* The effect on knob k, and the setup it plays on (KNOBS = CHANNELS: channel k). */
static unsigned lsdj_fx_knob(const lsdj_fx_ui_t *u, unsigned k) {
    return lsdj_fx_chmode(u) ? lsdj_fx_chparam[u->param] : lsdj_fx_page_knob[lsdj_fx_cur(u)][k & 3u];
}
static lsdj_fx_ui_t lsdj_fx_kui(const lsdj_fx_ui_t *u, unsigned k) {
    lsdj_fx_ui_t t = *u;
    if (lsdj_fx_chmode(u)) t.target = (uint8_t)(k & 3u);
    return t;
}

static int lsdj_fxk_is_echo(unsigned k) { return k >= FXK_ECHO_TIME && k <= FXK_ECHO_WOBBLE; }
static unsigned lsdj_fxk_m(unsigned k) {
    static const uint8_t m[] = {FXM_TIME, FXM_FEEDBACK, FXM_MIX, FXM_TONE, FXM_WOBBLE};
    return m[k - FXK_ECHO_TIME];
}
/* alpha 66's per-channel effects (fx->p.q) */
static int lsdj_fxk_is_q(unsigned k) { return k >= FXK_WARM && k <= FXK_LFO_DEST; }
static unsigned lsdj_fxk_q(unsigned k) {
    static const uint8_t m[] = {FXQ_WARM, FXQ_LFO_RATE, FXQ_LFO_DEPTH, FXQ_DIST, FXQ_LFO_SHAPE, FXQ_LFO_DEST};
    return m[k - FXK_WARM];
}
/* Effects that act on the target channel(s). */
static int lsdj_fxk_per_channel(unsigned k) { return k <= FXK_SEND || k == FXK_TYPE || lsdj_fxk_is_q(k); }
/* ... and the channel's FX on/off: the pop-up's title names the channel. */
static int lsdj_fxk_on_channel(unsigned k) { return lsdj_fxk_per_channel(k) || k == FXK_FXON; }
static unsigned lsdj_fxk_ch_param(unsigned k) {
    static const uint8_t m[] = {FXP_CUTOFF, FXP_RESO, FXP_DRIVE, FXP_CRUSH, FXP_DECIM, FXP_SEND};
    return k == FXK_TYPE ? FXP_TYPE : m[k];
}
/* The channel(s) edits go to: CURSOR follows LSDj's cursor (ALL if unknown). */
static unsigned lsdj_fx_eff(const lsdj_fx_ui_t *u) {
    if (u->target != LSDJ_FX_CURSOR) return u->target;
    return u->cursor_ch < 4u ? u->cursor_ch : LSDJ_FX_ALL;
}
static unsigned lsdj_fx_shown_ch(const lsdj_fx_ui_t *u) { unsigned t = lsdj_fx_eff(u); return t == LSDJ_FX_ALL ? 0u : t; }

/* Raw value of k (on the target; the first channel if ALL). */
static unsigned lsdj_fxk_get(const lsdj_fx_t *fx, const lsdj_fx_ui_t *u, unsigned k) {
    if (k == FXK_NONE) return 0;
    if (lsdj_fxk_is_echo(k)) return fx->p.m[lsdj_fxk_m(k)];
    if (lsdj_fxk_is_q(k)) return fx->p.q[lsdj_fx_shown_ch(u)][lsdj_fxk_q(k)];
    if (lsdj_fxk_per_channel(k)) return fx->p.ch[lsdj_fx_shown_ch(u)][lsdj_fxk_ch_param(k)];
    switch (k) {
    case FXK_ALLON: return !fx->p.bypass;
    case FXK_COMP: return fx->p.comp;
    case FXK_FXON: return !((fx->p.fx_off >> lsdj_fx_shown_ch(u)) & 1u);
    case FXK_SYNC: return fx->p.m[FXM_SYNC];
    case FXK_MODE: return fx->p.m[FXM_MODE];
    default: return fx->freeze;                 /* FXK_FREEZE */
    }
}
/* Number of choices of a choice effect (0 = a 0..127 value). */
static unsigned lsdj_fxk_choices(unsigned k) {
    switch (k) {
    case FXK_TYPE: return LSDJ_FX_FILTERS; case FXK_FXON: case FXK_ALLON: return 2; case FXK_SYNC: case FXK_MODE: case FXK_FREEZE: return 2;
    case FXK_DIST: return LSDJ_FX_DISTS; case FXK_LFO_SHAPE: return LSDJ_FX_SHAPES; case FXK_LFO_DEST: return LSDJ_FX_DESTS;
    }
    return 0;
}
/* Amount bars use the raw 00..7F setting, matching the value below the bar.
   Hz, milliseconds and synced note names remain in the active-knob readout. */
static unsigned lsdj_fxk_level(const lsdj_fx_t *fx, const lsdj_fx_ui_t *u, unsigned k) {
    unsigned v = lsdj_fxk_get(fx, u, k), n = lsdj_fxk_choices(k);
    return n ? v * 127u / (n - 1u) : v;
}

static unsigned lsdj_fx_clamp127(int v) { return v < 0 ? 0u : v > 127 ? 127u : (unsigned)v; }
static uint32_t lsdj_fx_put(char *d, uint32_t n, const char *s) { while (*s && n < 19u) d[n++] = *s++; d[n] = 0; return n; }
static uint32_t lsdj_fx_num(char *d, uint32_t n, unsigned v) {
    char b[5]; int k = 0; do { b[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 4);
    while (k && n < 19u) d[n++] = b[--k];
    d[n] = 0; return n;
}
/* Short value text (up to 5 characters): "64", "LP", "1/8.", "372", "D20", "PING". */
static void lsdj_fxk_value(const lsdj_fx_t *fx, const lsdj_fx_ui_t *u, unsigned k, char out[6]) {
    unsigned v = lsdj_fxk_get(fx, u, k);
    out[0] = 0;
    if (k == FXK_NONE) return;
    switch (k) {
    case FXK_TYPE: lsdj_fx_put(out, 0, lsdj_fx_filter_name[v % LSDJ_FX_FILTERS]); return;
    case FXK_DIST: lsdj_fx_put(out, 0, lsdj_fx_dist_name[v % LSDJ_FX_DISTS]); return;
    case FXK_LFO_SHAPE: lsdj_fx_put(out, 0, lsdj_fx_shape_name[v % LSDJ_FX_SHAPES]); return;
    case FXK_LFO_DEST: lsdj_fx_put(out, 0, lsdj_fx_dest_name[v % LSDJ_FX_DESTS]); return;
    case FXK_LFO_RATE:
        if (v >= LSDJ_FX_LFO_FREE) { lsdj_fx_put(out, 0, lsdj_fx_lfo_note_name[lsdj_fx_lfo_note(v)]); return; }
        {   /* Hz: 0.05 .. 12.0 (4 characters) */
            uint32_t h = (uint32_t)(((uint64_t)lsdj_fx_lfo_step(v, 120) * 4410000u) >> 32);   /* Hz x 100 */
            if (h < 1000u) { out[0] = (char)('0' + h / 100u); out[1] = '.'; out[2] = (char)('0' + h / 10u % 10u); out[3] = (char)('0' + h % 10u); out[4] = 0; }
            else { out[0] = (char)('0' + h / 1000u); out[1] = (char)('0' + h / 100u % 10u); out[2] = '.'; out[3] = (char)('0' + h / 10u % 10u); out[4] = 0; }
        }
        return;
    case FXK_FXON: case FXK_ALLON: lsdj_fx_put(out, 0, v ? "ON" : "OFF"); return;
    case FXK_SYNC: lsdj_fx_put(out, 0, v ? "TEMPO" : "FREE"); return;
    case FXK_MODE: lsdj_fx_put(out, 0, v ? "PING" : "MONO"); return;
    case FXK_FREEZE: lsdj_fx_put(out, 0, v ? "ON" : "OFF"); return;
    case FXK_ECHO_TIME:
        if (fx->p.m[FXM_SYNC]) lsdj_fx_put(out, 0, lsdj_fx_note_name[lsdj_fx_note_of(v)]);
        else lsdj_fx_num(out, 0, lsdj_fx_echo_samples(fx) * 10u / 221u);   /* ms */
        return;
    case FXK_ECHO_TONE:
        if (v == LSDJ_FX_TONE_FLAT) { lsdj_fx_put(out, 0, "FLAT"); return; }
        lsdj_fx_num(out, lsdj_fx_put(out, 0, v < LSDJ_FX_TONE_FLAT ? "D" : "T"), v < LSDJ_FX_TONE_FLAT ? LSDJ_FX_TONE_FLAT - v : v - LSDJ_FX_TONE_FLAT);
        return;
    }
    {   /* a 0..127 value: two hex digits, as LSDj shows its values */
        static const char hx[] = "0123456789ABCDEF";
        out[0] = hx[(v >> 4) & 15u]; out[1] = hx[v & 15u]; out[2] = 0;
    }
}

static const char *lsdj_fxk_choice_short(unsigned k, unsigned i);
/* The value in 2 characters, for its column under the bars (LSDj's channel data
   columns are 2 wide): 0..127 values, echo times and tones as their hex setting;
   choices use the same names as their selectors. The full value is in the
   active-knob readout (lsdj_fx_bars). */
static void lsdj_fxk_value_short(const lsdj_fx_t *fx, const lsdj_fx_ui_t *u, unsigned k, char out[6]) {
    static const char hx[] = "0123456789ABCDEF";
    unsigned v = lsdj_fxk_get(fx, u, k);
    out[2] = 0;
    if (lsdj_fxk_choices(k)) {
        const char *s = lsdj_fxk_choice_short(k, v);
        out[0] = s[0]; out[1] = s[1]; return;
    }
    out[0] = hx[(v >> 4) & 15u]; out[1] = hx[v & 15u];
}

/* Change k by `steps` knob clicks on the target. 0..127 values move 2 per click;
   choices one per click (a synced echo time: one note). Turning a cutoff or
   resonance with no filter switches on low-pass; an echo send with the echo
   silent sets a mix. */
#define LSDJ_FX_KNOB_STEP 2
static void lsdj_fxk_turn(lsdj_fx_t *fx, lsdj_fx_ui_t *u, unsigned k, int steps) {
    if (!steps || k == FXK_NONE) return;
    int dir = steps > 0 ? 1 : -1;
    if (k == FXK_ECHO_TIME && fx->p.m[FXM_SYNC]) {
        int note = (int)lsdj_fx_note_of(fx->p.m[FXM_TIME]) + steps;
        if (note < 0) note = 0;
        if (note >= (int)LSDJ_FX_NOTES) note = LSDJ_FX_NOTES - 1;
        fx->p.m[FXM_TIME] = (uint8_t)lsdj_fx_time_of((unsigned)note);
    } else if (lsdj_fxk_is_echo(k)) {
        uint8_t *v = &fx->p.m[lsdj_fxk_m(k)];
        *v = (uint8_t)lsdj_fx_clamp127(*v + steps * LSDJ_FX_KNOB_STEP);
    } else if (k == FXK_COMP) {
        fx->p.comp = (uint8_t)lsdj_fx_clamp127(fx->p.comp + steps * LSDJ_FX_KNOB_STEP);
    } else if (lsdj_fxk_is_q(k)) {          /* alpha 66 per channel: choices one per click, values 2 */
        unsigned qi = lsdj_fxk_q(k), t = lsdj_fx_eff(u), n = lsdj_fxk_choices(k);
        int to = (int)fx->p.q[lsdj_fx_shown_ch(u)][qi] + (n ? steps : steps * LSDJ_FX_KNOB_STEP);
        to = to < 0 ? 0 : n ? (to >= (int)n ? (int)n - 1 : to) : (to > 127 ? 127 : to);
        for (unsigned c = 0; c < 4; c++) {
            if (t != LSDJ_FX_ALL && t != c) continue;
            fx->p.q[c][qi] = (uint8_t)to;
            /* an LFO on the cutoff, or warmth, with no filter: low-pass on (as the cutoff knob does) */
            if (((k == FXK_LFO_DEPTH && fx->p.q[c][FXQ_LFO_DEST] == 0u) || k == FXK_WARM) && to && !fx->p.ch[c][FXP_TYPE]) fx->p.ch[c][FXP_TYPE] = 1;
        }
    } else if (lsdj_fxk_per_channel(k)) {
        unsigned p = lsdj_fxk_ch_param(k), t = lsdj_fx_eff(u);
        int type_to = (int)fx->p.ch[lsdj_fx_shown_ch(u)][FXP_TYPE] + steps;    /* one value for every target channel */
        if (type_to < 0) type_to = 0;
        if (type_to > (int)LSDJ_FX_FILTERS - 1) type_to = LSDJ_FX_FILTERS - 1;
        for (unsigned c = 0; c < 4; c++) {
            if (t != LSDJ_FX_ALL && t != c) continue;
            if (k == FXK_TYPE) { fx->p.ch[c][FXP_TYPE] = (uint8_t)type_to; continue; }
            fx->p.ch[c][p] = (uint8_t)lsdj_fx_clamp127(fx->p.ch[c][p] + steps * LSDJ_FX_KNOB_STEP);
            if ((k == FXK_CUTOFF || k == FXK_RESO) && !fx->p.ch[c][FXP_TYPE]) fx->p.ch[c][FXP_TYPE] = 1;
        }
        if (k == FXK_SEND && steps > 0 && !fx->p.m[FXM_MIX]) {
            fx->p.m[FXM_MIX] = 80;
            if (!fx->p.m[FXM_TIME]) fx->p.m[FXM_TIME] = 40;
            if (!fx->p.m[FXM_FEEDBACK]) fx->p.m[FXM_FEEDBACK] = 50;
        }
    } else if (k == FXK_ALLON) {
        fx->p.bypass = dir < 0;
    } else if (k == FXK_FXON) {               /* clockwise on, anticlockwise off (dry) */
        unsigned t = lsdj_fx_eff(u);
        for (unsigned c = 0; c < 4; c++)
            if (t == LSDJ_FX_ALL || t == c) fx->p.fx_off = (uint8_t)(dir > 0 ? fx->p.fx_off & ~(1u << c) : fx->p.fx_off | (1u << c));
    } else if (k == FXK_SYNC) {
        uint8_t on = dir > 0;
        if (on && !fx->p.m[FXM_SYNC]) fx->p.m[FXM_TIME] = (uint8_t)lsdj_fx_time_of(5);   /* start at 1/8 */
        fx->p.m[FXM_SYNC] = on;
    } else if (k == FXK_MODE) fx->p.m[FXM_MODE] = dir > 0;
    else if (k == FXK_FREEZE) fx->freeze = dir > 0;
    lsdj_fx_update(fx);
}

/* KNOB 1-4: steps[k] = clicks of knob k+1 this frame. Returns the knob (0..3)
   turned last, or -1. */
static int lsdj_fx_knobs(lsdj_fx_t *fx, lsdj_fx_ui_t *u, const int32_t steps[4]) {
    int last = -1;
    for (unsigned k = 0; k < 4; k++) {
        if (!steps[k] || lsdj_fx_knob(u, k) == FXK_NONE) continue;
        lsdj_fx_ui_t t = lsdj_fx_kui(u, k);
        lsdj_fxk_turn(fx, &t, lsdj_fx_knob(u, k), steps[k]);
        last = (int)k;
    }
    return last;
}
/* The page's four knob names (up to 6 characters), for LSDj's bottom rows. */
static void lsdj_fx_names(const lsdj_fx_ui_t *u, char names[4][7]) {
    for (unsigned k = 0; k < 4; k++) lsdj_fx_put(names[k], 0, lsdj_fx_chmode(u) ? lsdj_fx_target_name[k] : lsdj_fxk_label[lsdj_fx_knob(u, k)]);
}
/* SELECT knob: clicks turn the pages within the group shown, stopping at its
   first and last page; wrap: going round (PAGE WRAP). */
static void lsdj_fx_page_turn(lsdj_fx_ui_t *u, int steps, int wrap) {
    int n = (int)lsdj_fx_count(u), i = (int)lsdj_fx_pos(u) + steps;
    if (wrap) { i %= n; if (i < 0) i += n; }
    else i = i < 0 ? 0 : i >= n ? n - 1 : i;
    if (lsdj_fx_on_master(u)) { u->page = lsdj_fx_order[LSDJ_FX_CHANNEL_PAGES + (unsigned)i]; return; }
    if (u->channels) {   /* KNOBS = CHANNELS: the effect, and the page it is on */
        u->param = (uint8_t)i;
        for (unsigned p = FX_PAGE_FILTER; p <= FX_PAGE_LOFI; p++)
            for (unsigned k = 0; k < 4; k++) if (lsdj_fx_page_knob[p][k] == lsdj_fx_chparam[i]) u->page = (uint8_t)p;
    } else u->page = lsdj_fx_order[i];
    u->chpage = u->page;
}

/* Discrete modes are choices, never amounts. Keep the same short names in the
   selector and the value row. The active-knob readout gives the full name. */
static const char *lsdj_fxk_choice_short(unsigned k, unsigned i) {
    static const char *const filter[LSDJ_FX_FILTERS] = {"--", "LP", "HP", "BP", "NT", "L4"};
    static const char *const dist[LSDJ_FX_DISTS] = {"SO", "HA", "FO", "FZ"};
    static const char *const shape[LSDJ_FX_SHAPES] = {"SI", "TR", "SQ", "SW", "RN"};
    static const char *const dest[3] = {"CT", "VL", "PN"};
    switch (k) {
    case FXK_TYPE: return filter[i % LSDJ_FX_FILTERS];
    case FXK_DIST: return dist[i % LSDJ_FX_DISTS];
    case FXK_LFO_SHAPE: return shape[i % LSDJ_FX_SHAPES];
    case FXK_FXON: case FXK_ALLON: case FXK_FREEZE: return i ? "ON" : "OF";
    case FXK_SYNC: return i ? "TP" : "FR";
    case FXK_MODE: return i ? "PP" : "MO";
    case FXK_LFO_DEST: return dest[i % 3u];
    }
    return "";
}
static void lsdj_fx_bar_choices(const lsdj_fx_t *fx, const lsdj_fx_ui_t *u, uint8_t nchoice[4], uint8_t pick[4], char choice[4][6][3]) {
    for (unsigned k = 0; k < 4; k++) {
        unsigned e = lsdj_fx_knob(u, k), n = lsdj_fxk_choices(e);
        lsdj_fx_ui_t t = lsdj_fx_kui(u, k);
        nchoice[k] = 0; pick[k] = 0;
        if (n < 2u || n > 6u) continue;
        nchoice[k] = (uint8_t)n; pick[k] = (uint8_t)lsdj_fxk_get(fx, &t, e);
        for (unsigned i = 0; i < n; i++) { const char *s = lsdj_fxk_choice_short(e, i); choice[k][i][0] = s[0]; choice[k][i][1] = s[1]; choice[k][i][2] = 0; }
    }
}

/* What the bars show: title "FILTER PU1 1/4", per knob its level, value,
   name and "K1".."K4"; the bottom line: the effect of knob `hot` and its value
   ("CUTOFF 64"), or with hot < 0 how pages turn. Fields as lsdj_ov_bars_t. */
static void lsdj_fx_bars(const lsdj_fx_t *fx, const lsdj_fx_ui_t *u, int hot, char title[20], char big[20],
                         char label[4][7], char value[4][6], char caption[4][4], uint8_t level[4]) {
    unsigned pg = lsdj_fx_cur(u), cm = (unsigned)lsdj_fx_chmode(u);
    uint32_t n = lsdj_fx_put(title, 0, cm ? lsdj_fxk_name[lsdj_fx_knob(u, 0)] : lsdj_fx_page_name[pg]);   /* KNOBS = CHANNELS: the effect */
    int chan = 0;
    for (unsigned k = 0; k < 4; k++) chan |= lsdj_fxk_on_channel(lsdj_fx_knob(u, k));
    if (chan && !cm) { n = lsdj_fx_put(title, n, " "); n = lsdj_fx_put(title, n, lsdj_fx_target_name[lsdj_fx_eff(u)]); }
    n = lsdj_fx_put(title, n, " "); n = lsdj_fx_num(title, n, lsdj_fx_pos(u) + 1u); n = lsdj_fx_put(title, n, "/"); lsdj_fx_num(title, n, lsdj_fx_count(u));
    for (unsigned k = 0; k < 4; k++) {
        unsigned e = lsdj_fx_knob(u, k);
        lsdj_fx_ui_t t = lsdj_fx_kui(u, k);
        if (e == FXK_NONE) { label[k][0] = value[k][0] = caption[k][0] = 0; level[k] = 0; continue; }
        lsdj_fx_put(label[k], 0, cm ? lsdj_fx_target_name[k] : lsdj_fxk_short[e]);
        lsdj_fxk_value_short(fx, &t, e, value[k]);
        caption[k][0] = 'K'; caption[k][1] = (char)('1' + k); caption[k][2] = 0;
        level[k] = (uint8_t)lsdj_fxk_level(fx, &t, e);
    }
    if (hot >= 0 && hot < 4 && lsdj_fx_knob(u, (unsigned)hot) != FXK_NONE) {
        unsigned e = lsdj_fx_knob(u, (unsigned)hot);
        lsdj_fx_ui_t t = lsdj_fx_kui(u, (unsigned)hot);
        char full[6]; lsdj_fxk_value(fx, &t, e, full);
        const char *name = cm ? lsdj_fx_target_name[hot] : lsdj_fxk_name[e];   /* KNOBS = CHANNELS: "K2 PU2 5A" */
        if (name[0] == 'E' && name[1] == 'C' && name[2] == 'H' && name[3] == 'O' && name[4] == ' ') name += 5;
        big[0] = 'K'; big[1] = (char)('1' + hot); big[2] = ' '; big[3] = 0;
        uint32_t m = lsdj_fx_put(big, 3, name);
        m = lsdj_fx_put(big, m, " "); lsdj_fx_put(big, m, full);
    } else lsdj_fx_put(big, 0, lsdj_fx_on_master(u) ? "C4-F4: MUTE" : fx->p.bypass ? "ALL FX OFF" : "SELECT: PAGE");
}

/* ---------- hold FX: the channel strip ----------
   The master pages show PU1 PU2 WAV NOI under the bars (lit: playing), the same
   mute as the web page's channel buttons and MIDI CC 81. C4 D4 E4 F4 (key bits,
   F3 = bit 0) toggle them; their lights are lit while the channel plays. Channel FX
   on/off is FILTER K1, freeze is ECHO MOD K4. */
static const uint8_t lsdj_fx_mute_key[4] = {7, 9, 11, 12};
/* Keys pressed this frame on a master page; 1 if a mute changed. */
static int lsdj_fx_hold_keys(lsdj_fx_t *fx, uint32_t down) {
    int did = 0;
    for (unsigned c = 0; c < 4; c++)
        if (down & (1u << lsdj_fx_mute_key[c])) { fx->mute ^= (uint8_t)(1u << c); did = 1; }
    if (did) lsdj_fx_update(fx);
    return did;
}
static uint32_t lsdj_fx_hold_lights(const lsdj_fx_t *fx) {
    uint32_t l = 0;
    for (unsigned c = 0; c < 4; c++) if (!((fx->mute >> c) & 1u)) l |= 1u << lsdj_fx_mute_key[c];
    return l;
}

/* ---------- saving: 17 words (mutes and freeze are not kept) ----------
   Words 0..10 as before alpha 66 (byte 43: the compressor), words 11..16 the per-channel
   alpha 66 effects (4 x 6 bytes). Older firmware reads the first 11 and ignores the rest. */
#define LSDJ_FX_WORDS 17u
#define LSDJ_FX_WORDS_OLD 11u
#define LSDJ_FX_MARK 0xF2u
static void lsdj_fx_pack(const lsdj_fx_t *fx, const lsdj_fx_ui_t *u, uint32_t w[LSDJ_FX_WORDS]) {
    uint8_t b[68] = {0};
    for (unsigned c = 0; c < 4; c++) for (unsigned p = 0; p < FXP_CH_COUNT; p++) b[c * FXP_CH_COUNT + p] = fx->p.ch[c][p];
    for (unsigned m = 0; m < FXM_COUNT; m++) b[28 + m] = fx->p.m[m];
    b[35] = fx->p.fx_off; b[36] = u->target; b[37] = 2; b[38] = u->page;   /* layout 2: MASTER first */
    b[39] = fx->p.bypass;
    b[42] = LSDJ_FX_MARK; b[43] = fx->p.comp;
    for (unsigned c = 0; c < 4; c++) for (unsigned q = 0; q < FXQ_COUNT; q++) b[44 + c * FXQ_COUNT + q] = fx->p.q[c][q];
    for (unsigned i = 0; i < LSDJ_FX_WORDS; i++)
        w[i] = (uint32_t)b[i * 4] | (uint32_t)b[i * 4 + 1] << 8 | (uint32_t)b[i * 4 + 2] << 16 | (uint32_t)b[i * 4 + 3] << 24;
}
/* 0 if the words are not a saved effects setup (defaults kept). Also reads the
   10-word setup of alphas 27-29 (mark 0xF1 in byte 37; w[10] unused) and the
   knob assignments of alphas 30-33 (byte 38 was knob 1's effect: page 0). */
/* n: 17 words, or 11 (before alpha 66: the new effects stay off). */
static int lsdj_fx_unpack_n(lsdj_fx_t *fx, lsdj_fx_ui_t *u, const uint32_t *w, unsigned n) {
    uint8_t b[68] = {0};
    if (n != LSDJ_FX_WORDS && n != LSDJ_FX_WORDS_OLD) return 0;
    for (unsigned i = 0; i < n; i++) for (unsigned j = 0; j < 4; j++) b[i * 4 + j] = (uint8_t)(w[i] >> (8 * j));
    for (unsigned i = 0; i < 28; i++) if (b[i] > 127 || (i % FXP_CH_COUNT == FXP_TYPE && b[i] >= LSDJ_FX_FILTERS)) return 0;
    uint8_t comp = 0;
    if (b[42] == LSDJ_FX_MARK) comp = b[43];
    if (comp > 127) return 0;
    for (unsigned c = 0; c < 4; c++) {
        const uint8_t *q = b + 44 + c * FXQ_COUNT;
        if (q[FXQ_WARM] > 127 || q[FXQ_LFO_RATE] > 127 || q[FXQ_LFO_DEPTH] > 127 || q[FXQ_DIST] >= LSDJ_FX_DISTS ||
            q[FXQ_LFO_SHAPE] >= LSDJ_FX_SHAPES || q[FXQ_LFO_DEST] >= LSDJ_FX_DESTS) return 0;
    }
    uint8_t m[FXM_COUNT] = {0, 0, 0, 0, 0, LSDJ_FX_TONE_FLAT, 0}, off = 0, target, page = FX_PAGE_FILTER, bypass = 0;
    if (b[37] == 0xF1) {                     /* old: 3 echo values, target */
        for (unsigned i = 0; i < 3; i++) m[i] = b[28 + i];
        target = b[31];
    } else if (b[42] == LSDJ_FX_MARK) {
        for (unsigned i = 0; i < FXM_COUNT; i++) m[i] = b[28 + i];
        off = b[35]; target = b[36];
        if (b[37] == 2) {
            if (n != LSDJ_FX_WORDS || b[38] >= LSDJ_FX_PAGES || b[39] > 1 || b[40] || b[41]) return 0;
            page = b[38]; bypass = b[39];
        } else if (b[37] > 1) return 0;
        else if (b[39] == 0 && b[40] == 0 && b[41] == 0) {
            static const uint8_t old4[] = {FX_PAGE_FILTER, FX_PAGE_LOFI, FX_PAGE_ECHO, FX_PAGE_ECHO_MOD};
            page = n == LSDJ_FX_WORDS_OLD ? old4[b[38] % 4u] : (uint8_t)(1u + b[38] % 6u);
        }
    } else return 0;
    for (unsigned i = 0; i < FXM_COUNT; i++) if (m[i] > 127) return 0;
    if (m[FXM_SYNC] > 1 || m[FXM_MODE] > 1 || off > 15 || target > 5) return 0;
    for (unsigned c = 0; c < 4; c++) for (unsigned p = 0; p < FXP_CH_COUNT; p++) fx->p.ch[c][p] = b[c * FXP_CH_COUNT + p];
    for (unsigned i = 0; i < FXM_COUNT; i++) fx->p.m[i] = m[i];
    for (unsigned c = 0; c < 4; c++) for (unsigned q = 0; q < FXQ_COUNT; q++) fx->p.q[c][q] = b[44 + c * FXQ_COUNT + q];
    fx->p.comp = comp;
    fx->p.bypass = bypass;
    (void)target;   /* the effects always follow LSDj's cursor now (a saved lock was easy to set by accident) */
    if (lsdj_fx_is_master(page)) page = FX_PAGE_FILTER;   /* opens on FILTER */
    fx->p.fx_off = off; u->target = LSDJ_FX_CURSOR; u->page = u->chpage = (uint8_t)(page % LSDJ_FX_PAGES);
    lsdj_fx_update(fx);
    return 1;
}
static int lsdj_fx_unpack(lsdj_fx_t *fx, lsdj_fx_ui_t *u, const uint32_t w[LSDJ_FX_WORDS]) { return lsdj_fx_unpack_n(fx, u, w, LSDJ_FX_WORDS); }

/* Older live clients cannot change effects they do not expose. Saved setups
   use unpack_n directly, with the legacy defaults instead. */
static int lsdj_fx_set_words(lsdj_fx_t *fx, lsdj_fx_ui_t *u, const uint32_t *w, unsigned n) {
    lsdj_fx_params_t before = fx->p;
    if (!lsdj_fx_unpack_n(fx, u, w, n)) return 0;
    if (n == LSDJ_FX_WORDS_OLD) {
        memcpy(fx->p.q, before.q, sizeof before.q);
        fx->p.comp = before.comp;
    }
    if (((w[9] >> 8) & 255u) != 2) fx->p.bypass = before.bypass;
    lsdj_fx_update(fx);
    return 1;
}
