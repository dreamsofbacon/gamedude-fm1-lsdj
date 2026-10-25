/* SPDX-License-Identifier: GPL-3.0-only */
/* Smoke-test a browser-generated slot with the actual emulator, without hardware. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb_port.h"
#include "lsdj_slot.h"

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: lsdj_rom SLOT\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 2; }
    long len = ftell(f);
    if (len < 256 || len > 256 + 128 * 16384 || fseek(f, 0, SEEK_SET)) { fclose(f); return 2; }
    uint8_t *slot = malloc((size_t)len);
    if (!slot) { fclose(f); return 2; }
    if (fread(slot, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(slot); return 2; }
    fclose(f);
    lsdj_slot_t map;
    int error = lsdj_slot_parse(slot, (uint32_t)len, &map);
    if (!error) {
        gp_set_bank_map(map.bank_map, map.logical_banks);
        error = gp_load(slot + LSDJ_SLOT_HDR, map.logical_banks * LSDJ_SLOT_BANK);
    }
    int16_t audio[740 * 2];
    for (unsigned i = 0; !error && i < 600; i++) {
        gp_frame(); gp_audio_read(audio, 740); error = (int)gp_error();
    }
    if (error || gp_frame_count() != 600 || gp_save_size() != 131072 || strncmp(gp_title(), "LSDj", 4)) {
        fprintf(stderr, "FAIL slot emulation: error=%d frames=%u\n", error, gp_frame_count());
        free(slot); return 1;
    }
    printf("PASS slot smoke: %s, 600 frames, 128 KiB save RAM (not a musical accuracy test)\n", gp_title());
    free(slot);
    return 0;
}
