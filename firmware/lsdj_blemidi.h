/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI over Bluetooth LE (the MMA BLE-MIDI 1.0 packet format), both ways.

   A packet: a header byte 10tttttt (timestamp bits 12..7), then messages, each
   after a timestamp byte 1ttttttt (bits 6..0). Running status may drop a status
   byte. A SysEx may span packets: a continuation packet is the header and then
   data bytes only; its F7 comes after a timestamp byte. System real-time bytes
   (F8..FF) may sit anywhere, after their own timestamp byte.

   In:  lsdj_bm_rx() takes one packet and calls back with each whole message:
        real-time and short messages (1..3 bytes, status filled in for running
        status) through msg() with the sender's 13-bit millisecond timestamp,
        a SysEx whole (the bytes between F0 and F7) through sysex(). A SysEx
        longer than LSDJ_BM_SX_MAX is dropped whole.
        lsdj_bm_clock() turns a sender timestamp into our milliseconds: when
        the message was sent, less the link's smallest delay seen, so ticks
        keep their spacing whatever the connection interval does to arrival.
   Out: lsdj_bm_tx_put() queues whole messages; lsdj_bm_tx_packet() fills one
        packet of at most max bytes (the ATT MTU - 3), continuing a long SysEx
        over as many packets as it needs.
   Pure: tests/lsdj_blemidi.c, and the page's JS against it (harness). */
#pragma once
#include <stdint.h>

#define LSDJ_BM_SX_MAX 320u     /* a link request: 4 + 8 + 2 x 128 bytes, and room */
#define LSDJ_BM_TX_RING 640u    /* queued output bytes: two link replies (263 each) and room */

typedef void (*lsdj_bm_msg_fn)(void *ctx, const uint8_t *m, uint32_t n, uint32_t ts);
typedef struct {
    uint8_t running, need, got, msg[3], ts_lo;
    uint16_t ts_hi, ts;   /* header bits 12..7; the current message's timestamp */
    uint8_t in_sx, sx_over;
    uint16_t sx_n;
    uint8_t sx[LSDJ_BM_SX_MAX];
    uint32_t packets, bad, sx_dropped;
} lsdj_bm_rx_t;

static inline void lsdj_bm_rx_reset(lsdj_bm_rx_t *r) {
    r->running = r->need = r->got = 0; r->in_sx = r->sx_over = 0; r->sx_n = 0;
    r->packets = r->bad = r->sx_dropped = 0;
}
/* data bytes after a status byte, by status */
static inline uint8_t lsdj_bm_need(uint8_t st) {
    if (st < 0xF0u) { uint8_t hi = st & 0xF0u; return (hi == 0xC0u || hi == 0xD0u) ? 1u : 2u; }
    if (st == 0xF1u || st == 0xF3u) return 1u;
    if (st == 0xF2u) return 2u;
    return 0u;
}
static inline void lsdj_bm_rx_status(lsdj_bm_rx_t *r, uint8_t b, lsdj_bm_msg_fn msg, void *ctx) {
    if (b >= 0xF8u) { msg(ctx, &b, 1, r->ts); return; }          /* real-time: on its own, nothing else changes */
    if (b == 0xF0u) { r->in_sx = 1; r->sx_n = 0; r->sx_over = 0; r->running = 0; r->got = r->need = 0; return; }
    if (b == 0xF7u) return;                                /* a stray end */
    r->in_sx = 0;
    r->msg[0] = b; r->got = 0; r->need = lsdj_bm_need(b);
    r->running = b < 0xF0u ? b : 0u;                       /* system common clears running status */
    if (!r->need) { msg(ctx, r->msg, 1, r->ts); r->running = 0; }
}
static inline void lsdj_bm_rx_data(lsdj_bm_rx_t *r, uint8_t b, lsdj_bm_msg_fn msg, void *ctx) {
    if (!r->need) {                                        /* running status, or a byte with no status */
        if (!r->running) { r->bad++; return; }
        r->msg[0] = r->running; r->got = 0; r->need = lsdj_bm_need(r->running);
    }
    r->msg[1u + r->got++] = b;
    if (r->got == r->need) { msg(ctx, r->msg, 1u + r->need, r->ts); r->need = 0; r->got = 0; }
}
typedef void (*lsdj_bm_sx_fn)(void *ctx, const uint8_t *m, uint32_t n);
/* a timestamp byte: the low 7 bits; a smaller one than the last in the packet has wrapped (the spec) */
static inline void lsdj_bm_rx_ts(lsdj_bm_rx_t *r, uint8_t b) {
    uint8_t lo = b & 0x7Fu;
    if (lo < r->ts_lo) r->ts_hi = (uint16_t)((r->ts_hi + 0x80u) & 0x1F80u);
    r->ts_lo = lo; r->ts = (uint16_t)(r->ts_hi | lo);
}
static inline void lsdj_bm_rx(lsdj_bm_rx_t *r, const uint8_t *p, uint32_t n,
                              lsdj_bm_msg_fn msg, lsdj_bm_sx_fn sysex, void *ctx) {
    if (n < 2u || (p[0] & 0xC0u) != 0x80u) { r->bad++; return; }
    r->packets++;
    r->ts_hi = (uint16_t)((p[0] & 0x3Fu) << 7); r->ts_lo = 0;
    uint32_t i = 1;
    /* a continuation packet of a SysEx: data bytes straight after the header */
    if (r->in_sx) {
        while (i < n && !(p[i] & 0x80u)) {
            if (r->sx_n < LSDJ_BM_SX_MAX) r->sx[r->sx_n++] = p[i]; else r->sx_over = 1;
            i++;
        }
    } else if (!(p[1] & 0x80u)) { r->bad++; return; }      /* a packet's second byte is a timestamp */
    while (i < n) {
        /* a timestamp byte, then a status byte (or data under running status) */
        if (!(p[i] & 0x80u)) { r->bad++; return; }
        lsdj_bm_rx_ts(r, p[i]);
        i++;
        if (i >= n) { r->bad++; return; }
        uint8_t b = p[i];
        if (r->in_sx) {
            if (b == 0xF7u) {                              /* the end */
                r->in_sx = 0; i++;
                if (r->sx_over) r->sx_dropped++; else sysex(ctx, r->sx, r->sx_n);
                continue;
            }
            if (b >= 0xF8u) { msg(ctx, &b, 1, r->ts); i++; goto sx_data; }   /* real-time inside a SysEx */
            r->in_sx = 0; r->sx_dropped++;                 /* another status: the SysEx was cut */
        }
        if (b & 0x80u) { lsdj_bm_rx_status(r, b, msg, ctx); i++; }
        if (r->in_sx) {
sx_data:
            while (i < n && !(p[i] & 0x80u)) {
                if (r->sx_n < LSDJ_BM_SX_MAX) r->sx[r->sx_n++] = p[i]; else r->sx_over = 1;
                i++;
            }
            continue;
        }
        /* the message's data bytes; then more running-status messages may follow without a timestamp
           (BLE-MIDI allows them only after a timestamp, but some senders do; accept both) */
        while (i < n && !(p[i] & 0x80u)) { lsdj_bm_rx_data(r, p[i], msg, ctx); i++; }
    }
}

/* ---- the sender's clock ---- */
typedef struct { uint32_t base; uint32_t refreshed_ms; uint8_t valid; } lsdj_bm_clock_t;
/* our millisecond time a message timestamped ts (13 bits) was sent, as now_ms less its extra delay over the
   smallest seen; never in the future. The smallest delay creeps up 1 ms a second without a new low, so two
   clocks drifting apart (or a new link) settle again. */
static inline uint32_t lsdj_bm_clock(lsdj_bm_clock_t *c, uint32_t ts, uint32_t now_ms) {
    uint32_t d = (now_ms - ts) & 0x1FFFu;                  /* our time less theirs, mod 8.192 s */
    if (!c->valid) { c->valid = 1; c->base = d; c->refreshed_ms = now_ms; }
    while (now_ms - c->refreshed_ms >= 1000u) { c->base = (c->base + 1u) & 0x1FFFu; c->refreshed_ms += 1000u; }
    int32_t extra = (int32_t)(((d - c->base + 4096u) & 0x1FFFu)) - 4096;
    if (extra <= 0) { c->base = d; c->refreshed_ms = now_ms; extra = 0; }   /* the smallest delay, again or new */
    if (extra > 1000) { c->base = d; c->refreshed_ms = now_ms; extra = 0; }   /* nonsense (a stall, a new sender): start over */
    return now_ms - (uint32_t)extra;
}

/* ---- out ---- */
typedef struct {
    uint8_t ring[LSDJ_BM_TX_RING];
    uint32_t head, tail;   /* bytes in: head - tail */
    uint8_t in_sx;         /* the next packet continues a SysEx */
    uint32_t dropped;
} lsdj_bm_tx_t;
static inline void lsdj_bm_tx_reset(lsdj_bm_tx_t *t) { t->head = t->tail = 0; t->in_sx = 0; }
static inline uint32_t lsdj_bm_tx_pending(const lsdj_bm_tx_t *t) { return t->head - t->tail; }
/* one whole message (a SysEx with its F0 and F7); 0 if it does not fit (dropped whole) */
static inline int lsdj_bm_tx_put(lsdj_bm_tx_t *t, const uint8_t *m, uint32_t n) {
    if (!n || n > LSDJ_BM_TX_RING - (t->head - t->tail)) { t->dropped++; return 0; }
    for (uint32_t i = 0; i < n; i++) t->ring[(t->head + i) % LSDJ_BM_TX_RING] = m[i];
    t->head += n;
    return 1;
}
static inline uint8_t lsdj_bm_tx_peek(const lsdj_bm_tx_t *t, uint32_t k) { return t->ring[(t->tail + k) % LSDJ_BM_TX_RING]; }
/* the next packet into out (max >= 5), timestamped ts_ms; its length, 0 if nothing is queued */
static inline uint32_t lsdj_bm_tx_packet(lsdj_bm_tx_t *t, uint32_t ts_ms, uint8_t *out, uint32_t max) {
    if (t->head == t->tail || max < 5u) return 0;
    uint8_t th = (uint8_t)(0x80u | ((ts_ms >> 7) & 0x3Fu)), tl = (uint8_t)(0x80u | (ts_ms & 0x7Fu));
    uint32_t o = 0;
    out[o++] = th;
    while (t->head != t->tail) {
        uint8_t b = lsdj_bm_tx_peek(t, 0);
        if (t->in_sx) {
            if (b == 0xF7u) {                              /* the end: a timestamp, then F7 */
                if (max - o < 2u) break;
                out[o++] = tl; out[o++] = 0xF7u; t->tail++; t->in_sx = 0;
                continue;
            }
            if (o >= max) break;
            out[o++] = b; t->tail++;                       /* data: straight on */
            continue;
        }
        if (b == 0xF0u) {                                  /* a timestamp, F0, then data in this or later packets */
            if (max - o < 3u) break;                       /* (F0 with at least one more byte) */
            out[o++] = tl; out[o++] = 0xF0u; t->tail++; t->in_sx = 1;
            continue;
        }
        uint32_t len = 1u + (b & 0x80u ? lsdj_bm_need(b) : 0u);
        if (len > lsdj_bm_tx_pending(t)) break;            /* (put queues whole messages) */
        if (max - o < 1u + len) break;
        out[o++] = tl;
        for (uint32_t k = 0; k < len; k++) out[o++] = lsdj_bm_tx_peek(t, k);
        t->tail += len;
    }
    return o > 1u ? o : 0u;
}
