/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_sysex.h: save export, import, bad CRC, flash
   failure, out-of-order chunks and a host that disappears mid-import. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_sysex.h"

#define SIZE 131072u
static uint8_t ram[SIZE], flash[SIZE];
static int paused, resets, fail_store, stores;
static uint8_t *io_save(void) { return ram; }
static uint32_t io_size(void) { return SIZE; }
static void io_pause(int on) { paused = on; }
static int io_store(const uint8_t *p, uint32_t n) { if (fail_store) return -1; memcpy(flash, p, n); stores++; return 0; }
static int io_load(uint8_t *p, uint32_t n) { memcpy(p, flash, n); return 0; }
static void io_reset(void) { resets++; }
static const lsdj_sx_io_t io = {io_save, io_size, io_pause, io_store, io_load, io_reset};
static lsdj_sx_t st;
static uint8_t reply[LSDJ_SX_REPLY_MAX];
static uint32_t rn;

/* Send op with raw data bytes (packed here); returns the status byte. */
static int req(uint8_t op, const uint8_t *data, uint32_t n, uint32_t now) {
    uint8_t q[4 + 2 * 140] = {0x7d, 'G', 'S', op};
    for (uint32_t i = 0; i < n; i++) { q[4 + 2 * i] = data[i] & 127; q[5 + 2 * i] = data[i] >> 7; }
    assert(lsdj_sx_match(q, 4 + 2 * n));
    rn = lsdj_sx_handle(&st, &io, q, 4 + 2 * n, now, reply);
    assert(reply[0] == 0xf0 && reply[4] == op && reply[rn - 1] == 0xf7 && rn <= LSDJ_SX_REPLY_MAX);
    for (uint32_t i = 1; i < rn - 1; i++) assert(reply[i] < 128);
    return reply[5];
}
static uint32_t rd(uint32_t i) { return reply[6 + 2 * i] | (uint32_t)reply[7 + 2 * i] << 7; }
static uint32_t rd32(uint32_t i) { return rd(i) | rd(i + 1) << 8 | rd(i + 2) << 16 | rd(i + 3) << 24; }
static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static int begin(uint32_t len, uint32_t crc, uint32_t now) {
    uint8_t b[8]; le32(b, len); le32(b + 4, crc); return req(LSDJ_SX_BEGIN, b, 8, now);
}
static int chunk(const uint8_t *src, uint32_t off, uint32_t now) {
    uint8_t b[4 + LSDJ_SX_CHUNK]; le32(b, off); memcpy(b + 4, src + off, LSDJ_SX_CHUNK);
    return req(LSDJ_SX_CHUNK_OP, b, sizeof b, now);
}

int main(void) {
    static uint8_t song[SIZE], other[SIZE], got[SIZE];
    for (uint32_t i = 0; i < SIZE; i++) { song[i] = (uint8_t)(i * 31 + 7); other[i] = (uint8_t)(i ^ 0xa5); }
    memcpy(ram, song, SIZE);

    /* Export. */
    assert(req(LSDJ_SX_INFO, 0, 0, 0) == LSDJ_SX_OK);
    assert(rd32(0) == SIZE && rd32(4) == lsdj_sx_crc32(song, SIZE) && rd(8) == 0);
    for (uint32_t off = 0; off < SIZE; off += LSDJ_SX_CHUNK) {
        uint8_t b[4]; le32(b, off); assert(req(LSDJ_SX_READ, b, 4, 0) == LSDJ_SX_OK);
        for (uint32_t i = 0; i < LSDJ_SX_CHUNK; i++) got[off + i] = (uint8_t)rd(i);
    }
    assert(!memcmp(got, song, SIZE));
    { uint8_t b[4]; le32(b, 100); assert(req(LSDJ_SX_READ, b, 4, 0) == LSDJ_SX_BAD);
      le32(b, SIZE); assert(req(LSDJ_SX_READ, b, 4, 0) == LSDJ_SX_BAD); }

    /* Import: the current song is stored first, then replaced. */
    assert(begin(SIZE, lsdj_sx_crc32(other, SIZE), 0) == LSDJ_SX_OK);
    assert(paused && stores == 1 && !memcmp(flash, song, SIZE));
    { uint8_t b[4] = {0}; assert(req(LSDJ_SX_READ, b, 4, 0) == LSDJ_SX_STATE); }
    assert(begin(SIZE, 0, 0) == LSDJ_SX_STATE);
    assert(chunk(other, 256, 1) == LSDJ_SX_BAD);                       /* out of order */
    assert(req(LSDJ_SX_COMMIT, 0, 0, 1) == LSDJ_SX_STATE);              /* incomplete */
    for (uint32_t off = 0; off < SIZE; off += LSDJ_SX_CHUNK) {
        assert(chunk(other, off, 2) == LSDJ_SX_OK);
        if (off == 1024) assert(chunk(other, off, 2) == LSDJ_SX_OK);   /* resend is harmless */
    }
    assert(req(LSDJ_SX_COMMIT, 0, 0, 3) == LSDJ_SX_OK);
    assert(!paused && resets == 1 && !memcmp(ram, other, SIZE) && !memcmp(flash, other, SIZE));

    /* Bad CRC: the stored song comes back. */
    assert(begin(SIZE, 12345, 10) == LSDJ_SX_OK);
    for (uint32_t off = 0; off < SIZE; off += LSDJ_SX_CHUNK) assert(chunk(song, off, 11) == LSDJ_SX_OK);
    assert(req(LSDJ_SX_COMMIT, 0, 0, 12) == LSDJ_SX_CRC);
    assert(!paused && resets == 1 && !memcmp(ram, other, SIZE));

    /* Host vanishes mid-import: the timeout restores the song. */
    assert(begin(SIZE, 0, 100) == LSDJ_SX_OK && chunk(song, 0, 200) == LSDJ_SX_OK);
    lsdj_sx_poll(&st, &io, 200 + LSDJ_SX_TIMEOUT_MS); assert(st.importing);
    lsdj_sx_poll(&st, &io, 201 + LSDJ_SX_TIMEOUT_MS);
    assert(!st.importing && !paused && !memcmp(ram, other, SIZE));

    /* Explicit abort, and flash failure on BEGIN / COMMIT. */
    assert(begin(SIZE, 0, 0) == LSDJ_SX_OK && chunk(song, 0, 0) == LSDJ_SX_OK);
    assert(req(LSDJ_SX_ABORT, 0, 0, 0) == LSDJ_SX_OK && !st.importing && !memcmp(ram, other, SIZE));
    fail_store = 1; assert(begin(SIZE, 0, 0) == LSDJ_SX_FLASH && !paused && !st.importing); fail_store = 0;
    assert(begin(SIZE, lsdj_sx_crc32(song, SIZE), 0) == LSDJ_SX_OK);
    for (uint32_t off = 0; off < SIZE; off += LSDJ_SX_CHUNK) assert(chunk(song, off, 0) == LSDJ_SX_OK);
    fail_store = 1; assert(req(LSDJ_SX_COMMIT, 0, 0, 0) == LSDJ_SX_FLASH); fail_store = 0;
    assert(!paused && !memcmp(ram, other, SIZE));

    /* Malformed requests. */
    assert(begin(4096, 0, 0) == LSDJ_SX_BAD);
    assert(req(9, 0, 0, 0) == LSDJ_SX_BAD);
    { uint8_t q[6] = {0x7d, 'G', 'S', LSDJ_SX_READ, 0x80, 0};
      rn = lsdj_sx_handle(&st, &io, q, 6, 0, reply); assert(reply[5] == LSDJ_SX_BAD); }
    puts("PASS: LSDj save SysEx: export, import, resend, bad CRC, timeout, abort, flash errors, malformed");
    return 0;
}
