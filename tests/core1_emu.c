/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for the emulator-on-CPU1 handover (gp_core1_step, src/gb_port.c):
   a thread plays CPU1, main plays CPU0.
     - the sound stream equals a plain single-core run, with a button press
       made while CPU1 is parked;
     - while parked, no frame advances and cartridge RAM does not change;
     - a picture never changes between "ready" (pic_seq) and CPU0's ack;
     - note-mode key codes can be queued while CPU1 runs.
   Usage: core1_emu ROM SAV */
#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gb_port.h"

static uint8_t *load(const char *p, uint32_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); *n = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) exit(2); fclose(f); return b;
}
static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p; while (n--) { h ^= *b++; h *= 1099511628211ull; } return h;
}
static uint8_t *rom, *sav; static uint32_t rom_n, sav_n;
static void boot(void) {
    assert(gp_load(rom, rom_n) == 0);
    memcpy(gp_save_data(), sav, sav_n < gp_save_size() ? sav_n : gp_save_size());
    gp_reset();
}
static void nap(long us) { struct timespec t = {0, us * 1000L}; nanosleep(&t, 0); }

#define TOTAL 1500u        /* frames compared */
static int16_t buf[512 * 2];

/* Reference: one core, START held for frames [press, release). */
static uint64_t reference(uint32_t press, uint32_t release) {
    boot(); gp_render(0);
    uint64_t h = 1469598103934665603ull;
    for (uint32_t f = 0; f < TOTAL; ) {
        gp_buttons(f >= press && f < release ? GP_START : 0);
        if (gp_frame()) f++;
        uint32_t q;
        while ((q = gp_audio_queued()) > 0) { uint32_t n = q > 512 ? 512 : q; gp_audio_read(buf, n); h = fnv(h, buf, n * 4); }
    }
    return h;
}

static volatile int stop;
static void *cpu1(void *arg) { (void)arg; while (!stop && gp_core1.frames < TOTAL) gp_core1_step(); return 0; }

static uint32_t pause_gen;
static void park(void) {
    gp_core1.pause_gen = ++pause_gen; __sync_synchronize(); gp_core1.pause_req = 1;
    while (gp_core1.paused_gen != pause_gen) ;
    __sync_synchronize();
}
static void unpark(void) { __sync_synchronize(); gp_core1.pause_req = 0; }

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    rom = load(argv[1], &rom_n); sav = load(argv[2], &sav_n);
    srand(7);

    boot();
    gp_core1.pic_seq = gp_core1.pic_ack = 0; gp_core1.audio_target = GP_AUDIO_TARGET;
    pthread_t t; pthread_create(&t, 0, cpu1, 0);
    uint64_t h = 1469598103934665603ull;
    uint32_t consumed = 0, press = 0, pressed = 0, released = 0, pics = 0, parks = 0;
    while (gp_core1.frames < TOTAL || gp_audio_queued()) {
        /* Sound: drain whatever is there (stops at TOTAL frames' worth). */
        uint32_t q = gp_audio_queued();
        if (q) { uint32_t n = q > 512 ? 512 : q; gp_audio_read(buf, n); h = fnv(h, buf, n * 4); consumed += n; }
        /* Picture: stable from ready until ack. */
        if (gp_core1.pic_seq != gp_core1.pic_ack) {
            __sync_synchronize();
            uint64_t a = fnv(1, gp_pixels(), 160 * 144 * 2);
            nap(rand() % 3000);
            uint64_t b = fnv(1, gp_pixels(), 160 * 144 * 2);
            assert(a == b);
            gp_core1.pic_ack = gp_core1.pic_seq; pics++;
        }
        /* A button press made while parked, at whatever frame CPU1 reached. */
        if (!pressed && gp_core1.frames >= 300) {
            park(); press = gp_core1.frames; gp_buttons(GP_START); pressed = 1; unpark();
        }
        if (pressed && !released && gp_core1.frames >= press + 4) {
            park(); released = gp_core1.frames; gp_buttons(0); unpark();
        }
        /* Parking: nothing moves. */
        static uint32_t last_park;
        if (gp_core1.frames > 100 && gp_core1.frames < TOTAL - 10 && gp_core1.frames / 97 != last_park && parks < 20) {
            park();
            uint32_t f0 = gp_core1.frames; uint64_t s0 = fnv(1, gp_save_data(), gp_save_size());
            nap(2000);
            assert(gp_core1.frames == f0 && fnv(1, gp_save_data(), gp_save_size()) == s0);
            last_park = f0 / 97; parks++; unpark();
        }
    }
    stop = 1; pthread_join(t, 0);
    printf("  CPU1 run: %u frames, %u pictures, %u parks, START at frame %u..%u\n", gp_core1.frames, pics, parks, press, released);
    assert(gp_core1.frames == TOTAL && pics > 100 && parks >= 10);
    assert(reference(press, released) == h);
    puts("  sound stream identical to a single-core run with the same press and release frames");

    /* Note-mode key codes queued by CPU0 while CPU1 consumes them (a song in
       SYNC=KEYBD plays them, so this phase is not compared). */
    boot(); stop = 0;
    gp_core1.frames = 0; gp_core1.pic_seq = gp_core1.pic_ack = 0;
    pthread_create(&t, 0, cpu1, 0);
    uint32_t sent = 0;
    while (gp_core1.frames < TOTAL) {
        uint32_t q = gp_audio_queued();
        if (q) gp_audio_read(buf, q > 512 ? 512 : q);
        if (gp_core1.pic_seq != gp_core1.pic_ack) gp_core1.pic_ack = gp_core1.pic_seq;
        if (gp_keybd_pending() < 1000) { gp_keybd_send((uint8_t)(0x15 + sent % 20)); sent++; }
    }
    stop = 1; pthread_join(t, 0);
    printf("  key queue: %u codes sent while CPU1 ran, %u still queued\n", sent, gp_keybd_pending());
    puts("PASS: emulator on a second core: parking, picture handover, sound, key queue");
    return 0;
}
