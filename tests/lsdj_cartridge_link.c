/* SPDX-License-Identifier: GPL-3.0-only */
/* Real read-only protocol over stdin/stdout for the website's transfer tests. */
#include <stdio.h>
#include <stdlib.h>
#include "lsdj_link.h"
#include "lsdj_slot.h"

static uint8_t *slot;
static uint32_t length;
static uint32_t no_save(void) { return 0; }
static const volatile uint8_t *cart(uint32_t *len) { *len = length; return slot; }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: lsdj_cartridge_link SLOT\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 2; }
    long len = ftell(f);
    if (len < 256 || len > 256 + 128 * 16384 || fseek(f, 0, SEEK_SET)) { fclose(f); return 2; }
    slot = malloc((size_t)len);
    if (!slot) { fclose(f); return 2; }
    if (fread(slot, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(slot); return 2; }
    fclose(f); length = (uint32_t)len;
    lsdj_slot_t parsed;
    if (lsdj_slot_parse(slot, length, &parsed)) { fprintf(stderr, "invalid slot\n"); free(slot); return 2; }
    const lsdj_link_io_t io = {.size = no_save, .cartridge = cart};
    lsdj_link_t state = {0};
    char line[128];
    uint8_t q[32], reply[LSDJ_LINK_REPLY_MAX];
    setvbuf(stdout, 0, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        unsigned n = 0, v; int consumed; char *p = line;
        while (n < sizeof q && sscanf(p, "%2x%n", &v, &consumed) == 1) {
            q[n++] = (uint8_t)v; p += consumed;
        }
        if (n < 6 || q[0] != 0xf0 || q[n - 1] != 0xf7 || !lsdj_link_match(q + 1, n - 2) ||
            (q[4] != LSDJ_LINK_CART_INFO && q[4] != LSDJ_LINK_CART_READ)) {
            fprintf(stderr, "unexpected non-read request\n"); free(slot); return 1;
        }
        uint32_t count = lsdj_link_handle(&state, &io, q + 1, n - 2, 0, reply);
        for (uint32_t i = 0; i < count; i++) printf("%02X%s", reply[i], i + 1 < count ? " " : "\n");
    }
    free(slot);
    return 0;
}
