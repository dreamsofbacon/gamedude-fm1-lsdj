/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_midisync.h against a model of LSDj: START toggles
   armed on its press edge, one frame later. */
#include <assert.h>
#include <stdio.h>
#include "lsdj_midisync.h"

static int sent;
static void send(uint8_t b) { assert(b == 0); sent++; }
static int armed, was_down, presses;
/* n frames; returns frames START was held */
static int frames(lsdj_ms_t *s, int n) {
    int held = 0;
    for (int i = 0; i < n; i++) {
        int down = lsdj_ms_frame(s, armed);
        if (down && !was_down) { armed = !armed; presses++; }
        was_down = down; held += down;
    }
    return held;
}

int main(void) {
    lsdj_ms_t s; lsdj_ms_init(&s);
    /* Not in MIDI mode: nothing passes, Start only asks for SYNC = MIDI. */
    assert(lsdj_ms_byte(&s, 0xF8, 0, 0, send) == LSDJ_MS_SAID_NOTHING && !sent);
    assert(lsdj_ms_byte(&s, 0xFA, 0, 0, send) == LSDJ_MS_SAID_SET_SYNC);
    assert(!frames(&s, 30) && !armed);
    /* Stopped, nobody armed: clock is not passed on (no stale ticks). */
    for (int i = 0; i < 100; i++) lsdj_ms_byte(&s, 0xF8, 1, armed, send);
    assert(!sent);
    /* Start arms with one press; ticks pass from the Start on. */
    assert(lsdj_ms_byte(&s, 0xFA, 1, armed, send) == LSDJ_MS_SAID_START);
    lsdj_ms_byte(&s, 0xF8, 1, armed, send); assert(sent == 1);
    frames(&s, 20); assert(armed && presses == 1);
    /* Stop disarms; clock that keeps coming is dropped. */
    assert(lsdj_ms_byte(&s, 0xFC, 1, armed, send) == LSDJ_MS_SAID_STOP);
    frames(&s, 20); assert(!armed && presses == 2);
    sent = 0; for (int i = 0; i < 50; i++) lsdj_ms_byte(&s, 0xF8, 1, armed, send); assert(!sent);
    /* Continue arms; Continue while armed does nothing. */
    lsdj_ms_byte(&s, 0xFB, 1, armed, send); frames(&s, 20); assert(armed && presses == 3);
    lsdj_ms_byte(&s, 0xFB, 1, armed, send); frames(&s, 20); assert(armed && presses == 3);
    /* Start while playing: off, then on again (restart). */
    lsdj_ms_byte(&s, 0xFA, 1, armed, send); frames(&s, 30); assert(armed && presses == 5);
    /* Stop while stopped: nothing. */
    lsdj_ms_byte(&s, 0xFC, 1, armed, send); frames(&s, 20); assert(!armed && presses == 6);
    lsdj_ms_byte(&s, 0xFC, 1, armed, send); frames(&s, 20); assert(!armed && presses == 6);
    /* PLAY pressed by hand (armed without Start): its clock passes. */
    armed = 1; sent = 0; lsdj_ms_byte(&s, 0xF8, 1, armed, send); assert(sent == 1); armed = 0;
    /* LSDj never responds: gives up after LSDJ_MS_TRIES presses. */
    {
        lsdj_ms_t t; lsdj_ms_init(&t);
        lsdj_ms_byte(&t, 0xFB, 1, 0, send);
        int held = 0; for (int i = 0; i < 200; i++) held += lsdj_ms_frame(&t, 0);
        assert(held == (int)(LSDJ_MS_TRIES * LSDJ_MS_HOLD) && t.want == LSDJ_MS_NONE);
    }
    /* START is held LSDJ_MS_HOLD frames, released at least LSDJ_MS_GAP between presses. */
    {
        lsdj_ms_t t; lsdj_ms_init(&t); int a = 1, prev = 0, run = 0, gap = 99;
        lsdj_ms_byte(&t, 0xFA, 1, a, send);
        for (int i = 0; i < 40; i++) {
            int d = lsdj_ms_frame(&t, a);
            if (d) { if (!prev) { assert(gap >= (int)LSDJ_MS_GAP); a = !a; } run++; gap = 0; }
            else { if (prev) assert(run == (int)LSDJ_MS_HOLD); run = 0; gap++; }
            prev = d;
        }
        assert(a == 1);
    }
    puts("PASS: MIDI sync: Start/Continue/Stop press START, clock passes only when running, restart, gives up");
    return 0;
}
