/* SPDX-License-Identifier: GPL-3.0-only */
/* Song row cue (ARP): hold ARP, K1 picks a song row (K2 by 16), let go: every
   playing channel goes to that row when its current phrase ends (the next bar),
   from the chain's first phrase, and the song carries on from there.
   How: LSDj moves to the next song row when a phrase ends at the end of its
   chain. Setting a channel's song row to N-1 and its place in the chain to the
   last (15) makes the current phrase end its chain, so LSDj goes to row N,
   phrase 1, by itself. Written at any point of the phrase, it lands on the
   phrase end (emulator: harness/cue_check3.sh). LSDj 9.3.9 addresses: song rows
   C200-C203, chain places C17C-C17F, the song table (256 rows x 4 chains, FF =
   none) at cartridge RAM 0x1290 - passed in by board.c.
   Pure logic: tests/lsdj_cue.c. */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t held;      /* ARP down: choosing */
    uint8_t turned;    /* a knob moved since ARP went down */
    uint8_t row;       /* the row being chosen / cued */
    uint8_t pending;   /* let go after choosing: write it as soon as something plays */
    uint8_t waiting;   /* written: until a channel is on the row (ARP lit) */
    uint32_t waited;   /* frames since written */
} lsdj_cue_t;

static int lsdj_cue_first(const uint8_t playing[4]) { for (int c = 0; c < 4; c++) if (playing[c]) return c; return -1; }

/* ARP down: start from the row after the one playing (row 00 when stopped). */
static void lsdj_cue_open(lsdj_cue_t *q, const uint8_t playing[4], const uint8_t song_rows[4]) {
    int c = lsdj_cue_first(playing);
    q->held = 1; q->turned = 0;
    q->row = c < 0 ? 0u : (uint8_t)(song_rows[c] + 1u);
}
/* While held: K1 clicks move the row by 1, K2 clicks by 16 (wrapping 00..FF). */
static void lsdj_cue_turn(lsdj_cue_t *q, int fine, int coarse) {
    if (!q->held || (!fine && !coarse)) return;
    q->row = (uint8_t)(q->row + fine + 16 * coarse);
    q->turned = 1;
}
/* ARP up: cue the row if a knob was turned (a press alone only shows the row). */
static int lsdj_cue_release(lsdj_cue_t *q) {
    int cue = q->held && q->turned;
    q->held = 0;
    if (cue) { q->pending = 1; q->waiting = 0; }
    return cue;
}
/* Once per frame. When pending and something plays: the writes (addresses and
   values, up to 8) that cue it; returns how many (0: nothing to write now).
   Also ends `waiting` once a playing channel is on the row (or after ~4 s). */
static unsigned lsdj_cue_frame(lsdj_cue_t *q, const uint8_t playing[4], const uint8_t song_rows[4],
                               uint16_t song_row_addr, uint16_t chain_pos_addr, uint16_t addr[8], uint8_t val[8]) {
    int c0 = lsdj_cue_first(playing);
    if (q->waiting) {
        if ((c0 >= 0 && song_rows[c0] == q->row) || ++q->waited > 240u) q->waiting = 0;
    }
    if (!q->pending || c0 < 0) return 0;
    unsigned n = 0;
    for (unsigned c = 0; c < 4; c++) {
        if (!playing[c]) continue;
        addr[n] = (uint16_t)(song_row_addr + c); val[n++] = (uint8_t)(q->row - 1u);
        addr[n] = (uint16_t)(chain_pos_addr + c); val[n++] = 15u;
    }
    q->pending = 0; q->waiting = 1; q->waited = 0;
    return n;
}
static int lsdj_cue_lit(const lsdj_cue_t *q) { return q->held || q->pending || q->waiting; }

/* The pop-up while choosing (fields as lsdj_ov_bars_t): title "CUE ROW 08",
   per channel its chain on that row ("06", "--" for none) and a full bar if it
   has one; the bottom line what to do. song: the song table (row * 4 + channel). */
static void lsdj_cue_bars(const lsdj_cue_t *q, const uint8_t *song, char title[20], char big[20],
                          char label[4][7], char value[4][6], char caption[4][4], uint8_t level[4]) {
    static const char hex[] = "0123456789ABCDEF";
    static const char *const ch[4] = {"P1", "P2", "WV", "NS"};   /* 2 wide, as LSDj's columns */
    static const char t[] = "CUE ROW ";
    unsigned n = 0;
    for (const char *s = t; *s; s++) title[n++] = *s;
    title[n++] = hex[q->row >> 4]; title[n++] = hex[q->row & 15u]; title[n] = 0;
    for (unsigned c = 0; c < 4; c++) {
        uint8_t chain = song ? song[q->row * 4u + c] : 0xFFu;
        unsigned k = 0; for (const char *s = ch[c]; *s; s++) label[c][k++] = *s; label[c][k] = 0;
        if (chain == 0xFFu) { value[c][0] = '-'; value[c][1] = '-'; }
        else { value[c][0] = hex[chain >> 4]; value[c][1] = hex[chain & 15u]; }
        value[c][2] = 0; caption[c][0] = 0;
        level[c] = chain == 0xFFu ? 0u : 127u;
    }
    const char *b = q->turned ? "LET GO: CUE" : "K1 ROW K2 BY 16";
    n = 0; for (const char *s = b; *s; s++) big[n++] = *s; big[n] = 0;
}
