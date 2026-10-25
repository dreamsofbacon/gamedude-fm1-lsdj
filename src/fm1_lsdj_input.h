/* SPDX-License-Identifier: GPL-3.0-only */
/* LSDj control layout for the FM-1. Pure logic, no hardware calls, so the
   same code runs in the firmware and in the host test (tests/lsdj_input.c).

   Knobs, always active, one Game Boy tap per detent:
     PRESETS    up / down          ALGORITHM  left / right
     (counter-clockwise / clockwise). KNOB 3 and KNOB 4 are kept free.
   Piano keys, toggled with HOME:
     button mode: GameDude mapping (gp_input_keys); held keys do all combos
     note mode:   keys go to LSDJ_KEYBD_NOTE (LSDj SYNC=KEYBD), OCT-/OCT+ shift
   PLAY = Start and REC = Select in both modes. */
#pragma once
#include <stdint.h>
#include "gb_port.h"

/* Encoder index (FM1_ENC order) and button id of each control, from Dinghy's
   documented ids (docs/API.md, platform.c): its FM1_ENC wiring table and
   FM1_KEYMAP matrix are identical to vendor/felucca/firmware/hal/fm1_input.h.
   -1 = disabled. */
#ifndef LSDJ_ENC_PRESETS
#define LSDJ_ENC_PRESETS   6   /* ENC_PRESETS */
#endif
#ifndef LSDJ_ENC_ALGORITHM
#define LSDJ_ENC_ALGORITHM 1   /* ENC_ALGORITHM */
#endif
/* KNOB 3 (ENC_KNOB3 = 4) and KNOB 4 (ENC_KNOB4 = 5): free for later features. */
#ifndef LSDJ_ENC_KNOB3
#define LSDJ_ENC_KNOB3     (-1)
#endif
#ifndef LSDJ_ENC_KNOB4
#define LSDJ_ENC_KNOB4     (-1)
#endif
/* HOME toggles the piano keys between button mode and note mode (LSDj keyboard,
   PROJECT > SYNC = KEYBD). */
#ifndef LSDJ_BTN_HOME
#define LSDJ_BTN_HOME      8   /* BTN_HOME */
#endif
#define LSDJ_BTN_OCT_DOWN  0   /* FM1_BTN_OCT_DOWN */
#define LSDJ_BTN_OCT_UP    1   /* FM1_BTN_OCT_UP */
#define LSDJ_BTN_PLAY      12  /* measured (board.c) */
#define LSDJ_BTN_REC       13  /* measured (board.c) */

/* Note-mode hook: note is a MIDI note number. Empty until the KEYBD path exists. */
#ifndef LSDJ_KEYBD_NOTE
#define LSDJ_KEYBD_NOTE(note, on) ((void)(note), (void)(on))
#endif

#define LSDJ_TAP_DOWN  2u  /* frames a knob tap is held */
#define LSDJ_TAP_UP    2u  /* frames released before the next tap */
#define LSDJ_TAP_QUEUE 6u  /* taps kept from a fast spin; extra detents are dropped */
#define LSDJ_NOTE_BASE 53  /* key 0 = F3 */

enum { LSDJ_KEYS_BUTTONS = 0, LSDJ_KEYS_NOTES = 1 };

typedef struct {
    int8_t dir;    /* direction of the queued taps: -1, 0, +1 */
    uint8_t queue; /* taps waiting */
    uint8_t t;     /* frame within the current tap, 0 = idle */
    uint8_t out;   /* Game Boy button of the current tap */
} lsdj_knob_t;

typedef struct {
    lsdj_knob_t knob[4];
    uint8_t mode;
    int8_t octave;     /* note mode, in octaves */
    uint32_t prev_buttons;
    uint32_t ignore;   /* keys held across a mode switch: ignored until let go */
    uint32_t notes_on; /* note-mode keys currently sounding */
    int8_t note_of[27];
    /* From the settings menu (lsdj_input_config): */
    uint8_t knob_out[4][2];   /* {counter-clockwise, clockwise} per knob */
    uint8_t play_btn, rec_btn;
    uint32_t select_keys;     /* extra Select key(s) in button mode */
} lsdj_input_t;

static const int8_t lsdj_knob_enc[4] = {
    LSDJ_ENC_PRESETS, LSDJ_ENC_ALGORITHM, LSDJ_ENC_KNOB3, LSDJ_ENC_KNOB4 };
/* {counter-clockwise, clockwise} */
static const uint8_t lsdj_knob_out[4][2] = {
    {GP_UP, GP_DOWN}, {GP_LEFT, GP_RIGHT}, {GP_SELECT, GP_START}, {GP_B, GP_A} };

/* Settings: knobs_swapped gives PRESETS left/right and ALGORITHM up/down;
   presets_rev / algo_rev reverse those two knobs; play_is_select swaps PLAY and
   REC; select_key_mask adds Select on those piano keys in button mode. */
static inline void lsdj_input_config(lsdj_input_t *s, int knobs_swapped, int presets_rev, int algo_rev,
                                     int play_is_select, uint32_t select_key_mask) {
    for (unsigned i = 0; i < 4; i++) { s->knob_out[i][0] = lsdj_knob_out[i][0]; s->knob_out[i][1] = lsdj_knob_out[i][1]; }
    if (knobs_swapped)
        for (unsigned k = 0; k < 2; k++) { uint8_t a = s->knob_out[0][k]; s->knob_out[0][k] = s->knob_out[1][k]; s->knob_out[1][k] = a; }
    if (presets_rev) { uint8_t a = s->knob_out[0][0]; s->knob_out[0][0] = s->knob_out[0][1]; s->knob_out[0][1] = a; }
    if (algo_rev) { uint8_t a = s->knob_out[1][0]; s->knob_out[1][0] = s->knob_out[1][1]; s->knob_out[1][1] = a; }
    s->play_btn = play_is_select ? GP_SELECT : GP_START;
    s->rec_btn = play_is_select ? GP_START : GP_SELECT;
    s->select_keys = select_key_mask;
}

static inline void lsdj_input_init(lsdj_input_t *s) {
    lsdj_knob_t z = {0, 0, 0, 0};
    for (unsigned i = 0; i < 4; i++) s->knob[i] = z;
    lsdj_input_config(s, 0, 0, 0, 0, 0);
    s->mode = LSDJ_KEYS_BUTTONS; s->octave = 0;
    s->prev_buttons = 0; s->ignore = 0; s->notes_on = 0;
}

static inline uint32_t lsdj_knob_frame(lsdj_knob_t *k, const uint8_t out[2], int32_t steps) {
    if (steps) {
        int8_t d = steps > 0 ? 1 : -1;
        uint32_t n = (uint32_t)(steps > 0 ? steps : -steps);
        if (d != k->dir) { k->dir = d; k->queue = 0; }  /* turning back cancels the backlog */
        n += k->queue;
        k->queue = (uint8_t)(n > LSDJ_TAP_QUEUE ? LSDJ_TAP_QUEUE : n);
    }
    if (!k->t && k->queue) { k->queue--; k->t = 1; k->out = out[k->dir > 0]; }
    if (!k->t) return 0;
    uint32_t p = k->t <= LSDJ_TAP_DOWN ? k->out : 0;
    if (++k->t > LSDJ_TAP_DOWN + LSDJ_TAP_UP) k->t = 0;
    return p;
}

static inline void lsdj_notes_off(lsdj_input_t *s) {
    for (unsigned k = 0; k < 27; k++)
        if (s->notes_on & (1u << k)) LSDJ_KEYBD_NOTE(s->note_of[k], 0);
    s->notes_on = 0;
}

static inline int lsdj_pressed(uint32_t now, uint32_t before, int id) {
    return id >= 0 && (now & ~before & (1u << id));
}

/* Call once per emulated frame. notes: 27-bit key mask (fm1_in.notes),
   buttons: 14-bit panel mask (fm1_in.buttons), enc: detent steps per encoder
   since the last call (+ = clockwise). Returns Game Boy pressed bits. */
static inline uint32_t lsdj_input_frame(lsdj_input_t *s, uint32_t notes,
                                        uint32_t buttons, const int32_t enc[7]) {
    uint32_t before = s->prev_buttons;
    s->prev_buttons = buttons;
    if (lsdj_pressed(buttons, before, LSDJ_BTN_HOME)) {
        if (s->mode == LSDJ_KEYS_NOTES) lsdj_notes_off(s);
        s->mode ^= 1u;
        s->ignore = notes;
    }
    s->ignore &= notes;
    uint32_t keys = notes & ~s->ignore;

    uint32_t p = 0;
    if (s->mode == LSDJ_KEYS_BUTTONS) {
        p |= gp_input_keys(keys, 0);
        if (keys & s->select_keys) p |= GP_SELECT;
    } else {
        if (lsdj_pressed(buttons, before, LSDJ_BTN_OCT_DOWN) && s->octave > -2) s->octave--;
        if (lsdj_pressed(buttons, before, LSDJ_BTN_OCT_UP) && s->octave < 2) s->octave++;
        uint32_t on = keys & ~s->notes_on, off = s->notes_on & ~keys;
        for (unsigned k = 0; k < 27; k++) {
            if (off & (1u << k)) LSDJ_KEYBD_NOTE(s->note_of[k], 0);
            if (on & (1u << k)) {
                s->note_of[k] = (int8_t)(LSDJ_NOTE_BASE + 12 * s->octave + (int)k);
                LSDJ_KEYBD_NOTE(s->note_of[k], 1);
            }
        }
        s->notes_on = keys;
    }
    if (buttons & (1u << LSDJ_BTN_PLAY)) p |= s->play_btn;
    if (buttons & (1u << LSDJ_BTN_REC)) p |= s->rec_btn;
    for (unsigned i = 0; i < 4; i++) {
        int e = lsdj_knob_enc[i];
        p |= lsdj_knob_frame(&s->knob[i], s->knob_out[i], e >= 0 ? enc[e] : 0);
    }
    return p;  /* gp_buttons() cancels opposite directions */
}
