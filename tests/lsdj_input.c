/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for src/fm1_lsdj_input.h: the FM-1 control layout for LSDj. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "gb_port.h"
/* Button-mode keys come from gp_input_keys(); a stand-in keeps this test small. */
uint32_t gp_input_keys(uint32_t lo, uint32_t hi) { (void)hi; return lo & 1u ? GP_A : 0; }
static int note_log[64], note_n;
#define LSDJ_KEYBD_NOTE(n, on) (on ? (void)(note_log[note_n++] = (n)) : (void)0)
#include "fm1_lsdj_input.h"

static lsdj_input_t s;
/* One detent on encoder e; returns the buttons seen over the tap's frames. */
static uint32_t turn(int e, int dir) {
    int32_t enc[7] = {0}; enc[e] = dir;
    uint32_t seen = lsdj_input_frame(&s, 0, 0, enc);
    int32_t none[7] = {0};
    for (int f = 0; f < 6; f++) seen |= lsdj_input_frame(&s, 0, 0, none);
    return seen;
}

int main(void) {
    lsdj_input_init(&s);
    int32_t none[7] = {0};
    assert(turn(LSDJ_ENC_PRESETS, +1) == GP_DOWN && turn(LSDJ_ENC_PRESETS, -1) == GP_UP);
    assert(turn(LSDJ_ENC_ALGORITHM, +1) == GP_RIGHT && turn(LSDJ_ENC_ALGORITHM, -1) == GP_LEFT);
    assert(turn(4, +1) == 0 && turn(4, -1) == 0 && turn(5, +1) == 0 && turn(5, -1) == 0);   /* KNOB 3/4 free */
    assert(lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_PLAY, none) == GP_START);
    assert(lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_REC, none) == GP_SELECT);
    lsdj_input_frame(&s, 0, 0, none);
    assert(lsdj_input_frame(&s, 1u, 0, none) == GP_A);                     /* button mode */
    lsdj_input_frame(&s, 0, 0, none);
    lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_HOME, none);                    /* HOME: note mode */
    assert(s.mode == LSDJ_KEYS_NOTES);
    assert(lsdj_input_frame(&s, 1u, 0, none) == 0 && note_n == 1 && note_log[0] == LSDJ_NOTE_BASE);
    lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_OCT_UP, none);
    assert(s.octave == 1);
    assert(lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_PLAY, none) == GP_START);   /* PLAY still works */
    /* Settings menu options. */
    lsdj_input_init(&s);
    lsdj_input_config(&s, 1, 0, 0, 0, 0);                                    /* knobs swapped */
    assert(turn(LSDJ_ENC_PRESETS, +1) == GP_RIGHT && turn(LSDJ_ENC_PRESETS, -1) == GP_LEFT);
    assert(turn(LSDJ_ENC_ALGORITHM, +1) == GP_DOWN && turn(LSDJ_ENC_ALGORITHM, -1) == GP_UP);
    lsdj_input_config(&s, 1, 1, 1, 0, 0);                                    /* ... and both reversed */
    assert(turn(LSDJ_ENC_PRESETS, +1) == GP_LEFT && turn(LSDJ_ENC_ALGORITHM, +1) == GP_UP);
    lsdj_input_config(&s, 0, 1, 0, 0, 0);                                    /* only PRESETS reversed */
    assert(turn(LSDJ_ENC_PRESETS, +1) == GP_UP && turn(LSDJ_ENC_ALGORITHM, +1) == GP_RIGHT);
    lsdj_input_config(&s, 0, 0, 0, 1, 1u << 25);                             /* PLAY=Select, F#5 = Select */
    assert(lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_PLAY, none) == GP_SELECT);
    assert(lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_REC, none) == GP_START);
    lsdj_input_frame(&s, 0, 0, none);
    assert(lsdj_input_frame(&s, 1u << 25, 0, none) == GP_SELECT);
    lsdj_input_frame(&s, 0, 0, none);
    lsdj_input_frame(&s, 0, 1u << LSDJ_BTN_HOME, none);                    /* note mode: F#5 is a note */
    note_n = 0; assert(lsdj_input_frame(&s, 1u << 25, 0, none) == 0 && note_n == 1);
    puts("PASS: PRESETS cw=down ccw=up, ALGORITHM cw=right ccw=left, KNOB 3/4 free, PLAY=Start, REC=Select, HOME note mode, OCT");
    puts("PASS: settings: knobs swapped, each knob reversed, PLAY/REC swapped, extra Select key (button mode only)");
    return 0;
}
