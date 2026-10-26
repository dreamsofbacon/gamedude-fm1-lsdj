/* SPDX-License-Identifier: GPL-3.0-only */
/* Per-channel effects and a master echo for LSDj on the FM-1. Runs on CPU0 in
   the audio interrupt (gp_audio_read -> lsdj_fx_mix), between the emulator's
   four channels (split, see minigb_apu_split) and the stereo output.

   Per channel (pulse 1, pulse 2, wave, noise), all values 0..127:
     filter type (0 off, 1 low-pass, 2 high-pass, 3 band-pass), cutoff
     (40 Hz .. 8 kHz), resonance, drive (soft saturation), crush (fewer
     bits), decimate (lower sample rate), echo send.
   Per channel also: FX off (plays dry, settings kept; saved) and mute (silent,
   its echo keeps ringing; not saved).
   Master echo, at 22.05 kHz in 8-bit mu-law (LSDJ_FX_ECHO_LEN bytes, 1.49 s):
     time (free: 3 ms .. 1.49 s, ping-pong half that; or synced: a note length
     at LSDj's tempo), feedback, mix, sync, mode (mono / ping-pong), tone
     (64 flat, below darker repeats, above thinner), wobble (tape pitch wobble),
     and freeze (loops what is in the buffer; not saved).
   Alpha 66 (no extra memory, a few bytes of state per channel):
     filter types 4 notch and 5 24 dB low-pass (two stages); warm (the
     filter's resonance saturates: rounder, less shrill); distortion type for
     drive (soft, hard clip, wavefolder, fuzz); an LFO per channel (rate free
     0.05..12.8 Hz or tempo-synced 1/32..4 bars, depth, shape sine / triangle
     / square / saw / random, to the cutoff, the volume or the pan); and a
     master compressor (one amount, make-up gain automatic).
   With every value at 0 the output is the plain MiniGB mix, bit for bit.
   Pure logic, fixed point: tests/lsdj_fx.c. */
#pragma once
#include <stdint.h>

#ifndef LSDJ_FX_ECHO_LEN
#define LSDJ_FX_ECHO_LEN 32768u              /* bytes = samples, a power of 2: 1.49 s at 22.05 kHz */
#endif
enum { FXP_TYPE, FXP_CUTOFF, FXP_RESO, FXP_DRIVE, FXP_CRUSH, FXP_DECIM, FXP_SEND, FXP_CH_COUNT };
enum { FXM_TIME, FXM_FEEDBACK, FXM_MIX, FXM_SYNC, FXM_MODE, FXM_TONE, FXM_WOBBLE, FXM_COUNT };
/* alpha 66, per channel: distortion type, warm, LFO rate / depth / shape / destination */
enum { FXQ_DIST, FXQ_WARM, FXQ_LFO_RATE, FXQ_LFO_DEPTH, FXQ_LFO_SHAPE, FXQ_LFO_DEST, FXQ_COUNT };
#define LSDJ_FX_FILTERS 6u                   /* off, low-pass, high-pass, band-pass, notch, low-pass 24 dB */
#define LSDJ_FX_DISTS 4u                     /* soft, hard, fold, fuzz */
#define LSDJ_FX_SHAPES 5u                    /* sine, triangle, square, saw, random */
#define LSDJ_FX_DESTS 3u                     /* cutoff, volume, pan */
#define LSDJ_FX_LFO_FREE 64u                 /* rates 0..63 free (Hz), 64..127 tempo-synced notes */
#define LSDJ_FX_LFO_NOTES 15u
/* synced LFO cycles in 1/6 sixteenth notes: the echo's twelve, then 1, 2 and 4 bars */
static const uint16_t lsdj_fx_lfo_note_u[LSDJ_FX_LFO_NOTES] = {3, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 96, 192, 384};
static const char *const lsdj_fx_lfo_note_name[LSDJ_FX_LFO_NOTES] = {
    "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1BAR", "2BAR", "4BAR" };
#define LSDJ_FX_TONE_FLAT 64u
#define LSDJ_FX_WOBBLE_MAX 96                /* +-samples of read-position swing */
#define LSDJ_FX_ECHO_MARGIN (4 * LSDJ_FX_WOBBLE_MAX + 4)   /* ping-pong reads 2 x (time + 2 x wobble) back */
#define LSDJ_FX_NOTES 12u
/* Synced echo times in 1/6 sixteenth notes: 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/2T 1/4. 1/2 */
static const uint8_t lsdj_fx_note_u[LSDJ_FX_NOTES] = {3, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48};
static const char *const lsdj_fx_note_name[LSDJ_FX_NOTES] = {
    "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2" };

typedef struct {
    uint8_t ch[4][FXP_CH_COUNT];             /* per channel parameters */
    uint8_t m[FXM_COUNT];                    /* master echo */
    uint8_t fx_off;                          /* bit c: channel c plays dry */
    uint8_t q[4][FXQ_COUNT];                 /* per channel, alpha 66 */
    uint8_t comp;                            /* master compressor amount */
    uint8_t bypass;                          /* all effects dry; settings retained */
} lsdj_fx_params_t;

typedef struct {
    int32_t low, band;                       /* filter state */
    int32_t low2, band2;                     /* the 24 dB low-pass's second stage */
    int32_t hold; uint32_t hold_n;           /* decimator */
    int32_t dc_x, dc_y;                      /* fuzz: DC blocker */
    uint32_t lfo_ph, lfo_inc, rnd;           /* LFO phase, step per sample, random state */
    int32_t lfo_sh;                          /* random shape: the held value */
    int32_t pan_l, pan_r;                    /* LFO to pan: gains, Q15 */
} lsdj_fx_chan_t;

typedef struct {
    lsdj_fx_params_t p;
    uint8_t mute;                            /* bit c: channel c silent */
    uint8_t freeze;                          /* echo loops its buffer */
    uint16_t bpm;                            /* LSDj's tempo, for synced times */
    lsdj_fx_chan_t c[4];
    uint8_t *echo; uint32_t echo_pos, echo_half, echo_len, lfo;
    int32_t echo_in, echo_l, echo_r, tone_st, tone_k;
    int32_t comp_env;                        /* compressor: the peak envelope */
    uint8_t active;                          /* anything to do (lsdj_fx_update) */
} lsdj_fx_t;

/* 2 sin(pi f / 44100) in Q14, f = 40 Hz * 200^(i / 127). */
static const uint16_t lsdj_fx_cutoff[128] = {
93, 97, 101, 106, 110, 115, 120, 125, 130, 136, 142, 148, 154, 161, 167, 175,
182, 190, 198, 206, 215, 224, 234, 244, 254, 265, 276, 288, 300, 313, 326, 340,
355, 370, 386, 402, 419, 437, 456, 475, 495, 516, 538, 561, 585, 610, 636, 663,
692, 721, 752, 784, 817, 852, 888, 926, 966, 1007, 1050, 1094, 1141, 1189, 1240, 1293,
1348, 1405, 1465, 1528, 1593, 1660, 1731, 1805, 1881, 1962, 2045, 2132, 2223, 2317, 2416, 2519,
2626, 2737, 2853, 2975, 3101, 3233, 3370, 3513, 3662, 3817, 3979, 4148, 4324, 4507, 4697, 4896,
5103, 5318, 5543, 5776, 6020, 6273, 6537, 6811, 7097, 7394, 7703, 8025, 8359, 8707, 9068, 9444,
9834, 10239, 10660, 11096, 11548, 12017, 12504, 13007, 13528, 14067, 14624, 15199, 15792, 16403, 17033, 17680
};

/* Stability limit of the filter coefficient per resonance, Q14: a Chamberlin
   filter with damping q is stable only for f < sqrt(q*q + 4) - q (here x 0.97).
   Without it, low resonance with a cutoff above ~121 blew up into distortion. */
static const uint16_t lsdj_fx_fmax[128] = {
13165, 13235, 13306, 13377, 13449, 13521, 13594, 13668, 13743, 13818, 13894, 13971, 14048, 14126, 14205, 14284,
14365, 14446, 14527, 14610, 14694, 14778, 14863, 14948, 15035, 15122, 15210, 15299, 15389, 15480, 15572, 15664,
15757, 15852, 15947, 16043, 16140, 16238, 16337, 16437, 16538, 16639, 16742, 16846, 16951, 17056, 17164, 17271,
17380, 17490, 17601, 17713, 17826, 17941, 18056, 18173, 18291, 18410, 18530, 18651, 18773, 18897, 19021, 19147,
19275, 19403, 19533, 19664, 19796, 19930, 20065, 20201, 20338, 20477, 20617, 20759, 20902, 21046, 21191, 21338,
21487, 21637, 21788, 21941, 22095, 22251, 22408, 22566, 22726, 22888, 23051, 23216, 23382, 23550, 23719, 23890,
24063, 24237, 24412, 24589, 24769, 24949, 25131, 25315, 25500, 25687, 25876, 26066, 26258, 26452, 26648, 26845,
27043, 27244, 27446, 27650, 27856, 28063, 28273, 28484, 28696, 28911, 29127, 29344, 29564, 29785, 30009, 30234
};

/* 8-bit mu-law (G.711) for the echo buffer: about 13 bits of range in a byte. */
static uint8_t lsdj_fx_ulaw(int32_t x) {
    uint8_t mask = 0xFF;
    x >>= 2;
    if (x < 0) { x = -x; mask = 0x7F; }
    if (x > 8158) x = 8158;
    x += 33;
    uint32_t seg = 0; for (int32_t v = x >> 6; v; v >>= 1) seg++;
    return (uint8_t)(((seg << 4) | ((uint32_t)(x >> (seg + 1)) & 15u)) ^ mask);
}
static int32_t lsdj_fx_unulaw(uint8_t u) {
    u = (uint8_t)~u;
    int32_t t = (((int32_t)u & 15) << 3) + 0x84;
    t <<= (u >> 4) & 7u;
    return (u & 0x80) ? 0x84 - t : t - 0x84;
}
#define LSDJ_FX_SILENT_BYTE 0xFFu            /* lsdj_fx_ulaw(0) */

static void lsdj_fx_init(lsdj_fx_t *f, uint8_t *echo_buf) {
    uint8_t *b = (uint8_t *)f; for (uint32_t i = 0; i < sizeof *f; i++) b[i] = 0;
    f->echo = echo_buf; f->bpm = 120; f->p.m[FXM_TONE] = LSDJ_FX_TONE_FLAT;
    for (uint32_t c = 0; c < 4; c++) { f->c[c].pan_l = f->c[c].pan_r = 32768; f->c[c].rnd = 0x9E3779B9u * (c + 1u); }
    for (uint32_t i = 0; i < LSDJ_FX_ECHO_LEN; i++) echo_buf[i] = LSDJ_FX_SILENT_BYTE;
}
/* LFO step per 44.1 kHz sample (2^32 a cycle) for a RATE value: 0..63 free, 0.05 Hz doubling every 8 to
   about 12 Hz; 64..127 one of the synced cycles at LSDj's tempo (2^32 / (44100 * 15 / bpm * u / 6)). */
static uint32_t lsdj_fx_lfo_step(unsigned rate, unsigned bpm) {
    if (rate < LSDJ_FX_LFO_FREE) return (4870u << (rate >> 3)) * (8u + (rate & 7u)) / 8u;
    unsigned n = (rate - LSDJ_FX_LFO_FREE) * LSDJ_FX_LFO_NOTES / (128u - LSDJ_FX_LFO_FREE);
    if (bpm < 40u) bpm = 40u;
    return 38957u * bpm / lsdj_fx_lfo_note_u[n];
}
static unsigned lsdj_fx_lfo_note(unsigned rate) { return (rate - LSDJ_FX_LFO_FREE) * LSDJ_FX_LFO_NOTES / (128u - LSDJ_FX_LFO_FREE); }
/* Longest echo time, in 22.05 kHz samples (ping-pong reads twice as far back). */
static uint32_t lsdj_fx_echo_max(const lsdj_fx_t *f) {
    uint32_t m = LSDJ_FX_ECHO_LEN - LSDJ_FX_ECHO_MARGIN;
    return f->p.m[FXM_MODE] ? m / 2u : m;
}
/* The note a synced TIME value picks, and a TIME value for a note. */
static unsigned lsdj_fx_note_of(unsigned time) { return time * LSDJ_FX_NOTES / 128u; }
static unsigned lsdj_fx_time_of(unsigned note) { return (note * 128u + 64u) / LSDJ_FX_NOTES; }
/* Echo time in 22.05 kHz samples from TIME, SYNC, MODE and the tempo. A synced
   time too long for the buffer halves until it fits. */
static uint32_t lsdj_fx_echo_samples(const lsdj_fx_t *f) {
    uint32_t max = lsdj_fx_echo_max(f), t = f->p.m[FXM_TIME];
    if (!f->p.m[FXM_SYNC]) return 64u + t * (max - 64u) / 127u;
    uint32_t bpm = f->bpm < 40u ? 40u : f->bpm;
    uint32_t n = 55125u * lsdj_fx_note_u[lsdj_fx_note_of(t)] / bpm;   /* 22050 * 15 / bpm / 6 per unit */
    while (n > max) n /= 2u;
    return n < 64u ? 64u : n;
}
/* Call after changing f->p, f->mute or f->bpm. */
static void lsdj_fx_update(lsdj_fx_t *f) {
    uint8_t any = (uint8_t)(f->p.m[FXM_MIX] != 0) | f->mute | f->p.comp;
    for (uint32_t c = 0; c < 4; c++) {
        if (!((f->p.fx_off >> c) & 1u))
            any |= f->p.ch[c][FXP_TYPE] | f->p.ch[c][FXP_DRIVE] | f->p.ch[c][FXP_CRUSH] |
                   f->p.ch[c][FXP_DECIM] | f->p.ch[c][FXP_SEND] | f->p.q[c][FXQ_LFO_DEPTH];
        f->c[c].lfo_inc = lsdj_fx_lfo_step(f->p.q[c][FXQ_LFO_RATE], f->bpm);
        if (!f->p.q[c][FXQ_LFO_DEPTH] || f->p.q[c][FXQ_LFO_DEST] != 2u) f->c[c].pan_l = f->c[c].pan_r = 32768;
    }
    f->echo_len = lsdj_fx_echo_samples(f);
    /* Tone: a one-pole filter on what goes into the buffer, Q14. Below 64 a
       low-pass (repeats darker, down to ~300 Hz); above, a high-pass (thinner, up to ~1.5 kHz). */
    int32_t t = f->p.m[FXM_TONE];
    f->tone_k = t < 64 ? 1300 + (t * t) * (16384 - 1300) / (63 * 63)
              : t > 64 ? (t - 64) * (t - 64) * 7000 / (63 * 63) : 0;
    f->active = any != 0;
}
static void lsdj_fx_set_bpm(lsdj_fx_t *f, unsigned bpm) {
    if (bpm == f->bpm) return;
    f->bpm = (uint16_t)bpm;
    if (f->p.m[FXM_SYNC]) f->echo_len = lsdj_fx_echo_samples(f);
    for (uint32_t c = 0; c < 4; c++)
        if (f->p.q[c][FXQ_LFO_RATE] >= LSDJ_FX_LFO_FREE) f->c[c].lfo_inc = lsdj_fx_lfo_step(f->p.q[c][FXQ_LFO_RATE], bpm);
}

static int32_t lsdj_fx_clamp16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
/* Filter state limit (x 256 scale) and a Q14 multiply in 32 bits for |b| < 2^21:
   b = hi * 64 + lo (lo 0..63), so a * hi and a * lo both stay below 2^31. */
static int32_t lsdj_fx_clampst(int32_t v) { return v > 2000000 ? 2000000 : v < -2000000 ? -2000000 : v; }
static int32_t lsdj_fx_mq14(int32_t a, int32_t b) {
    int32_t hi = b >> 6, lo = b - (hi << 6);
    return ((a * hi) >> 8) + ((a * lo) >> 14);
}

/* The LFO's next value, -32768..32767 (one per sample, the channel's step). */
static int32_t lsdj_fx_lfo(lsdj_fx_chan_t *s, unsigned shape) {
    uint32_t ph = s->lfo_ph + s->lfo_inc;
    int wrapped = ph < s->lfo_ph;
    s->lfo_ph = ph;
    int32_t tri = (int32_t)(ph >> 16) - 32768;
    tri = (tri < 0 ? -tri : tri) * 2 - 32768;
    if (tri > 32767) tri = 32767;
    switch (shape) {
    case 0: return tri * (65536 - (tri < 0 ? -tri : tri)) >> 15;              /* sine (parabolic) */
    case 1: return tri;
    case 2: return ph < 0x80000000u ? 32767 : -32768;                          /* square */
    case 3: return 32767 - (int32_t)(ph >> 16);                                /* saw, falling */
    default:                                                                   /* random: a new value each cycle */
        if (wrapped) { s->rnd = s->rnd * 1664525u + 1013904223u; s->lfo_sh = (int32_t)(s->rnd >> 16) - 32768; }
        return s->lfo_sh;
    }
}

/* One channel sample (MiniGB scale, about +-1024) through its effects. */
static int32_t lsdj_fx_chan(lsdj_fx_chan_t *s, const uint8_t *p, const uint8_t *q, int32_t x) {
    int32_t depth = q[FXQ_LFO_DEPTH], lfo = depth ? lsdj_fx_lfo(s, q[FXQ_LFO_SHAPE]) : 0;
    unsigned dest = q[FXQ_LFO_DEST];
    if (p[FXP_DRIVE]) {                      /* gain 1..17, then the distortion type */
        int32_t g = x * (int32_t)(8u + p[FXP_DRIVE]) / 8;
        int32_t a = g < 0 ? -g : g;
        switch (q[FXQ_DIST]) {
        default: x = g * 1024 / (1024 + a); break;                               /* soft: x / (1 + |x|) */
        case 1: x = g > 1024 ? 1024 : g < -1024 ? -1024 : g; break;              /* hard clip */
        case 2: { int32_t u = (g + 1024) & 4095; x = (u < 2048 ? u : 4096 - u) - 1024; break; }   /* wavefolder */
        case 3: {                                                                 /* fuzz: soft up, hard down, DC removed */
            int32_t y = g > 0 ? g * 1024 / (1024 + g) : (g < -384 ? -384 : g);
            s->dc_y += ((y - s->dc_x) << 8) - (s->dc_y >> 8);   /* one-pole high-pass (~27 Hz), state x 256 */
            s->dc_x = y; x = (s->dc_y + 128) >> 8; break;
        }
        }
    }
    if (p[FXP_TYPE]) {                       /* Chamberlin state-variable filter */
        /* Coefficients in Q14, states with 8 fractional bits (x 256): at low
           cutoffs a coarser state gets stuck at a leftover value once the
           sound stops. */
        int32_t ci = p[FXP_CUTOFF] & 127u;
        if (depth && dest == 0u) {           /* LFO to the cutoff: +-63 steps at full depth */
            ci += lfo * depth / 65536;
            ci = ci < 0 ? 0 : ci > 127 ? 127 : ci;
        }
        int32_t fc = lsdj_fx_cutoff[ci];
        if (fc > lsdj_fx_fmax[p[FXP_RESO] & 127u]) fc = lsdj_fx_fmax[p[FXP_RESO] & 127u];   /* stay stable */
        int32_t qd = 16384 * 2 - (int32_t)p[FXP_RESO] * (16384 * 19 / 10) / 127;   /* damping 2 .. 0.1 */
        s->low = lsdj_fx_clampst(s->low + lsdj_fx_mq14(fc, s->band));
        int32_t high = lsdj_fx_clampst((x << 8) - s->low - lsdj_fx_mq14(qd, s->band));
        s->band = lsdj_fx_clampst(s->band + lsdj_fx_mq14(fc, high));
        if (q[FXQ_WARM]) {                   /* warm: the resonance saturates above a knee K (8192 .. 2100), towards 2K */
            int32_t bs = s->band >> 8, k = 8192 - (int32_t)q[FXQ_WARM] * 48, a = bs < 0 ? -bs : bs;
            if (a > k) {
                a = k + (a - k) * k / a;
                s->band = ((bs < 0 ? -a : a) << 8) | (s->band & 255);
            }
        }
        int32_t y;
        switch (p[FXP_TYPE]) {
        case 1: y = s->low; break;
        case 2: y = high; break;
        case 4: y = s->low + high; break;   /* notch */
        case 5: {                            /* 24 dB: a second, gentle stage (damping 1.4) on the low-pass */
            int32_t fc2 = fc > 16538 ? 16538 : fc;
            s->low2 = lsdj_fx_clampst(s->low2 + lsdj_fx_mq14(fc2, s->band2));
            int32_t h2 = lsdj_fx_clampst(s->low - s->low2 - lsdj_fx_mq14(22938, s->band2));
            s->band2 = lsdj_fx_clampst(s->band2 + lsdj_fx_mq14(fc2, h2));
            y = s->low2; break;
        }
        default: y = s->band; break;
        }
        x = (lsdj_fx_clampst(y) + 128) >> 8;
    }
    if (p[FXP_CRUSH]) {                      /* step 1 .. 512 on a +-1024 signal */
        int32_t step = 1 << ((uint32_t)p[FXP_CRUSH] * 9u / 127u);
        x = x >= 0 ? x / step * step : -((-x) / step * step);
    }
    if (p[FXP_DECIM]) {                      /* hold every sample for 1 .. 32 samples */
        if (s->hold_n == 0) { s->hold = x; s->hold_n = 1u + (uint32_t)p[FXP_DECIM] * 31u / 127u; }
        s->hold_n--; x = s->hold;
    }
    if (depth && dest == 1u)                 /* LFO to the volume: tremolo, down to silence at full depth */
        x = x * (32768 - ((lfo + 32768) >> 1) * depth / 127) >> 15;
    if (depth && dest == 2u) {               /* LFO to the pan: one side dips, then the other */
        s->pan_l = 32768 - (lfo > 0 ? lfo : 0) * depth / 127;
        s->pan_r = 32768 - (lfo < 0 ? -lfo : 0) * depth / 127;
    }
    return x;
}

/* Echo buffer read d (Q8 samples) back from the write position, interpolated. */
#define LSDJ_FX_EMASK (LSDJ_FX_ECHO_LEN - 1u)
static int32_t lsdj_fx_echo_read(const lsdj_fx_t *f, uint32_t d) {
    uint32_t i = d >> 8, fr = d & 255u;
    int32_t a = lsdj_fx_unulaw(f->echo[(f->echo_pos - i) & LSDJ_FX_EMASK]);
    if (!fr) return a;
    int32_t b = lsdj_fx_unulaw(f->echo[(f->echo_pos - i - 1u) & LSDJ_FX_EMASK]);
    return a + (b - a) * (int32_t)fr / 256;
}
/* One 22.05 kHz echo step: in = the sends. Mono: one tap at the echo time.
   Ping-pong: taps at 1x (left) and 2x (right), feedback from 2x, so repeats
   alternate left, right. Freeze copies the loop back in instead of new sound. */
static void lsdj_fx_echo_tick(lsdj_fx_t *f, int32_t in) {
    uint32_t len = f->echo_len, ping = f->p.m[FXM_MODE];
    uint32_t d = len << 8;
    if (f->p.m[FXM_WOBBLE] && !f->freeze) {  /* 0.6 Hz triangle on the read position */
        f->lfo += 116871u;                   /* 2^32 * 0.6 / 22050 */
        int32_t tri = (int32_t)(f->lfo >> 16) - 32768; tri = (tri < 0 ? -tri : tri) * 2 - 32768;   /* -32768..32766 */
        int32_t depth = (int32_t)f->p.m[FXM_WOBBLE] * (LSDJ_FX_WOBBLE_MAX * 256) / 127;
        d += (uint32_t)(depth + tri * (depth >> 4) / 2048);   /* len .. len + 2 depth */
    }
    int32_t t1 = lsdj_fx_echo_read(f, d), t2 = ping ? lsdj_fx_echo_read(f, d * 2u) : t1;
    f->echo_l = t1; f->echo_r = t2;
    if (f->freeze) {
        uint32_t loop = ping ? len * 2u : len;
        f->echo[f->echo_pos] = f->echo[(f->echo_pos - loop) & LSDJ_FX_EMASK];
    } else {
        int32_t w = in + t2 * (int32_t)f->p.m[FXM_FEEDBACK] * 115 / (127 * 128);   /* feedback <= 0.9 */
        w = lsdj_fx_clamp16(w);
        if (f->tone_k) {                     /* one-pole, state x 16 */
            int32_t dlt = (w << 4) - f->tone_st;
            f->tone_st += lsdj_fx_mq14(f->tone_k, dlt);
            int32_t lp = f->tone_st >> 4;
            w = f->p.m[FXM_TONE] < LSDJ_FX_TONE_FLAT ? lp : w - lp;
        }
        f->echo[f->echo_pos] = lsdj_fx_ulaw(w);
    }
    f->echo_pos = (f->echo_pos + 1u) & LSDJ_FX_EMASK;
}

/* The stereo mix of one split sample (4 channels + panning word, see
   minigb_apu_split) with the effects; before the volume. */
static void lsdj_fx_mix(lsdj_fx_t *f, const int16_t *smp, int32_t *lo, int32_t *ro) {
    uint32_t pan = (uint16_t)smp[4], vl = (pan >> 8) & 7u, vr = (pan >> 11) & 7u;
    int16_t dry_l = 0, dry_r = 0;
    if (!f->active || f->p.bypass) {          /* exactly MiniGB: int16 sums in channel order */
        int16_t L = 0, R = 0;
        for (uint32_t c = 0; c < 4; c++) {
            if ((f->mute >> c) & 1u) continue;
            L = (int16_t)(L + smp[c] * (int32_t)((pan >> (4 + c)) & 1u) * (int32_t)vl);
            R = (int16_t)(R + smp[c] * (int32_t)((pan >> c) & 1u) * (int32_t)vr);
        }
        if (!f->active) { *lo = L; *ro = R; return; }
        dry_l = L; dry_r = R;
    }
    int32_t L = 0, R = 0, send = 0;
    for (uint32_t c = 0; c < 4; c++) {
        if ((f->mute >> c) & 1u) continue;
        int dry = (f->p.fx_off >> c) & 1u;
        int32_t y = dry ? smp[c] : lsdj_fx_chan(&f->c[c], f->p.ch[c], f->p.q[c], smp[c]);
        int32_t l = y * (int32_t)((pan >> (4 + c)) & 1u) * (int32_t)vl;
        int32_t r = y * (int32_t)((pan >> c) & 1u) * (int32_t)vr;
        if (!dry && f->c[c].pan_l != 32768) l = l * f->c[c].pan_l >> 15;
        if (!dry && f->c[c].pan_r != 32768) r = r * f->c[c].pan_r >> 15;
        L += l; R += r;
        if (!dry && f->p.ch[c][FXP_SEND]) send += (l + r) / 2 * (int32_t)f->p.ch[c][FXP_SEND] / 127;
    }
    if (f->p.m[FXM_MIX] && f->echo) {        /* master echo at half rate (22.05 kHz) */
        f->echo_in += send;
        if (++f->echo_half == 2u) {
            f->echo_half = 0;
            lsdj_fx_echo_tick(f, f->echo_in / 2);
            f->echo_in = 0;
        }
        L += f->echo_l * (int32_t)f->p.m[FXM_MIX] / 127;
        R += f->echo_r * (int32_t)f->p.m[FXM_MIX] / 127;
    }
    if (f->p.comp) {                         /* master compressor: peak envelope (attack ~0.4 ms, release ~90 ms),
                                                4:1 above a threshold the amount lowers (0 .. -16 dB), make-up gain */
        L = lsdj_fx_clamp16(L); R = lsdj_fx_clamp16(R);
        int32_t a = L < 0 ? -L : L, b = R < 0 ? -R : R;
        if (b > a) a = b;
        f->comp_env += a > f->comp_env ? (a - f->comp_env) >> 4 : (a - f->comp_env) >> 12;
        int32_t th = 32767 - (int32_t)f->p.comp * 215, g = 32768;
        if (f->comp_env > th) g = (int32_t)(((uint32_t)(th + ((f->comp_env - th) >> 2)) << 15) / (uint32_t)f->comp_env);
        g = g * (32768 + (int32_t)f->p.comp * 96) >> 15;
        L = L * g >> 15; R = R * g >> 15;
    }
    /* Keep DSP state advancing while bypassed, so echo/LFO do not resume stale. */
    *lo = f->p.bypass ? dry_l : lsdj_fx_clamp16(L);
    *ro = f->p.bypass ? dry_r : lsdj_fx_clamp16(R);
}
