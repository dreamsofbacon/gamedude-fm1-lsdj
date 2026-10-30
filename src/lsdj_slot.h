/* SPDX-License-Identifier: GPL-3.0-only */
/* LSDj ROM slot. The public firmware template carries an empty (all 0xFF)
   cartridge window; the web installer (web/lsdj/lsdj-firmware.js) writes the
   user's own trimmed LSDj into it. Nothing here contains or needs a ROM.

   Window layout, little endian:
     0   magic "LSDJSLOT"
     8   u16 version (1 or 2)
     10  u16 logical banks (16 KiB each, as LSDj addresses them)
     12  u16 stored banks (follow the header, in order)
     14  u16 kits kept (information only)
     16  u32 CRC32 of everything after the header (version 1: the stored banks;
         version 2: the bank table and the data)
     20  u8  bank_map[128]: logical bank -> stored bank, 0xFF = absent (reads 0xFF)
     148 u8 cursor profile: 0 = unknown, 1 = verified LSDj 9.3.9 program (every helper),
            2 = another known LSDj (the web page's list): only the two addresses below
     149 u16 profile 2: LSDj's cursor channel byte (WRAM, 0xC000..0xDFFF)
     151 u16 profile 2: LSDj's tempo byte (WRAM, or 0 = none)
     153 version 2: u32 data bytes (the stored banks' lengths added up); version 1: zero
     157 zero padding (149..152 too, profiles 0 and 1)
     252 u32 CRC32 of bytes 0..251
     256 version 1: the stored banks, 16 KiB each
         version 2 (FM-1_991): a bank table, then the data. Per stored bank, 8 bytes:
             u32 start (in the data), u32 length (1..16384). Banks follow each other in
             order. A bank's bytes past its length read 0xFF: LSDj's banks end in 0xFF
             filler (a kit often uses half its bank), which is not stored, so more kits fit.
   Pure logic, so it runs in the firmware and in tests/lsdj_slot.c. */
#pragma once
#include <stdint.h>

#define LSDJ_SLOT_HDR       256u
#define LSDJ_SLOT_VERSION   2u   /* the newest; version 1 is still read */
#define LSDJ_SLOT_MAX_BANKS 128u
#define LSDJ_SLOT_BANK      16384u

enum { LSDJ_SLOT_OK = 0, LSDJ_SLOT_EMPTY = -1, LSDJ_SLOT_BAD_HEADER = -2,
       LSDJ_SLOT_BAD_LAYOUT = -3, LSDJ_SLOT_BAD_DATA = -4 };

typedef struct {
    uint32_t logical_banks, stored_banks, kits;
    uint32_t version;
    uint32_t data_off;        /* where stored bank 0 starts, from the window start */
    uint32_t total;           /* the window's used bytes: header (+ table) + data */
    uint8_t profile;          /* byte 148 */
    uint16_t cursor_addr;     /* 0 = unknown (profile 1: 0xC41D) */
    uint16_t tempo_addr;      /* profile 2 only (profile 1: board.c's table) */
    uint8_t bank_map[LSDJ_SLOT_MAX_BANKS];
    uint32_t start[LSDJ_SLOT_MAX_BANKS];   /* per stored bank, from data_off */
    uint16_t len[LSDJ_SLOT_MAX_BANKS];     /* per stored bank, 1..16384; past it reads 0xFF */
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
    s->version = lsdj_slot_u16(w + 8);
    if ((s->version != 1u && s->version != 2u) ||
        lsdj_slot_u32(w + 252) != lsdj_slot_crc32(0, w, 252)) return LSDJ_SLOT_BAD_HEADER;
    if (w[148] > 2u) return LSDJ_SLOT_BAD_LAYOUT;
    s->profile = w[148];
    s->cursor_addr = w[148] == 1u ? 0xC41Du : 0;
    s->tempo_addr = 0;
    {
        uint32_t c = lsdj_slot_u16(w + 149), t = lsdj_slot_u16(w + 151);
        if (w[148] == 2u) {
            if (c < 0xC000u || c > 0xDFFFu || (t && (t < 0xC000u || t > 0xDFFFu))) return LSDJ_SLOT_BAD_LAYOUT;
            s->cursor_addr = (uint16_t)c; s->tempo_addr = (uint16_t)t;
        } else if (c || t) return LSDJ_SLOT_BAD_LAYOUT;
    }
    s->logical_banks = lsdj_slot_u16(w + 10);
    s->stored_banks = lsdj_slot_u16(w + 12);
    s->kits = lsdj_slot_u16(w + 14);
    if (s->logical_banks < 2 || s->logical_banks > LSDJ_SLOT_MAX_BANKS ||
        (s->logical_banks & (s->logical_banks - 1u)) || !s->stored_banks || s->kits > s->stored_banks ||
        s->stored_banks > s->logical_banks) return LSDJ_SLOT_BAD_LAYOUT;
    if (s->version == 1u) {   /* whole banks, back to back */
        if (lsdj_slot_u32(w + 153) || s->stored_banks > (capacity - LSDJ_SLOT_HDR) / LSDJ_SLOT_BANK) return LSDJ_SLOT_BAD_LAYOUT;
        for (unsigned i = 0; i < s->stored_banks; i++) { s->start[i] = i * LSDJ_SLOT_BANK; s->len[i] = (uint16_t)LSDJ_SLOT_BANK; }
        s->data_off = LSDJ_SLOT_HDR;
        s->total = LSDJ_SLOT_HDR + s->stored_banks * LSDJ_SLOT_BANK;
    } else {                  /* the bank table: each bank right after the one before */
        uint32_t table = s->stored_banks * 8u, data = lsdj_slot_u32(w + 153), at = 0;
        if (table > capacity - LSDJ_SLOT_HDR || data > capacity - LSDJ_SLOT_HDR - table) return LSDJ_SLOT_BAD_LAYOUT;
        for (unsigned i = 0; i < s->stored_banks; i++) {
            uint32_t st = lsdj_slot_u32(w + LSDJ_SLOT_HDR + 8u * i), n = lsdj_slot_u32(w + LSDJ_SLOT_HDR + 8u * i + 4u);
            if (st != at || n < 1u || n > LSDJ_SLOT_BANK) return LSDJ_SLOT_BAD_LAYOUT;
            s->start[i] = st; s->len[i] = (uint16_t)n; at += n;
        }
        if (at != data) return LSDJ_SLOT_BAD_LAYOUT;
        s->data_off = LSDJ_SLOT_HDR + table;
        s->total = s->data_off + data;
    }
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
        lsdj_slot_crc32(0, w + LSDJ_SLOT_HDR, s->total - LSDJ_SLOT_HDR)) return LSDJ_SLOT_BAD_DATA;
    return LSDJ_SLOT_OK;
}
