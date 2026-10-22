/* SPDX-License-Identifier: GPL-3.0-only */
/* LSDj save export/import over USB-MIDI SysEx (web/lsdj/lsdj-save.js).

   Request  F0 7D 'G' 'S' op [data] F7
   Reply    F0 7D 'G' 'S' op status [data] F7
   Data bytes travel as pairs (low 7 bits, high bit), as in web/usb-library.js.

   op 1 INFO               -> u32 size, u32 crc32 of save RAM, u8 importing
   op 2 READ  u32 offset   -> 128 bytes of save RAM (offset multiple of 128)
   op 3 BEGIN u32 len, u32 crc32
                           stores the current song to flash first (so a failed
                           import loses nothing), then pauses LSDj
   op 4 CHUNK u32 offset, 128 bytes   in order; resending the last one is fine
   op 5 COMMIT             crc ok: write to flash, restart LSDj with the import
                           crc bad: reload the stored song, status 3
   op 6 ABORT              reload the stored song
   An import with no CHUNK/COMMIT for LSDJ_SX_TIMEOUT_MS is aborted.
   status 0 ok, 1 bad request, 2 wrong state, 3 crc mismatch, 4 flash error.
   Pure logic: the hardware comes in through lsdj_sx_io_t (tests/lsdj_sysex.c). */
#pragma once
#include <stdint.h>

#define LSDJ_SX_CHUNK      128u
#define LSDJ_SX_TIMEOUT_MS 5000u
#define LSDJ_SX_REPLY_MAX  (6u + 2u * LSDJ_SX_CHUNK + 1u)

enum { LSDJ_SX_INFO = 1, LSDJ_SX_READ, LSDJ_SX_BEGIN, LSDJ_SX_CHUNK_OP, LSDJ_SX_COMMIT, LSDJ_SX_ABORT };
enum { LSDJ_SX_OK = 0, LSDJ_SX_BAD = 1, LSDJ_SX_STATE = 2, LSDJ_SX_CRC = 3, LSDJ_SX_FLASH = 4 };

typedef struct {
    uint8_t *(*save)(void);
    uint32_t (*size)(void);
    void (*pause)(int on);
    int (*store)(const uint8_t *p, uint32_t n);  /* to flash, 0 = ok */
    int (*load)(uint8_t *p, uint32_t n);         /* from flash, 0 = ok */
    void (*reset)(void);                         /* restart the game */
} lsdj_sx_io_t;

typedef struct {
    uint8_t importing;
    uint32_t len, crc, next, last_ms;
} lsdj_sx_t;

static uint32_t lsdj_sx_crc32(const uint8_t *p, uint32_t n) {
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* Decode n data bytes from 7-bit pairs; 0 if malformed. */
static int lsdj_sx_unpack(const uint8_t *in, uint32_t pairs_len, uint8_t *out, uint32_t n) {
    if (pairs_len != 2u * n) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (in[2 * i] > 127 || in[2 * i + 1] > 1) return 0;
        out[i] = (uint8_t)(in[2 * i] | in[2 * i + 1] << 7);
    }
    return 1;
}
static uint32_t lsdj_sx_le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t lsdj_sx_put(uint8_t *r, uint32_t n, const uint8_t *p, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) { r[n++] = p[i] & 127; r[n++] = p[i] >> 7; }
    return n;
}
static uint32_t lsdj_sx_put32(uint8_t *r, uint32_t n, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return lsdj_sx_put(r, n, b, 4);
}

static void lsdj_sx_abort(lsdj_sx_t *s, const lsdj_sx_io_t *io) {
    if (!s->importing) return;
    io->load(io->save(), io->size());
    s->importing = 0; io->pause(0);
}

/* 1 if this frame (7-bit bytes between F0 and F7) is for us. */
static int lsdj_sx_match(const uint8_t *q, uint32_t n) {
    return n >= 4 && q[0] == 0x7d && q[1] == 'G' && q[2] == 'S';
}

/* Handle one request; writes the complete F0..F7 reply to r and returns its length. */
static uint32_t lsdj_sx_handle(lsdj_sx_t *s, const lsdj_sx_io_t *io, const uint8_t *q, uint32_t qn,
                               uint32_t now_ms, uint8_t r[LSDJ_SX_REPLY_MAX]) {
    uint8_t op = q[3], status = LSDJ_SX_OK, d[4 + LSDJ_SX_CHUNK];
    const uint8_t *args = q + 4; uint32_t an = qn - 4, size = io->size();
    uint32_t n = 0;
    r[n++] = 0xf0; r[n++] = 0x7d; r[n++] = 'G'; r[n++] = 'S'; r[n++] = op; r[n++] = 0;
    switch (op) {
    case LSDJ_SX_INFO:
        if (an) { status = LSDJ_SX_BAD; break; }
        n = lsdj_sx_put32(r, n, size);
        n = lsdj_sx_put32(r, n, s->importing ? 0 : lsdj_sx_crc32(io->save(), size));
        { uint8_t b = s->importing; n = lsdj_sx_put(r, n, &b, 1); }
        break;
    case LSDJ_SX_READ: {
        if (!lsdj_sx_unpack(args, an, d, 4)) { status = LSDJ_SX_BAD; break; }
        uint32_t off = lsdj_sx_le32(d);
        if (off % LSDJ_SX_CHUNK || off >= size) { status = LSDJ_SX_BAD; break; }
        if (s->importing) { status = LSDJ_SX_STATE; break; }
        n = lsdj_sx_put(r, n, io->save() + off, LSDJ_SX_CHUNK);
        break;
    }
    case LSDJ_SX_BEGIN:
        if (!lsdj_sx_unpack(args, an, d, 8)) { status = LSDJ_SX_BAD; break; }
        if (lsdj_sx_le32(d) != size) { status = LSDJ_SX_BAD; break; }
        if (s->importing) { status = LSDJ_SX_STATE; break; }
        io->pause(1);
        if (io->store(io->save(), size)) { io->pause(0); status = LSDJ_SX_FLASH; break; }
        s->importing = 1; s->len = size; s->crc = lsdj_sx_le32(d + 4); s->next = 0; s->last_ms = now_ms;
        break;
    case LSDJ_SX_CHUNK_OP: {
        if (!lsdj_sx_unpack(args, an, d, 4 + LSDJ_SX_CHUNK)) { status = LSDJ_SX_BAD; break; }
        if (!s->importing) { status = LSDJ_SX_STATE; break; }
        uint32_t off = lsdj_sx_le32(d);
        if (off + LSDJ_SX_CHUNK == s->next) { s->last_ms = now_ms; break; }  /* repeat of the last chunk */
        if (off != s->next || off + LSDJ_SX_CHUNK > s->len) { status = LSDJ_SX_BAD; break; }
        uint8_t *dst = io->save() + off;
        for (uint32_t i = 0; i < LSDJ_SX_CHUNK; i++) dst[i] = d[4 + i];
        s->next += LSDJ_SX_CHUNK; s->last_ms = now_ms;
        break;
    }
    case LSDJ_SX_COMMIT:
        if (an) { status = LSDJ_SX_BAD; break; }
        if (!s->importing || s->next != s->len) { status = LSDJ_SX_STATE; break; }
        if (lsdj_sx_crc32(io->save(), s->len) != s->crc) { lsdj_sx_abort(s, io); status = LSDJ_SX_CRC; break; }
        if (io->store(io->save(), s->len)) { lsdj_sx_abort(s, io); status = LSDJ_SX_FLASH; break; }
        s->importing = 0; io->reset(); io->pause(0);
        break;
    case LSDJ_SX_ABORT:
        lsdj_sx_abort(s, io);
        break;
    default:
        status = LSDJ_SX_BAD;
    }
    r[5] = status; r[n++] = 0xf7;
    return n;
}

/* Call regularly: gives up on an import whose host went away. */
static void lsdj_sx_poll(lsdj_sx_t *s, const lsdj_sx_io_t *io, uint32_t now_ms) {
    if (s->importing && now_ms - s->last_ms > LSDJ_SX_TIMEOUT_MS) lsdj_sx_abort(s, io);
}
