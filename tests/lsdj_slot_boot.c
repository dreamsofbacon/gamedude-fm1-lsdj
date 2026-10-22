/* SPDX-License-Identifier: GPL-3.0-only */
/* Boot a ROM in GameDude's core (src/gb_port.c), either straight from a .gb
   file or from an LSDj slot built by the web installer, the way firmware/board.c
   does. Prints a hash of the screen after N frames; writes it as a PPM.
     lsdj_slot_boot rom FILE.gb  FRAMES OUT.ppm
     lsdj_slot_boot slot SLOT.bin FRAMES OUT.ppm   (tests/lsdj_boot.mjs) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb_port.h"
#include "lsdj_slot.h"

static uint8_t *read_file(const char *path, uint32_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); *n = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*n); if (fread(p, 1, *n, f) != *n) exit(2); fclose(f); return p;
}

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    uint32_t n; uint8_t *data = read_file(argv[2], &n);
    static lsdj_slot_t slot;
    int rc;
    if (!strcmp(argv[1], "slot")) {
        int parsed = lsdj_slot_parse(data, n, &slot);
        if (parsed) { printf("slot parse error %d\n", parsed); return 1; }
        gp_set_bank_map(slot.bank_map, slot.logical_banks);
        rc = gp_load(data + LSDJ_SLOT_HDR, slot.logical_banks * LSDJ_SLOT_BANK);
        printf("slot: %u logical banks, %u stored, %u kits\n", slot.logical_banks, slot.stored_banks, slot.kits);
    } else rc = gp_load(data, n);
    if (rc) { printf("gp_load error %d\n", rc); return 1; }
    int16_t audio[2048 * 2];
    for (int f = 0, frames = atoi(argv[3]); f < frames; ) {
        if (gp_frame()) f++;
        gp_audio_read(audio, 2048);
    }
    const uint16_t *px = gp_pixels();
    uint64_t h = 1469598103934665603ull;
    for (unsigned i = 0; i < 160 * 144; i++) { h ^= px[i]; h *= 1099511628211ull; }
    printf("screen %016llx title %s\n", (unsigned long long)h, gp_title());
    FILE *o = fopen(argv[4], "wb"); fprintf(o, "P6 160 144 255\n");
    for (unsigned i = 0; i < 160 * 144; i++) {
        uint8_t rgb[3] = {(uint8_t)((px[i] >> 11) << 3), (uint8_t)(((px[i] >> 5) & 63) << 2), (uint8_t)((px[i] & 31) << 3)};
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
    return 0;
}
