/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for ftl_save_now() (firmware/lsdj_ftl.h), the SAVE button: many
   saves of random edits, a power cut at every possible flash operation of a
   save, and a full-pool case. After each save or cut, a reboot must load either
   the previous save or the new one, never a mix. Usage: lsdj_ftl_save [save.sav] */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t flash[0x100000];
static long ops_left = -1;
static unsigned long outside, ops, erase_total;
static int cut(void) { ops++; if (ops_left == 0) return 1; if (ops_left > 0) ops_left--; return 0; }
static int sim_erase(uint32_t off) {
    if (cut()) return -9;
    if ((off & 0xFFFu) || off < 0x97000u || off + 0x1000u > 0xE0000u) { outside++; return -1; }
    erase_total++;
    memset(flash + off, 0xFF, 0x1000u); return 0;
}
static int sim_write(uint32_t off, const uint8_t *p, uint32_t n) {
    if (off < 0x97000u || off + n > 0xE0000u) { outside++; return -1; }
    for (uint32_t i = 0; i < n; i++) {
        if ((i & 255u) == 0 && cut()) {               /* power cut: tear this page */
            uint32_t torn = (uint32_t)rand() % 256u;
            for (uint32_t k = 0; k < torn && i + k < n; k++) flash[off + i + k] &= p[i + k];
            return -9;
        }
        flash[off + i] &= p[i];
    }
    return 0;
}
static int sim_read(uint32_t off, uint8_t *p, uint32_t n) { memcpy(p, flash + off, n); return 0; }
#define LSDJ_FL_ERASE(off) sim_erase(off)
#define LSDJ_FL_WRITE(off, p, n) sim_write(off, p, n)
#define LSDJ_FL_READ(off, p, n) sim_read(off, p, n)
#define LSDJ_FL_IDLE() ((void)0)
#include "../firmware/lsdj_ftl.h"

static uint8_t ram[131072], prev[131072], boot_ram[131072];
static ftl_t F;

static void edit(void) {
    uint32_t nb = 1 + (uint32_t)rand() % 4;
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t b = rand() % 3 ? (uint32_t)rand() % 8 : (uint32_t)rand() % 32;
        for (int k = 0; k < 1 + rand() % 60; k++) ram[b * 4096u + (uint32_t)rand() % 4096u] = (uint8_t)rand();
    }
}
static int reboot_is(const uint8_t *want) {
    static ftl_t B; memset(boot_ram, 0, sizeof boot_ram);
    return ftl_boot(&B, boot_ram) == 1 && !memcmp(boot_ram, want, sizeof boot_ram);
}

int main(int argc, char **argv) {
    memset(flash, 0xFF, sizeof flash);
    srand(4321);
    if (argc > 1) { FILE *f = fopen(argv[1], "rb"); assert(f && fread(ram, 1, sizeof ram, f) == sizeof ram); fclose(f); }
    else for (size_t i = 0; i < sizeof ram; i++) ram[i] = (uint8_t)rand();
    assert(ftl_format(&F, ram, NULL) == 0);
    assert(ftl_boot(&F, boot_ram) == 1 && !memcmp(boot_ram, ram, sizeof ram));

    /* Nothing changed: nothing written. */
    unsigned long o = ops;
    assert(ftl_save_now(&F, ram) == 0 && ops == o);
    puts("SAVE with no change writes nothing                                ok");

    /* Many saves: each reboots to exactly what was saved; no stale sectors left. */
    unsigned long most_ops = 0;
    for (int n = 0; n < 2000; n++) {
        edit();
        o = ops;
        assert(ftl_save_now(&F, ram) == 1);
        if (ops - o > most_ops) most_ops = ops - o;
        assert(ftl_count(&F, FTL_DIRTY) == 0);
        assert(reboot_is(ram));
    }
    printf("2000 saves, each reloads exactly; worst save %lu flash ops; no stale sectors  ok\n", most_ops);

    /* Every block changed (e.g. an imported .sav). */
    for (size_t i = 0; i < sizeof ram; i++) ram[i] ^= 0x5A;
    assert(ftl_save_now(&F, ram) == 1 && reboot_is(ram));
    puts("SAVE with all 32 blocks changed                                    ok");

    /* Power cut at every flash operation of a save: old or new, never a mix. */
    unsigned old_seen = 0, new_seen = 0;
    for (long at = 0;; at++) {
        assert(ftl_boot(&F, ram) == 1);
        for (uint32_t b = 0; b < 32; b++)
            if (F.map_of[b] == FTL_NONE) { printf("round %ld starts with block %u unreadable\n", at, b); return 1; }
        memcpy(prev, ram, sizeof ram);
        edit(); if (at % 7 == 0) for (uint32_t b = 0; b < 32; b += 3) ram[b * 4096u] ^= 0xFF;
        ops_left = at;
        int r = ftl_save_now(&F, ram);
        int finished = ops_left != 0;  /* the cut point was never reached */
        ops_left = -1;
        if (reboot_is(prev)) old_seen++;
        else if (reboot_is(ram)) new_seen++;
        else {   /* say which blocks came back old, new or neither */
            static ftl_t B2; memset(boot_ram, 0, sizeof boot_ram);
            int ok = ftl_boot(&B2, boot_ram);
            printf("MIX: cut at flash op %ld, save returned %d, boot %d:", at, r, ok);
            for (uint32_t b = 0; b < 32; b++) {
                int po = !memcmp(boot_ram + b * 4096u, prev + b * 4096u, 4096);
                int no = !memcmp(boot_ram + b * 4096u, ram + b * 4096u, 4096);
                if (!po || !no) printf(" b%u=%s", b, po ? "old" : no ? "new" : "neither");
                if (!po && !no) {
                    uint32_t s = F.map_of[b], blank = 1;
                    for (uint32_t k = 0; k < 4096u && s < FTL_N; k++) blank &= flash[0x97000u + s * 4096u + k] == 0xFF;
                    printf(" [saver says sector %u, reboot says %u, sector %s, ram==prev:%d]", s, B2.map_of[b],
                           blank ? "ERASED" : "has data", !memcmp(ram + b * 4096u, prev + b * 4096u, 4096));
                }
            }
            printf("\n");
            return 1;
        }
        if (finished) { assert(r == 1); break; }
        memcpy(ram, prev, sizeof ram);   /* not used further: next round reboots */
    }
    printf("power cut at each flash op of a save: previous save %u times, new save %u times, never a mix  ok\n",
           old_seen, new_seen);
    /* Settings records (settings menu). */
    {
        static ftl_t B;
        assert(ftl_boot(&F, ram) == 1);
        memcpy(prev, ram, sizeof ram);
        assert(!F.has_settings);                                   /* old maps: none, defaults */
        assert(ftl_put_settings(&F, 0x5A000011u) == 0);
        assert(ftl_boot(&B, boot_ram) == 1 && B.has_settings && B.settings == 0x5A000011u);
        assert(!memcmp(boot_ram, prev, sizeof ram));               /* songs untouched */
        /* Fill the map with settings records until it must compact; songs and the newest value survive. */
        uint32_t gen0 = F.map_gen;
        for (uint32_t i = 0; F.map_gen == gen0 && i < 400; i++) assert(ftl_put_settings(&F, 0x5A000000u | (i & 0x3Fu)) == 0);
        assert(F.map_gen != gen0);
        assert(ftl_boot(&B, boot_ram) == 1 && B.settings == F.settings && !memcmp(boot_ram, prev, sizeof ram));
        /* A song save after that keeps the settings (compaction carries them). */
        edit(); assert(ftl_save_now(&F, ram) == 1);
        assert(ftl_boot(&B, boot_ram) == 1 && B.settings == F.settings && !memcmp(boot_ram, ram, sizeof ram));
        printf("settings: kept across reboot, %u-record map compaction and a song save   ok\n", FTL_ENT_PER_MAP);
        /* Power cut at each flash op of a settings write: old or new value, songs intact. */
        unsigned o = 0, n = 0;
        for (long at = 0;; at++) {
            assert(ftl_boot(&F, ram) == 1);
            uint32_t was = F.settings, want = was ^ 0x40u;
            memcpy(prev, ram, sizeof ram);
            ops_left = at; int r = ftl_put_settings(&F, want); int finished = ops_left != 0; ops_left = -1;
            assert(ftl_boot(&B, boot_ram) == 1 && !memcmp(boot_ram, prev, sizeof ram));
            if (B.settings == was) o++; else { assert(B.settings == want); n++; }
            if (finished) { assert(r == 0 && B.settings == want); break; }
        }
        printf("power cut during a settings write: old value %u times, new %u times, songs intact  ok\n", o, n);
    }
    /* Numbered records 1.. (effects setup), next to record 0 (settings). */
    {
        static ftl_t B;
        assert(ftl_boot(&F, ram) == 1);
        memcpy(prev, ram, sizeof ram);
        uint32_t s0 = F.settings;
        for (unsigned n = 1; n <= 10; n++) assert(ftl_put_record(&F, n, 0xA0000000u + n * 7u) == 0);
        assert(ftl_put_record(&F, 3, 0x12345678u) == 0);          /* newest wins */
        assert(ftl_put_record(&F, FTL_RECS, 1) == -1);            /* out of range */
        assert(ftl_boot(&B, boot_ram) == 1 && !memcmp(boot_ram, prev, sizeof ram) && B.settings == s0);
        for (unsigned n = 1; n <= 10; n++) {
            assert(B.rec_has & (1u << n));
            assert(B.rec[n] == (n == 3 ? 0x12345678u : 0xA0000000u + n * 7u));
        }
        /* compaction (fill the map) and a song save keep them all */
        uint32_t gen0 = F.map_gen;
        for (uint32_t i = 0; F.map_gen == gen0 && i < 400; i++) assert(ftl_put_record(&F, 1, 0xB0000000u + i) == 0);
        edit(); assert(ftl_save_now(&F, ram) == 1);
        assert(ftl_boot(&B, boot_ram) == 1 && !memcmp(boot_ram, ram, sizeof ram));
        assert(B.rec[1] == F.rec[1] && B.rec[3] == 0x12345678u && B.rec[10] == 0xA0000000u + 70u && B.settings == s0);
        puts("records 1..10 next to the settings word: kept, newest wins, carried by compaction  ok");
    }
    /* SAVE while playing (board.c play_save_*): ftl_begin, then one ftl_step per
       frame with silent = 0 while the song keeps changing RAM. Never an erase; a
       reboot gives, per saved block, the snapshot taken when the save reached it,
       and the old copy of every other block. */
    {
        static ftl_t B; static uint8_t snap[131072];
        unsigned saves = 0, skipped = 0;
        for (int round = 0; round < 200; round++) {
            assert(ftl_boot(&F, ram) == 1);
            while (ftl_count(&F, FTL_DIRTY)) ftl_step(&F, ram, 1);   /* as after a SAVE with the song stopped */
            memcpy(prev, ram, sizeof ram); memcpy(snap, ram, sizeof ram);
            edit();
            uint32_t mask = ftl_changed(&F, ram);
            int rc = ftl_begin(&F, mask);
            if (rc == -2) { F.retry = 0; skipped++; continue; }
            assert(rc == 0);
            unsigned long erases0 = erase_total;
            int steps = 0, committed = 0;
            while (ftl_busy(&F)) {
                int was = F.cur_block;
                int r = ftl_step(&F, ram, 0);
                assert(r >= 0);
                if (r == 1) committed = 1;
                if (F.cur_block >= 0 && F.cur_block != was) memcpy(snap + F.cur_block * 4096u, F.buf, 4096u);
                if (++steps % 3 == 0) edit();                        /* the song keeps changing */
            }
            assert(committed && erase_total == erases0);
            assert(ftl_boot(&B, boot_ram) == 1);
            for (uint32_t b = 0; b < 32; b++)
                assert(!memcmp(boot_ram + b * 4096u, ((mask >> b) & 1u ? snap : prev) + b * 4096u, 4096));
            saves++;
        }
        printf("SAVE while playing: %u saves, edits between steps, no erase, blocks = snapshots (%u skipped)  ok\n", saves, skipped);
        assert(saves > 150);
    }
    assert(outside == 0);
    puts("all flash access inside 0x97000..0xE0000                         ok");
    puts("lsdj_ftl_save test passed");
    return 0;
}
