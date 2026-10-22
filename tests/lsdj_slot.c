/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for src/lsdj_slot.h: empty, corrupt and valid ROM slots. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_slot.h"

#define CAP (LSDJ_SLOT_HDR + 4u * LSDJ_SLOT_BANK)
static uint8_t win[CAP];

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }
/* Valid slot: 64 logical banks, 3 stored (0, 1 and a kit at 8), the rest absent. */
static void build(void) {
    memset(win, 0xff, sizeof win);
    memcpy(win, "LSDJSLOT", 8); put16(win + 8, 1); put16(win + 10, 64); put16(win + 12, 3); put16(win + 14, 1);
    memset(win + 148, 0, 104);
    for (unsigned b = 0; b < 3 * LSDJ_SLOT_BANK; b++) win[LSDJ_SLOT_HDR + b] = (uint8_t)(b * 7 + b / 16384);
    win[20 + 0] = 0; win[20 + 1] = 1; win[20 + 8] = 2;
    put32(win + 16, lsdj_slot_crc32(0, win + LSDJ_SLOT_HDR, 3 * LSDJ_SLOT_BANK));
    put32(win + 252, lsdj_slot_crc32(0, win, 252));
}
static void reseal(void) { put32(win + 252, lsdj_slot_crc32(0, win, 252)); }

int main(void) {
    lsdj_slot_t s;
    memset(win, 0xff, sizeof win);
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_EMPTY);

    build();
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK);
    assert(s.logical_banks == 64 && s.stored_banks == 3 && s.kits == 1);
    assert(s.bank_map[0] == 0 && s.bank_map[1] == 1 && s.bank_map[8] == 2 && s.bank_map[2] == 0xff && s.bank_map[127] == 0xff);

    build(); win[0] = 'X';                     assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_HEADER);
    build(); win[30] ^= 1;                     assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_HEADER);
    build(); put16(win + 8, 2); reseal();      assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_HEADER);
    build(); win[20 + 3] = 3; reseal();        assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* map past stored */
    build(); win[20 + 0] = 1; reseal();        assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* bank 0 not first */
    build(); win[20 + 64] = 0; reseal();       assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* beyond logical */
    build(); put16(win + 12, 5); reseal();     assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* doesn't fit */
    build(); put16(win + 10, 200); reseal();   assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    build(); win[LSDJ_SLOT_HDR + 40000] ^= 1;  assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_DATA);
    assert(lsdj_slot_parse(win, 100, &s) == LSDJ_SLOT_BAD_LAYOUT);
    puts("PASS: LSDj slot: empty, valid, bad magic/CRC/version, bad map, oversize, corrupt data");
    return 0;
}
