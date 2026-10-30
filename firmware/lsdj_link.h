/* SPDX-License-Identifier: GPL-3.0-only */
/* The web page's link to the running firmware, over USB-MIDI SysEx. Nothing here
   writes firmware: songs, settings, effects and themes are stored by the
   firmware's own save store (firmware/lsdj_ftl.h).

   Request  F0 7D 'G' 'S' op [data] F7
   Reply    F0 7D 'G' 'S' op status [data] F7
   Data bytes travel as pairs (low 7 bits, high bit). Little endian.

   op 0x01 INFO           -> u8 protocol (1), 8 bytes firmware id ("FM-1_9NN"),
                             16 bytes game title, u32 save size, u32 crc32 of the
                             save RAM, u8 importing, u8 flags (bit 0: LSDj's
                             addresses known: live data)
   Songs (the save RAM):
   op 0x02 READ  u32 off  -> 128 bytes (off a multiple of 128)
   op 0x03 BEGIN u32 len, u32 crc32   stores the current songs first (a failed
                             import loses nothing), pauses LSDj
   op 0x04 CHUNK u32 off, 128 bytes   in order; resending the last one is fine
   op 0x05 COMMIT         crc ok: stored, LSDj restarts with the import;
                          crc bad: the stored songs come back, status 3
   op 0x06 ABORT          the stored songs come back
   Configuration (live at once; kept in flash with op 0x16 STORE):
   op 0x10 SETTINGS_GET   -> u32 (firmware/lsdj_settings.h packed)
   op 0x11 SETTINGS_SET u32
   op 0x12 FX_GET         -> 17 x u32 (firmware/lsdj_fx_ui.h packed; 11 before alpha 66)
   op 0x13 FX_SET 17 x u32           or 11 x u32 (a page from before alpha 66: the new effects stay)
   op 0x14 THEME_GET      -> 8 x u16: the custom theme (firmware/lsdj_theme.h)
   op 0x15 THEME_SET 8 x u16          the custom theme (used when THEME = CUSTOM)
   op 0x16 STORE          settings, effects and custom theme to flash
   op 0x20 LIVE           -> u8 page, u8 cursor channel (0xFF unknown), u16 bpm,
                             u8 mute, u8 freeze, u8 playing (bit per channel),
                             17 x u32 effects, u8 battery (FM-1_979+: 0..3 bars,
                             +0x80 on USB power; 0xFF unknown)
   op 0x17 MIDI_GET       -> u32 (firmware/lsdj_midi.h packed)
   op 0x18 MIDI_SET u32   (kept with op 0x16 STORE)
   op 0x19 FEAT_GET       -> u32 (firmware/lsdj_feat.h packed; FM-1_979+, status 1 before)
   op 0x1A FEAT_SET u32   (kept with op 0x16 STORE)
   op 0x21 BLUETOOTH      -> 13 x u32 (firmware/lsdj_ble.h lsdj_ble_stat: state, error,
                             packets in / out, SysEx in, bad packets, MTU payload, heap
                             high-water, heap refusals, longest task run, start stage,
                             fatal, dropped out); status 1 in a build without Bluetooth
   op 0x22 CART_INFO      -> u32 slot length, u32 header CRC, u32 data CRC;
                             status 1 if this build cannot export a validated slot
   op 0x23 CART_READ u32 off -> u32 off, up to 120 slot bytes (last chunk shorter);
                             off must be a multiple of 120, strictly within the slot
   op 0x24 MUTE_SET u8 mute (bit per channel), u8 freeze (0/1)   live only, never saved
   Cartridge export reads immutable application data only; never saves, arbitrary
   addresses, flash writes or pauses. Both operations refuse during save import.
   The screen (FM-1_983+; status 1 before): the 160x144 picture the LCD shows, panels
   included; read only. FM-1_987: read back from the LCD image (io->screen: 240x216, the
   picture scaled A,A,B, byte-swapped as the LCD takes it), which holds finished frames
   only (983-986 read the emulator's picture while the next frame was drawn into it):
   op 0x25 SCREEN_INFO    -> u32 pictures shown, 18 x u16 hashes (one per 8-row band)
   op 0x26 SCREEN_ROW u8 row (0..143) -> u8 row, u8 colours (1..16), colours x u16
                             RGB565 (as the LCD), 80 bytes: pixel 2i low nibble,
                             2i+1 high (colour indices); more than 16: status 2
   op 0x27 SCREEN_SYNC u32 force (bit per band), 18 x u16 hashes the page holds, u8 round
                          -> u32 pictures shown, 18 x u16 hashes, u32 bands that follow
                             (FM-1_985+, USB only: status 1 over Bluetooth and before).
                             The bands whose hash differs (or forced) then come by
                             themselves, top to bottom, one SCREEN_PIXELS message per row,
                             between the main loop's other work; a new SYNC replaces the
                             rows still to come (no bands: stops them).
   op 0x28 SCREEN_PIXELS  sent, never asked: F0 7D 'G' 'S' 28 status round bits F7
                             (round: the SYNC's, low 7 bits, so the page can tell a late
                             row from an earlier SYNC), the bits 7 to a byte, lowest
                             first (not pairs): u8 row, 4 bits
                             colours - 1, colours x u16 RGB565, then 160 colour indices
                             of 0 (one colour), 1, 2, 3 or 4 bits. More than 16
                             colours: status 2 and the row only.
   FM-1_990 (USB): SCREEN_SYNC with force bit 31 = push. The FM-1 then also sends each new picture by
   itself, for LSDJ_LINK_PUSH_MS after that SYNC (the page renews it every second): when its picture
   changes, after the rows still going out,
   op 0x29 SCREEN_FRAME   sent, never asked: F0 7D 'G' 'S' 29 00 round bits F7 (bits as SCREEN_PIXELS:
                             u32 pictures shown, 18 bits bands that follow, 18 x u16 hashes), then
                             those bands' rows (SCREEN_PIXELS, this round). Round: the SYNC's + 1, + 2 ...
                             (low 7 bits). The FM-1 counts the rows as arrived; a page that lost some
                             sends SCREEN_SYNC again (with the hashes it holds).
   status 0 ok, 1 bad request, 2 wrong state, 3 crc mismatch, 4 flash error.
   An import with no CHUNK / COMMIT for LSDJ_LINK_TIMEOUT_MS is aborted.
   Pure logic, the hardware through lsdj_link_io_t: tests/lsdj_link.c. */
#pragma once
#include <stdint.h>

#define LSDJ_LINK_PROTOCOL   1u
#define LSDJ_LINK_CHUNK      128u
#define LSDJ_LINK_CART_CHUNK 120u
#define LSDJ_LINK_TIMEOUT_MS 5000u
#define LSDJ_LINK_FX_WORDS   17u   /* firmware/lsdj_fx_ui.h; 11 before alpha 66 (FX_SET takes both) */
#define LSDJ_LINK_FX_WORDS_OLD 11u
#define LSDJ_LINK_REPLY_MAX  (6u + 2u * LSDJ_LINK_CHUNK + 1u)

enum { LSDJ_LINK_INFO = 0x01, LSDJ_LINK_READ, LSDJ_LINK_BEGIN, LSDJ_LINK_CHUNK_OP, LSDJ_LINK_COMMIT, LSDJ_LINK_ABORT,
       LSDJ_LINK_SETTINGS_GET = 0x10, LSDJ_LINK_SETTINGS_SET, LSDJ_LINK_FX_GET, LSDJ_LINK_FX_SET,
       LSDJ_LINK_THEME_GET, LSDJ_LINK_THEME_SET, LSDJ_LINK_STORE, LSDJ_LINK_MIDI_GET, LSDJ_LINK_MIDI_SET,
       LSDJ_LINK_FEAT_GET, LSDJ_LINK_FEAT_SET,
       LSDJ_LINK_LIVE = 0x20, LSDJ_LINK_BT, LSDJ_LINK_CART_INFO, LSDJ_LINK_CART_READ, LSDJ_LINK_MUTE_SET,
       LSDJ_LINK_SCREEN_INFO, LSDJ_LINK_SCREEN_ROW, LSDJ_LINK_SCREEN_SYNC, LSDJ_LINK_SCREEN_PIXELS,
       LSDJ_LINK_SCREEN_FRAME };
#define LSDJ_LINK_SCREEN_W     160u
#define LSDJ_LINK_SCREEN_H     144u
#define LSDJ_LINK_SCREEN_BANDS 18u   /* LSDj's character rows */
#define LSDJ_LINK_BT_STATS 13u
#define LSDJ_LINK_PUSH     0x80000000u   /* SCREEN_SYNC force bit 31 (FM-1_990) */
#define LSDJ_LINK_PUSH_MS  3000u
enum { LSDJ_LINK_OK = 0, LSDJ_LINK_BAD = 1, LSDJ_LINK_STATE = 2, LSDJ_LINK_CRC = 3, LSDJ_LINK_FLASH = 4 };

typedef struct {
    uint8_t page, cursor_ch, mute, freeze, playing, battery;
    uint16_t bpm;
} lsdj_link_live_t;

typedef struct {
    /* songs */
    uint8_t *(*save)(void);
    uint32_t (*size)(void);
    void (*pause)(int on);
    int (*store)(void);                          /* the save RAM to flash, 0 = ok */
    int (*load)(void);                           /* the save RAM from flash, 0 = ok */
    void (*reset)(void);                         /* restart LSDj */
    /* about */
    const char *(*firmware)(void);               /* "FM-1_9NN" */
    const char *(*title)(void);
    int (*live_known)(void);
    /* configuration: set returns 0 if the value is not valid (nothing changed) */
    uint32_t (*settings_get)(void);
    int (*settings_set)(uint32_t v);
    void (*fx_get)(uint32_t w[LSDJ_LINK_FX_WORDS]);
    int (*fx_set)(const uint32_t *w, unsigned n);   /* n: LSDJ_LINK_FX_WORDS or _OLD */
    void (*theme_get)(uint16_t c[8]);
    void (*theme_set)(const uint16_t c[8]);
    int (*store_config)(void);                   /* 0 = ok */
    void (*live)(lsdj_link_live_t *l);
    uint32_t (*bt_stat)(unsigned i);             /* 0 in a build without Bluetooth */
    uint32_t (*midi_get)(void);                  /* the MIDI options; 0 = none (status 1) */
    int (*midi_set)(uint32_t v);
    const volatile uint8_t *(*cartridge)(uint32_t *len); /* validated immutable slot, or NULL */
    int (*mute_set)(uint8_t mute, uint8_t freeze);   /* 0 if not valid; NULL: not supported (status 1) */
    uint32_t (*feat_get)(void);                  /* the feature switches; NULL: not supported (status 1) */
    int (*feat_set)(uint32_t v);
    const volatile uint16_t *(*screen)(uint32_t *shown); /* the LCD image (lsdj_link_px); NULL: not supported (status 1) */
    int (*screen_stream)(uint32_t bands, uint8_t round); /* send these bands' rows after this reply (SCREEN_SYNC;
                                                    bit 31: LSDJ_LINK_PUSH, FM-1_990);
                                                    0 over a link that cannot (status 1); NULL: not supported */
} lsdj_link_io_t;

/* Picture pixel (x, y) from the LCD image: 240x216, LCD row y' shows picture row y'*2/3 (so
   picture row y is LCD row ceil(3y/2)), columns A,A,B (x even: 3*(x/2), odd: +2), bytes
   swapped for the LCD (src/video_job.h gb_video_scale). */
#define LSDJ_LINK_LCD_W 240u
static inline uint16_t lsdj_link_px(const volatile uint16_t *lcd, unsigned x, unsigned y) {
    uint16_t c = lcd[((3u * y + 1u) >> 1) * LSDJ_LINK_LCD_W + 3u * (x >> 1) + ((x & 1u) << 1)];
    return (uint16_t)(c >> 8 | c << 8);
}
/* One band's hash (FNV-1a over the pixels, folded to 16 bits). */
static uint16_t lsdj_link_band_hash(const volatile uint16_t *px, unsigned band) {
    uint32_t h = 2166136261u;
    for (unsigned y = band * 8u; y < band * 8u + 8u; y++) {   /* = lsdj_link_px, a row at a time */
        const volatile uint16_t *row = px + ((3u * y + 1u) >> 1) * LSDJ_LINK_LCD_W;
        for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x += 2u, row += 3) {
            uint16_t a = row[0], b = row[2];
            h ^= (uint16_t)(a >> 8 | a << 8); h *= 16777619u;
            h ^= (uint16_t)(b >> 8 | b << 8); h *= 16777619u;
        }
    }
    return (uint16_t)(h ^ h >> 16);
}

/* One row as colours (pal) and an index per pixel; the number of colours, 0 if more than 16. */
static unsigned lsdj_link_row_colours(const volatile uint16_t *px, unsigned y, uint16_t pal[16], uint8_t k[LSDJ_LINK_SCREEN_W]) {
    unsigned np = 0;
    for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x++) {
        uint16_t c = lsdj_link_px(px, x, y); unsigned i = 0;   /* read once: indices match the colours */
        while (i < np && pal[i] != c) i++;
        if (i == np) { if (np == 16u) return 0; pal[np++] = c; }
        k[x] = (uint8_t)i;
    }
    return np;
}
typedef struct { uint8_t *r; uint32_t n, acc; unsigned bits; } lsdj_link_bits_t;
static void lsdj_link_bits(lsdj_link_bits_t *b, uint32_t v, unsigned bits) {   /* bits <= 16 */
    b->acc |= v << b->bits; b->bits += bits;
    while (b->bits >= 7u) { b->r[b->n++] = (uint8_t)(b->acc & 127u); b->acc >>= 7; b->bits -= 7u; }
}
/* A SCREEN_PIXELS message for row y (at most 138 bytes); its length. */
static uint32_t lsdj_link_screen_pixels(const volatile uint16_t *px, unsigned y, uint8_t round, uint8_t *r) {
    uint16_t pal[16]; uint8_t k[LSDJ_LINK_SCREEN_W];
    unsigned np = lsdj_link_row_colours(px, y, pal, k);
    lsdj_link_bits_t b = {r, 0, 0, 0};
    r[b.n++] = 0xf0; r[b.n++] = 0x7d; r[b.n++] = 'G'; r[b.n++] = 'S'; r[b.n++] = LSDJ_LINK_SCREEN_PIXELS;
    r[b.n++] = np ? 0u : 2u;
    r[b.n++] = round & 127u;
    lsdj_link_bits(&b, y, 8);
    if (np) {
        unsigned w = np > 8u ? 4u : np > 4u ? 3u : np > 2u ? 2u : np > 1u ? 1u : 0u;
        lsdj_link_bits(&b, np - 1u, 4);
        for (unsigned i = 0; i < np; i++) lsdj_link_bits(&b, pal[i], 16);
        if (w) for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x++) lsdj_link_bits(&b, k[x], w);
    }
    if (b.bits) r[b.n++] = (uint8_t)(b.acc & 127u);
    r[b.n++] = 0xf7;
    return b.n;
}

/* FM-1_990 push (USB): what the page holds once the rows sent have arrived. */
typedef struct { uint16_t hash[LSDJ_LINK_SCREEN_BANDS]; uint32_t shown, until; uint8_t on, round; } lsdj_link_push_t;
/* SCREEN_SYNC with the push bit: its rows bring the page to this picture. */
static void lsdj_link_push_start(lsdj_link_push_t *p, const volatile uint16_t *px, uint32_t shown, uint8_t round, uint32_t now_ms) {
    for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) p->hash[b] = lsdj_link_band_hash(px, b);
    p->shown = shown; p->round = round; p->until = now_ms + LSDJ_LINK_PUSH_MS; p->on = 1;
}
/* A new picture (shown moved on): the SCREEN_FRAME message for the bands that changed, its length
   (0: nothing to send; push over, or the same picture). bands and h: for lsdj_link_push_sent. */
static uint32_t lsdj_link_push_frame(lsdj_link_push_t *p, const volatile uint16_t *px, uint32_t shown, uint32_t now_ms,
                                     uint8_t *r, uint32_t *bands, uint16_t h[LSDJ_LINK_SCREEN_BANDS]) {
    *bands = 0;
    if (!p->on) return 0;
    if ((int32_t)(now_ms - p->until) > 0) { p->on = 0; return 0; }   /* the page stopped renewing it */
    if (shown == p->shown) return 0;
    for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) { h[b] = lsdj_link_band_hash(px, b); if (h[b] != p->hash[b]) *bands |= 1u << b; }
    if (!*bands) { p->shown = shown; return 0; }
    lsdj_link_bits_t k = {r, 0, 0, 0};
    r[k.n++] = 0xf0; r[k.n++] = 0x7d; r[k.n++] = 'G'; r[k.n++] = 'S'; r[k.n++] = LSDJ_LINK_SCREEN_FRAME;
    r[k.n++] = 0; r[k.n++] = (uint8_t)((p->round + 1u) & 127u);
    lsdj_link_bits(&k, shown & 0xFFFFu, 16); lsdj_link_bits(&k, shown >> 16, 16);
    lsdj_link_bits(&k, *bands & 0xFFFFu, 16); lsdj_link_bits(&k, *bands >> 16, 2);
    for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) lsdj_link_bits(&k, h[b], 16);
    if (k.bits) r[k.n++] = (uint8_t)(k.acc & 127u);
    r[k.n++] = 0xf7;
    return k.n;
}
/* The SCREEN_FRAME went out: its bands count as the page's; the next rows go with this round. */
static uint8_t lsdj_link_push_sent(lsdj_link_push_t *p, uint32_t shown, uint32_t bands, const uint16_t h[LSDJ_LINK_SCREEN_BANDS]) {
    for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) if (bands >> b & 1u) p->hash[b] = h[b];
    p->shown = shown; p->round = (uint8_t)((p->round + 1u) & 127u);
    return p->round;
}

typedef struct {
    uint8_t importing;
    uint32_t len, crc, next, last_ms;
} lsdj_link_t;

static uint32_t lsdj_link_crc32(const uint8_t *p, uint32_t n) {
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
/* n data bytes from 7-bit pairs; 0 if malformed. */
static int lsdj_link_unpack(const uint8_t *in, uint32_t pairs_len, uint8_t *out, uint32_t n) {
    if (pairs_len != 2u * n) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (in[2 * i] > 127 || in[2 * i + 1] > 1) return 0;
        out[i] = (uint8_t)(in[2 * i] | in[2 * i + 1] << 7);
    }
    return 1;
}
static uint32_t lsdj_link_le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t lsdj_link_put(uint8_t *r, uint32_t n, const uint8_t *p, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) { r[n++] = p[i] & 127; r[n++] = p[i] >> 7; }
    return n;
}
static uint32_t lsdj_link_put8(uint8_t *r, uint32_t n, uint8_t v) { return lsdj_link_put(r, n, &v, 1); }
static uint32_t lsdj_link_put16(uint8_t *r, uint32_t n, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; return lsdj_link_put(r, n, b, 2); }
static uint32_t lsdj_link_put32(uint8_t *r, uint32_t n, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return lsdj_link_put(r, n, b, 4);
}
static uint32_t lsdj_link_puts(uint8_t *r, uint32_t n, const char *s, uint32_t len) {   /* fixed length, 0-padded */
    for (uint32_t i = 0; i < len; i++) { uint8_t c = 0; if (s && *s) c = (uint8_t)*s++; n = lsdj_link_put8(r, n, c); }
    return n;
}

static void lsdj_link_abort(lsdj_link_t *s, const lsdj_link_io_t *io) {
    if (!s->importing) return;
    io->load();
    s->importing = 0; io->pause(0);
}
/* 1 if this frame (the 7-bit bytes between F0 and F7) is for us. */
static int lsdj_link_match(const uint8_t *q, uint32_t n) { return n >= 4 && q[0] == 0x7d && q[1] == 'G' && q[2] == 'S'; }

/* One request; the complete F0..F7 reply goes to r, its length is returned. */
static uint32_t lsdj_link_handle(lsdj_link_t *s, const lsdj_link_io_t *io, const uint8_t *q, uint32_t qn,
                                 uint32_t now_ms, uint8_t r[LSDJ_LINK_REPLY_MAX]) {
    uint8_t op = q[3], status = LSDJ_LINK_OK, d[4 + LSDJ_LINK_CHUNK];
    const uint8_t *args = q + 4; uint32_t an = qn - 4, size = io->size();
    uint32_t n = 0;
    r[n++] = 0xf0; r[n++] = 0x7d; r[n++] = 'G'; r[n++] = 'S'; r[n++] = op; r[n++] = 0;
    switch (op) {
    case LSDJ_LINK_CART_INFO:
    case LSDJ_LINK_CART_READ: {
        uint32_t len = 0;
        const volatile uint8_t *cart = io->cartridge ? io->cartridge(&len) : 0;
        if (!cart || len < 256u || len > 256u + 128u * 16384u) { status = LSDJ_LINK_BAD; break; }
        if (s->importing) { status = LSDJ_LINK_STATE; break; }
        if (op == LSDJ_LINK_CART_INFO) {
            if (an) { status = LSDJ_LINK_BAD; break; }
            n = lsdj_link_put32(r, n, len);
            for (unsigned i = 252; i < 256; i++) n = lsdj_link_put8(r, n, cart[i]);
            for (unsigned i = 16; i < 20; i++) n = lsdj_link_put8(r, n, cart[i]);
        } else {
            if (!lsdj_link_unpack(args, an, d, 4)) { status = LSDJ_LINK_BAD; break; }
            uint32_t off = lsdj_link_le32(d);
            if (off % LSDJ_LINK_CART_CHUNK || off >= len) { status = LSDJ_LINK_BAD; break; }
            uint32_t count = len - off;
            if (count > LSDJ_LINK_CART_CHUNK) count = LSDJ_LINK_CART_CHUNK;
            n = lsdj_link_put32(r, n, off);
            for (uint32_t i = 0; i < count; i++) n = lsdj_link_put8(r, n, cart[off + i]);
        }
        break;
    }
    case LSDJ_LINK_INFO:
        if (an) { status = LSDJ_LINK_BAD; break; }
        n = lsdj_link_put8(r, n, LSDJ_LINK_PROTOCOL);
        n = lsdj_link_puts(r, n, io->firmware(), 8);
        n = lsdj_link_puts(r, n, io->title(), 16);
        n = lsdj_link_put32(r, n, size);
        n = lsdj_link_put32(r, n, s->importing ? 0 : lsdj_link_crc32(io->save(), size));
        n = lsdj_link_put8(r, n, s->importing);
        n = lsdj_link_put8(r, n, (uint8_t)(io->live_known() ? 1u : 0u));
        break;
    case LSDJ_LINK_READ: {
        if (!lsdj_link_unpack(args, an, d, 4)) { status = LSDJ_LINK_BAD; break; }
        uint32_t off = lsdj_link_le32(d);
        if (off % LSDJ_LINK_CHUNK || off >= size) { status = LSDJ_LINK_BAD; break; }
        if (s->importing) { status = LSDJ_LINK_STATE; break; }
        n = lsdj_link_put(r, n, io->save() + off, LSDJ_LINK_CHUNK);
        break;
    }
    case LSDJ_LINK_BEGIN:
        if (!lsdj_link_unpack(args, an, d, 8)) { status = LSDJ_LINK_BAD; break; }
        if (lsdj_link_le32(d) != size) { status = LSDJ_LINK_BAD; break; }
        if (s->importing) { status = LSDJ_LINK_STATE; break; }
        io->pause(1);
        if (io->store()) { io->pause(0); status = LSDJ_LINK_FLASH; break; }
        s->importing = 1; s->len = size; s->crc = lsdj_link_le32(d + 4); s->next = 0; s->last_ms = now_ms;
        break;
    case LSDJ_LINK_CHUNK_OP: {
        if (!lsdj_link_unpack(args, an, d, 4 + LSDJ_LINK_CHUNK)) { status = LSDJ_LINK_BAD; break; }
        if (!s->importing) { status = LSDJ_LINK_STATE; break; }
        uint32_t off = lsdj_link_le32(d);
        if (off + LSDJ_LINK_CHUNK == s->next) { s->last_ms = now_ms; break; }   /* the last chunk again */
        if (off != s->next || off + LSDJ_LINK_CHUNK > s->len) { status = LSDJ_LINK_BAD; break; }
        uint8_t *dst = io->save() + off;
        for (uint32_t i = 0; i < LSDJ_LINK_CHUNK; i++) dst[i] = d[4 + i];
        s->next += LSDJ_LINK_CHUNK; s->last_ms = now_ms;
        break;
    }
    case LSDJ_LINK_COMMIT:
        if (an) { status = LSDJ_LINK_BAD; break; }
        if (!s->importing || s->next != s->len) { status = LSDJ_LINK_STATE; break; }
        if (lsdj_link_crc32(io->save(), s->len) != s->crc) { lsdj_link_abort(s, io); status = LSDJ_LINK_CRC; break; }
        if (io->store()) { lsdj_link_abort(s, io); status = LSDJ_LINK_FLASH; break; }
        s->importing = 0; io->reset(); io->pause(0);
        break;
    case LSDJ_LINK_ABORT:
        lsdj_link_abort(s, io);
        break;
    case LSDJ_LINK_SETTINGS_GET:
        if (an) { status = LSDJ_LINK_BAD; break; }
        n = lsdj_link_put32(r, n, io->settings_get());
        break;
    case LSDJ_LINK_SETTINGS_SET:
        if (!lsdj_link_unpack(args, an, d, 4) || !io->settings_set(lsdj_link_le32(d))) status = LSDJ_LINK_BAD;
        break;
    case LSDJ_LINK_FX_GET: {
        if (an) { status = LSDJ_LINK_BAD; break; }
        uint32_t w[LSDJ_LINK_FX_WORDS]; io->fx_get(w);
        for (unsigned i = 0; i < LSDJ_LINK_FX_WORDS; i++) n = lsdj_link_put32(r, n, w[i]);
        break;
    }
    case LSDJ_LINK_FX_SET: {
        uint8_t b[4 * LSDJ_LINK_FX_WORDS]; uint32_t w[LSDJ_LINK_FX_WORDS];
        unsigned words = an == 8u * LSDJ_LINK_FX_WORDS_OLD ? LSDJ_LINK_FX_WORDS_OLD : LSDJ_LINK_FX_WORDS;
        if (!lsdj_link_unpack(args, an, b, 4u * words)) { status = LSDJ_LINK_BAD; break; }
        for (unsigned i = 0; i < words; i++) w[i] = lsdj_link_le32(b + 4 * i);
        if (!io->fx_set(w, words)) status = LSDJ_LINK_BAD;
        break;
    }
    case LSDJ_LINK_THEME_GET: {
        if (an) { status = LSDJ_LINK_BAD; break; }
        uint16_t c[8]; io->theme_get(c);
        for (unsigned i = 0; i < 8; i++) n = lsdj_link_put16(r, n, c[i]);
        break;
    }
    case LSDJ_LINK_THEME_SET: {
        uint8_t b[16]; uint16_t c[8];
        if (!lsdj_link_unpack(args, an, b, sizeof b)) { status = LSDJ_LINK_BAD; break; }
        for (unsigned i = 0; i < 8; i++) c[i] = (uint16_t)(b[2 * i] | b[2 * i + 1] << 8);
        io->theme_set(c);
        break;
    }
    case LSDJ_LINK_STORE:
        if (an) { status = LSDJ_LINK_BAD; break; }
        if (s->importing) { status = LSDJ_LINK_STATE; break; }
        if (io->store_config()) status = LSDJ_LINK_FLASH;
        break;
    case LSDJ_LINK_LIVE: {
        if (an) { status = LSDJ_LINK_BAD; break; }
        lsdj_link_live_t l; io->live(&l);
        uint32_t w[LSDJ_LINK_FX_WORDS]; io->fx_get(w);
        n = lsdj_link_put8(r, n, l.page); n = lsdj_link_put8(r, n, l.cursor_ch); n = lsdj_link_put16(r, n, l.bpm);
        n = lsdj_link_put8(r, n, l.mute); n = lsdj_link_put8(r, n, l.freeze); n = lsdj_link_put8(r, n, l.playing);
        for (unsigned i = 0; i < LSDJ_LINK_FX_WORDS; i++) n = lsdj_link_put32(r, n, w[i]);
        n = lsdj_link_put8(r, n, l.battery);
        break;
    }
    case LSDJ_LINK_MIDI_GET:
    case LSDJ_LINK_FEAT_GET: {
        uint32_t (*get)(void) = op == LSDJ_LINK_MIDI_GET ? io->midi_get : io->feat_get;
        if (an || !get) { status = LSDJ_LINK_BAD; break; }
        n = lsdj_link_put32(r, n, get());
        break;
    }
    case LSDJ_LINK_MIDI_SET:
    case LSDJ_LINK_FEAT_SET: {
        int (*set)(uint32_t) = op == LSDJ_LINK_MIDI_SET ? io->midi_set : io->feat_set;
        if (!set || !lsdj_link_unpack(args, an, d, 4) || !set(lsdj_link_le32(d))) status = LSDJ_LINK_BAD;
        break;
    }
    case LSDJ_LINK_MUTE_SET:
        if (!io->mute_set || !lsdj_link_unpack(args, an, d, 2) || !io->mute_set(d[0], d[1])) status = LSDJ_LINK_BAD;
        break;
    case LSDJ_LINK_SCREEN_INFO: {
        uint32_t shown = 0;
        const volatile uint16_t *px = io->screen ? io->screen(&shown) : 0;
        if (an || !px) { status = LSDJ_LINK_BAD; break; }
        n = lsdj_link_put32(r, n, shown);
        for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) n = lsdj_link_put16(r, n, lsdj_link_band_hash(px, b));
        break;
    }
    case LSDJ_LINK_SCREEN_ROW: {
        uint32_t shown;
        const volatile uint16_t *px = io->screen ? io->screen(&shown) : 0;
        if (!px || !lsdj_link_unpack(args, an, d, 1) || d[0] >= LSDJ_LINK_SCREEN_H) { status = LSDJ_LINK_BAD; break; }
        uint16_t pal[16]; uint8_t k[LSDJ_LINK_SCREEN_W];
        unsigned np = lsdj_link_row_colours(px, d[0], pal, k);
        if (!np) { status = LSDJ_LINK_STATE; break; }
        uint8_t *ix = d + 1;   /* 80 bytes (d holds 132) */
        for (unsigned x = 0; x < LSDJ_LINK_SCREEN_W; x += 2u) ix[x >> 1] = (uint8_t)(k[x] | k[x + 1u] << 4);
        n = lsdj_link_put8(r, n, d[0]); n = lsdj_link_put8(r, n, (uint8_t)np);
        for (unsigned k = 0; k < np; k++) n = lsdj_link_put16(r, n, pal[k]);
        n = lsdj_link_put(r, n, ix, LSDJ_LINK_SCREEN_W / 2u);
        break;
    }
    case LSDJ_LINK_SCREEN_SYNC: {
        uint32_t shown = 0, send = 0;
        const volatile uint16_t *px = io->screen && io->screen_stream ? io->screen(&shown) : 0;
        if (!px || !lsdj_link_unpack(args, an, d, 5u + 2u * LSDJ_LINK_SCREEN_BANDS)) { status = LSDJ_LINK_BAD; break; }
        uint32_t force = lsdj_link_le32(d);
        n = lsdj_link_put32(r, n, shown);
        for (unsigned b = 0; b < LSDJ_LINK_SCREEN_BANDS; b++) {
            uint16_t hb = lsdj_link_band_hash(px, b);
            n = lsdj_link_put16(r, n, hb);
            if ((force >> b & 1u) || hb != (uint16_t)(d[4 + 2 * b] | d[5 + 2 * b] << 8)) send |= 1u << b;
        }
        if (!io->screen_stream(send | (force & LSDJ_LINK_PUSH), d[4 + 2 * LSDJ_LINK_SCREEN_BANDS])) { status = LSDJ_LINK_BAD; n = 6; break; }
        n = lsdj_link_put32(r, n, send);
        break;
    }
    case LSDJ_LINK_BT:
        if (an || !io->bt_stat) { status = LSDJ_LINK_BAD; break; }
        for (unsigned i = 0; i < LSDJ_LINK_BT_STATS; i++) n = lsdj_link_put32(r, n, io->bt_stat(i));
        break;
    default:
        status = LSDJ_LINK_BAD;
    }
    r[5] = status; r[n++] = 0xf7;
    return n;
}
/* Call regularly: gives up on an import whose page went away. */
static void lsdj_link_poll(lsdj_link_t *s, const lsdj_link_io_t *io, uint32_t now_ms) {
    if (s->importing && now_ms - s->last_ms > LSDJ_LINK_TIMEOUT_MS) lsdj_link_abort(s, io);
}
