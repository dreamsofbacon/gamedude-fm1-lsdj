/* SPDX-License-Identifier: GPL-3.0-only */
/* LSDj ROM slot. The public firmware template carries an empty (all 0xFF)
   cartridge window; the web installer (web/lsdj/lsdj-firmware.js) writes the
   user's own trimmed LSDj into it. Nothing here contains or needs a ROM.

   Window layout, little endian:
     0   magic "LSDJSLOT"
     8   u16 version (1)
     10  u16 logical banks (16 KiB each, as LSDj addresses them)
     12  u16 stored banks (follow the header, in order)
     14  u16 kits kept (information only)
     16  u32 CRC32 of the stored banks
     20  u8  bank_map[128]: logical bank -> stored bank, 0xFF = absent (reads 0xFF)
     148 zero padding
     252 u32 CRC32 of bytes 0..251
     256 stored banks
   Pure logic, so it runs in the firmware and in tests/lsdj_slot.c. */
#pragma once
#include <stdint.h>

#define LSDJ_SLOT_HDR       256u
#define LSDJ_SLOT_VERSION   1u
#define LSDJ_SLOT_MAX_BANKS 128u
#define LSDJ_SLOT_BANK      16384u

enum { LSDJ_SLOT_OK = 0, LSDJ_SLOT_EMPTY = -1, LSDJ_SLOT_BAD_HEADER = -2,
       LSDJ_SLOT_BAD_LAYOUT = -3, LSDJ_SLOT_BAD_DATA = -4 };

typedef struct {
    uint32_t logical_banks, stored_banks, kits;
    uint8_t bank_map[LSDJ_SLOT_MAX_BANKS];
} lsdj_slot_t;

static uint32_t lsdj_slot_crc32(uint32_t crc, const volatile uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t lsdj_slot_u16(const volatile uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t lsdj_slot_u32(const volatile uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Check the window and fill *s. The window is read through a volatile pointer:
   the browser patches it after the compiler has seen it as all 0xFF. */
static int lsdj_slot_parse(const volatile uint8_t *w, uint32_t capacity, lsdj_slot_t *s) {
    static const char magic[8] = {'L','S','D','J','S','L','O','T'};
    if (capacity < LSDJ_SLOT_HDR) return LSDJ_SLOT_BAD_LAYOUT;
    unsigned blank = 1;
    for (unsigned i = 0; i < 8; i++) blank &= w[i] == 0xff;
    if (blank) return LSDJ_SLOT_EMPTY;
    for (unsigned i = 0; i < 8; i++) if (w[i] != (uint8_t)magic[i]) return LSDJ_SLOT_BAD_HEADER;
    if (lsdj_slot_u16(w + 8) != LSDJ_SLOT_VERSION ||
        lsdj_slot_u32(w + 252) != lsdj_slot_crc32(0, w, 252)) return LSDJ_SLOT_BAD_HEADER;
    s->logical_banks = lsdj_slot_u16(w + 10);
    s->stored_banks = lsdj_slot_u16(w + 12);
    s->kits = lsdj_slot_u16(w + 14);
    if (s->logical_banks < 2 || s->logical_banks > LSDJ_SLOT_MAX_BANKS ||
        (s->logical_banks & (s->logical_banks - 1u)) || !s->stored_banks || s->kits > s->stored_banks ||
        s->stored_banks > s->logical_banks ||
        s->stored_banks > (capacity - LSDJ_SLOT_HDR) / LSDJ_SLOT_BANK) return LSDJ_SLOT_BAD_LAYOUT;
    unsigned next = 0;
    for (unsigned b = 0; b < LSDJ_SLOT_MAX_BANKS; b++) {
        uint8_t m = w[20 + b];
        if (b >= s->logical_banks ? m != 0xff : (m != 0xff && m >= s->stored_banks))
            return LSDJ_SLOT_BAD_LAYOUT;
        s->bank_map[b] = m;
        if (m != 0xff && m != next++) return LSDJ_SLOT_BAD_LAYOUT;
    }
    if (s->bank_map[0] != 0 || next != s->stored_banks) return LSDJ_SLOT_BAD_LAYOUT;
    if (lsdj_slot_u32(w + 16) !=
        lsdj_slot_crc32(0, w + LSDJ_SLOT_HDR, s->stored_banks * LSDJ_SLOT_BANK)) return LSDJ_SLOT_BAD_DATA;
    return LSDJ_SLOT_OK;
}
