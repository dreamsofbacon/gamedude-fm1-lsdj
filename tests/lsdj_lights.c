/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_lights.h. */
#include <assert.h>
#include <stdio.h>
#include "lsdj_lights.h"

int main(void) {
    uint8_t off[4] = {0, 0, 0, 0}, rows[4] = {0, 0, 0, 0};
    /* Stopped: lit, whatever the rows and mode. */
    for (int r = 0; r < 16; r++) { rows[0] = (uint8_t)r; assert(lsdj_play_light(off, rows, 0) && lsdj_play_light(off, rows, 1)); }
    /* Song mode, all playing: lit on rows 0 4 8 12 only; a bar = 4 flashes. */
    uint8_t all[4] = {1, 1, 1, 1}; int flashes = 0;
    for (int r = 0; r < 16; r++) {
        rows[0] = rows[1] = rows[2] = rows[3] = (uint8_t)r;
        int on = lsdj_play_light(all, rows, 0);
        assert(on == (r % 4 == 0));
        flashes += on;
        assert(lsdj_play_light(all, rows, 1) == !on);   /* live: the inverse */
    }
    assert(flashes == 4);
    /* Live mode, only WAV playing (row 5): follows WAV, not the idle PU1 row. */
    uint8_t wav[4] = {0, 0, 1, 0}; uint8_t r2[4] = {0, 0, 5, 0};
    assert(lsdj_play_light(wav, r2, 1) == 1);   /* row 5: not a beat, live -> lit */
    r2[2] = 8; assert(lsdj_play_light(wav, r2, 1) == 0);
    /* Row values above 15 (another counter's high bits): only the low two bits count. */
    r2[2] = 0x14; assert(lsdj_play_light(wav, r2, 0) == 1);
    /* D-pad: stopped nothing; a bar = up, right, down, left on rows 0 4 8 12 only. */
    { uint8_t r[4] = {0, 0, 0, 0}; assert(lsdj_dpad_soft(off, r) == 0); }
    { const uint32_t want[4] = {LSDJ_DPAD_UP, LSDJ_DPAD_RIGHT, LSDJ_DPAD_DOWN, LSDJ_DPAD_LEFT}; uint32_t seen = 0;
      for (int r = 0; r < 16; r++) {
          uint8_t rr[4] = {(uint8_t)r, (uint8_t)r, (uint8_t)r, (uint8_t)r};
          uint32_t m = lsdj_dpad_soft(all, rr);
          assert(m == (r % 4 ? 0u : want[r / 4]));
          assert(!(seen & m)); seen |= m;
      }
      assert(seen == (LSDJ_DPAD_UP | LSDJ_DPAD_RIGHT | LSDJ_DPAD_DOWN | LSDJ_DPAD_LEFT));
      /* exactly the keys gp_input_keys maps to the D-pad: F3 G3 G#3 A3 A#3 B3 C4 */
      assert(seen == ((1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7))); }
    puts("PASS: PLAY light: lit when stopped, song flashes on the beat, live dips on the beat; D-pad keys take turns on the beat");
    return 0;
}
