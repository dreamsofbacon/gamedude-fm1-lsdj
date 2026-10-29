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
static const lsdj_link_io_t io = {io_save, io_size, io_pause, io_store, io_load, io_reset, io_fw, io_title, io_known,
                                  io_sget, io_sset, io_fget, io_fset, io_tget, io_tset, io_cstore, io_live, 0, io_mget, io_mset, io_cart, io_mute,
                                  io_eget, io_eset};
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
    /* Junk */
    assert(req(0x7E, 0, 0, 0) == LSDJ_LINK_BAD);
    assert(req(LSDJ_LINK_INFO, (const uint8_t[1]){1}, 1, 0) == LSDJ_LINK_BAD);
    { uint8_t q[4] = {0x7d, 'G', 'D', 1}; assert(!lsdj_link_match(q, 4)); }
    puts("PASS: web link: info, song export/import (crc, flash, order, timeout), settings, effects, theme, store, live");
    return 0;
}
