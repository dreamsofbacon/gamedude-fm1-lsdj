/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_ftl.h on a simulated NOR flash: random edits,
   saves interleaved with playback (no erase allowed) and silence, power cuts at
   random points including torn page programs. Usage: lsdj_ftl [save.sav] */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t flash[0x100000];
static long ops_left = -1;
static int sound_playing;
static unsigned long erase_while_playing, outside, erases[256];
static int cut(void) { if (ops_left == 0) return 1; if (ops_left > 0) ops_left--; return 0; }
static int sim_erase(uint32_t off) {
    if (cut()) return -9;
    if ((off & 0xFFFu) || off < 0x97000u || off + 0x1000u > 0xE0000u) { outside++; return -1; }
    if (sound_playing) erase_while_playing++;
    erases[(off - 0x97000u) >> 12]++;
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

static uint8_t ram[131072], committed[131072], pending_img[131072], boot_ram[131072];
static ftl_t F;

static uint32_t edit(void) {                          /* change 1-3 random blocks */
    uint32_t mask = 0, nb = 1 + (uint32_t)rand() % 3;
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t b = (uint32_t)rand() % 8;            /* LSDj edits cluster in the first 32 KiB */
        if (rand() % 4 == 0) b = (uint32_t)rand() % 32;
        for (int k = 0; k < 1 + rand() % 40; k++) ram[b * 4096u + (uint32_t)rand() % 4096u] = (uint8_t)rand();
        mask |= 1u << b;
    }
    return mask;
}

int main(int argc, char **argv) {
    memset(flash, 0xFF, sizeof flash);
    srand(1234);
    if (argc > 1) { FILE *f = fopen(argv[1], "rb"); assert(f && fread(ram, 1, sizeof ram, f) == sizeof ram); fclose(f); }
    else for (size_t i = 0; i < sizeof ram; i++) ram[i] = (uint8_t)rand();
    /* An old-format save occupies sectors 0..33: format must not touch them until done. */
    uint8_t keep[FTL_N] = {0}; for (int s = 0; s < 34; s++) { keep[s] = 1; memset(flash + 0x97000 + s * 4096, 0x5A, 4096); }
    assert(ftl_format(&F, ram, keep) == 0);
    memcpy(committed, ram, sizeof ram);
    assert(ftl_boot(&F, boot_ram) == 1 && !memcmp(boot_ram, committed, sizeof ram));
    puts("format from an old save, boot reloads it exactly                         ok");

    unsigned saves = 0, cuts = 0, got_new = 0, got_old = 0, map_gens0 = F.map_gen;
    uint32_t pending_mask = 0;
    for (int round = 0; round < 6000; round++) {
        pending_mask |= edit();
        sound_playing = rand() % 3 != 0;               /* mostly playing */
        if (ftl_begin(&F, pending_mask) == 0) { memcpy(pending_img, ram, sizeof ram); pending_mask = 0; }
        int cut_now = rand() % 25 == 0;
        if (cut_now) ops_left = rand() % 40;
        for (int k = 0; k < 120; k++) {
            int r = ftl_step(&F, ram, !sound_playing);
            if (r == 1) { memcpy(committed, pending_img, sizeof ram); saves++; }
            if (ops_left == 0) break;
        }
        if (cut_now) {                                 /* power cut, reboot */
            ops_left = -1; cuts++; sound_playing = 0;
            memset(boot_ram, 0x00, sizeof boot_ram);
            assert(ftl_boot(&F, boot_ram) == 1);
            if (!memcmp(boot_ram, committed, sizeof ram)) got_old++;
            else { assert(!memcmp(boot_ram, pending_img, sizeof ram)); got_new++; memcpy(committed, pending_img, sizeof ram); }
            memcpy(ram, boot_ram, sizeof ram); pending_mask = 0;
        }
    }
    /* finish any pending work in silence and check the final save */
    sound_playing = 0;
    if (pending_mask && ftl_begin(&F, pending_mask) == 0) memcpy(pending_img, ram, sizeof ram);
    for (int k = 0; k < 4000; k++) if (ftl_step(&F, ram, 1) == 1) memcpy(committed, pending_img, sizeof ram);
    assert(ftl_boot(&F, boot_ram) == 1 && !memcmp(boot_ram, ram, sizeof ram));
    printf("6000 rounds: %u saves, %u power cuts (reboot gave the previous save %u times, the new one %u times, never a mix)  ok\n",
           saves, cuts, got_old, got_new);
    printf("map compactions: %u                                                        ok\n", F.map_gen - map_gens0);
    assert(F.map_gen - map_gens0 > 2);
    printf("erases while sound was playing: %lu                                         %s\n", erase_while_playing,
           erase_while_playing ? "FAIL" : "ok");
    assert(erase_while_playing == 0);
    unsigned long mx = 0, mn = ~0ul; for (unsigned s = 0; s < FTL_N; s++) { if (erases[s] > mx) mx = erases[s]; if (erases[s] < mn) mn = erases[s]; }
    printf("wear: erases per sector min %lu, max %lu                                ok\n", mn, mx);
    assert(outside == 0);
    puts("all flash access inside 0x97000..0xE0000                                 ok");
    puts("lsdj_ftl test passed");
    return 0;
}
