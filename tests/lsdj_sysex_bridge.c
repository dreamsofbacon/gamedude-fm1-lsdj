/* SPDX-License-Identifier: GPL-3.0-only */
/* Runs firmware/lsdj_sysex.h behind stdin/stdout so tests/lsdj_save.mjs can
   drive the browser client (web/lsdj/lsdj-save.js) against the real handler.
   stdin: F0..F7 messages; stdout: replies. Save RAM starts as argv[1]. */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lsdj_sysex.h"

#define SIZE 131072u
static uint8_t ram[SIZE], flash[SIZE];
static uint8_t *io_save(void) { return ram; }
static uint32_t io_size(void) { return SIZE; }
static void io_pause(int on) { (void)on; }
static int io_store(const uint8_t *p, uint32_t n) { memcpy(flash, p, n); return 0; }
static int io_load(uint8_t *p, uint32_t n) { memcpy(p, flash, n); return 0; }
static void io_reset(void) {}
static const lsdj_sx_io_t io = {io_save, io_size, io_pause, io_store, io_load, io_reset};

static uint32_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(ram, 1, SIZE, f) != SIZE) return 2;
    fclose(f);
    static lsdj_sx_t st;
    uint8_t q[700], r[LSDJ_SX_REPLY_MAX];
    uint32_t n = 0; int c, in = 0;
    while ((c = getchar()) != EOF) {
        if (c == 0xf0) { in = 1; n = 0; continue; }
        if (!in) continue;
        if (c != 0xf7) { if (n < sizeof q) q[n++] = (uint8_t)c; continue; }
        in = 0;
        lsdj_sx_poll(&st, &io, now_ms());
        if (!lsdj_sx_match(q, n)) continue;
        uint32_t rn = lsdj_sx_handle(&st, &io, q, n, now_ms(), r);
        fwrite(r, 1, rn, stdout); fflush(stdout);
    }
    return 0;
}
