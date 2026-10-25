/* SPDX-License-Identifier: GPL-3.0-only */
/* Tempo control (EDIT): hold EDIT and turn K1 (1 BPM) or K2 (10 BPM), or tap
   EDIT in time (tap tempo, the last 2..5 taps).
   How: LSDj counts its ticks from a fixed timer and a period it works out when
   the tempo changes, ~1834880 / BPM (WRAM C954-C955 on 9.3.9). Writing the
   period changes the speed at once (emulator: harness/tempo_poke_check2.sh);
   the tempo setting (WRAM C529 and the song's 3FB4: the PROJECT screen, the
   echo's sync, SAVE) is written with it. Tempo bytes: 40..255 as is, 256..295
   as 0..39 (LSDj's encoding). Pure logic: tests/lsdj_tempo.c. */
#pragma once
#include <stdint.h>

#define LSDJ_TEMPO_MIN 40u
#define LSDJ_TEMPO_MAX 295u
#define LSDJ_TAP_GAP_MS 2000u   /* a longer pause starts a new tap run */

static uint16_t lsdj_tempo_period(unsigned bpm) { return (uint16_t)(1834880u / bpm); }
static uint8_t lsdj_tempo_byte(unsigned bpm) { return (uint8_t)(bpm & 0xFFu); }            /* 256..295 -> 0..39 */
static unsigned lsdj_tempo_of_byte(uint8_t b) { return b < LSDJ_TEMPO_MIN ? b + 256u : b; }
static unsigned lsdj_tempo_clamp(int bpm) { return bpm < (int)LSDJ_TEMPO_MIN ? LSDJ_TEMPO_MIN : bpm > (int)LSDJ_TEMPO_MAX ? LSDJ_TEMPO_MAX : (unsigned)bpm; }

typedef struct {
    uint8_t held, turned;
    uint16_t bpm;              /* the tempo being set */
    uint32_t tap[5]; uint8_t taps;   /* press times of the current tap run, oldest first */
} lsdj_tempo_t;

/* EDIT down at now_ms with LSDj at bpm: a tap. Returns the tapped tempo once
   there are two taps in a run, else 0. */
static unsigned lsdj_tempo_press(lsdj_tempo_t *t, unsigned bpm, uint32_t now_ms) {
    t->held = 1; t->turned = 0; t->bpm = (uint16_t)bpm;
    if (t->taps && now_ms - t->tap[t->taps - 1u] > LSDJ_TAP_GAP_MS) t->taps = 0;
    if (t->taps == 5u) { for (unsigned i = 0; i < 4u; i++) t->tap[i] = t->tap[i + 1u]; t->taps = 4u; }
    t->tap[t->taps++] = now_ms;
    if (t->taps < 2u) return 0;
    uint32_t span = t->tap[t->taps - 1u] - t->tap[0];
    if (!span) return 0;
    t->bpm = (uint16_t)lsdj_tempo_clamp((int)((60000u * (t->taps - 1u) + span / 2u) / span));
    return t->bpm;
}
/* While held: K1 clicks 1 BPM, K2 clicks 10. Returns the new tempo if it changed, else 0.
   Turning makes this press not a tap. */
static unsigned lsdj_tempo_turn(lsdj_tempo_t *t, int fine, int coarse) {
    if (!t->held || (!fine && !coarse)) return 0;
    if (!t->turned) { t->turned = 1; t->taps = 0; }
    unsigned b = lsdj_tempo_clamp((int)t->bpm + fine + 10 * coarse);
    if (b == t->bpm) return 0;
    t->bpm = (uint16_t)b;
    return b;
}
static void lsdj_tempo_release(lsdj_tempo_t *t) { t->held = 0; }

/* The pop-up while held (fields as lsdj_ov_bars_t): "TEMPO 170", K1 / K2. */
static void lsdj_tempo_bars(const lsdj_tempo_t *t, int midi_sync, char title[20], char big[20],
                            char label[4][7], char value[4][6], char caption[4][4], uint8_t level[4]) {
    static const char *const lab[2] = {"BP", "10"};
    unsigned n = 0, b = t->bpm;
    for (const char *s = "TEMPO "; *s; s++) title[n++] = *s;
    if (b >= 100u) title[n++] = (char)('0' + b / 100u);
    title[n++] = (char)('0' + b / 10u % 10u); title[n++] = (char)('0' + b % 10u); title[n] = 0;
    for (unsigned k = 0; k < 4; k++) {
        label[k][0] = value[k][0] = caption[k][0] = 0; level[k] = 0;
        if (k >= 2u) continue;
        unsigned i = 0; for (const char *s = lab[k]; *s; s++) label[k][i++] = *s; label[k][i] = 0;
        i = 0;   /* the BPM is in the title: no value in the 2-wide column */
        value[k][i] = 0;
        caption[k][0] = 'K'; caption[k][1] = (char)('1' + k); caption[k][2] = 0;
        level[k] = (uint8_t)((b - LSDJ_TEMPO_MIN) * 127u / (LSDJ_TEMPO_MAX - LSDJ_TEMPO_MIN));
    }
    const char *s = midi_sync ? "MIDI CLOCK SETS" : "OR TAP EDIT";
    n = 0; while (*s) big[n++] = *s++; big[n] = 0;
}
