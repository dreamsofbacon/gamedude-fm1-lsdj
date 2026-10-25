/* SPDX-License-Identifier: GPL-3.0-only */
/* Battery RAM (LSDj's songs) kept in internal flash: two A/B slots in Felucca's
   data store (fm1_flash.h FL_DATA, 0x97000..0xE0000), well below the OTA staging.
   A slot is a 4 KiB header sector + the save data. A save goes to the slot that
   does not hold the newest valid save, data first and the header last, so a power
   cut mid-save leaves the previous save intact.

   Flash access goes through four macros so the same code runs in the firmware
   (fm1_flash.h wrappers) and in the host test (tests/lsdj_save.c, a RAM flash):
     LSDJ_FL_ERASE(off)        erase the 4 KiB sector at off, 0 = ok
     LSDJ_FL_WRITE(off, p, n)  program n bytes from RAM, 0 = ok
     LSDJ_FL_READ(off, p, n)   read n bytes into RAM, 0 = ok
     LSDJ_FL_IDLE()            between sectors (watchdog) */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LSDJ_SAVE_LO      0x00097000u
#define LSDJ_SAVE_DATA    0x00020000u                    /* 128 KiB: MBC5 RAM size 4 */
#define LSDJ_SAVE_SLOT    (0x1000u + LSDJ_SAVE_DATA)     /* header sector + data */
#define LSDJ_SAVE_HI      (LSDJ_SAVE_LO + 2u * LSDJ_SAVE_SLOT)   /* 0xD9000 */
#define LSDJ_SAVE_MAGIC   0x4A44534Cu                    /* "LSDJ" */
#define LSDJ_SAVE_VERSION 1u

typedef struct {
    uint32_t magic, version, seq, len, data_crc, hdr_crc;
} lsdj_save_hdr_t;

/* RAM bounce buffer for reads (boot only): the board may lend one (LSDJ_SAVE_BUF, 4096 bytes) */
#define LSDJ_SAVE_BUF_N 4096u
#ifndef LSDJ_SAVE_BUF
static uint8_t lsdj_save_buf_mem[LSDJ_SAVE_BUF_N];
#define LSDJ_SAVE_BUF lsdj_save_buf_mem
#endif

static uint32_t lsdj_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t lsdj_save_slot_off(unsigned slot) { return LSDJ_SAVE_LO + slot * LSDJ_SAVE_SLOT; }

/* 1 if the slot's header is intact and describes a save of `len` bytes. */
static int lsdj_save_hdr(unsigned slot, uint32_t len, lsdj_save_hdr_t *h) {
    if (LSDJ_FL_READ(lsdj_save_slot_off(slot), (uint8_t *)h, sizeof *h)) return 0;
    return h->magic == LSDJ_SAVE_MAGIC && h->version == LSDJ_SAVE_VERSION && h->len == len &&
           h->hdr_crc == lsdj_crc32(0, (const uint8_t *)h, offsetof(lsdj_save_hdr_t, hdr_crc));
}

/* Slot holding the newest intact header, or -1. */
static int lsdj_save_newest(uint32_t len, lsdj_save_hdr_t *best) {
    lsdj_save_hdr_t h; int found = -1;
    for (unsigned s = 0; s < 2; s++)
        if (lsdj_save_hdr(s, len, &h) && (found < 0 || (int32_t)(h.seq - best->seq) > 0)) {
            *best = h; found = (int)s;
        }
    return found;
}

/* Copy the slot's data into ram (len bytes) and check its CRC. 0 = ok. */
static int lsdj_save_read_slot(unsigned slot, const lsdj_save_hdr_t *h, uint8_t *ram, uint32_t len) {
    uint32_t base = lsdj_save_slot_off(slot) + 0x1000u, crc = 0;
    for (uint32_t o = 0; o < len; o += LSDJ_SAVE_BUF_N) {
        uint32_t n = len - o < LSDJ_SAVE_BUF_N ? len - o : LSDJ_SAVE_BUF_N;
        if (LSDJ_FL_READ(base + o, LSDJ_SAVE_BUF, n)) return -1;
        crc = lsdj_crc32(crc, LSDJ_SAVE_BUF, n);
        memcpy(ram + o, LSDJ_SAVE_BUF, n);
        LSDJ_FL_IDLE();
    }
    return crc == h->data_crc ? 0 : -2;
}

/* At boot: load the newest valid save into ram. 1 = loaded, 0 = none (ram untouched
   unless a newer slot was corrupt and an older one loaded instead). */
static int lsdj_save_load(uint8_t *ram, uint32_t len) {
    if (len != LSDJ_SAVE_DATA) return 0;
    lsdj_save_hdr_t h[2]; int ok[2];
    for (unsigned s = 0; s < 2; s++) ok[s] = lsdj_save_hdr(s, len, &h[s]);
    int first = ok[0] && ok[1] ? ((int32_t)(h[1].seq - h[0].seq) > 0) : ok[1];
    for (int i = 0; i < 2; i++) {
        unsigned s = (unsigned)(i == 0 ? first : !first);
        if (ok[s] && !lsdj_save_read_slot(s, &h[s], ram, len)) return 1;
    }
    memset(ram, 0xFF, len);
    return 0;
}

/* Write ram to the older slot, header last. 0 = ok, <0 = failed (the previous
   save is still intact). */
static int lsdj_save_store(const uint8_t *ram, uint32_t len) {
    if (len != LSDJ_SAVE_DATA) return -1;
    lsdj_save_hdr_t newest;
    int cur = lsdj_save_newest(len, &newest);
    unsigned slot = cur == 0 ? 1u : 0u;
    uint32_t base = lsdj_save_slot_off(slot);
    /* Invalidate the target slot's header first, then the data, then commit. */
    for (uint32_t o = 0; o < LSDJ_SAVE_SLOT; o += 0x1000u) {
        if (LSDJ_FL_ERASE(base + o)) return -2;
        LSDJ_FL_IDLE();
    }
    for (uint32_t o = 0; o < len; o += 0x1000u) {
        if (LSDJ_FL_WRITE(base + 0x1000u + o, ram + o, 0x1000u)) return -3;
        LSDJ_FL_IDLE();
    }
    /* Read back before committing. */
    uint32_t crc = 0, want = lsdj_crc32(0, ram, len);
    for (uint32_t o = 0; o < len; o += LSDJ_SAVE_BUF_N) {
        if (LSDJ_FL_READ(base + 0x1000u + o, LSDJ_SAVE_BUF, LSDJ_SAVE_BUF_N)) return -4;
        crc = lsdj_crc32(crc, LSDJ_SAVE_BUF, LSDJ_SAVE_BUF_N);
        LSDJ_FL_IDLE();
    }
    if (crc != want) return -5;
    lsdj_save_hdr_t h = { LSDJ_SAVE_MAGIC, LSDJ_SAVE_VERSION, cur < 0 ? 1u : newest.seq + 1u, len, crc, 0 };
    h.hdr_crc = lsdj_crc32(0, (const uint8_t *)&h, offsetof(lsdj_save_hdr_t, hdr_crc));
    static lsdj_save_hdr_t h_ram;           /* the write source must be in RAM */
    h_ram = h;
    if (LSDJ_FL_WRITE(base, (const uint8_t *)&h_ram, sizeof h_ram)) return -6;
    lsdj_save_hdr_t back;
    if (!lsdj_save_hdr(slot, len, &back) || back.seq != h.seq) return -7;
    return 0;
}
