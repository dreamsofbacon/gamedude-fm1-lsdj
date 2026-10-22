/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_state.h (LZSS + save-state pool) on a simulated NOR
   flash. Usage: lsdj_state [save files ...]  (real LSDj .sav files, 128 KiB). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t flash[0x100000];
static long ops_left = -1;
static unsigned long outside;
static unsigned erases[256];
static int cut(void) { if (ops_left == 0) return 1; if (ops_left > 0) ops_left--; return 0; }
static int sim_erase(uint32_t off) {
    if (cut()) return -9;
    if ((off & 0xFFFu) || off < 0x97000u || off + 0x1000u > 0xE0000u) { outside++; return -1; }
    erases[(off - 0x97000u) / 0x1000u]++;
    memset(flash + off, 0xFF, 0x1000u); return 0;
}
static int sim_write(uint32_t off, const uint8_t *p, uint32_t n) {
    if (cut()) return -9;
    if (off < 0x97000u || off + n > 0xE0000u) { outside++; return -1; }
    for (uint32_t i = 0; i < n; i++) flash[off + i] &= p[i];
    return 0;
}
static int sim_read(uint32_t off, uint8_t *p, uint32_t n) { memcpy(p, flash + off, n); return 0; }
#define LSDJ_FL_ERASE(off) sim_erase(off)
#define LSDJ_FL_WRITE(off, p, n) sim_write(off, p, n)
#define LSDJ_FL_READ(off, p, n) sim_read(off, p, n)
#define LSDJ_FL_IDLE() ((void)0)
#include "../firmware/lsdj_state.h"

static uint32_t work[LZ_WORK_BYTES / 4];
static uint8_t ring[32768], io[4096];

/* A state: cart RAM + "gb_s" + "apu", like the firmware. */
typedef struct { uint8_t cart[131072], gb[16984], apu[312]; } state_t;
static void regions(state_t *s, lst_region_t r[3]) {
    r[0] = (lst_region_t){ s->cart, sizeof s->cart };
    r[1] = (lst_region_t){ s->gb, sizeof s->gb };
    r[2] = (lst_region_t){ s->apu, sizeof s->apu };
}
static void fill_state(state_t *s, const uint8_t *cart, unsigned seed) {
    srand(seed);
    if (cart) memcpy(s->cart, cart, sizeof s->cart);
    else for (size_t i = 0; i < sizeof s->cart; i++) s->cart[i] = (i % 97 < 60) ? 0 : (uint8_t)rand();
    for (size_t i = 0; i < sizeof s->gb; i++) s->gb[i] = (i % 5) ? (uint8_t)(i * seed) : (uint8_t)rand();
    for (size_t i = 0; i < sizeof s->apu; i++) s->apu[i] = (uint8_t)rand();
}
static int same(const state_t *a, const state_t *b) { return !memcmp(a, b, sizeof *a); }

static state_t A, B, C, D, X;
static uint8_t saves[64][131072]; static unsigned nsaves;

int main(int argc, char **argv) {
    lst_region_t r[3], rx[3];
    memset(flash, 0xFF, sizeof flash);
    for (int i = 1; i < argc && nsaves < 64; i++) {
        FILE *f = fopen(argv[i], "rb"); if (!f) continue;
        if (fread(saves[nsaves], 1, 131072, f) == 131072) nsaves++;
        fclose(f);
    }

    /* 1. Codec: every real save round-trips; report sizes. */
    uint32_t sizes[64], worst = 0, tot = 0;
    for (unsigned i = 0; i < nsaves; i++) {
        memset(flash, 0xFF, sizeof flash);
        fill_state(&A, saves[i], i + 1); regions(&A, r);
        assert(lst_save(LST_AUTO, r, 3, 7, work, io) == LST_OK);
        lst_entry_t e[LST_KINDS]; lst_scan(e, NULL);
        sizes[i] = e[LST_AUTO].h.comp_len; tot += sizes[i]; if (sizes[i] > worst) worst = sizes[i];
        memset(&X, 0xAA, sizeof X); regions(&X, rx); int full = 0;
        assert(lst_load(LST_AUTO, rx, 3, 7, ring, io, &full) == LST_OK && full && same(&A, &X));
    }
    if (nsaves) {
        for (unsigned i = 0; i < nsaves; i++) for (unsigned j = i + 1; j < nsaves; j++)
            if (sizes[j] < sizes[i]) { uint32_t t = sizes[i]; sizes[i] = sizes[j]; sizes[j] = t; }
        printf("codec: %u real saves round-trip exactly; full state compressed median %.1f KiB, worst %.1f KiB\n",
               nsaves, sizes[nsaves / 2] / 1024.0, worst / 1024.0);
    }

    /* 2. Three slots + auto, load each back, delete, refuse when full. */
    memset(flash, 0xFF, sizeof flash);
    fill_state(&A, nsaves ? saves[0] : NULL, 11); fill_state(&B, nsaves > 1 ? saves[1] : NULL, 22);
    fill_state(&C, nsaves > 2 ? saves[2] : NULL, 33); fill_state(&D, NULL, 44);
    regions(&A, r); assert(lst_save(LST_SLOT1, r, 3, 7, work, io) == LST_OK);
    regions(&B, r); assert(lst_save(LST_SLOT2, r, 3, 7, work, io) == LST_OK);
    regions(&C, r); assert(lst_save(LST_SLOT3, r, 3, 7, work, io) == LST_OK);
    regions(&D, r); assert(lst_save(LST_AUTO, r, 3, 7, work, io) == LST_OK);
    state_t *want[LST_KINDS] = { 0, &A, &B, &C, &D };
    for (unsigned k = 1; k < LST_KINDS; k++) {
        memset(&X, 0, sizeof X); regions(&X, rx); int full;
        assert(lst_load(k, rx, 3, 7, ring, io, &full) == LST_OK && full && same(&X, want[k]));
    }
    lst_info_t inf[LST_KINDS]; uint32_t free_kb = lst_info(inf, 7);
    printf("3 slots + auto stored: %u + %u + %u + %u KiB, %u KiB free, each loads back exactly  ok\n",
           inf[1].kb, inf[2].kb, inf[3].kb, inf[4].kb, free_kb);
    /* Fill the pool with incompressible states until FULL; earlier entries must stay intact. */
    static state_t R; srand(99); for (size_t i = 0; i < sizeof R; i++) ((uint8_t *)&R)[i] = (uint8_t)rand();
    regions(&R, r);
    int rc = lst_save(LST_SLOT2, r, 3, 7, work, io);
    if (rc == LST_FULL) {
        memset(&X, 0, sizeof X); regions(&X, rx); int full;
        assert(lst_load(LST_SLOT2, rx, 3, 7, ring, io, &full) == LST_OK && same(&X, &B));
        puts("incompressible state that does not fit: FULL, old slot 2 still loads             ok");
    } else {
        assert(rc == LST_OK);
        puts("incompressible state fitted (pool had room)                                       ok");
    }
    assert(lst_delete(LST_SLOT3) == LST_OK);
    memset(&X, 0, sizeof X); regions(&X, rx);
    assert(lst_load(LST_SLOT3, rx, 3, 7, ring, io, NULL) == LST_EMPTY);
    regions(&C, r); assert(lst_save(LST_SLOT3, r, 3, 7, work, io) == LST_OK);
    puts("delete slot 3, slot reads empty, save again into the freed space                  ok");

    /* 3. Many autosaves of changing size: the newest always loads (stale headers). */
    for (unsigned i = 0; i < 30; i++) {
        fill_state(&D, nsaves ? saves[i % nsaves] : NULL, 100 + i); regions(&D, r);
        assert(lst_save(LST_AUTO, r, 3, 7, work, io) == LST_OK);
        memset(&X, 0, sizeof X); regions(&X, rx);
        assert(lst_load(LST_AUTO, rx, 3, 7, ring, io, NULL) == LST_OK && same(&X, &D));
        for (unsigned k = 1; k <= 3; k++) {
            memset(&X, 0, sizeof X); regions(&X, rx);
            assert(lst_load(k, rx, 3, 7, ring, io, NULL) == LST_OK);
        }
    }
    puts("30 autosaves of changing size: newest loads, slots 1-3 still load each time      ok");

    /* 4a. Tight pool: autosave overwrites in place; a power cut may lose only the
       autosave, never a slot. */
    {
        static uint8_t snap0[0x100000]; memcpy(snap0, flash, sizeof flash);
        static state_t T; fill_state(&T, nsaves > 4 ? saves[4] : NULL, 777); regions(&T, r);
        ops_left = 1L << 30; int rc2 = lst_save(LST_AUTO, r, 3, 7, work, io);
        long tot2 = (1L << 30) - ops_left; ops_left = -1;
        assert(rc2 == LST_OK);
        for (long k = 0; k < tot2; k++) {
            memcpy(flash, snap0, sizeof flash);
            ops_left = k; lst_save(LST_AUTO, r, 3, 7, work, io); ops_left = -1;
            for (unsigned s = 1; s <= 3; s++) { regions(&X, rx); assert(lst_load(s, rx, 3, 7, ring, io, NULL) == LST_OK); }
            regions(&X, rx); int lr = lst_load(LST_AUTO, rx, 3, 7, ring, io, NULL);
            assert(lr == LST_OK || lr == LST_EMPTY);
        }
        memcpy(flash, snap0, sizeof flash);
        printf("tight pool, power cut after each of %ld ops of an autosave: slots 1-3 always intact  ok\n", tot2);
    }

    /* 4b. Room available (fresh pool with slot 1, slot 3 and an autosave): power cut
       after every flash op of an autosave replacing the current one never loses it. */
    memset(flash, 0xFF, sizeof flash);
    regions(&A, r); assert(lst_save(LST_SLOT1, r, 3, 7, work, io) == LST_OK);
    regions(&C, r); assert(lst_save(LST_SLOT3, r, 3, 7, work, io) == LST_OK);
    regions(&D, r); assert(lst_save(LST_AUTO, r, 3, 7, work, io) == LST_OK);
    static uint8_t snap[0x100000]; memcpy(snap, flash, sizeof flash);
    state_t old = D;
    fill_state(&B, nsaves > 3 ? saves[3] : NULL, 555); regions(&B, r);
    ops_left = 1L << 30; assert(lst_save(LST_AUTO, r, 3, 7, work, io) == LST_OK);
    long total = (1L << 30) - ops_left; ops_left = -1;
    unsigned got_old = 0, got_new = 0;
    for (long k = 0; k < total; k++) {
        memcpy(flash, snap, sizeof flash);
        ops_left = k; lst_save(LST_AUTO, r, 3, 7, work, io); ops_left = -1;
        memset(&X, 0, sizeof X); regions(&X, rx);
        assert(lst_load(LST_AUTO, rx, 3, 7, ring, io, NULL) == LST_OK);
        if (same(&X, &B)) got_new++; else { assert(same(&X, &old)); got_old++; }
        for (unsigned s = 1; s <= 3; s += 2) { regions(&X, rx); assert(lst_load(s, rx, 3, 7, ring, io, NULL) == LST_OK); }
    }
    printf("room available, power cut after each of %ld flash ops: old state %u times, new %u times, never broken; slots intact  ok\n",
           total, got_old, got_new);

    /* 5. Different build: only cartridge RAM comes back. */
    memset(&X, 0, sizeof X); regions(&X, rx); int full = 1;
    assert(lst_load(LST_SLOT1, rx, 3, 8, ring, io, &full) == LST_OK && !full);
    assert(!memcmp(X.cart, A.cart, sizeof A.cart));
    for (size_t i = 0; i < sizeof X.gb; i++) assert(X.gb[i] == 0);
    puts("state from another build: song memory restored, emulator state left alone         ok");

    /* 6. Wear: 300 cartridge-RAM-only autosaves next to two slots. */
    memset(flash, 0xFF, sizeof flash); memset(erases, 0, sizeof erases);
    regions(&A, r); assert(lst_save(LST_SLOT1, r, 3, 7, work, io) == LST_OK);
    regions(&C, r); assert(lst_save(LST_SLOT2, r, 3, 7, work, io) == LST_OK);
    memset(erases, 0, sizeof erases);
    for (unsigned i = 0; i < 300; i++) {
        fill_state(&D, nsaves ? saves[i % nsaves] : NULL, 900 + i);
        lst_region_t c1 = { D.cart, sizeof D.cart };
        assert(lst_save(LST_AUTO, &c1, 1, 7, work, io) == LST_OK);
        static uint8_t back[131072]; lst_region_t b1 = { back, sizeof back };
        assert(lst_load(LST_AUTO, &b1, 1, 7, ring, io, NULL) == LST_OK && !memcmp(back, D.cart, sizeof back));
    }
    unsigned mx = 0, used = 0;
    for (unsigned k = 0; k < LST_SECTORS; k++) { if (erases[k] > mx) mx = erases[k]; used += erases[k] > 0; }
    printf("300 autosaves: erases spread over %u of %u sectors, busiest sector erased %u times  ok\n", used, LST_SECTORS, mx);
    for (unsigned k = 1; k <= 2; k++) { regions(&X, rx); assert(lst_load(k, rx, 3, 7, ring, io, NULL) == LST_OK); }

    assert(outside == 0);
    puts("all flash writes inside 0x97000..0xE0000                                          ok");
    puts("lsdj_state test passed");
    return 0;
}
