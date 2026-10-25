/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_cpu.h: percentages from known tick counts, the
   one-second window, and the overlay lines (they must fit the 19-column box). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_cpu.h"

static lsdj_cpu_t c;

int main(void) {
    char l1[24], l2[24];   /* larger than the 20 the firmware uses: an overflow shows up as a length > 19 */
    /* A one-second window: 40% emulation, 10% LCD, 45% idle, 5% interrupts. */
    uint32_t s = LSDJ_CPU_HZ;
    c.start = 1000;
    for (int f = 0; f < 60; f++) lsdj_cpu_frame(&c, s * 40u / 100u / 60u + (f == 7 ? 100000u : 0u));
    c.lcd = s / 10u; c.idle = s * 45u / 100u; c.isr = s / 20u; c.snd = s * 30u / 100u; c.renders = 660;
    assert(lsdj_cpu_poll(&c, 1000 + s - 1) == 0);           /* not a full second yet */
    assert(lsdj_cpu_poll(&c, 1000 + s) == 1);
    assert(c.emu_pct == 40 || c.emu_pct == 41);
    assert(c.lcd_pct == 10 && c.isr_pct == 5 && c.snd_pct == 30 && c.renders_per_frame == 11);
    /* free = idle minus the interrupts' idle share: 45% - 5% of 45% = 42.75% */
    assert(c.free_pct >= 42 && c.free_pct <= 43);
    /* slowest frame: 1/60 s * 0.40 + 100000 ticks, against a 16.74 ms frame */
    unsigned want = (unsigned)((s * 40u / 100u / 60u + 100000u) * 100u / LSDJ_CPU_FRAME);
    assert(c.worst_pct == want || c.worst_pct == want + 1);
    assert(c.emu == 0 && c.idle == 0 && c.isr == 0 && c.start == 1000 + s);
    lsdj_cpu_lines(&c, l1, l2);
    printf("  %s | %s\n", l1, l2);
    assert(!strcmp(l1, "FPS60 U0 LOAD40 R0") && !strncmp(l2, "MAX6", 4) && strstr(l2, "% FREE43%"));

    /* Extremes: everything 100%, a frame that took 10x too long, a late poll. */
    lsdj_cpu_frame(&c, 10u * LSDJ_CPU_FRAME); c.underrun_count = 5000; c.reason = 5;
    c.emu = 3u * s; c.lcd = 3u * s; c.idle = 3u * s; c.isr = 3u * s; c.snd = 3u * s; c.renders = 100000;
    assert(lsdj_cpu_poll(&c, c.start + 2u * s) == 1);
    assert(c.worst_pct == 100 && c.emu_pct == 100 && c.isr_pct == 100 && c.free_pct == 0);
    lsdj_cpu_lines(&c, l1, l2);
    printf("  %s | %s\n", l1, l2);
    assert(strlen(l1) <= 19 && strlen(l2) <= 19 && c.max_pct == 999 && c.underruns == 999);
    /* Nothing at all: all zero, still well-formed. */
    assert(lsdj_cpu_poll(&c, c.start + s) == 1);
    lsdj_cpu_lines(&c, l1, l2);
    c.core1 = 1; lsdj_cpu_lines(&c, l1, l2);
    assert(!strcmp(l1, "FPS0 U0 LOAD0 C1") && !strcmp(l2, "MAX0% FREE0%"));
    /* Tick counter wrap. */
    c.start = 0xFFFFFF00u; c.idle = s / 2u;
    assert(lsdj_cpu_poll(&c, 0xFFFFFF00u + s) == 1 && c.free_pct == 50);
    puts("PASS: CPU meter percentages, window, extremes, counter wrap, overlay lines fit");
    return 0;
}
