/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_tempo.h. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_tempo.h"
#include "lsdj_overlay.h"

static void fits(const char *t, unsigned max) { assert(strlen(t) <= max); for (const char *c = t; *c; c++) assert(strchr(lsdj_ov_chars, *c)); }

int main(void) {
    /* The period matches what LSDj 9.3.9 itself works out (emulator, PROJECT > TEMPO). */
    assert(lsdj_tempo_period(170) == 0x2A29 && lsdj_tempo_period(171) == 0x29EA && lsdj_tempo_period(180) == 0x27D1);
    assert(lsdj_tempo_period(190) == 0x25B9 && lsdj_tempo_period(90) == 0x4FA3);
    assert(lsdj_tempo_period(40) > lsdj_tempo_period(41) && lsdj_tempo_period(295) > 6000);
    /* Tempo bytes: LSDj's 40..295 in a byte. */
    for (unsigned b = 40; b <= 295; b++) assert(lsdj_tempo_of_byte(lsdj_tempo_byte(b)) == b);

    lsdj_tempo_t t; memset(&t, 0, sizeof t);
    /* Knobs: K1 1 BPM, K2 10, clamped to 40..295. */
    assert(lsdj_tempo_press(&t, 170, 1000) == 0);
    assert(lsdj_tempo_turn(&t, 3, 0) == 173 && lsdj_tempo_turn(&t, 0, -2) == 153);
    assert(lsdj_tempo_turn(&t, 0, -50) == 40 && lsdj_tempo_turn(&t, 0, -1) == 0);
    assert(lsdj_tempo_turn(&t, 0, 99) == 295);
    lsdj_tempo_release(&t);
    /* Turning made that press no tap: the next press alone is not a tempo. */
    assert(lsdj_tempo_press(&t, 120, 1300) == 0); lsdj_tempo_release(&t);
    /* Taps 500 ms apart = 120 BPM; 4 gaps averaged; a 2 s pause starts again. */
    memset(&t, 0, sizeof t);
    assert(lsdj_tempo_press(&t, 90, 10000) == 0); lsdj_tempo_release(&t);
    assert(lsdj_tempo_press(&t, 90, 10500) == 120); lsdj_tempo_release(&t);
    assert(lsdj_tempo_press(&t, 120, 11010) == 119); lsdj_tempo_release(&t);   /* 1010 ms / 2 */
    lsdj_tempo_press(&t, 120, 11500); lsdj_tempo_release(&t);
    lsdj_tempo_press(&t, 120, 12000); lsdj_tempo_release(&t);
    assert(t.taps == 5 && t.bpm == 120);
    assert(lsdj_tempo_press(&t, 120, 12400) > 120 && t.taps == 5);           /* oldest dropped, faster */
    lsdj_tempo_release(&t);
    assert(lsdj_tempo_press(&t, 120, 16000) == 0 && t.taps == 1);            /* pause: a new run */
    lsdj_tempo_release(&t);
    /* Very slow / fast taps clamp. */
    memset(&t, 0, sizeof t); lsdj_tempo_press(&t, 120, 0); assert(lsdj_tempo_press(&t, 120, 1900) == 40);
    memset(&t, 0, sizeof t); lsdj_tempo_press(&t, 120, 0); assert(lsdj_tempo_press(&t, 120, 100) == 295);
    /* Pop-up text. */
    char title[20], big[20], label[4][7], value[4][6], caption[4][4]; uint8_t level[4];
    t.bpm = 170; lsdj_tempo_bars(&t, 0, title, big, label, value, caption, level);
    assert(!strcmp(title, "TEMPO 170") && !strcmp(label[1], "10") && !label[2][0]);
    fits(title, 17); fits(big, 19); for (int k = 0; k < 4; k++) { fits(label[k], 5); fits(value[k], 5); }
    t.bpm = 40; lsdj_tempo_bars(&t, 1, title, big, label, value, caption, level);
    assert(!strcmp(title, "TEMPO 40") && level[0] == 0); fits(big, 19);
    puts("PASS: tempo: LSDj's period, byte encoding, K1/K2, tap tempo (average, restart, clamp), pop-up");
    return 0;
}
