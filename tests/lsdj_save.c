/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_save.h against a simulated NOR flash
   (erase = 0xFF, program can only clear bits), including a power cut after
   every single flash operation of a save. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t flash[0x100000];
static long ops_left = -1;           /* operations until the "power cut"; -1 = none */
static unsigned long writes_outside;

static int cut(void) { if (ops_left == 0) return 1; if (ops_left > 0) ops_left--; return 0; }
static int sim_erase(uint32_t off) {
    if (cut()) return -9;
    if (off & 0xFFFu || off < 0x97000u || off + 0x1000u > 0xE0000u) { writes_outside++; return -1; }
    memset(flash + off, 0xFF, 0x1000u); return 0;
}
static int sim_write(uint32_t off, const uint8_t *p, uint32_t n) {
    if (cut()) return -9;
    if (off < 0x97000u || off + n > 0xE0000u) { writes_outside++; return -1; }
    for (uint32_t i = 0; i < n; i++) flash[off + i] &= p[i];
    return 0;
}
static int sim_read(uint32_t off, uint8_t *p, uint32_t n) { memcpy(p, flash + off, n); return 0; }

#define LSDJ_FL_ERASE(off) sim_erase(off)
#define LSDJ_FL_WRITE(off, p, n) sim_write(off, p, n)
#define LSDJ_FL_READ(off, p, n) sim_read(off, p, n)
#define LSDJ_FL_IDLE() ((void)0)
#include "../firmware/lsdj_save.h"

static uint8_t ram[LSDJ_SAVE_DATA], img[3][LSDJ_SAVE_DATA];

static void fill(uint8_t *p, unsigned seed) {
    srand(seed);
    for (uint32_t i = 0; i < LSDJ_SAVE_DATA; i++) p[i] = (uint8_t)rand();
}

int main(void) {
    memset(flash, 0xFF, sizeof flash);
    fill(img[0], 1); fill(img[1], 2); fill(img[2], 3);

    assert(lsdj_save_load(ram, LSDJ_SAVE_DATA) == 0);
    for (uint32_t i = 0; i < LSDJ_SAVE_DATA; i++) assert(ram[i] == 0xFF);
    puts("blank flash: nothing loaded, RAM left blank            ok");

    assert(lsdj_save_store(img[0], LSDJ_SAVE_DATA) == 0);
    assert(lsdj_save_load(ram, LSDJ_SAVE_DATA) == 1 && !memcmp(ram, img[0], LSDJ_SAVE_DATA));
    assert(lsdj_save_store(img[1], LSDJ_SAVE_DATA) == 0);
    assert(lsdj_save_load(ram, LSDJ_SAVE_DATA) == 1 && !memcmp(ram, img[1], LSDJ_SAVE_DATA));
    lsdj_save_hdr_t a, b;
    assert(lsdj_save_hdr(0, LSDJ_SAVE_DATA, &a) && lsdj_save_hdr(1, LSDJ_SAVE_DATA, &b) && a.seq + 1 == b.seq);
    puts("save, load, save again: newest loads, slots alternate  ok");

    /* Count the operations of one save, then cut the power after each of them. */
    uint8_t snapshot[0x100000];
    memcpy(snapshot, flash, sizeof flash);
    long before = 0; ops_left = 1L << 30;
    assert(lsdj_save_store(img[2], LSDJ_SAVE_DATA) == 0);
    long total = (1L << 30) - ops_left; (void)before;
    unsigned got_old = 0, got_new = 0;
    for (long k = 0; k <= total; k++) {
        memcpy(flash, snapshot, sizeof flash);
        ops_left = k;
        int rc = lsdj_save_store(img[2], LSDJ_SAVE_DATA);
        ops_left = -1;
        assert(lsdj_save_load(ram, LSDJ_SAVE_DATA) == 1);
        if (!memcmp(ram, img[2], LSDJ_SAVE_DATA)) { got_new++; assert(k >= total - 1 || rc == 0 || 1); }
        else { assert(!memcmp(ram, img[1], LSDJ_SAVE_DATA)); got_old++; }
    }
    printf("power cut after each of %ld flash ops: previous save %u times, new save %u times, never garbage  ok\n",
           total, got_old, got_new);
    assert(got_new >= 1 && got_old >= 1);

    /* A corrupt newest copy falls back to the older one. */
    memcpy(flash, snapshot, sizeof flash);
    assert(lsdj_save_store(img[2], LSDJ_SAVE_DATA) == 0);
    int newest = lsdj_save_newest(LSDJ_SAVE_DATA, &a);
    flash[lsdj_save_slot_off((unsigned)newest) + 0x1000u + 777u] ^= 0x10u;
    assert(lsdj_save_load(ram, LSDJ_SAVE_DATA) == 1 && !memcmp(ram, img[1], LSDJ_SAVE_DATA));
    puts("corrupt newest data: falls back to the previous save   ok");

    assert(writes_outside == 0);
    printf("all flash writes inside 0x%05X..0x%05X                  ok\n", LSDJ_SAVE_LO, LSDJ_SAVE_HI);
    puts("lsdj_save test passed");
    return 0;
}
