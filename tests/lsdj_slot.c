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
/* Version 2: the same three banks stored 300, 16384 and 5 bytes long. */
#define V2_DATA (300u + 16384u + 5u)
static void reseal2(void) {
    put32(win + 16, lsdj_slot_crc32(0, win + LSDJ_SLOT_HDR, 24 + V2_DATA));
    reseal();
}
static void build2(void) {
    static const uint32_t len[3] = {300, 16384, 5};
    build(); put16(win + 8, 2); put32(win + 153, V2_DATA);
    uint32_t at = 0;
    for (unsigned i = 0; i < 3; i++) { put32(win + LSDJ_SLOT_HDR + 8 * i, at); put32(win + LSDJ_SLOT_HDR + 8 * i + 4, len[i]); at += len[i]; }
    for (unsigned b = 0; b < V2_DATA; b++) win[LSDJ_SLOT_HDR + 24 + b] = (uint8_t)(b * 13 + 1);
    reseal2();
}

int main(void) {
    lsdj_slot_t s;
    memset(win, 0xff, sizeof win);
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_EMPTY);

    build();
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK);
    assert(s.logical_banks == 64 && s.stored_banks == 3 && s.kits == 1);
    assert(s.cursor_addr == 0);
    assert(s.bank_map[0] == 0 && s.bank_map[1] == 1 && s.bank_map[8] == 2 && s.bank_map[2] == 0xff && s.bank_map[127] == 0xff);
    win[148] = 1; reseal();
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK && s.cursor_addr == 0xC41D);
    win[148] = 0; reseal();
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK && s.cursor_addr == 0);
    assert(s.profile == 0 && s.tempo_addr == 0);
    /* profile 2: another known LSDj, its cursor and tempo bytes in the header */
    win[148] = 2; put16(win + 149, 0xC41E); put16(win + 151, 0xC52A); reseal();
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK && s.profile == 2 && s.cursor_addr == 0xC41E && s.tempo_addr == 0xC52A);
    put16(win + 151, 0); reseal();
    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK && s.cursor_addr == 0xC41E && s.tempo_addr == 0);
    put16(win + 149, 0x8000); reseal();   assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);   /* not WRAM */
    put16(win + 149, 0); reseal();        assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    put16(win + 149, 0xC41E); put16(win + 151, 0xFF00); reseal(); assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    win[148] = 1; put16(win + 151, 0); reseal(); assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);   /* addresses only with 2 */
    win[148] = 3; put16(win + 149, 0); reseal(); assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);

    build(); win[0] = 'X';                     assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_HEADER);
    build(); win[30] ^= 1;                     assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_HEADER);
    build(); put16(win + 8, 3); reseal();      assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_HEADER);
    build(); win[20 + 3] = 3; reseal();        assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* map past stored */
    build(); win[20 + 0] = 1; reseal();        assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* bank 0 not first */
    build(); win[20 + 64] = 0; reseal();       assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* beyond logical */
    build(); put16(win + 12, 5); reseal();     assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* doesn't fit */
    build(); put16(win + 10, 200); reseal();   assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    build(); put16(win + 10, 63); reseal();    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    build(); put16(win + 14, 4); reseal();     assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    build(); win[20 + 8] = 1; reseal();       assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    build(); win[20 + 8] = 0xff; reseal();    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);
    build(); win[LSDJ_SLOT_HDR + 40000] ^= 1;  assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_DATA);
    assert(lsdj_slot_parse(win, 100, &s) == LSDJ_SLOT_BAD_LAYOUT);
    build(); assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK && s.version == 1 && s.data_off == LSDJ_SLOT_HDR &&
                    s.total == LSDJ_SLOT_HDR + 3 * LSDJ_SLOT_BANK && s.start[2] == 2 * LSDJ_SLOT_BANK && s.len[2] == LSDJ_SLOT_BANK);

    /* version 2 (FM-1_991): banks without their 0xFF filler, a table of start and length */
    build2(); assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_OK && s.version == 2);
    assert(s.stored_banks == 3 && s.data_off == LSDJ_SLOT_HDR + 24 && s.total == LSDJ_SLOT_HDR + 24 + V2_DATA);
    assert(s.start[0] == 0 && s.len[0] == 300 && s.start[1] == 300 && s.len[1] == 16384 && s.start[2] == 16684 && s.len[2] == 5);
    assert(lsdj_slot_parse(win, LSDJ_SLOT_HDR + 24 + V2_DATA, &s) == LSDJ_SLOT_OK);            /* exactly fits */
    assert(lsdj_slot_parse(win, LSDJ_SLOT_HDR + 24 + V2_DATA - 1, &s) == LSDJ_SLOT_BAD_LAYOUT); /* one byte short */
    build2(); put32(win + LSDJ_SLOT_HDR + 8, 301); reseal2();  assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* gap */
    build2(); put32(win + LSDJ_SLOT_HDR + 4, 0); reseal2();    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* empty bank */
    build2(); put32(win + LSDJ_SLOT_HDR + 12, 16385); reseal2(); assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT); /* over 16 KiB */
    build2(); put32(win + 153, V2_DATA + 1); reseal2();        assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* sum differs */
    build2(); put32(win + 153, 0xFFFFFF00u); reseal2();        assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* huge */
    build2(); win[LSDJ_SLOT_HDR + 2] ^= 1;                     assert(lsdj_slot_parse(win, CAP, &s) != LSDJ_SLOT_OK);          /* table changed, CRC old */
    build2(); win[LSDJ_SLOT_HDR + 24 + 16683] ^= 1;            assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_DATA);    /* data changed */
    build(); put32(win + 153, 1); reseal();                    assert(lsdj_slot_parse(win, CAP, &s) == LSDJ_SLOT_BAD_LAYOUT);  /* v1: no data length */
    puts("PASS: LSDj slot: empty, valid, profiles 0/1/2, bad magic/CRC/version, bad map, oversize, corrupt data; "
         "version 2: bank table, exact capacity, gaps, empty / oversize banks, data length, corrupt table / data");
    return 0;
}
