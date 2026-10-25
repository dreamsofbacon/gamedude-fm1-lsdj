/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock sync for LSDj (PROJECT > SYNC = MIDI), as an Arduinoboy does it:
   every MIDI clock tick (0xF8, 24 per beat) goes into the link port as one 0x00
   byte, and LSDj moves one step per 6 ticks. LSDj only follows the clock once
   START has armed it, so the transport messages press START for the player:
     Start (0xFA)    restart: disarm if armed, then arm (plays from the cursor)
     Continue (0xFB) arm if not armed (carries on where it paused)
     Stop (0xFC)     disarm
   Ticks that arrive while LSDj is being armed wait in the link queue (LSDj
   only reads it once armed), so it starts in step with the clock.
   Pure logic: tests/lsdj_midisync.c. */
#pragma once
#include <stdint.h>

#define LSDJ_MS_HOLD 2u        /* frames START is held */
#define LSDJ_MS_GAP 4u         /* frames released before LSDj's state is trusted again */
#define LSDJ_MS_TRIES 4u       /* presses per request, if LSDj does not respond */

enum { LSDJ_MS_NONE, LSDJ_MS_ARM, LSDJ_MS_DISARM, LSDJ_MS_RESTART };
/* What a transport message did, for the on-screen message. */
enum { LSDJ_MS_SAID_NOTHING, LSDJ_MS_SAID_START, LSDJ_MS_SAID_CONTINUE, LSDJ_MS_SAID_STOP, LSDJ_MS_SAID_SET_SYNC };

typedef struct {
    uint8_t want;              /* LSDJ_MS_* */
    uint8_t hold, gap, tries;
    uint32_t ticks;            /* clock ticks passed to LSDj */
    uint8_t running;           /* between Start/Continue and Stop */
} lsdj_ms_t;

static void lsdj_ms_init(lsdj_ms_t *s) { uint8_t *b = (uint8_t *)s; for (uint32_t i = 0; i < sizeof *s; i++) b[i] = 0; }

/* One MIDI realtime status byte. midi_mode: LSDj's SYNC is MIDI; armed: LSDj is
   armed or playing. A tick is passed on (send(0)) between Start/Continue and
   Stop, or while LSDj is armed (started with PLAY). Returns LSDJ_MS_SAID_*. */
static int lsdj_ms_byte(lsdj_ms_t *s, uint8_t st, int midi_mode, int armed, void (*send)(uint8_t)) {
    if (!midi_mode) return st == 0xFAu ? LSDJ_MS_SAID_SET_SYNC : LSDJ_MS_SAID_NOTHING;
    switch (st) {
    case 0xF8:                         /* stale ticks would make LSDj jump ahead when armed */
        if (s->running || armed) { send(0x00); s->ticks++; }
        return LSDJ_MS_SAID_NOTHING;
    case 0xFA: s->want = LSDJ_MS_RESTART; s->tries = 0; s->running = 1; return LSDJ_MS_SAID_START;
    case 0xFB: s->want = LSDJ_MS_ARM; s->tries = 0; s->running = 1; return LSDJ_MS_SAID_CONTINUE;
    case 0xFC: s->want = LSDJ_MS_DISARM; s->tries = 0; s->running = 0; return LSDJ_MS_SAID_STOP;
    }
    return LSDJ_MS_SAID_NOTHING;
}

/* Once per emulated frame: armed = LSDj is armed or playing. Returns 1 while
   START should be held down. */
static int lsdj_ms_frame(lsdj_ms_t *s, int armed) {
    if (s->hold) { if (--s->hold == 0) s->gap = LSDJ_MS_GAP; return 1; }
    if (s->gap) { s->gap--; return 0; }
    int press = 0;
    switch (s->want) {
    case LSDJ_MS_RESTART: if (armed) press = 1; else { s->want = LSDJ_MS_ARM; s->tries = 0; return lsdj_ms_frame(s, armed); } break;
    case LSDJ_MS_ARM: if (!armed) press = 1; else s->want = LSDJ_MS_NONE; break;
    case LSDJ_MS_DISARM: if (armed) press = 1; else s->want = LSDJ_MS_NONE; break;
    }
    if (!press) return 0;
    if (s->tries >= LSDJ_MS_TRIES) { s->want = LSDJ_MS_NONE; return 0; }   /* LSDj not responding: give up */
    s->tries++;
    s->hold = LSDJ_MS_HOLD - 1u;
    if (!s->hold) s->gap = LSDJ_MS_GAP;
    return 1;
}
