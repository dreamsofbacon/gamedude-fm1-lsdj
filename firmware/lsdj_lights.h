/* SPDX-License-Identifier: GPL-3.0-only */
/* The PLAY button's light (one colour) shows LSDj's state, on the beat:
     stopped     lit, as always
     song mode   dark, flashing on each beat
     live mode   lit, dipping dark on each beat (the song light inverted)
   A beat is a phrase row 0, 4, 8, 12 (LSDj steps are sixteenth notes) of the
   first channel that is playing (in live mode channels start on their own).
   board.c reads LSDj's per-channel playing flags, phrase rows and live flag.
   Pure logic: tests/lsdj_lights.c. */
#pragma once
#include <stdint.h>

static int lsdj_play_light(const uint8_t playing[4], const uint8_t rows[4], int live) {
    for (unsigned c = 0; c < 4; c++) {
        if (!playing[c]) continue;
        int beat = (rows[c] & 3u) == 0;
        return live ? !beat : beat;
    }
    return 1;   /* stopped */
}

/* The D-pad keys (button mode, src/gb_port.c gp_input_keys; F3 = bit 0):
   up G#3 A#3, right B3 C4, down A3, left F3 G3. While LSDj plays, on each
   beat one direction goes soft (lit -> the LEDs' mid level) for that step,
   round the cross clockwise: up, right, down, left - the D-pad keys stand
   out from A / B / START / SELECT. Returns the key mask to soften (0 when
   stopped or between beats). */
#define LSDJ_DPAD_UP    ((1u << 3) | (1u << 5))
#define LSDJ_DPAD_RIGHT ((1u << 6) | (1u << 7))
#define LSDJ_DPAD_DOWN  (1u << 4)
#define LSDJ_DPAD_LEFT  ((1u << 0) | (1u << 2))
static uint32_t lsdj_dpad_soft(const uint8_t playing[4], const uint8_t rows[4]) {
    static const uint32_t dir[4] = {LSDJ_DPAD_UP, LSDJ_DPAD_RIGHT, LSDJ_DPAD_DOWN, LSDJ_DPAD_LEFT};
    for (unsigned c = 0; c < 4; c++) {
        if (!playing[c]) continue;
        if (rows[c] & 3u) return 0;
        return dir[(rows[c] >> 2) & 3u];
    }
    return 0;
}
