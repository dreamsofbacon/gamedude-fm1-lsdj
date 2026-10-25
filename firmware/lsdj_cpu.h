/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU meter for CPU0 (GLO toggles it on screen). board.c adds the 24 MHz ticks
   spent in each kind of work; every second the shares are turned into
   percentages and two overlay lines (alpha 21):
     "FPS59 U0 LOAD130%"    FPS   = Game Boy frames emulated per second (59.7 = full speed)
                            U     = audio buffer underruns per second (gaps you hear)
                            LOAD  = average time per emulated frame, as a share of the
                                    16.7 ms a frame may take: over 100% = too slow.
                                    Unlike EMU it does not saturate at the limit.
     "MAX180% FREE0%"       MAX   = the slowest frame on the same scale, uncapped
   Older form:
     "FREE 52% MAX 71%"     FREE  = time the main loop had nothing to do,
                                    minus the interrupts that ran meanwhile
                            MAX   = the slowest emulated frame, as a share of
                                    one Game Boy frame (16.7 ms); 100% = too slow
     "EMU38 SND9 ISR6 R11"  EMU   = Game Boy CPU + sound (gp_frame)
                            SND   = the sound rendering part of EMU
                            ISR   = interrupts (10 kHz key scan, audio DMA)
                            R     = sound renders per frame (cycle-synced
                                    catch-up: one per sound register write burst)
   Pure logic: tests/lsdj_cpu.c. */
#pragma once
#include <stdint.h>

#define LSDJ_CPU_HZ        24000000u
#define LSDJ_CPU_FRAME     401816u     /* 70224 / 4194304 s in 24 MHz ticks */

typedef struct {
    uint32_t start;                     /* window start (ticks) */
    uint32_t emu, lcd, idle, worst, snd, renders, frames;   /* this window */
    volatile uint32_t isr;              /* added by the interrupt handlers */
    uint8_t free_pct, worst_pct, emu_pct, isr_pct, lcd_pct, snd_pct, ready;
    uint16_t renders_per_frame, fps, underruns, load_pct, max_pct;
    uint32_t underrun_count;              /* board: underruns in this window */
    uint8_t core1, reason;                /* board: emulator on CPU1? else why not (0..9) */
} lsdj_cpu_t;

static void lsdj_cpu_frame(lsdj_cpu_t *c, uint32_t ticks) { c->emu += ticks; c->frames++; if (ticks > c->worst) c->worst = ticks; }

/* 32-bit only: the firmware's freestanding libc has no 64-bit division. */
static uint8_t lsdj_cpu_pct(uint32_t part, uint32_t whole) {
    if (!whole) return 0;
    if (part > whole) part = whole;
    while (whole > 40000000u) { whole >>= 1; part >>= 1; }   /* keep part * 100 in 32 bits */
    uint32_t p = (part * 100u + whole / 2u) / whole;
    return (uint8_t)(p > 100u ? 100u : p);
}

/* Call from the main loop; closes a window after one second. 1 = new numbers. */
static int lsdj_cpu_poll(lsdj_cpu_t *c, uint32_t now) {
    uint32_t win = now - c->start;
    if (win < LSDJ_CPU_HZ) return 0;
    uint32_t isr = c->isr; c->isr = 0;
    /* Interrupts also ran during idle time; take their idle share off. */
    uint32_t isr_share = isr / (win >> 10 ? win >> 10 : 1u);          /* in 1/1024 */
    uint32_t isr_in_idle = (c->idle >> 10) * (isr_share > 1024u ? 1024u : isr_share);
    uint32_t free_ticks = c->idle > isr_in_idle ? c->idle - isr_in_idle : 0u;
    c->free_pct = lsdj_cpu_pct(free_ticks, win);
    uint32_t worst = c->worst > LSDJ_CPU_FRAME ? LSDJ_CPU_FRAME : c->worst;   /* shown as 100% */
    c->worst_pct = lsdj_cpu_pct(worst, LSDJ_CPU_FRAME);
    c->emu_pct = lsdj_cpu_pct(c->emu, win);
    c->isr_pct = lsdj_cpu_pct(isr, win);
    c->lcd_pct = lsdj_cpu_pct(c->lcd, win);
    c->snd_pct = lsdj_cpu_pct(c->snd, win);
    c->renders_per_frame = (uint16_t)(c->frames ? (c->renders + c->frames / 2u) / c->frames : 0u);
    c->fps = (uint16_t)(c->frames * (LSDJ_CPU_HZ / 1000u) / (win / 1000u ? win / 1000u : 1u));   /* 32-bit */
    c->underruns = (uint16_t)(c->underrun_count > 999u ? 999u : c->underrun_count); c->underrun_count = 0;
    {   /* average and worst frame cost against one frame period, 0..999 % */
        uint32_t avg = c->frames ? c->emu / c->frames : 0u;
        uint32_t a = avg / (LSDJ_CPU_FRAME / 100u), m = c->worst / (LSDJ_CPU_FRAME / 100u);
        c->load_pct = (uint16_t)(a > 999u ? 999u : a); c->max_pct = (uint16_t)(m > 999u ? 999u : m);
    }
    c->emu = c->lcd = c->idle = c->worst = c->snd = c->renders = c->frames = 0; c->start = now; c->ready = 1;
    return 1;
}

static char *lsdj_cpu_put(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *lsdj_cpu_num(char *p, unsigned v) {
    if (v >= 100) *p++ = (char)('0' + v / 100);
    if (v >= 10) *p++ = (char)('0' + v / 10 % 10);
    *p++ = (char)('0' + v % 10);
    return p;
}
/* The two overlay lines (at most 19 characters each; line 2 shows 99 for 99-100). */
static void lsdj_cpu_lines(const lsdj_cpu_t *c, char l1[20], char l2[20]) {
    char *p = l1;
    p = lsdj_cpu_put(p, "FPS"); p = lsdj_cpu_num(p, c->fps > 99 ? 99 : c->fps);
    p = lsdj_cpu_put(p, " U"); p = lsdj_cpu_num(p, c->underruns > 9 ? 9 : c->underruns);   /* 9 = 9 or more */
    p = lsdj_cpu_put(p, " LOAD"); p = lsdj_cpu_num(p, c->load_pct);                         /* percent */
    /* C1: the emulator runs on CPU1; Rn: on CPU0, and why (video_retire_reason) */
    if (c->core1) p = lsdj_cpu_put(p, " C1"); else { p = lsdj_cpu_put(p, " R"); *p++ = (char)('0' + c->reason % 10u); }
    *p = 0;
    p = l2;
    p = lsdj_cpu_put(p, "MAX"); p = lsdj_cpu_num(p, c->max_pct); p = lsdj_cpu_put(p, "%");
    p = lsdj_cpu_put(p, " FREE"); p = lsdj_cpu_num(p, c->free_pct > 99 ? 99 : c->free_pct); p = lsdj_cpu_put(p, "%"); *p = 0;
}
