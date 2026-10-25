/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_blemidi.h: spec examples (running status,
   real-time inside SysEx, SysEx over several packets), link-sized SysEx
   round trips at every packet size 5..244, oversize SysEx dropped whole,
   malformed packets refused, and the output's packets are valid BLE-MIDI. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_blemidi.h"

static uint8_t got[64][3]; static uint32_t got_len[64], ngot;
static uint8_t sx[8][LSDJ_BM_SX_MAX]; static uint32_t sx_len[8], nsx;
static uint32_t got_ts[64];
static void on_msg(void *c, const uint8_t *m, uint32_t n, uint32_t ts) { (void)c; got_ts[ngot] = ts; assert(n >= 1 && n <= 3 && ngot < 64); memcpy(got[ngot], m, n); got_len[ngot++] = n; }
static void on_sx(void *c, const uint8_t *m, uint32_t n) { (void)c; assert(nsx < 8); memcpy(sx[nsx], m, n); sx_len[nsx++] = n; }
static void clear(void) { ngot = nsx = 0; }
static lsdj_bm_rx_t rx;
static void feed(const uint8_t *p, uint32_t n) { lsdj_bm_rx(&rx, p, n, on_msg, on_sx, 0); }

int main(void) {
    lsdj_bm_rx_reset(&rx);
    /* note on, then two more under running status (each after a timestamp), then a clock */
    { const uint8_t p[] = {0x80, 0x81, 0x90, 0x3C, 0x40, 0x82, 0x3E, 0x41, 0x83, 0x40, 0x00, 0x84, 0xF8};
      clear(); feed(p, sizeof p);
      assert(ngot == 4 && got_len[0] == 3 && got[0][0] == 0x90 && got[1][0] == 0x90 && got[1][1] == 0x3E && got[2][2] == 0x00);
      assert(got_len[3] == 1 && got[3][0] == 0xF8 && got_ts[0] == 1 && got_ts[3] == 4); }
    /* timestamps: header bits 12..7; a low part smaller than the last wraps into the next 128 ms */
    { const uint8_t p[] = {0x80 | 0x05, 0xFE, 0xF8, 0x82, 0xF8};
      clear(); feed(p, sizeof p); assert(ngot == 2 && got_ts[0] == (5u << 7 | 0x7E) && got_ts[1] == (6u << 7 | 2)); }
    /* running status without a timestamp in between (accepted), program change (1 data byte) */
    { const uint8_t p[] = {0x80, 0x81, 0xC2, 0x05, 0x06, 0x81, 0xFA};
      clear(); feed(p, sizeof p);
      assert(ngot == 3 && got[0][0] == 0xC2 && got[0][1] == 5 && got[1][0] == 0xC2 && got[1][1] == 6 && got[2][0] == 0xFA); }
    /* SysEx in one packet, with a clock inside it */
    { const uint8_t p[] = {0x80, 0x81, 0xF0, 0x7D, 0x47, 0x82, 0xF8, 0x53, 0x01, 0x83, 0xF7};
      clear(); feed(p, sizeof p);
      assert(nsx == 1 && sx_len[0] == 4 && !memcmp(sx[0], "\x7D\x47\x53\x01", 4) && ngot == 1 && got[0][0] == 0xF8); }
    /* SysEx over three packets (continuations are header + data) */
    { const uint8_t a[] = {0x80, 0x81, 0xF0, 1, 2}, b[] = {0x80, 3, 4, 5}, c[] = {0x80, 6, 0x85, 0xF7, 0x86, 0xFC};
      clear(); feed(a, sizeof a); assert(nsx == 0); feed(b, sizeof b); feed(c, sizeof c);
      assert(nsx == 1 && sx_len[0] == 6 && sx[0][5] == 6 && ngot == 1 && got[0][0] == 0xFC); }
    /* too long: dropped whole, and the next one still arrives */
    { lsdj_bm_rx_reset(&rx); clear();
      uint8_t p[200]; p[0] = 0x80; p[1] = 0x80; p[2] = 0xF0; for (int i = 3; i < 200; i++) p[i] = 9; feed(p, 200);
      uint8_t q[200]; q[0] = 0x80; for (int i = 1; i < 200; i++) q[i] = 9; feed(q, 200);
      const uint8_t e[] = {0x80, 0x81, 0xF7}; feed(e, 3); assert(nsx == 0 && rx.sx_dropped == 1);
      const uint8_t ok[] = {0x80, 0x81, 0xF0, 0x11, 0x82, 0xF7}; feed(ok, sizeof ok); assert(nsx == 1 && sx_len[0] == 1); }
    /* malformed: no header bit, too short, data where a timestamp must be */
    { uint32_t bad = rx.bad; const uint8_t a[] = {0x00, 0x81, 0xF8}, b[] = {0x80}, c[] = {0x80, 0x10, 0x20};
      clear(); feed(a, 3); feed(b, 1); feed(c, 3); assert(ngot == 0 && rx.bad == bad + 3); }

    /* round trips: link replies and requests (up to 270 bytes) and short messages, at every packet size */
    for (uint32_t max = 5; max <= 244; max++) {
        static lsdj_bm_tx_t tx; lsdj_bm_tx_reset(&tx); lsdj_bm_rx_reset(&rx); clear();
        uint8_t big[272]; big[0] = 0xF0; for (int i = 1; i < 271; i++) big[i] = (uint8_t)((i * 37) & 0x7F); big[271] = 0xF7;
        const uint8_t clk[] = {0xF8}, note[] = {0x91, 0x40, 0x7F}, small[] = {0xF0, 0x7D, 0x47, 0x53, 0x01, 0xF7};
        assert(lsdj_bm_tx_put(&tx, small, sizeof small) && lsdj_bm_tx_put(&tx, big, sizeof big));
        assert(lsdj_bm_tx_put(&tx, clk, 1) && lsdj_bm_tx_put(&tx, note, 3) && lsdj_bm_tx_put(&tx, big, sizeof big));
        uint8_t pkt[256]; uint32_t n, packets = 0;
        while ((n = lsdj_bm_tx_packet(&tx, 1234 + packets, pkt, max))) {
            assert(n <= max && (pkt[0] & 0xC0) == 0x80);
            feed(pkt, n); packets++;
            assert(packets < 2000);
        }
        assert(lsdj_bm_tx_pending(&tx) == 0 && !tx.in_sx);
        assert(nsx == 3 && sx_len[0] == 4 && sx_len[1] == 270 && !memcmp(sx[1], big + 1, 270) && sx_len[2] == 270);
        assert(ngot == 2 && got[0][0] == 0xF8 && got_len[1] == 3 && got[1][0] == 0x91 && got[1][2] == 0x7F);
        if (rx.bad || rx.sx_dropped) { printf("max %u: bad %u dropped %u\n", max, rx.bad, rx.sx_dropped); return 1; }
    }
    /* a full output queue drops a whole message, never part of one */
    { static lsdj_bm_tx_t tx; lsdj_bm_tx_reset(&tx); uint8_t m[400]; memset(m, 1, sizeof m); m[0] = 0xF0; m[399] = 0xF7;
      assert(lsdj_bm_tx_put(&tx, m, 400) && !lsdj_bm_tx_put(&tx, m, 400) && lsdj_bm_tx_pending(&tx) == 400 && tx.dropped == 1); }
    /* the sender's clock: arrival jitter goes, spacing stays; drift and stalls settle */
    { lsdj_bm_clock_t c = {0}; uint32_t ts = 8000;   /* (near the 13-bit wrap) */
      for (int k = 0; k < 400; k++) {                /* a tick every 20 ms; arrival 5..34 ms late */
          uint32_t sent = 100000u + 20u * (uint32_t)k, late = 5u + (uint32_t)((k * 7) % 30);
          uint32_t t = lsdj_bm_clock(&c, (ts + 20u * (uint32_t)k) & 0x1FFF, sent + late);
          if (k > 10) assert(t == sent + 5u);          /* once the 5 ms low is seen: exact spacing */
          assert(t <= sent + late); }
      uint32_t t = lsdj_bm_clock(&c, 1234, 500000u); assert(t == 500000u);   /* a new sender: start over */
    }
    printf("PASS lsdj_blemidi: timestamps, the sender clock, running status, real-time in SysEx, multi-packet SysEx, oversize dropped, round trips at packet sizes 5..244\n");
    return 0;
}
