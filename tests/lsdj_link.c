/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_link.h, with fake hardware: info, song export,
   import (good, bad crc, flash failure, out of order, timeout), settings,
   effects, custom theme, store, live data; every reply is valid 7-bit SysEx. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_link.h"

#define SIZE 131072u
static uint8_t ram[SIZE], flash[SIZE];
static int paused, resets, fail_store, stores, config_stores;
static uint32_t settings = 0x5A000000u, fxw[LSDJ_LINK_FX_WORDS];
static uint16_t theme[8];
static uint8_t *io_save(void) { return ram; }
static uint32_t io_size(void) { return SIZE; }
static void io_pause(int on) { paused = on; }
static int io_store(void) { if (fail_store) return -1; memcpy(flash, ram, SIZE); stores++; return 0; }
static int io_load(void) { memcpy(ram, flash, SIZE); return 0; }
static void io_reset(void) { resets++; }
static const char *io_fw(void) { return "FM-1_955"; }
static const char *io_title(void) { return "LSDj-v9.3.9"; }
static int io_known(void) { return 1; }
static uint32_t io_sget(void) { return settings; }
static int io_sset(uint32_t v) { if (v >> 24 != 0x5A) return 0; settings = v; return 1; }
static void io_fget(uint32_t w[LSDJ_LINK_FX_WORDS]) { memcpy(w, fxw, sizeof fxw); }
static int io_fset(const uint32_t *w, unsigned n) {
    if ((n != LSDJ_LINK_FX_WORDS && n != LSDJ_LINK_FX_WORDS_OLD) || ((w[10] >> 16) & 0xFF) != 0xF2) return 0;
    memcpy(fxw, w, 4 * n); return 1;   /* 11 words replace the first 11, the rest stay (as board.c does) */
}
static void io_tget(uint16_t c[8]) { memcpy(c, theme, sizeof theme); }
static void io_tset(const uint16_t c[8]) { memcpy(theme, c, sizeof theme); }
static int io_cstore(void) { config_stores++; return 0; }
static void io_live(lsdj_link_live_t *l) { l->page = 2; l->cursor_ch = 3; l->bpm = 170; l->mute = 5; l->freeze = 1; l->playing = 0xF; l->battery = 0x82; }
static uint32_t midi_word = 0x4D000009u;
static uint32_t io_mget(void) { return midi_word; }
static int io_mset(uint32_t v) { if (v >> 24 != 0x4D) return 0; midi_word = v; return 1; }
static uint8_t cartridge[256 + 2 * 16384];
static int cart_available = 1;
static const volatile uint8_t *io_cart(uint32_t *len) { *len = sizeof cartridge; return cart_available ? cartridge : 0; }
static uint8_t mute_now, freeze_now;
static int io_mute(uint8_t m, uint8_t f) { if (m > 15 || f > 1) return 0; mute_now = m; freeze_now = f; return 1; }
static uint32_t feat_word = 0x460001C6u;
static uint32_t io_eget(void) { return feat_word; }
static int io_eset(uint32_t v) { if (v >> 24 != 0x46) return 0; feat_word = v; return 1; }
static uint16_t pic[LSDJ_LINK_SCREEN_W * LSDJ_LINK_SCREEN_H];
/* FM-1_987: the link reads the LCD image; this scales pic as src/video_job.h gb_video_scale does. */
static uint16_t lcd_img[240u * 216u];
static const uint16_t *scaled(void) {
    for (unsigned y = 0; y < 216u; y++) for (unsigned x = 0; x < 240u; x++) {
        uint16_t c = pic[(y * 2u / 3u) * LSDJ_LINK_SCREEN_W + (x / 3u) * 2u + (x % 3u == 2u)];
        lcd_img[y * 240u + x] = (uint16_t)(c >> 8 | c << 8);
    }
    return lcd_img;
}
static const volatile uint16_t *io_screen(uint32_t *shown) { *shown = 4242; return scaled(); }
static uint32_t stream_bands = 0xDEAD; static int stream_ok = 1;   /* 0: as over Bluetooth */
static uint8_t stream_round;
static int io_stream(uint32_t bands, uint8_t round) { if (!stream_ok) return 0; stream_bands = bands; stream_round = round; return 1; }
static const lsdj_link_io_t io = {io_save, io_size, io_pause, io_store, io_load, io_reset, io_fw, io_title, io_known,
                                  io_sget, io_sset, io_fget, io_fset, io_tget, io_tset, io_cstore, io_live, 0, io_mget, io_mset, io_cart, io_mute,
                                  io_eget, io_eset, io_screen, io_stream};
/* A SCREEN_PIXELS message -> row (or -1), px; checks the framing. Status in *status. */
static int unpixels(const uint8_t *m, uint32_t n, uint16_t px[LSDJ_LINK_SCREEN_W], unsigned *status) {
    assert(n >= 10 && m[0] == 0xf0 && m[1] == 0x7d && m[2] == 'G' && m[3] == 'S' && m[4] == LSDJ_LINK_SCREEN_PIXELS && m[n - 1] == 0xf7);
    for (uint32_t i = 1; i < n - 1; i++) assert(m[i] < 128);
    *status = m[5];
    assert(m[6] == 99);   /* the round */
    uint32_t acc = 0, i = 7; unsigned bits = 0;
#define TAKE(w, out) do { while (bits < (w)) { assert(i < n - 1); acc |= (uint32_t)m[i++] << bits; bits += 7; } (out) = acc & ((1u << (w)) - 1u); acc >>= (w); bits -= (w); } while (0)
    unsigned row, np1, pal[16]; TAKE(8, row);
    if (*status) { assert(i == n - 1); return (int)row; }
    TAKE(4, np1); unsigned np = np1 + 1, w = np > 8 ? 4 : np > 4 ? 3 : np > 2 ? 2 : np > 1 ? 1 : 0;
    for (unsigned k = 0; k < np; k++) TAKE(16, pal[k]);
    for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x++) { unsigned k = 0; if (w) TAKE(w, k); assert(k < np); px[x] = (uint16_t)pal[k]; }
    assert(i == n - 1 && bits < 7);   /* nothing left over */
#undef TAKE
    return (int)row;
}
/* firmware before FM-1_979: no feature switches */
static const lsdj_link_io_t io_old = {io_save, io_size, io_pause, io_store, io_load, io_reset, io_fw, io_title, io_known,
                                      io_sget, io_sset, io_fget, io_fset, io_tget, io_tset, io_cstore, io_live, 0, io_mget, io_mset, io_cart, io_mute};
static lsdj_link_t st;
static uint8_t reply[LSDJ_LINK_REPLY_MAX];
static uint32_t rn;

static int req(uint8_t op, const uint8_t *data, uint32_t n, uint32_t now) {
    uint8_t q[4 + 2 * 140] = {0x7d, 'G', 'S', op};
    for (uint32_t i = 0; i < n; i++) { q[4 + 2 * i] = data[i] & 127; q[5 + 2 * i] = data[i] >> 7; }
    assert(lsdj_link_match(q, 4 + 2 * n));
    rn = lsdj_link_handle(&st, &io, q, 4 + 2 * n, now, reply);
    assert(reply[0] == 0xf0 && reply[4] == op && reply[rn - 1] == 0xf7 && rn <= LSDJ_LINK_REPLY_MAX);
    for (uint32_t i = 1; i < rn - 1; i++) assert(reply[i] < 128);
    return reply[5];
}
static uint32_t rd(uint32_t i) { return reply[6 + 2 * i] | (uint32_t)reply[7 + 2 * i] << 7; }
static uint32_t rd16(uint32_t i) { return rd(i) | rd(i + 1) << 8; }
static uint32_t rd32(uint32_t i) { return rd(i) | rd(i + 1) << 8 | rd(i + 2) << 16 | rd(i + 3) << 24; }
static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static int begin(uint32_t len, uint32_t crc, uint32_t now) { uint8_t b[8]; le32(b, len); le32(b + 4, crc); return req(LSDJ_LINK_BEGIN, b, 8, now); }
static int chunk(const uint8_t *src, uint32_t off, uint32_t now) {
    uint8_t b[4 + LSDJ_LINK_CHUNK]; le32(b, off); memcpy(b + 4, src + off, LSDJ_LINK_CHUNK);
    return req(LSDJ_LINK_CHUNK_OP, b, sizeof b, now);
}

int main(void) {
    static uint8_t song[SIZE], other[SIZE], got[SIZE];
    for (uint32_t i = 0; i < SIZE; i++) { song[i] = (uint8_t)(i * 31 + 7); other[i] = (uint8_t)(i ^ 0xa5); }
    memcpy(ram, song, SIZE); memcpy(flash, song, SIZE);
    for (unsigned i = 0; i < sizeof cartridge; i++) cartridge[i] = (uint8_t)(i * 19u);
    assert(req(LSDJ_LINK_CART_INFO, 0, 0, 0) == LSDJ_LINK_OK);
    assert(rd32(0) == sizeof cartridge && rd32(4) == lsdj_link_le32(cartridge + 252) && rd32(8) == lsdj_link_le32(cartridge + 16));
    for (uint32_t off = 0; off < sizeof cartridge; off += LSDJ_LINK_CART_CHUNK) {
        uint8_t b[4]; le32(b, off);
        for (unsigned retry = 0; retry < 2; retry++) {
            assert(req(LSDJ_LINK_CART_READ, b, 4, 0) == LSDJ_LINK_OK && rd32(0) == off);
            unsigned count = sizeof cartridge - off;
            if (count > LSDJ_LINK_CART_CHUNK) count = LSDJ_LINK_CART_CHUNK;
            assert(rn == 7 + 2 * (count + 4));
            for (unsigned i = 0; i < count; i++) assert(rd(4 + i) == cartridge[off + i]);
        }
    }
    { uint8_t b[4];
      const uint32_t bad[] = {1, sizeof cartridge, 0xffffffb0u, 0xffffffffu};
      for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
          le32(b, bad[i]); assert(req(LSDJ_LINK_CART_READ, b, 4, 0) == LSDJ_LINK_BAD);
      }
      assert(req(LSDJ_LINK_CART_READ, b, 3, 0) == LSDJ_LINK_BAD);
      assert(req(LSDJ_LINK_CART_INFO, b, 1, 0) == LSDJ_LINK_BAD);
      cart_available = 0;
      assert(req(LSDJ_LINK_CART_INFO, 0, 0, 0) == LSDJ_LINK_BAD);
      assert(req(LSDJ_LINK_CART_READ, b, 4, 0) == LSDJ_LINK_BAD);
      cart_available = 1;
    }
    assert(!paused && !resets && !stores && !config_stores);
    assert(!memcmp(ram, song, SIZE) && !memcmp(flash, song, SIZE));

    /* INFO */
    assert(req(LSDJ_LINK_INFO, 0, 0, 0) == LSDJ_LINK_OK);
    assert(rd(0) == LSDJ_LINK_PROTOCOL);
    { char fw[9] = {0}, ti[17] = {0}; for (int i = 0; i < 8; i++) fw[i] = (char)rd(1 + i); for (int i = 0; i < 16; i++) ti[i] = (char)rd(9 + i);
      assert(!strcmp(fw, "FM-1_955") && !strcmp(ti, "LSDj-v9.3.9")); }
    assert(rd32(25) == SIZE && rd32(29) == lsdj_link_crc32(song, SIZE) && rd(33) == 0 && rd(34) == 1);

    /* Export */
    for (uint32_t off = 0; off < SIZE; off += LSDJ_LINK_CHUNK) {
        uint8_t b[4]; le32(b, off); assert(req(LSDJ_LINK_READ, b, 4, 0) == LSDJ_LINK_OK);
        for (uint32_t i = 0; i < LSDJ_LINK_CHUNK; i++) got[off + i] = (uint8_t)rd(i);
    }
    assert(!memcmp(got, song, SIZE));
    { uint8_t b[4]; le32(b, 100); assert(req(LSDJ_LINK_READ, b, 4, 0) == LSDJ_LINK_BAD); }

    /* Import: stored first, chunks in order, commit restarts LSDj */
    assert(begin(SIZE, lsdj_link_crc32(other, SIZE), 0) == LSDJ_LINK_OK && paused && stores == 1);
    assert(req(LSDJ_LINK_READ, (const uint8_t[4]){0}, 4, 0) == LSDJ_LINK_STATE);
    assert(req(LSDJ_LINK_CART_INFO, 0, 0, 0) == LSDJ_LINK_STATE);
    assert(req(LSDJ_LINK_CART_READ, (const uint8_t[4]){0}, 4, 0) == LSDJ_LINK_STATE);
    assert(chunk(other, 256, 1) == LSDJ_LINK_BAD);                       /* out of order */
    for (uint32_t off = 0; off < SIZE; off += LSDJ_LINK_CHUNK) assert(chunk(other, off, 2) == LSDJ_LINK_OK);
    assert(chunk(other, SIZE - LSDJ_LINK_CHUNK, 3) == LSDJ_LINK_OK);      /* the last one again */
    assert(req(LSDJ_LINK_COMMIT, 0, 0, 4) == LSDJ_LINK_OK && !paused && resets == 1 && !memcmp(flash, other, SIZE));
    /* Bad crc: the stored songs come back */
    memcpy(ram, other, SIZE);
    assert(begin(SIZE, 12345, 10) == LSDJ_LINK_OK);
    for (uint32_t off = 0; off < SIZE; off += LSDJ_LINK_CHUNK) chunk(song, off, 11);
    assert(req(LSDJ_LINK_COMMIT, 0, 0, 12) == LSDJ_LINK_CRC && !memcmp(ram, other, SIZE) && !paused);
    /* Flash failing at BEGIN: nothing starts */
    fail_store = 1; assert(begin(SIZE, 1, 20) == LSDJ_LINK_FLASH && !paused && !st.importing); fail_store = 0;
    /* A page that goes away: aborted after the timeout */
    assert(begin(SIZE, 1, 100) == LSDJ_LINK_OK); chunk(song, 0, 101); assert(ram[0] == song[0]);
    lsdj_link_poll(&st, &io, 101 + LSDJ_LINK_TIMEOUT_MS / 2); assert(st.importing);
    lsdj_link_poll(&st, &io, 102 + LSDJ_LINK_TIMEOUT_MS); assert(!st.importing && !paused && !memcmp(ram, other, SIZE));

    /* Settings */
    assert(req(LSDJ_LINK_SETTINGS_GET, 0, 0, 0) == LSDJ_LINK_OK && rd32(0) == 0x5A000000u);
    { uint8_t b[4]; le32(b, 0x5A000041u); assert(req(LSDJ_LINK_SETTINGS_SET, b, 4, 0) == LSDJ_LINK_OK && settings == 0x5A000041u);
      le32(b, 0x12345678u); assert(req(LSDJ_LINK_SETTINGS_SET, b, 4, 0) == LSDJ_LINK_BAD && settings == 0x5A000041u); }
    /* Effects */
    { uint8_t b[68]; for (int i = 0; i < 68; i++) b[i] = (uint8_t)i; b[42] = 0xF2;
      assert(req(LSDJ_LINK_FX_SET, b, 68, 0) == LSDJ_LINK_OK);
      assert(req(LSDJ_LINK_FX_GET, 0, 0, 0) == LSDJ_LINK_OK);
      for (int i = 0; i < 68; i++) assert(rd(i) == b[i]);
      b[42] = 0; assert(req(LSDJ_LINK_FX_SET, b, 68, 0) == LSDJ_LINK_BAD);
      /* 11 words (a page from before alpha 66): the first 11 replaced, words 11..16 kept */
      uint8_t o[44]; for (int i = 0; i < 44; i++) o[i] = (uint8_t)(0x80 + i); o[42] = 0xF2;
      assert(req(LSDJ_LINK_FX_SET, o, 44, 0) == LSDJ_LINK_OK);
      assert(req(LSDJ_LINK_FX_GET, 0, 0, 0) == LSDJ_LINK_OK);
      for (int i = 0; i < 44; i++) assert(rd(i) == o[i]);
      for (int i = 44; i < 68; i++) assert(rd(i) == b[i]);
      assert(req(LSDJ_LINK_FX_SET, b, 60, 0) == LSDJ_LINK_BAD);   /* neither 11 nor 17 words */
      b[42] = 0xF2; assert(req(LSDJ_LINK_FX_SET, b, 68, 0) == LSDJ_LINK_OK); }
    /* Custom theme */
    { uint8_t b[16]; for (int i = 0; i < 16; i++) b[i] = (uint8_t)(0xF0 + i);
      assert(req(LSDJ_LINK_THEME_SET, b, 16, 0) == LSDJ_LINK_OK && theme[3] == (uint16_t)(0xF6 | 0xF7 << 8));
      assert(req(LSDJ_LINK_THEME_GET, 0, 0, 0) == LSDJ_LINK_OK && rd16(14) == (0xFE | 0xFF << 8)); }
    /* Store, live */
    assert(req(LSDJ_LINK_STORE, 0, 0, 0) == LSDJ_LINK_OK && config_stores == 1);
    assert(req(LSDJ_LINK_LIVE, 0, 0, 0) == LSDJ_LINK_OK);
    assert(rd(0) == 2 && rd(1) == 3 && rd16(2) == 170 && rd(4) == 5 && rd(5) == 1 && rd(6) == 0xF && rd(7) == 0 && rd(7 + 42) == 0xF2);
    assert(rd(7 + 4 * LSDJ_LINK_FX_WORDS) == 0x82 && rn == 6u + 2u * (8u + 4u * LSDJ_LINK_FX_WORDS) + 1u);   /* battery: 2 bars, USB */
    assert(req(LSDJ_LINK_BT, 0, 0, 0) == LSDJ_LINK_BAD);   /* no Bluetooth in this build */
    { uint8_t b[4] = {0x01, 0x80, 0x02, 0x4D};
      assert(req(LSDJ_LINK_MIDI_SET, b, 4, 0) == LSDJ_LINK_OK && midi_word == 0x4D028001u);
      assert(req(LSDJ_LINK_MIDI_GET, 0, 0, 0) == LSDJ_LINK_OK && rd(0) == 0x01 && rd(3) == 0x4D);
      uint8_t bad[4] = {0, 0, 0, 0x5A}; assert(req(LSDJ_LINK_MIDI_SET, bad, 4, 0) == LSDJ_LINK_BAD); }
    /* FEAT_GET / FEAT_SET: the feature switches (FM-1_979); older firmware answers status 1 */
    { uint8_t b[4] = {0x03, 0x02, 0x00, 0x46};
      assert(req(LSDJ_LINK_FEAT_GET, 0, 0, 0) == LSDJ_LINK_OK && rd32(0) == 0x460001C6u);
      assert(req(LSDJ_LINK_FEAT_SET, b, 4, 0) == LSDJ_LINK_OK && feat_word == 0x46000203u);
      assert(req(LSDJ_LINK_FEAT_GET, 0, 0, 0) == LSDJ_LINK_OK && rd32(0) == 0x46000203u);
      uint8_t bad[4] = {0, 0, 0, 0x4D}; assert(req(LSDJ_LINK_FEAT_SET, bad, 4, 0) == LSDJ_LINK_BAD && feat_word == 0x46000203u);
      assert(req(LSDJ_LINK_FEAT_SET, b, 3, 0) == LSDJ_LINK_BAD && req(LSDJ_LINK_FEAT_GET, b, 1, 0) == LSDJ_LINK_BAD);
      uint8_t q[4 + 8] = {0x7d, 'G', 'S', LSDJ_LINK_FEAT_GET};
      rn = lsdj_link_handle(&st, &io_old, q, 4, 0, reply); assert(reply[5] == LSDJ_LINK_BAD);
      q[3] = LSDJ_LINK_FEAT_SET; for (int i = 0; i < 4; i++) { q[4 + 2 * i] = b[i] & 127; q[5 + 2 * i] = b[i] >> 7; }
      rn = lsdj_link_handle(&st, &io_old, q, 12, 0, reply); assert(reply[5] == LSDJ_LINK_BAD); }
    /* MUTE_SET: the web page's channel buttons and freeze */
    { uint8_t m[2] = {5, 1}; assert(req(LSDJ_LINK_MUTE_SET, m, 2, 0) == LSDJ_LINK_OK && mute_now == 5 && freeze_now == 1);
      uint8_t bad[2] = {16, 0}; assert(req(LSDJ_LINK_MUTE_SET, bad, 2, 0) == LSDJ_LINK_BAD && mute_now == 5);
      assert(req(LSDJ_LINK_MUTE_SET, m, 1, 0) == LSDJ_LINK_BAD); }
    /* SCREEN_INFO / SCREEN_ROW: the picture, band hashes, rows as colours + 4-bit indices */
    { for (unsigned i = 0; i < LSDJ_LINK_SCREEN_W * LSDJ_LINK_SCREEN_H; i++) pic[i] = (uint16_t)(0x1000u * ((i / 7u) % 4u) + 0x0FFFu);
      assert(req(LSDJ_LINK_SCREEN_INFO, 0, 0, 0) == LSDJ_LINK_OK && rn == 6u + 2u * (4u + 2u * LSDJ_LINK_SCREEN_BANDS) + 1u && rd32(0) == 4242);
      uint16_t h[LSDJ_LINK_SCREEN_BANDS]; for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) h[b] = (uint16_t)rd16(4 + 2 * b);
      pic[5 * 8 * LSDJ_LINK_SCREEN_W + 3 * LSDJ_LINK_SCREEN_W + 77] ^= 0x0101u;   /* one pixel in band 5 */
      assert(req(LSDJ_LINK_SCREEN_INFO, 0, 0, 0) == LSDJ_LINK_OK);
      for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) assert((rd16(4 + 2 * b) == h[b]) == (b != 5));
      for (unsigned y = 0; y < LSDJ_LINK_SCREEN_H; y++) {
          uint8_t a = (uint8_t)y; assert(req(LSDJ_LINK_SCREEN_ROW, &a, 1, 0) == LSDJ_LINK_OK && rd(0) == y);
          unsigned np = rd(1); assert(np >= 1 && np <= 16 && rn == 6u + 2u * (2u + 2u * np + 80u) + 1u);
          for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x++) {
              unsigned k = (rd(2 + 2 * np + x / 2) >> (4 * (x & 1))) & 15u;
              assert(k < np && rd16(2 + 2 * k) == pic[y * LSDJ_LINK_SCREEN_W + x]);
          }
      }
      for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x++) pic[x] = (uint16_t)((x % 16u) * 0x111u);   /* 16 colours: still fine */
      { uint8_t a = 0; assert(req(LSDJ_LINK_SCREEN_ROW, &a, 1, 0) == LSDJ_LINK_OK && rd(1) == 16); }
      pic[16] = 0xBEEF; pic[17] = 0xBEEF;   /* 17: status 2, the page skips the row */
      { uint8_t a = 0; assert(req(LSDJ_LINK_SCREEN_ROW, &a, 1, 0) == LSDJ_LINK_STATE); }
      { uint8_t a = 144; assert(req(LSDJ_LINK_SCREEN_ROW, &a, 1, 0) == LSDJ_LINK_BAD); }
      assert(req(LSDJ_LINK_SCREEN_ROW, 0, 0, 0) == LSDJ_LINK_BAD && req(LSDJ_LINK_SCREEN_INFO, (const uint8_t[1]){0}, 1, 0) == LSDJ_LINK_BAD);
      uint8_t q[6] = {0x7d, 'G', 'S', LSDJ_LINK_SCREEN_INFO};   /* firmware before FM-1_983 */
      rn = lsdj_link_handle(&st, &io_old, q, 4, 0, reply); assert(reply[5] == LSDJ_LINK_BAD);
      q[3] = LSDJ_LINK_SCREEN_ROW; rn = lsdj_link_handle(&st, &io_old, q, 6, 0, reply); assert(reply[5] == LSDJ_LINK_BAD); }
    /* SCREEN_SYNC / SCREEN_PIXELS (FM-1_985): the bands that differ, then their rows packed */
    { for (unsigned i = 0; i < LSDJ_LINK_SCREEN_W * LSDJ_LINK_SCREEN_H; i++) pic[i] = (uint16_t)(0x0841u * ((i * 7u / 5u) % 4u));
      uint8_t q[5 + 2 * LSDJ_LINK_SCREEN_BANDS] = {0}; le32(q, 0x3FFFFu); q[sizeof q - 1] = 200;   /* all forced: the page's first look */
      assert(req(LSDJ_LINK_SCREEN_SYNC, q, sizeof q, 0) == LSDJ_LINK_OK && rn == 6u + 2u * (4u + 2u * LSDJ_LINK_SCREEN_BANDS + 4u) + 1u);
      assert(rd32(0) == 4242 && rd32(4 + 2 * LSDJ_LINK_SCREEN_BANDS) == 0x3FFFFu && stream_bands == 0x3FFFFu && stream_round == 200);
      le32(q, 0); for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) { q[4 + 2 * b] = (uint8_t)rd(4 + 2 * b); q[5 + 2 * b] = (uint8_t)rd(5 + 2 * b); }
      assert(req(LSDJ_LINK_SCREEN_SYNC, q, sizeof q, 0) == LSDJ_LINK_OK && rd32(4 + 2 * LSDJ_LINK_SCREEN_BANDS) == 0 && stream_bands == 0);   /* the same: nothing */
      pic[17 * 8 * LSDJ_LINK_SCREEN_W + 7 * LSDJ_LINK_SCREEN_W + 159] = 0xF800u;   /* the last pixel: band 17 only */
      le32(q, 1u << 2);   /* and band 2 forced */
      assert(req(LSDJ_LINK_SCREEN_SYNC, q, sizeof q, 0) == LSDJ_LINK_OK && stream_bands == (1u << 17 | 1u << 2));
      /* rows: 4 colours = 2 bits a pixel (57 bytes + 7), one colour = no indices, 16 colours */
      uint8_t m[LSDJ_LINK_REPLY_MAX]; uint16_t px[LSDJ_LINK_SCREEN_W]; unsigned status;
      uint32_t most = 0;
      for (unsigned y = 0; y < LSDJ_LINK_SCREEN_H; y++) {
          uint32_t n = lsdj_link_screen_pixels(scaled(), y, 99, m);
          assert(unpixels(m, n, px, &status) == (int)y && status == 0 && !memcmp(px, pic + y * LSDJ_LINK_SCREEN_W, sizeof px));
          if (n > most) most = n;
      }
      assert(most == 7u + (8u + 4u + 5u * 16u + 160u * 3u + 6u) / 7u + 1u);   /* row 143: 5 colours, 3 bits */
      assert(lsdj_link_screen_pixels(scaled(), 0, 99 | 128, m) == 7u + (8u + 4u + 4u * 16u + 320u + 6u) / 7u + 1u);
      for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x++) pic[3 * LSDJ_LINK_SCREEN_W + x] = 0x1234u;
      { uint32_t n = lsdj_link_screen_pixels(scaled(), 3, 99 | 128, m); assert(n == 7u + 4u + 1u && unpixels(m, n, px, &status) == 3 && px[99] == 0x1234u); }
      for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x++) pic[4 * LSDJ_LINK_SCREEN_W + x] = (uint16_t)((x * 3u % 16u) * 0x1111u);
      { uint32_t n = lsdj_link_screen_pixels(scaled(), 4, 99 | 128, m); assert(n == 7u + (8u + 4u + 256u + 640u + 6u) / 7u + 1u && n <= 138u);
        assert(unpixels(m, n, px, &status) == 4 && status == 0 && !memcmp(px, pic + 4 * LSDJ_LINK_SCREEN_W, sizeof px)); }
      pic[4 * LSDJ_LINK_SCREEN_W + 5] = 0xBEEF;   /* 17 colours: status 2, the row only */
      { uint32_t n = lsdj_link_screen_pixels(scaled(), 4, 99 | 128, m); assert(unpixels(m, n, px, &status) == 4 && status == 2 && n == 7u + 2u + 1u); }
      { uint32_t n = lsdj_link_screen_pixels(scaled(), 143, 99 | 128, m); assert(unpixels(m, n, px, &status) == 143); }   /* row > 127: 8 bits */
      /* refused: a link that cannot stream (Bluetooth), wrong length, firmware before FM-1_985 */
      stream_ok = 0; stream_bands = 77; assert(req(LSDJ_LINK_SCREEN_SYNC, q, sizeof q, 0) == LSDJ_LINK_BAD && rn == 7 && stream_bands == 77); stream_ok = 1;
      assert(req(LSDJ_LINK_SCREEN_SYNC, q, sizeof q - 1, 0) == LSDJ_LINK_BAD && req(LSDJ_LINK_SCREEN_SYNC, 0, 0, 0) == LSDJ_LINK_BAD);
      lsdj_link_io_t io984 = io; io984.screen_stream = 0;
      uint8_t w[4 + 2 * sizeof q] = {0x7d, 'G', 'S', LSDJ_LINK_SCREEN_SYNC};
      for (unsigned i = 0; i < sizeof q; i++) { w[4 + 2 * i] = q[i] & 127; w[5 + 2 * i] = q[i] >> 7; }
      rn = lsdj_link_handle(&st, &io984, w, sizeof w, 0, reply); assert(reply[5] == LSDJ_LINK_BAD);
      rn = lsdj_link_handle(&st, &io_old, w, sizeof w, 0, reply); assert(reply[5] == LSDJ_LINK_BAD);
      w[3] = LSDJ_LINK_SCREEN_PIXELS; rn = lsdj_link_handle(&st, &io, w, 4, 0, reply); assert(reply[5] == LSDJ_LINK_BAD); }   /* never asked */
    /* FM-1_990 push: SCREEN_SYNC's force bit 31 reaches the stream; each new picture's SCREEN_FRAME */
    { uint8_t q[5 + 2 * LSDJ_LINK_SCREEN_BANDS] = {0}; le32(q, LSDJ_LINK_PUSH | 1u); q[sizeof q - 1] = 5;
      assert(req(LSDJ_LINK_SCREEN_SYNC, q, sizeof q, 0) == LSDJ_LINK_OK && (stream_bands & LSDJ_LINK_PUSH) && (stream_bands & 1u));
      assert(rd32(4 + 2 * LSDJ_LINK_SCREEN_BANDS) & 1u && !(rd32(4 + 2 * LSDJ_LINK_SCREEN_BANDS) & LSDJ_LINK_PUSH));   /* the reply: bands only */
      lsdj_link_push_t p = {0}; uint8_t m[LSDJ_LINK_REPLY_MAX]; uint32_t bands; uint16_t h[LSDJ_LINK_SCREEN_BANDS];
      assert(!lsdj_link_push_frame(&p, scaled(), 5, 0, m, &bands, h) && !bands);                 /* not pushing */
      lsdj_link_push_start(&p, scaled(), 5, 126, 1000);
      assert(p.on && p.round == 126 && !lsdj_link_push_frame(&p, scaled(), 5, 1100, m, &bands, h));   /* the same picture */
      assert(!lsdj_link_push_frame(&p, scaled(), 6, 1100, m, &bands, h) && p.shown == 6);       /* new, unchanged: nothing */
      pic[9 * 8 * LSDJ_LINK_SCREEN_W + 3 * LSDJ_LINK_SCREEN_W + 3] ^= 0xFFFFu;                   /* band 9 changes */
      uint32_t n = lsdj_link_push_frame(&p, scaled(), 70000, 1200, m, &bands, h);
      assert(bands == 1u << 9 && n == 7u + (32u + 18u + 16u * LSDJ_LINK_SCREEN_BANDS + 6u) / 7u + 1u && n <= 60u);
      assert(m[0] == 0xF0 && m[4] == LSDJ_LINK_SCREEN_FRAME && m[5] == 0 && m[6] == 127 && m[n - 1] == 0xF7);
      { uint32_t acc = 0, bits = 0, i = 7; uint32_t v[4 + LSDJ_LINK_SCREEN_BANDS]; const unsigned w[4] = {16, 16, 16, 2};
        for (unsigned f = 0; f < 4u + LSDJ_LINK_SCREEN_BANDS; f++) {
            unsigned wd = f < 4 ? w[f] : 16u;
            while (bits < wd) { assert(m[i] < 128); acc |= (uint32_t)m[i++] << bits; bits += 7; }
            v[f] = acc & ((1u << wd) - 1u); acc >>= wd; bits -= wd; }
        assert(i == n - 1u && (v[0] | v[1] << 16) == 70000u && (v[2] | v[3] << 16) == 1u << 9);
        for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) assert(v[4 + b] == lsdj_link_band_hash(scaled(), b)); }
      assert(lsdj_link_push_sent(&p, 70000, bands, h) == 127 && p.hash[9] == h[9] && p.shown == 70000);
      assert(!lsdj_link_push_frame(&p, scaled(), 70001, 1300, m, &bands, h));                   /* sent: the page has it */
      pic[0] ^= 0xFFFFu;
      assert(lsdj_link_push_frame(&p, scaled(), 70002, 1300, m, &bands, h) && bands == 1u && m[6] == 0);   /* round wraps */
      assert(lsdj_link_push_sent(&p, 70002, bands, h) == 0);
      pic[0] ^= 0xFFFFu;
      assert(!lsdj_link_push_frame(&p, scaled(), 70003, 1000 + LSDJ_LINK_PUSH_MS + 1, m, &bands, h) && !p.on); }   /* not renewed */
    /* Junk */
    assert(req(0x7E, 0, 0, 0) == LSDJ_LINK_BAD);
    assert(req(LSDJ_LINK_INFO, (const uint8_t[1]){1}, 1, 0) == LSDJ_LINK_BAD);
    { uint8_t q[4] = {0x7d, 'G', 'D', 1}; assert(!lsdj_link_match(q, 4)); }
    puts("PASS: web link: info, song export/import (crc, flash, order, timeout), settings, effects, theme, store, live, screen, screen sync, screen push");
    return 0;
}
