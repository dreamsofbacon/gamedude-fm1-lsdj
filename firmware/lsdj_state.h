/* SPDX-License-Identifier: GPL-3.0-only */
/* Save states in internal flash: three manual slots plus an autosave, each a full
   emulator state (cartridge RAM + CPU/memory + APU), LZSS-compressed, in a shared
   pool (Felucca's data store, 0x97000..0xE0000).

   Pool layout: an entry is a run of whole 4 KiB sectors. Page 0 of its first
   sector holds the header, the compressed data starts at offset 256. A save
   erases a free run, writes the data, verifies it, and programs the header last
   (the commit); only then is the previous entry of that kind invalidated by
   erasing its first sector. A power cut never loses the previous entry.

   Needs (defined by the includer, as in lsdj_save.h):
     LSDJ_FL_ERASE(off) LSDJ_FL_WRITE(off,p,n) LSDJ_FL_READ(off,p,n) LSDJ_FL_IDLE()
   and the state regions, filled in by the emulator port:
     lst_region_t   { uint8_t *p; uint32_t n; }  region 0 must be cartridge RAM. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LST_POOL_LO    0x00097000u
#define LST_POOL_HI    0x000E0000u
#define LST_SECTORS    ((LST_POOL_HI - LST_POOL_LO) / 0x1000u)   /* 73 */
#define LST_DATA_OFF   256u
#define LST_MAGIC      0x5453534Cu      /* "LSST" */
#define LST_VERSION    1u
#define LST_MAX_REGIONS 4u
enum { LST_SLOT1 = 1, LST_SLOT2 = 2, LST_SLOT3 = 3, LST_AUTO = 4, LST_KINDS = 5 };
enum { LST_OK = 0, LST_FULL = -1, LST_EMPTY = -2, LST_EFLASH = -3, LST_ECRC = -4, LST_EFORMAT = -5 };

typedef struct { uint8_t *p; uint32_t n; } lst_region_t;

typedef struct {
    uint32_t magic, version, kind, seq, build, comp_len, comp_crc, nreg;
    uint32_t reg_len[LST_MAX_REGIONS], reg_crc[LST_MAX_REGIONS];
    uint32_t hdr_crc;
} lst_hdr_t;

typedef struct { int valid; uint32_t sector, nsect; lst_hdr_t h; } lst_entry_t;

static uint32_t lst_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static uint32_t lst_hdr_crc(const lst_hdr_t *h) {
    return lst_crc32(0, (const uint8_t *)h, offsetof(lst_hdr_t, hdr_crc));
}
static uint32_t lst_sector_off(uint32_t s) { return LST_POOL_LO + s * 0x1000u; }

/* ---------------- LZSS codec ----------------
   Per group of 8 tokens a flag byte (bit i set = token i is a match).
   Literal: 1 byte. Match: distance-1 (15 bits, LE, 2 bytes), length-3 (1 byte).
   Matches stay inside one region; regions are coded back to back. */
#define LZ_MIN    3u
#define LZ_MAX    258u
#define LZ_WIN    32767u
#define LZ_HBITS  13u
#define LZ_DEPTH  24u
#define LZ_WORK_BYTES ((4u << LZ_HBITS) + 2u * 32768u)   /* 96 KiB caller workspace */

typedef struct {
    /* output sink */
    int (*page)(void *ctx, const uint8_t *buf, uint32_t n);   /* NULL = count only */
    void *ctx;
    uint8_t buf[256]; uint32_t fill, total, crc;
    /* token group */
    uint8_t grp[1 + 8 * 3]; uint32_t glen; unsigned gcount; int err;
} lz_out_t;

static void lz_out_byte(lz_out_t *o, uint8_t b) {
    o->buf[o->fill++] = b; o->total++;
    if (o->fill == sizeof o->buf) {
        o->crc = lst_crc32(o->crc, o->buf, o->fill);
        if (o->page && !o->err && o->page(o->ctx, o->buf, o->fill)) o->err = 1;
        o->fill = 0;
    }
}
static void lz_group_flush(lz_out_t *o) {
    if (!o->gcount) return;
    for (uint32_t i = 0; i < o->glen; i++) lz_out_byte(o, o->grp[i]);
    o->glen = 0; o->gcount = 0;
}
static void lz_token(lz_out_t *o, int match, uint32_t a, uint32_t b) {
    if (!o->gcount) { o->grp[0] = 0; o->glen = 1; }
    if (match) {
        o->grp[0] |= (uint8_t)(1u << o->gcount);
        o->grp[o->glen++] = (uint8_t)(a & 0xFF); o->grp[o->glen++] = (uint8_t)(a >> 8);
        o->grp[o->glen++] = (uint8_t)b;
    } else o->grp[o->glen++] = (uint8_t)a;
    if (++o->gcount == 8) lz_group_flush(o);
}
static void lz_out_end(lz_out_t *o) {
    lz_group_flush(o);
    if (o->fill) {
        o->crc = lst_crc32(o->crc, o->buf, o->fill);
        if (o->page && !o->err && o->page(o->ctx, o->buf, o->fill)) o->err = 1;
        o->fill = 0;
    }
}
static uint32_t lz_hash(const uint8_t *p) {
    return ((uint32_t)p[0] * 506832829u ^ (uint32_t)p[1] * 2654435761u ^ (uint32_t)p[2] * 40503u) >> (32u - LZ_HBITS) & ((1u << LZ_HBITS) - 1u);
}
/* Compress one region. work: LZ_WORK_BYTES, 4-aligned. */
static void lz_compress(lz_out_t *o, const uint8_t *src, uint32_t n, void *work) {
    uint32_t *head = (uint32_t *)work;                       /* position + 1, 0 = none */
    uint16_t *prev = (uint16_t *)((uint8_t *)work + (4u << LZ_HBITS)); /* distance to previous same-hash pos */
    memset(head, 0, 4u << LZ_HBITS);
    uint32_t i = 0;
    while (i < n) {
        uint32_t best = 0, bdist = 0;
        if (i + LZ_MIN <= n) {
            uint32_t h = lz_hash(src + i), cand = head[h], depth = 0;
            uint32_t maxl = n - i < LZ_MAX ? n - i : LZ_MAX;
            while (cand && depth++ < LZ_DEPTH) {
                uint32_t j = cand - 1, d = i - j;
                if (d > LZ_WIN) break;
                if (src[j + best] == src[i + best]) {
                    uint32_t l = 0;
                    while (l < maxl && src[j + l] == src[i + l]) l++;
                    if (l > best) { best = l; bdist = d; if (l == maxl) break; }
                }
                uint16_t pd = prev[j & 0x7FFFu];
                if (!pd || pd > j) break;
                cand = j - pd + 1;
            }
        }
        uint32_t step = best >= LZ_MIN ? best : 1;
        if (best >= LZ_MIN) lz_token(o, 1, bdist - 1, best - LZ_MIN);
        else lz_token(o, 0, src[i], 0);
        for (uint32_t k = 0; k < step; k++, i++) {
            if (i + LZ_MIN > n) continue;
            uint32_t h = lz_hash(src + i), old = head[h];
            uint32_t d = old ? i - (old - 1) : 0;
            prev[i & 0x7FFFu] = (uint16_t)(d && d <= LZ_WIN ? d : 0);
            head[h] = i + 1;
        }
    }
    lz_group_flush(o);
}

/* Streaming reader over a flash range (4 KiB at a time through a RAM buffer). */
typedef struct { uint32_t off, end, pos, len; uint8_t *buf; uint32_t cap; int err; uint32_t crc; } lz_in_t;
static int lz_in_byte(lz_in_t *in) {
    if (in->pos == in->len) {
        uint32_t n = in->end - in->off;
        if (!n) { in->err = 1; return -1; }
        if (n > in->cap) n = in->cap;
        if (LSDJ_FL_READ(in->off, in->buf, n)) { in->err = 1; return -1; }
        in->crc = lst_crc32(in->crc, in->buf, n);
        in->off += n; in->pos = 0; in->len = n;
        LSDJ_FL_IDLE();
    }
    return in->buf[in->pos++];
}
/* Decompress exactly n bytes into dst (dst == NULL: verify only, needs a 32 KiB+ ring). */
static int lz_decompress(lz_in_t *in, uint8_t *dst, uint32_t n, uint8_t *ring, uint32_t *crc) {
    uint32_t o = 0, c = 0; int flags = 0, left = 0;
    uint8_t *w = dst ? dst : ring;
    uint32_t mask = dst ? 0xFFFFFFFFu : 0x7FFFu;
    while (o < n) {
        if (!left) { flags = lz_in_byte(in); if (flags < 0) return -1; left = 8; }
        if (flags & 1) {
            int a = lz_in_byte(in), b = lz_in_byte(in), l = lz_in_byte(in);
            if (a < 0 || b < 0 || l < 0) return -1;
            uint32_t d = (uint32_t)(a | b << 8) + 1u, len = (uint32_t)l + LZ_MIN;
            if (d > o || len > n - o) return -2;
            for (uint32_t k = 0; k < len; k++, o++) {
                uint8_t v = w[(o - d) & mask];
                w[o & mask] = v; c = lst_crc32(c, &v, 1);
            }
        } else {
            int v = lz_in_byte(in); if (v < 0) return -1;
            uint8_t b8 = (uint8_t)v; w[o & mask] = b8; o++; c = lst_crc32(c, &b8, 1);
        }
        flags >>= 1; left--;
    }
    if (crc) *crc = c;
    return 0;
}

/* ---------------- pool ---------------- */
static int lst_read_hdr(uint32_t sector, lst_hdr_t *h) {
    if (LSDJ_FL_READ(lst_sector_off(sector), (uint8_t *)h, sizeof *h)) return 0;
    if (h->magic != LST_MAGIC || h->version != LST_VERSION || h->hdr_crc != lst_hdr_crc(h)) return 0;
    if (h->kind < 1 || h->kind >= LST_KINDS || h->nreg == 0 || h->nreg > LST_MAX_REGIONS) return 0;
    uint32_t nsect = (LST_DATA_OFF + h->comp_len + 0xFFFu) / 0x1000u;
    return sector + nsect <= LST_SECTORS;
}

/* Newest entry of each kind; owner[s] = kind owning sector s (0 = free/garbage).
   Older duplicates are left out (they are garbage). */
static void lst_scan(lst_entry_t e[LST_KINDS], uint8_t owner[LST_SECTORS]) {
    memset(e, 0, sizeof(lst_entry_t) * LST_KINDS);
    /* Every sector is checked (no skipping over an entry's run): a stale header of a
       replaced entry may claim sectors that a newer entry now uses. */
    for (uint32_t s = 0; s < LST_SECTORS; s++) {
        lst_hdr_t h;
        if (!lst_read_hdr(s, &h)) continue;
        uint32_t nsect = (LST_DATA_OFF + h.comp_len + 0xFFFu) / 0x1000u;
        lst_entry_t *x = &e[h.kind];
        if (!x->valid || (int32_t)(h.seq - x->h.seq) > 0) { x->valid = 1; x->sector = s; x->nsect = nsect; x->h = h; }
    }
    if (owner) {
        memset(owner, 0, LST_SECTORS);
        for (unsigned k = 1; k < LST_KINDS; k++)
            if (e[k].valid) for (uint32_t s = 0; s < e[k].nsect; s++) owner[e[k].sector + s] = (uint8_t)k;
    }
}
static uint32_t lst_next_seq(const lst_entry_t e[LST_KINDS]) {
    uint32_t m = 0;
    for (unsigned k = 1; k < LST_KINDS; k++) if (e[k].valid && (int32_t)(e[k].h.seq - m) > 0) m = e[k].h.seq;
    return m + 1;
}

/* Erase the header sector of every entry of `kind` except sequence `keep`
   (keep = 0: all of them). */
static int lst_retire(unsigned kind, uint32_t keep) {
    int err = 0;
    for (uint32_t s = 0; s < LST_SECTORS; s++) {
        lst_hdr_t h;
        if (lst_read_hdr(s, &h) && h.kind == kind && (keep == 0 || h.seq != keep)) {
            if (LSDJ_FL_ERASE(lst_sector_off(s))) err = 1;
            LSDJ_FL_IDLE();
        }
    }
    return err;
}

/* First run of `need` free sectors starting the search at `from` (wrapping to 0).
   Sectors owned by `as_free` count as free too. LST_SECTORS = none. */
static uint32_t lst_find_run(const uint8_t owner[LST_SECTORS], uint32_t need, uint32_t from, unsigned as_free) {
    for (int pass = 0; pass < 2; pass++) {
        /* The second pass runs past `from` so a free run straddling it is found. */
        uint32_t lo = pass ? 0 : from, run = 0;
        uint32_t hi = pass ? (from + need - 1 < LST_SECTORS ? from + need - 1 : LST_SECTORS) : LST_SECTORS;
        for (uint32_t s = lo; s < hi; s++) {
            run = (owner[s] && owner[s] != as_free) ? 0 : run + 1;
            if (run == need) return s + 1 - need;
        }
        if (from == 0) break;
    }
    return LST_SECTORS;
}

typedef struct { uint32_t off; int err; } lst_wr_t;
static int lst_page_writer(void *ctx, const uint8_t *buf, uint32_t n) {
    lst_wr_t *w = (lst_wr_t *)ctx;
    if (LSDJ_FL_WRITE(w->off, buf, n)) { w->err = 1; return 1; }
    w->off += n;
    LSDJ_FL_IDLE();
    return 0;
}

/* Save the regions as entry `kind`. work: LZ_WORK_BYTES; io: 4 KiB RAM buffer. */
static int lst_save(unsigned kind, const lst_region_t *r, unsigned nr, uint32_t build, void *work, uint8_t *io) {
    if (kind < 1 || kind >= LST_KINDS || nr == 0 || nr > LST_MAX_REGIONS) return LST_EFORMAT;
    lst_hdr_t h; memset(&h, 0, sizeof h);
    h.magic = LST_MAGIC; h.version = LST_VERSION; h.kind = kind; h.build = build; h.nreg = nr;
    /* Pass 1: size and CRCs, nothing written. */
    lz_out_t o; memset(&o, 0, sizeof o);
    for (unsigned i = 0; i < nr; i++) {
        h.reg_len[i] = r[i].n; h.reg_crc[i] = lst_crc32(0, r[i].p, r[i].n);
        lz_compress(&o, r[i].p, r[i].n, work);
    }
    lz_out_end(&o);
    h.comp_len = o.total; h.comp_crc = o.crc;
    uint32_t need = (LST_DATA_OFF + h.comp_len + 0xFFFu) / 0x1000u;
    /* Allocate: first free run; the current entry of this kind stays until the commit. */
    lst_entry_t e[LST_KINDS]; uint8_t owner[LST_SECTORS];
    lst_scan(e, owner);
    /* Next-fit from just after the newest entry, so repeated autosaves spread their
       erases over the whole pool instead of wearing the same sectors. */
    uint32_t cursor = 0, newest = 0;
    for (unsigned k = 1; k < LST_KINDS; k++)
        if (e[k].valid && (!newest || (int32_t)(e[k].h.seq - newest) > 0)) {
            newest = e[k].h.seq; cursor = (e[k].sector + e[k].nsect) % LST_SECTORS;
        }
    uint32_t start = lst_find_run(owner, need, cursor, 0);
    if (start == LST_SECTORS && e[kind].valid) {
        /* No room to keep the old copy until the commit: overwrite it instead. Only
           this kind is at risk from a power cut during this save; others never are. */
        start = lst_find_run(owner, need, cursor, kind);
        if (start != LST_SECTORS && lst_retire(kind, 0)) return LST_EFLASH;
    }
    if (start == LST_SECTORS) return LST_FULL;
    h.seq = lst_next_seq(e);
    for (uint32_t s = 0; s < need; s++) {
        if (LSDJ_FL_ERASE(lst_sector_off(start + s))) return LST_EFLASH;
        LSDJ_FL_IDLE();
    }
    /* Pass 2: write the data. */
    lst_wr_t w = { lst_sector_off(start) + LST_DATA_OFF, 0 };
    memset(&o, 0, sizeof o); o.page = lst_page_writer; o.ctx = &w;
    for (unsigned i = 0; i < nr; i++) lz_compress(&o, r[i].p, r[i].n, work);
    lz_out_end(&o);
    if (w.err || o.err || o.total != h.comp_len || o.crc != h.comp_crc) return LST_EFLASH;
    /* Read back. */
    lz_in_t in = { lst_sector_off(start) + LST_DATA_OFF, lst_sector_off(start) + LST_DATA_OFF + h.comp_len, 0, 0, io, 4096u, 0, 0 };
    while (in.off < in.end) { in.len = in.pos; if (lz_in_byte(&in) < 0) return LST_EFLASH; in.pos = in.len; }
    if (in.crc != h.comp_crc) return LST_ECRC;
    /* Commit. */
    h.hdr_crc = lst_hdr_crc(&h);
    static lst_hdr_t hram; hram = h;
    if (LSDJ_FL_WRITE(lst_sector_off(start), (const uint8_t *)&hram, sizeof hram)) return LST_EFLASH;
    lst_hdr_t back;
    if (!lst_read_hdr(start, &back) || back.seq != h.seq) return LST_EFLASH;
    /* Retire every older entry of this kind (and nothing else). */
    lst_retire(kind, h.seq);
    return LST_OK;
}

static int lst_delete(unsigned kind) {
    lst_entry_t e[LST_KINDS]; lst_scan(e, NULL);
    if (kind < 1 || kind >= LST_KINDS || !e[kind].valid) return LST_EMPTY;
    return lst_retire(kind, 0) ? LST_EFLASH : LST_OK;
}

/* Load entry `kind` into the regions. Verifies the stored data first (nothing is
   touched if that fails). If the build differs only region 0 (cartridge RAM) is
   restored; *full tells which. ring: 32 KiB+ RAM; io: 4 KiB RAM. */
static int lst_load(unsigned kind, const lst_region_t *r, unsigned nr, uint32_t build,
                    uint8_t *ring, uint8_t *io, int *full) {
    lst_entry_t e[LST_KINDS]; lst_scan(e, NULL);
    if (kind < 1 || kind >= LST_KINDS || !e[kind].valid) return LST_EMPTY;
    const lst_hdr_t *h = &e[kind].h;
    if (h->nreg < 1 || h->reg_len[0] != r[0].n) return LST_EFORMAT;
    int same = h->build == build && h->nreg == nr;
    for (unsigned i = 0; same && i < nr; i++) same = h->reg_len[i] == r[i].n;
    uint32_t base = lst_sector_off(e[kind].sector) + LST_DATA_OFF;
    /* Verify: stream the whole entry through the decompressor without touching RAM state. */
    lz_in_t in = { base, base + h->comp_len, 0, 0, io, 4096u, 0, 0 };
    for (unsigned i = 0; i < h->nreg; i++) {
        uint32_t c;
        if (lz_decompress(&in, NULL, h->reg_len[i], ring, &c) || c != h->reg_crc[i]) return LST_ECRC;
    }
    /* Apply. */
    lz_in_t in2 = { base, base + h->comp_len, 0, 0, io, 4096u, 0, 0 };
    unsigned upto = same ? nr : 1u;
    for (unsigned i = 0; i < upto; i++) if (lz_decompress(&in2, r[i].p, r[i].n, NULL, NULL)) return LST_ECRC;
    if (full) *full = same;
    return LST_OK;
}

/* For the overlay: what each kind holds. */
typedef struct { int used; uint32_t kb; uint32_t seq; int other_build; } lst_info_t;
static uint32_t lst_info(lst_info_t info[LST_KINDS], uint32_t build) {
    lst_entry_t e[LST_KINDS]; uint8_t owner[LST_SECTORS];
    lst_scan(e, owner);
    uint32_t free_s = 0;
    for (uint32_t s = 0; s < LST_SECTORS; s++) free_s += !owner[s];
    for (unsigned k = 0; k < LST_KINDS; k++) {
        info[k].used = e[k].valid;
        info[k].kb = e[k].valid ? (e[k].nsect * 4u) : 0;
        info[k].seq = e[k].valid ? e[k].h.seq : 0;
        info[k].other_build = e[k].valid && e[k].h.build != build;
    }
    return free_s * 4u;   /* free KiB */
}
