/* SPDX-License-Identifier: GPL-3.0-only */
/* Smoke-test a browser-generated slot with the actual emulator, without hardware. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb_port.h"
#include "lsdj_slot.h"
#include "lsdj_fx_ui.h"
#include <assert.h>

static void frames(unsigned n) {
    int16_t audio[740 * 2];
    while (n--) { gp_frame(); gp_audio_read(audio, 740); }
    assert(!gp_error());
}
static void press(uint32_t button) {
    gp_buttons(button); frames(3); gp_buttons(0); frames(12);
}
static void cursor_test(uint16_t addr) {
    static uint8_t echo[LSDJ_FX_ECHO_LEN];
    static lsdj_fx_t fx;
    lsdj_fx_ui_t u;
    assert(addr == 0xC41D);
    for (unsigned i = 0; i < 4; i++) press(GP_LEFT);
    const unsigned expected[] = {0, 1, 2, 3, 2, 1, 0};
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        if (i) press(i < 4 ? GP_RIGHT : GP_LEFT);
        unsigned c = gp_wram_peek(addr);
        if (c != expected[i]) fprintf(stderr, "cursor step %u: expected %u, got %u\n", i, expected[i], c);
        assert(c == expected[i]);
        lsdj_fx_init(&fx, echo); lsdj_fx_ui_init(&u);
        u.page = FX_PAGE_FILTER;
        u.cursor_ch = (uint8_t)c;
        int32_t turn[4] = {0, 0, 5, 0};
        assert(lsdj_fx_knobs(&fx, &u, turn) == 2);
        for (unsigned ch = 0; ch < 4; ch++)
            assert(fx.p.ch[ch][FXP_CUTOFF] == (ch == c ? 10u : 0u));
    }
    puts("PASS cursor FX: generated slot, real ROM cursor 0/1/2/3/2/1/0, cutoff changes only that channel");
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) { fprintf(stderr, "usage: lsdj_rom SLOT [128-KiB SONG-screen SAVE for cursor test]\n"); return 2; }
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
        gp_set_bank_map(map.bank_map, map.logical_banks, map.start, map.len);
        error = gp_load(slot + map.data_off, map.logical_banks * LSDJ_SLOT_BANK);
    }
    if (!error && argc == 3) {
        f = fopen(argv[2], "rb");
        if (!f) { perror(argv[2]); free(slot); return 2; }
        size_t n = fread(gp_save_data(), 1, GB_PORT_SAVE_MAX, f);
        int extra = fgetc(f), failed = ferror(f);
        fclose(f);
        if (n != GB_PORT_SAVE_MAX || extra != EOF || failed) {
            fprintf(stderr, "Cursor fixture must be exactly 128 KiB\n"); free(slot); return 2;
        }
        gp_reset();
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
    if (map.cursor_addr && argc == 3) cursor_test(map.cursor_addr);
    else puts("Cursor movement not exercised: requires a recognized profile and a SONG-screen save fixture");
    free(slot);
    return 0;
}
