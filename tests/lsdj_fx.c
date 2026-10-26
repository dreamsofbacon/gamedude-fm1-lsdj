/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_fx.h. */
#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lsdj_fx.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint8_t echo[LSDJ_FX_ECHO_LEN];
static lsdj_fx_t F;
#define ALL_LR 0x3800u | 0x0700u | 0xFFu       /* both volumes 7, every channel left and right */

/* MiniGB's own mix order and int16 arithmetic (gb_port gp_mix, before volume). */
static void plain(const int16_t *s, int32_t *l, int32_t *r) {
    uint32_t pan = (uint16_t)s[4], vl = (pan >> 8) & 7u, vr = (pan >> 11) & 7u;
    int16_t L = 0, R = 0;
    for (uint32_t c = 0; c < 4; c++) {
        L = (int16_t)(L + s[c] * (int32_t)((pan >> (4 + c)) & 1u) * (int32_t)vl);
        R = (int16_t)(R + s[c] * (int32_t)((pan >> c) & 1u) * (int32_t)vr);
    }
    *l = L; *r = R;
}
/* Peak output over n samples of a sine on channel c (only c audible). */
static int32_t sine_peak(int c, double hz, int n) {
    int32_t peak = 0;
    for (int i = 0; i < n; i++) {
        int16_t s[5] = {0, 0, 0, 0, (int16_t)ALL_LR};
        s[c] = (int16_t)(900.0 * sin(2 * M_PI * hz * i / 44100.0));
        int32_t l, r; lsdj_fx_mix(&F, s, &l, &r);
        if (i > n / 2 && abs(l) > peak) peak = abs(l);
    }
    return peak;
}
static void reset(void) { lsdj_fx_init(&F, echo); }

int main(void) {
    srand(3);
    /* 1. Bypass: identical to the plain mix, wrap-around included. */
    reset(); lsdj_fx_update(&F); assert(!F.active);
    for (int i = 0; i < 200000; i++) {
        int16_t s[5];
        for (int c = 0; c < 4; c++) s[c] = (int16_t)(rand() % 8001 - 4000);
        s[4] = (int16_t)(rand() & 0x3FFF);
        int32_t a, b, x, y; lsdj_fx_mix(&F, s, &a, &b); plain(s, &x, &y);
        assert(a == x && b == y);
    }
    puts("bypass: 200000 random samples identical to the plain mix              ok");

    /* All FX bypass: bit-exact dry/mutes, unchanged settings and continuous DSP state. */
    {
        static uint8_t wet_echo[LSDJ_FX_ECHO_LEN];
        lsdj_fx_t wet;
        reset();
        F.p.comp = 90; F.p.fx_off = 2;
        F.p.m[FXM_TIME] = 1; F.p.m[FXM_MIX] = 100; F.p.m[FXM_FEEDBACK] = 80;
        for (unsigned c = 0; c < 4; c++) {
            F.p.ch[c][FXP_TYPE] = 1; F.p.ch[c][FXP_CUTOFF] = 80;
            F.p.ch[c][FXP_DRIVE] = 70; F.p.ch[c][FXP_SEND] = 90;
            F.p.ch[c][FXP_CRUSH] = 40; F.p.ch[c][FXP_DECIM] = 20;
            F.p.q[c][FXQ_LFO_RATE] = 60; F.p.q[c][FXQ_LFO_DEPTH] = 40;
        }
        lsdj_fx_update(&F);
        wet = F; wet.echo = wet_echo; memcpy(wet_echo, echo, sizeof echo);
        lsdj_fx_params_t saved = F.p;
        F.p.bypass = 1; lsdj_fx_update(&F);
        int differs = 0;
        for (unsigned i = 0; i < 100000; i++) {
            int16_t s[5], dry[5]; int32_t l, r, wl, wr, dl, dr;
            F.mute = wet.mute = (uint8_t)((i / 1000) & 15);
            for (unsigned c = 0; c < 4; c++) s[c] = (int16_t)(rand() % 8001 - 4000);
            s[4] = (int16_t)(rand() & 0x3FFF);
            memcpy(dry, s, sizeof dry);
            for (unsigned c = 0; c < 4; c++) if (F.mute & (1u << c)) dry[c] = 0;
            plain(dry, &dl, &dr);
            lsdj_fx_mix(&F, s, &l, &r); lsdj_fx_mix(&wet, s, &wl, &wr);
            assert(l == dl && r == dr);
            differs |= l != wl || r != wr;
        }
        assert(differs && !memcmp(F.c, wet.c, sizeof F.c) && !memcmp(echo, wet_echo, sizeof echo));
        assert(F.echo_pos == wet.echo_pos && F.comp_env == wet.comp_env);
        F.p.bypass = 0; lsdj_fx_update(&F);
        assert(!memcmp(&saved, &F.p, sizeof saved));
        for (unsigned i = 0; i < 1000; i++) {
            int16_t s[5] = {500, -300, 700, 0, (int16_t)ALL_LR};
            int32_t l, r, wl, wr;
            lsdj_fx_mix(&F, s, &l, &r); lsdj_fx_mix(&wet, s, &wl, &wr);
            assert(l == wl && r == wr);
        }
        puts("All FX: exact dry/mutes, retained settings, echo/LFO/compressor continuity ok");
    }

    /* 2. Filters. */
    reset(); F.p.ch[0][FXP_TYPE] = 1; F.p.ch[0][FXP_CUTOFF] = 40; lsdj_fx_update(&F);   /* low-pass ~200 Hz */
    int32_t lo = sine_peak(0, 80, 20000), hi = sine_peak(0, 4000, 20000);
    printf("low-pass ~%.0f Hz: 80 Hz peak %d, 4 kHz peak %d\n", 40.0 * pow(200.0, 40 / 127.0), lo, hi);
    assert(lo > 4 * hi);
    reset(); F.p.ch[0][FXP_TYPE] = 2; F.p.ch[0][FXP_CUTOFF] = 90; lsdj_fx_update(&F);   /* high-pass ~1.7 kHz */
    lo = sine_peak(0, 80, 20000); hi = sine_peak(0, 6000, 20000);
    printf("high-pass: 80 Hz peak %d, 6 kHz peak %d\n", lo, hi);
    assert(hi > 4 * lo);
    /* Stability: every filter type, low to high resonance, every cutoff from 100 up
       (where low resonance used to blow up) and a spread below; a full-scale
       square, then silence: no blow-up, and it rings out. */
    static const int resos[] = {0, 20, 42, 64, 100, 127};
    int bad = 0;
    for (int t = 1; t <= 3; t++)
      for (unsigned ri = 0; ri < sizeof resos / sizeof resos[0]; ri++)
        for (int cut = 0; cut < 128; cut += (cut >= 100 ? 1 : 9)) {
            reset(); F.p.ch[1][FXP_TYPE] = (uint8_t)t; F.p.ch[1][FXP_CUTOFF] = (uint8_t)cut; F.p.ch[1][FXP_RESO] = (uint8_t)resos[ri];
            lsdj_fx_update(&F);
            int32_t l, r, peak = 0, tail = 0;
            for (int i = 0; i < 44100; i++) { int16_t s[5] = {0, (int16_t)((i / 50) & 1 ? 1024 : -1024), 0, 0, (int16_t)ALL_LR}; lsdj_fx_mix(&F, s, &l, &r); if (abs(l) > peak) peak = abs(l); }
            for (int i = 0; i < 44100; i++) { int16_t s[5] = {0, 0, 0, 0, (int16_t)ALL_LR}; lsdj_fx_mix(&F, s, &l, &r); if (i > 40000 && abs(l) > tail) tail = abs(l); }
            /* Blow-up: it does not ring out, or at low/medium resonance it clips (high
               resonance legitimately boosts a tone near the cutoff ~10x). A leftover
               of up to 2 units (-67 dB) at the very lowest cutoffs is inaudible. */
            if ((resos[ri] <= 64 && peak >= 32767) || tail > 16) { printf("  type %d reso %d cutoff %d: peak %d tail %d\n", t, resos[ri], cut, peak, tail); bad++; }
        }
    assert(!bad);
    puts("filters: every cutoff at resonance 0..127: no blow-up, rings out       ok");

    /* 3. Echo. mu-law: every code decodes and re-encodes to itself (freeze copies
       codes, and a re-encoded repeat does not drift); error under 3.2%. */
    for (int u = 0; u < 256; u++) assert(lsdj_fx_ulaw(lsdj_fx_unulaw((uint8_t)u)) == (uint8_t)u || lsdj_fx_unulaw((uint8_t)u) == 0);
    for (int x = -32768; x < 32768; x += 7) { int32_t y = lsdj_fx_unulaw(lsdj_fx_ulaw(x)); assert(abs(y - x) <= 16 + abs(x) / 32); }
    assert(lsdj_fx_ulaw(0) == LSDJ_FX_SILENT_BYTE && lsdj_fx_unulaw(LSDJ_FX_SILENT_BYTE) == 0);
    /* An impulse comes back after the set time; feedback decays. */
    reset(); F.p.ch[2][FXP_SEND] = 127; F.p.m[FXM_MIX] = 127; F.p.m[FXM_TIME] = 50; lsdj_fx_update(&F);
    uint32_t len = F.echo_len;
    assert(len == 64u + 50u * (LSDJ_FX_ECHO_LEN - LSDJ_FX_ECHO_MARGIN - 64u) / 127u);
    int32_t first = -1, l, r, prev_peak = 1 << 30, repeats = 0;
    F.p.m[FXM_FEEDBACK] = 127;
    for (int i = 0; i < (int)(len * 2 * 5); i++) {
        int16_t s[5] = {0, 0, (int16_t)(i < 2 ? 1000 : 0), 0, (int16_t)ALL_LR};
        lsdj_fx_mix(&F, s, &l, &r);
        if (i > 4 && abs(l) > 50 && first < 0) first = i;
    }
    printf("echo: impulse back after %d samples (set %u)\n", first, len * 2);
    assert(first >= (int)(len * 2) - 4 && first <= (int)(len * 2) + 4);
    /* repeats get quieter */
    reset(); F.p.ch[2][FXP_SEND] = 127; F.p.m[FXM_MIX] = 127; F.p.m[FXM_TIME] = 10; F.p.m[FXM_FEEDBACK] = 127; lsdj_fx_update(&F);
    len = F.echo_len;
    for (int rep = 0; rep < 6; rep++) {
        int32_t peak = 0;
        for (uint32_t i = 0; i < len * 2; i++) {
            int16_t s[5] = {0, 0, (int16_t)(rep == 0 && i < 2 ? 1000 : 0), 0, (int16_t)ALL_LR};
            lsdj_fx_mix(&F, s, &l, &r);
            if (rep > 0 && abs(l) > peak) peak = abs(l);
        }
        if (rep > 0) { assert(peak < prev_peak || peak == 0); prev_peak = peak; repeats++; }
    }
    puts("echo: mu-law exact round trip, delay time exact, repeats decay          ok");
    /* Longest time: 1.48 s mono, half in ping-pong. */
    reset(); F.p.m[FXM_TIME] = 127; lsdj_fx_update(&F);
    printf("echo: longest %u ms mono", F.echo_len * 1000u / 22050u);
    assert(F.echo_len * 1000u / 22050u >= 1460u);
    F.p.m[FXM_MODE] = 1; lsdj_fx_update(&F);
    printf(", %u ms ping-pong\n", F.echo_len * 1000u / 22050u);
    assert(F.echo_len * 2u + LSDJ_FX_ECHO_MARGIN <= LSDJ_FX_ECHO_LEN + 4u);
    /* Ping-pong: first repeat left only at T, second right only at 2T, third left at 3T. */
    {
        reset(); F.p.ch[2][FXP_SEND] = 127; F.p.m[FXM_MIX] = 127; F.p.m[FXM_TIME] = 20; F.p.m[FXM_FEEDBACK] = 100;
        F.p.m[FXM_MODE] = 1; lsdj_fx_update(&F);
        uint32_t T = F.echo_len * 2u;        /* output samples */
        int32_t fl[3] = {-1, -1, -1}, fr[3] = {-1, -1, -1};
        for (int i = 0; i < (int)(T * 3 + 20); i++) {
            int16_t s[5] = {0, 0, (int16_t)(i < 2 ? 1000 : 0), 0, (int16_t)ALL_LR};
            lsdj_fx_mix(&F, s, &l, &r);
            if (i < 8) continue;
            int seg = (int)((i + (int)T / 2) / (int)T) - 1;   /* nearest repeat */
            if (seg < 0 || seg > 2) continue;
            if (abs(l) > 50 && fl[seg] < 0) fl[seg] = i;
            if (abs(r) > 50 && fr[seg] < 0) fr[seg] = i;
        }
        printf("ping-pong: T %u; left at %d, %d, %d; right at %d, %d, %d\n", T, fl[0], fl[1], fl[2], fr[0], fr[1], fr[2]);
        assert(abs(fl[0] - (int)T) <= 4 && fr[0] < 0);           /* 1: left */
        assert(fl[1] < 0 && abs(fr[1] - 2 * (int)T) <= 4);       /* 2: right */
        assert(abs(fl[2] - 3 * (int)T) <= 4 && fr[2] < 0);       /* 3: left */
    }
    puts("echo: ping-pong alternates left, right, left                          ok");
    /* Tempo sync: 1/8 at 120 BPM = 0.25 s; follows the tempo; too long halves. */
    reset(); F.p.m[FXM_SYNC] = 1; F.p.m[FXM_TIME] = (uint8_t)lsdj_fx_time_of(5); lsdj_fx_update(&F);
    assert(!strcmp(lsdj_fx_note_name[lsdj_fx_note_of(F.p.m[FXM_TIME])], "1/8"));
    assert(F.echo_len == 5512u);
    lsdj_fx_set_bpm(&F, 150); assert(F.echo_len == 4410u);   /* 0.2 s */
    for (unsigned n = 0; n < LSDJ_FX_NOTES; n++) assert(lsdj_fx_note_of(lsdj_fx_time_of(n)) == n);
    F.p.m[FXM_TIME] = (uint8_t)lsdj_fx_time_of(LSDJ_FX_NOTES - 1); lsdj_fx_set_bpm(&F, 40); lsdj_fx_update(&F);   /* 1/2 at 40 = 3 s */
    assert(F.echo_len <= lsdj_fx_echo_max(&F) && F.echo_len == 66150u / 2u / 2u);
    puts("echo: tempo sync 1/8 at 120 = 250 ms, follows tempo, halves to fit      ok");
    /* Freeze: the loop keeps playing, no decay, new sound ignored. */
    {
        reset(); F.p.ch[2][FXP_SEND] = 127; F.p.m[FXM_MIX] = 127; F.p.m[FXM_TIME] = 10; F.p.m[FXM_FEEDBACK] = 60; lsdj_fx_update(&F);
        uint32_t T = F.echo_len * 2u;
        for (int i = 0; i < (int)T * 3; i++) { int16_t s[5] = {0, 0, (int16_t)(i < 2 ? 1000 : 0), 0, (int16_t)ALL_LR}; lsdj_fx_mix(&F, s, &l, &r); }
        F.freeze = 1;
        int32_t pk[4] = {0};
        for (int rep = 0; rep < 4; rep++)
            for (int i = 0; i < (int)T; i++) {
                int16_t s[5] = {0, 0, (int16_t)(i % 37 == 0 ? 3000 : 0), 0, (int16_t)ALL_LR};   /* ignored */
                lsdj_fx_mix(&F, s, &l, &r);
                int32_t e = F.echo_l; if (abs(e) > pk[rep]) pk[rep] = abs(e);
            }
        printf("freeze: loop peaks %d %d %d %d\n", pk[0], pk[1], pk[2], pk[3]);
        assert(pk[0] > 20 && pk[0] == pk[1] && pk[1] == pk[2] && pk[2] == pk[3]);
    }
    puts("echo: freeze loops without decay, ignores new sound                   ok");
    /* Tone: dark repeats lose highs, thin ones lose lows. Echo of a sine after 4 passes. */
    {
        int32_t pk[2][2];
        for (int tone = 0; tone < 2; tone++)
            for (int fq = 0; fq < 2; fq++) {
                reset(); F.p.ch[2][FXP_SEND] = 127; F.p.m[FXM_MIX] = 127; F.p.m[FXM_TIME] = 5; F.p.m[FXM_FEEDBACK] = 127;
                F.p.m[FXM_TONE] = (uint8_t)(tone ? 127 : 10); lsdj_fx_update(&F);
                double hz = fq ? 5000 : 150; int32_t p = 0;
                for (int i = 0; i < 44100; i++) {
                    int16_t s[5] = {0, 0, (int16_t)(i < 4000 ? 900.0 * sin(2 * M_PI * hz * i / 44100.0) : 0), 0, (int16_t)ALL_LR};
                    lsdj_fx_mix(&F, s, &l, &r);
                    if (i > 4000 + (int)F.echo_len * 8 && abs(F.echo_l) > p) p = abs(F.echo_l);
                }
                pk[tone][fq] = p;
            }
        printf("tone: dark 150 Hz %d / 5 kHz %d; thin 150 Hz %d / 5 kHz %d\n", pk[0][0], pk[0][1], pk[1][0], pk[1][1]);
        assert(pk[0][0] > 4 * pk[0][1] && pk[1][1] > 4 * pk[1][0]);
    }
    puts("echo: tone darkens / thins the repeats                                 ok");
    /* Wobble: full depth, ping-pong, a loud input from every channel: stays bounded,
       rings out, and changes the sound. */
    {
        int32_t peak = 0, tail = 0, diff = 0;
        for (int wob = 0; wob < 2; wob++) {
            reset(); for (int c = 0; c < 4; c++) F.p.ch[c][FXP_SEND] = 127;
            F.p.m[FXM_MIX] = 127; F.p.m[FXM_TIME] = 30; F.p.m[FXM_FEEDBACK] = 90; F.p.m[FXM_MODE] = 1;
            F.p.m[FXM_WOBBLE] = (uint8_t)(wob ? 127 : 0); lsdj_fx_update(&F);
            static int32_t ref[44100 * 2];
            for (int i = 0; i < 44100 * 12; i++) {
                int16_t v = (int16_t)(i < 44100 ? ((i / 40) & 1 ? 1024 : -1024) : 0);
                int16_t s[5] = {v, v, v, v, (int16_t)ALL_LR};
                lsdj_fx_mix(&F, s, &l, &r);
                if (abs(l) > peak) peak = abs(l);
                if (i > 44100 * 11 && wob && abs(l) > tail) tail = abs(l);
                if (i < 44100 * 2) { if (!wob) ref[i] = l; else if (abs(l - ref[i]) > diff) diff = abs(l - ref[i]); }
            }
        }
        printf("wobble: peak %d, tail after 10 s %d, differs from no wobble by %d\n", peak, tail, diff);
        assert(tail < 400 && diff > 200);
    }
    puts("echo: wobble stable, rings out, audible                                ok");

    /* Mute and FX off. */
    {
        reset(); for (int c = 0; c < 4; c++) F.p.ch[c][FXP_DRIVE] = 100;
        F.p.fx_off = 1u << 1; F.mute = 1u << 3; lsdj_fx_update(&F);
        for (int i = 0; i < 20000; i++) {
            int16_t s[5] = {0, 0, 0, 0, (int16_t)ALL_LR};
            s[1] = (int16_t)(rand() % 2001 - 1000); s[3] = (int16_t)(rand() % 2001 - 1000);
            int32_t a, b, x, y; lsdj_fx_mix(&F, s, &a, &b);
            int16_t q[5] = {0, s[1], 0, 0, s[4]}; plain(q, &x, &y);     /* PU2 dry, NOI silent */
            assert(a == x && b == y);
        }
        reset(); F.mute = 15; lsdj_fx_update(&F); assert(F.active);
        for (int i = 0; i < 1000; i++) { int16_t s[5] = {500, 500, 500, 500, (int16_t)ALL_LR}; lsdj_fx_mix(&F, s, &l, &r); assert(!l && !r); }
        /* FX off on every channel with only channel effects set: back to the bit-exact path */
        reset(); for (int c = 0; c < 4; c++) F.p.ch[c][FXP_CRUSH] = 60; F.p.fx_off = 15; lsdj_fx_update(&F); assert(!F.active);
    }
    puts("mute silences a channel, FX off plays it dry (exact)                   ok");

    /* 4. Crush and decimate. */
    reset(); F.p.ch[3][FXP_DECIM] = 127; lsdj_fx_update(&F);
    int32_t last = 0, changes = 0;
    for (int i = 0; i < 320; i++) { int16_t s[5] = {0, 0, 0, (int16_t)(i * 3), (int16_t)ALL_LR}; lsdj_fx_mix(&F, s, &l, &r); if (l != last) changes++; last = l; }
    assert(changes == 9);       /* held for 32 samples: 10 values, the first (0) equal to the start */
    reset(); F.p.ch[3][FXP_CRUSH] = 127; lsdj_fx_update(&F);
    for (int v = -1024; v <= 1024; v += 7) { int16_t s[5] = {0, 0, 0, (int16_t)v, (int16_t)ALL_LR}; lsdj_fx_mix(&F, s, &l, &r); assert(l % (512 * 7) == 0); }
    puts("crush / decimate                                                          ok");

    /* 4b. Alpha 66: notch, 24 dB low-pass, warm, distortion types, LFO, compressor. */
    {   /* notch: a sine at the cutoff mostly gone, one far from it passes */
        reset(); F.p.ch[0][FXP_TYPE] = 4; F.p.ch[0][FXP_CUTOFF] = 70; F.p.ch[0][FXP_RESO] = 0; lsdj_fx_update(&F);
        double fc = 40.0 * pow(200.0, 70 / 127.0);
        int32_t at = sine_peak(0, fc, 8000), far = sine_peak(0, fc * 8, 8000);
        assert(at * 3 < far);
        /* 24 dB falls faster than 12 dB two octaves above the cutoff */
        reset(); F.p.ch[0][FXP_TYPE] = 1; F.p.ch[0][FXP_CUTOFF] = 40; lsdj_fx_update(&F);
        double f40 = 40.0 * pow(200.0, 40 / 127.0);
        int32_t lp12 = sine_peak(0, f40 * 4, 8000);
        reset(); F.p.ch[0][FXP_TYPE] = 5; F.p.ch[0][FXP_CUTOFF] = 40; lsdj_fx_update(&F);
        int32_t lp24 = sine_peak(0, f40 * 4, 8000), pass24 = sine_peak(0, f40 / 4, 8000);
        assert(lp24 * 2 < lp12 && pass24 > 2500);
        printf("notch %d vs %d, 2 octaves above: 12 dB %d, 24 dB %d                ok\n", at, far, lp12, lp24);
    }
    {   /* warm: a screaming resonance comes out lower, and stays bounded */
        reset(); F.p.ch[0][FXP_TYPE] = 1; F.p.ch[0][FXP_CUTOFF] = 60; F.p.ch[0][FXP_RESO] = 127; lsdj_fx_update(&F);
        double f60 = 40.0 * pow(200.0, 60 / 127.0);
        int32_t cold = sine_peak(0, f60, 8000);
        F.p.q[0][FXQ_WARM] = 127; lsdj_fx_update(&F);
        int32_t warm = sine_peak(0, f60, 8000);
        assert(warm < cold && warm > 0);
        printf("resonance peak: cold %d, warm %d                                   ok\n", cold, warm);
    }
    {   /* distortion types: bounded, the fold folds, fuzz has no lasting DC */
        for (unsigned d = 0; d < LSDJ_FX_DISTS; d++) {
            reset(); F.p.ch[0][FXP_DRIVE] = 127; F.p.q[0][FXQ_DIST] = (uint8_t)d; lsdj_fx_update(&F);
            int64_t sum = 0; int32_t peak = 0;
            for (int i = 0; i < 44100; i++) {
                int16_t s[5] = {(int16_t)(900.0 * sin(2 * M_PI * 220.0 * i / 44100.0)), 0, 0, 0, (int16_t)ALL_LR};
                int32_t l, r; lsdj_fx_mix(&F, s, &l, &r);
                if (abs(l) > peak) peak = abs(l);
                if (i >= 22050) sum += l;
            }
            assert(peak <= 1024 * 7 + 8);
            if (d == 3) assert(llabs(sum / 22050) < 40);   /* DC blocked */
        }
        reset(); F.p.ch[0][FXP_DRIVE] = 9; F.p.q[0][FXQ_DIST] = 2; lsdj_fx_update(&F);   /* gain 17/8 */
        { int16_t s[5] = {400, 0, 0, 0, (int16_t)ALL_LR}; int32_t l, r; lsdj_fx_mix(&F, s, &l, &r);
          /* 400 x 17/8 = 850: under the fold */ assert(l == 850 * 7);
          s[0] = 900; lsdj_fx_mix(&F, s, &l, &r); /* 1912: folded back to 136 */ assert(l == 136 * 7); }
        puts("distortion: soft, hard, fold, fuzz bounded; fold folds; fuzz DC-free     ok");
    }
    {   /* LFO: steps (free and synced), shapes in range, to cutoff / volume / pan */
        assert(lsdj_fx_lfo_step(0, 120) == 4870u);                         /* 0.05 Hz */
        assert(lsdj_fx_lfo_note(99) == 8 && lsdj_fx_lfo_step(99, 120) == 38957u * 120u / 24u);   /* 1/4 at 120: 2 Hz */
        assert(lsdj_fx_lfo_note(127) == LSDJ_FX_LFO_NOTES - 1);
        for (unsigned sh = 0; sh < LSDJ_FX_SHAPES; sh++) {
            lsdj_fx_chan_t s = {0}; s.lfo_inc = 5000000u; s.rnd = 1;
            int32_t lo = 99999, hi = -99999;
            for (int i = 0; i < 20000; i++) { int32_t v = lsdj_fx_lfo(&s, sh); if (v < lo) lo = v; if (v > hi) hi = v; }
            assert(lo >= -32768 && hi <= 32767 && hi - lo > 40000);
        }
        /* tremolo: full depth square swings a steady tone between full and silent */
        reset(); F.p.q[0][FXQ_LFO_DEPTH] = 127; F.p.q[0][FXQ_LFO_SHAPE] = 2; F.p.q[0][FXQ_LFO_DEST] = 1; F.p.q[0][FXQ_LFO_RATE] = 40; lsdj_fx_update(&F);
        int32_t mn = 99999, mx = 0;
        for (int i = 0; i < 44100; i++) { int16_t s[5] = {800, 0, 0, 0, (int16_t)ALL_LR}; int32_t l, r; lsdj_fx_mix(&F, s, &l, &r); if (l < mn) mn = l; if (l > mx) mx = l; }
        assert(mn == 0 && mx == 800 * 7);
        /* pan: left and right take turns */
        reset(); F.p.q[0][FXQ_LFO_DEPTH] = 127; F.p.q[0][FXQ_LFO_SHAPE] = 2; F.p.q[0][FXQ_LFO_DEST] = 2; F.p.q[0][FXQ_LFO_RATE] = 40; lsdj_fx_update(&F);
        int left_only = 0, right_only = 0;
        for (int i = 0; i < 44100; i++) { int16_t s[5] = {800, 0, 0, 0, (int16_t)ALL_LR}; int32_t l, r; lsdj_fx_mix(&F, s, &l, &r); left_only += l > 0 && r == 0; right_only += r > 0 && l == 0; }
        assert(left_only > 1000 && right_only > 1000);
        /* cutoff sweep: a low-pass tone's level moves with the LFO */
        reset(); F.p.ch[0][FXP_TYPE] = 1; F.p.ch[0][FXP_CUTOFF] = 64; F.p.q[0][FXQ_LFO_DEPTH] = 127; F.p.q[0][FXQ_LFO_RATE] = 30; lsdj_fx_update(&F);
        int32_t a = 99999, b = 0;
        for (int blk = 0; blk < 40; blk++) { int32_t p = sine_peak(0, 3000.0, 1100); if (p < a) a = p; if (p > b) b = p; }
        assert(b > a * 2);
        puts("LFO: free and synced rates, five shapes, cutoff / volume / pan          ok");
    }
    {   /* compressor: loud comes down, quiet comes up, nothing clips; 0 = bypass */
        int32_t loud0, loud, quiet0, quiet;
        #define LOUD_PEAK(out) do { out = 0; for (int i = 0; i < 20000; i++) { int16_t v = (int16_t)(900.0 * sin(2 * M_PI * 220.0 * i / 44100.0));             int16_t s[5] = {v, v, v, v, (int16_t)ALL_LR}; int32_t l, r; lsdj_fx_mix(&F, s, &l, &r); if (i > 10000 && abs(l) > out) out = abs(l); } } while (0)
        reset(); F.p.ch[0][FXP_SEND] = 0; F.active = 1; LOUD_PEAK(loud0);   /* four channels: 4 x 900 x 7 */
        reset(); F.p.comp = 127; lsdj_fx_update(&F); LOUD_PEAK(loud);
        reset(); F.p.comp = 127; lsdj_fx_update(&F);
        quiet = 0; for (int i = 0; i < 20000; i++) { int16_t s[5] = {(int16_t)(60.0 * sin(i * 0.03)), 0, 0, 0, (int16_t)ALL_LR}; int32_t l, r; lsdj_fx_mix(&F, s, &l, &r); if (i > 10000 && abs(l) > quiet) quiet = abs(l); }
        quiet0 = 60 * 7;
        assert(loud < loud0 && quiet > quiet0);
        printf("compressor: loud %d -> %d, quiet %d -> %d                           ok\n", loud0, loud, quiet0, quiet);
    }
    {   /* everything at once, random settings: bounded, no runaway state */
        for (int round = 0; round < 40; round++) {
            reset();
            for (int c = 0; c < 4; c++) {
                F.p.ch[c][FXP_TYPE] = (uint8_t)(rand() % LSDJ_FX_FILTERS); F.p.ch[c][FXP_CUTOFF] = (uint8_t)(rand() & 127); F.p.ch[c][FXP_RESO] = (uint8_t)(rand() & 127);
                F.p.ch[c][FXP_DRIVE] = (uint8_t)(rand() & 127);
                F.p.q[c][FXQ_DIST] = (uint8_t)(rand() % LSDJ_FX_DISTS); F.p.q[c][FXQ_WARM] = (uint8_t)(rand() & 127);
                F.p.q[c][FXQ_LFO_RATE] = (uint8_t)(rand() & 127); F.p.q[c][FXQ_LFO_DEPTH] = (uint8_t)(rand() & 127);
                F.p.q[c][FXQ_LFO_SHAPE] = (uint8_t)(rand() % LSDJ_FX_SHAPES); F.p.q[c][FXQ_LFO_DEST] = (uint8_t)(rand() % LSDJ_FX_DESTS);
            }
            F.p.comp = (uint8_t)(rand() & 127); lsdj_fx_update(&F);
            for (int i = 0; i < 20000; i++) {
                int16_t s[5] = {(int16_t)(rand() % 2049 - 1024), (int16_t)(rand() % 2049 - 1024), (int16_t)(rand() % 2049 - 1024), (int16_t)(rand() % 2049 - 1024), (int16_t)ALL_LR};
                int32_t l, r; lsdj_fx_mix(&F, s, &l, &r);
                assert(l >= -32768 && l <= 32767 && r >= -32768 && r <= 32767);
            }
            for (int c = 0; c < 4; c++) assert(abs(F.c[c].low) <= 2000000 && abs(F.c[c].band) <= 2000000 && abs(F.c[c].low2) <= 2000000);
        }
        puts("random settings, all effects: bounded                                   ok");
    }

    /* 5. Cost with everything on (host CPU, for scale). */
    reset();
    for (int c = 0; c < 4; c++) { F.p.ch[c][FXP_TYPE] = 1; F.p.ch[c][FXP_CUTOFF] = 64; F.p.ch[c][FXP_RESO] = 64; F.p.ch[c][FXP_DRIVE] = 40;
                                  F.p.ch[c][FXP_CRUSH] = 30; F.p.ch[c][FXP_DECIM] = 10; F.p.ch[c][FXP_SEND] = 64;
                                  F.p.ch[c][FXP_TYPE] = 5; F.p.q[c][FXQ_WARM] = 64; F.p.q[c][FXQ_DIST] = 3;
                                  F.p.q[c][FXQ_LFO_DEPTH] = 90; F.p.q[c][FXQ_LFO_RATE] = 70; }
    F.p.comp = 80; F.p.m[FXM_MIX] = 64; F.p.m[FXM_TIME] = 64; F.p.m[FXM_FEEDBACK] = 64; lsdj_fx_update(&F);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile int32_t sink = 0;
    for (int i = 0; i < 44100 * 20; i++) { int16_t s[5] = {(int16_t)(i & 1023), (int16_t)-(i & 511), (int16_t)(i % 777), (int16_t)(i * 7 % 1000), (int16_t)ALL_LR}; lsdj_fx_mix(&F, s, &l, &r); sink += l; }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("cost, everything on: %.1f ns per sample on this PC\n", ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (44100.0 * 20));
    puts("PASS: effects bypass bit-exact, filters, stability, echo, crush, decimate");
    return 0;
}
