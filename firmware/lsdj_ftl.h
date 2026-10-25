/* SPDX-License-Identifier: GPL-3.0-only */
/* Glitch-free autosave for LSDj's cartridge RAM (128 KiB = 32 blocks of 4 KiB)
   in Felucca's data store (0x97000..0xE0000, 73 sectors).

   Measured on the FM-1: a 4 KiB erase blocks interrupts for up to 18 ms (the
   audio DMA half-buffer is 5.8 ms: audible), a 256-byte page program for up to
   1.6 ms (inaudible). So while sound may be playing this module only PROGRAMS
   pages into sectors erased earlier; erases happen only when the caller says the
   output is silent.

   Layout: any sector can hold one data block or be the map. The map is an
   append-only list of 16-byte entries after a 16-byte header:
     data entry   (block -> sector, CRC of the 4 KiB)
     commit entry (closes a save: its data entries count only from here on)
   A save writes the changed blocks into erased sectors page by page, appends
   their data entries, then a commit entry. A power cut before the commit leaves
   the previous save; one after it, the new one. When the map is full a compact
   copy (one entry per block + commit) is written to an erased sector and its
   header programmed last; the newest valid header wins.

   Flash access, as for lsdj_state.h:
     LSDJ_FL_ERASE(off) LSDJ_FL_WRITE(off,p,n) LSDJ_FL_READ(off,p,n) LSDJ_FL_IDLE() */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FTL_LO        0x00097000u
#define FTL_HI        0x000E0000u
#define FTL_N         ((FTL_HI - FTL_LO) / 0x1000u)    /* 73 */
#define FTL_BLOCKS    32u
#define FTL_MAGIC     0x4D46534Cu                       /* "LSFM" */
#define FTL_ENT_PER_MAP ((0x1000u - 16u) / 16u)          /* 255 */
#define FTL_NONE      0xFFu
enum { FTL_FREE = 0, FTL_DIRTY = 1, FTL_LIVE = 2, FTL_MAP = 3, FTL_PENDING = 4 };
enum { FTL_E_DATA = 0x5A, FTL_E_COMMIT = 0xC3,
       FTL_E_SETTINGS = 0x5E };   /* a 32-bit record in dcrc; record number in
                                     `block` (0xFF = record 0, the settings word;
                                     1.. = effects); the newest of each counts.
                                     Older firmware skips this type. */
#define FTL_RECS 23u   /* records 0..22: 0 settings, 1..11 effects, 12..15 the custom theme, 16 MIDI options,
                          17..22 the effects' words 11..16 (alpha 66) */

typedef struct { uint32_t magic, gen, rsv, hcrc; } ftl_hdr_t;
typedef struct { uint8_t type, block, sector, rsv; uint32_t seq, dcrc, ecrc; } ftl_ent_t;

typedef struct {
    uint8_t state[FTL_N];            /* FTL_FREE (erased) .. FTL_PENDING */
    uint8_t map_of[FTL_BLOCKS];      /* committed sector of each block, FTL_NONE = never saved */
    uint32_t crc_of[FTL_BLOCKS];
    uint32_t map_sector, map_gen, map_next, seq, cursor;
    /* save in progress */
    uint32_t todo;                   /* blocks still to write in this save */
    uint8_t pend_block[FTL_BLOCKS], pend_sector[FTL_BLOCKS];
    uint32_t pend_crc[FTL_BLOCKS], pend_n;
    int cur_block; uint32_t cur_sector, cur_page;
    uint32_t retry;                  /* blocks of a failed save, retried next time */
    uint32_t saves, failures;
    uint32_t settings; uint8_t has_settings;   /* record 0: the settings word */
    uint32_t rec[FTL_RECS]; uint32_t rec_has;  /* records 1..: bit n = record n kept */
    uint8_t buf[4096];               /* the block being written (a snapshot) */
} ftl_t;

static uint32_t ftl_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static uint32_t ftl_off(uint32_t s) { return FTL_LO + s * 0x1000u; }
static uint32_t ftl_ecrc(const ftl_ent_t *e) { return ftl_crc32(0x5EED, (const uint8_t *)e, offsetof(ftl_ent_t, ecrc)); }
static uint32_t ftl_hcrc(const ftl_hdr_t *h) { return ftl_crc32(0x5EED, (const uint8_t *)h, offsetof(ftl_hdr_t, hcrc)); }

static int ftl_is_erased(uint32_t off, uint32_t n, uint8_t *tmp) {
    for (uint32_t o = 0; o < n; o += 256u) {
        if (LSDJ_FL_READ(off + o, tmp, 256u)) return 0;
        for (unsigned i = 0; i < 256u; i++) if (tmp[i] != 0xFF) return 0;
    }
    return 1;
}
static int ftl_read_hdr(uint32_t s, ftl_hdr_t *h) {
    if (LSDJ_FL_READ(ftl_off(s), (uint8_t *)h, sizeof *h)) return 0;
    return h->magic == FTL_MAGIC && h->hcrc == ftl_hcrc(h);
}

/* ---------- boot ---------- */
/* Rebuild the state from flash and load the newest committed save into ram.
   1 = a save was loaded, 0 = no map (caller migrates or starts blank). */
static int ftl_boot(ftl_t *f, uint8_t *ram) {
    memset(f, 0, sizeof *f);
    memset(f->map_of, FTL_NONE, sizeof f->map_of);
    f->cur_block = -1;
    int best = -1; ftl_hdr_t h;
    for (uint32_t s = 0; s < FTL_N; s++)
        if (ftl_read_hdr(s, &h) && (best < 0 || (int32_t)(h.gen - f->map_gen) > 0)) { best = (int)s; f->map_gen = h.gen; }
    if (best >= 0) {
        f->map_sector = (uint32_t)best;
        uint8_t pb[FTL_BLOCKS], ps[FTL_BLOCKS]; uint32_t pc[FTL_BLOCKS], pn = 0;
        uint32_t i, last_used = 0;
        for (i = 0; i < FTL_ENT_PER_MAP; i++) {
            ftl_ent_t e;
            if (LSDJ_FL_READ(ftl_off(f->map_sector) + 16u + i * 16u, (uint8_t *)&e, sizeof e)) break;
            const uint8_t *raw = (const uint8_t *)&e; int blank = 1;
            for (unsigned k = 0; k < sizeof e; k++) if (raw[k] != 0xFF) { blank = 0; break; }
            if (blank) continue;
            last_used = i + 1;
            if (e.ecrc != ftl_ecrc(&e)) { pn = 0; continue; }       /* torn entry: drop its save */
            if ((int32_t)(e.seq - f->seq) > 0) f->seq = e.seq;
            if (e.type == FTL_E_DATA && e.block < FTL_BLOCKS && e.sector < FTL_N && pn < FTL_BLOCKS) {
                pb[pn] = e.block; ps[pn] = e.sector; pc[pn] = e.dcrc; pn++;
            } else if (e.type == FTL_E_SETTINGS) {
                unsigned n = e.block == 0xFF ? 0u : e.block;   /* not part of a save */
                if (n == 0) { f->settings = e.dcrc; f->has_settings = 1; }
                else if (n < FTL_RECS) { f->rec[n] = e.dcrc; f->rec_has |= 1u << n; }
            } else if (e.type == FTL_E_COMMIT) {
                for (uint32_t k = 0; k < pn; k++) { f->map_of[pb[k]] = ps[k]; f->crc_of[pb[k]] = pc[k]; }
                pn = 0;
            }
        }
        f->map_next = last_used;
        /* A save cut off after its data entries but before its commit left them
           pending. Appending the next save's commit to this map would adopt them
           too, so the next save starts a fresh map (compaction) instead. */
        if (pn) f->map_next = FTL_ENT_PER_MAP;
    }
    /* Sector states: map, live data, then erased-or-not for the rest. */
    for (uint32_t s = 0; s < FTL_N; s++) f->state[s] = FTL_DIRTY;
    if (best >= 0) f->state[f->map_sector] = FTL_MAP;
    int loaded = 0;
    for (uint32_t b = 0; b < FTL_BLOCKS; b++) {
        if (f->map_of[b] == FTL_NONE) continue;
        uint32_t s = f->map_of[b];
        if (LSDJ_FL_READ(ftl_off(s), f->buf, 4096u) == 0 && ftl_crc32(0, f->buf, 4096u) == f->crc_of[b]) {
            memcpy(ram + b * 4096u, f->buf, 4096u); f->state[s] = FTL_LIVE; loaded = 1;
        } else { f->map_of[b] = FTL_NONE; }   /* cannot happen without a flash fault */
        LSDJ_FL_IDLE();
    }
    for (uint32_t s = 0; s < FTL_N; s++)
        if (f->state[s] == FTL_DIRTY && ftl_is_erased(ftl_off(s), 0x1000u, f->buf)) f->state[s] = FTL_FREE;
    return best >= 0 && loaded;
}

static int ftl_pick_free(ftl_t *f) {
    for (uint32_t k = 0; k < FTL_N; k++) {
        uint32_t s = (f->cursor + k) % FTL_N;
        if (f->state[s] == FTL_FREE) { f->cursor = (s + 1) % FTL_N; return (int)s; }
    }
    return -1;
}
static uint32_t ftl_count(const ftl_t *f, int st) {
    uint32_t n = 0; for (uint32_t s = 0; s < FTL_N; s++) n += f->state[s] == st; return n;
}

static int ftl_put_entry(ftl_t *f, const ftl_ent_t *e) {
    static ftl_ent_t ram_e; ram_e = *e;   /* the program source must be in RAM */
    int rc = LSDJ_FL_WRITE(ftl_off(f->map_sector) + 16u + f->map_next * 16u, (const uint8_t *)&ram_e, sizeof ram_e);
    f->map_next++;                        /* a torn slot is skipped either way */
    return rc;
}
static ftl_ent_t ftl_entry(ftl_t *f, uint8_t type, uint8_t block, uint8_t sector, uint32_t dcrc) {
    ftl_ent_t e = { type, block, sector, 0xFF, ++f->seq, dcrc, 0 };
    e.ecrc = ftl_ecrc(&e);
    return e;
}

/* Write a fresh, compacted map (one entry per saved block + commit) into an
   erased sector; header last. Programs only. 0 = ok. */
static int ftl_compact(ftl_t *f) {
    int s = ftl_pick_free(f); if (s < 0) return -1;
    uint32_t old = f->map_sector, old_gen = f->map_gen, old_next = f->map_next; int had = f->state[old] == FTL_MAP;
    f->map_sector = (uint32_t)s; f->map_next = 0; f->state[s] = FTL_PENDING;
    for (uint32_t b = 0; b < FTL_BLOCKS; b++) {
        if (f->map_of[b] == FTL_NONE) continue;
        ftl_ent_t e = ftl_entry(f, FTL_E_DATA, (uint8_t)b, f->map_of[b], f->crc_of[b]);
        if (ftl_put_entry(f, &e)) goto fail;
    }
    { ftl_ent_t c = ftl_entry(f, FTL_E_COMMIT, 0xFF, 0xFF, 0); if (ftl_put_entry(f, &c)) goto fail; }
    if (f->has_settings) { ftl_ent_t st = ftl_entry(f, FTL_E_SETTINGS, 0xFF, 0xFF, f->settings); if (ftl_put_entry(f, &st)) goto fail; }
    for (uint32_t n = 1; n < FTL_RECS; n++)
        if (f->rec_has & (1u << n)) { ftl_ent_t st = ftl_entry(f, FTL_E_SETTINGS, (uint8_t)n, 0xFF, f->rec[n]); if (ftl_put_entry(f, &st)) goto fail; }
    {
        static ftl_hdr_t h; h.magic = FTL_MAGIC; h.gen = old_gen + 1; h.rsv = 0xFFFFFFFFu; h.hcrc = ftl_hcrc(&h);
        if (LSDJ_FL_WRITE(ftl_off((uint32_t)s), (const uint8_t *)&h, sizeof h)) goto fail;
        f->map_gen = h.gen;
    }
    f->state[s] = FTL_MAP;
    if (had) f->state[old] = FTL_DIRTY;
    return 0;
fail:
    f->state[s] = FTL_DIRTY; f->map_sector = old; f->map_gen = old_gen; f->map_next = old_next;
    return -1;
}

/* ---------- runtime ---------- */
/* Start a save of the blocks in `mask` (bit b = block b changed). Returns 0 if
   started, -1 if busy, -2 if not enough erased sectors (wait for silence). */
static int ftl_begin(ftl_t *f, uint32_t mask) {
    if (f->todo || f->pend_n || f->cur_block >= 0) return -1;
    mask |= f->retry; f->retry = 0;
    uint32_t n = 0; for (uint32_t m = mask; m; m &= m - 1) n++;
    if (!n) return 0;
    /* room in the map for n data entries + commit, else compact first (needs 1 sector) */
    uint32_t need_s = n + (f->state[f->map_sector] == FTL_MAP && f->map_next + n + 1 <= FTL_ENT_PER_MAP ? 0 : 1);
    if (ftl_count(f, FTL_FREE) < need_s) { f->retry = mask; return -2; }
    if (need_s > n && ftl_compact(f)) { f->retry = mask; return -2; }
    f->todo = mask;
    return 0;
}
static int ftl_busy(const ftl_t *f) { return f->todo || f->cur_block >= 0 || f->pend_n; }

/* One step of work; call often. ram = live cartridge RAM. silent = the audio
   output is silent (an erase is allowed). Returns 1 when a save was committed. */
static int ftl_step(ftl_t *f, const uint8_t *ram, int silent) {
    if (f->cur_block >= 0) {                                  /* programming a block */
        if (f->cur_page < 16u) {
            if (LSDJ_FL_WRITE(ftl_off(f->cur_sector) + f->cur_page * 256u, f->buf + f->cur_page * 256u, 256u)) goto fail;
            f->cur_page++;
            return 0;
        }
        /* verify, then note it for the commit */
        uint32_t crc = ftl_crc32(0, f->buf, 4096u), back = 0;
        static uint8_t tmp[256];
        for (uint32_t o = 0; o < 4096u; o += 256u) {
            if (LSDJ_FL_READ(ftl_off(f->cur_sector) + o, tmp, 256u)) goto fail;
            back = ftl_crc32(back, tmp, 256u);
        }
        if (back != crc) goto fail;
        f->pend_block[f->pend_n] = (uint8_t)f->cur_block; f->pend_sector[f->pend_n] = (uint8_t)f->cur_sector;
        f->pend_crc[f->pend_n] = crc; f->pend_n++;
        f->cur_block = -1;
        return 0;
    }
    if (f->todo) {                                            /* next block: snapshot it */
        uint32_t b = 0; while (!(f->todo & (1u << b))) b++;
        f->todo &= ~(1u << b);
        int s = ftl_pick_free(f); if (s < 0) goto fail;
        memcpy(f->buf, ram + b * 4096u, 4096u);
        f->cur_block = (int)b; f->cur_sector = (uint32_t)s; f->cur_page = 0; f->state[s] = FTL_PENDING;
        return 0;
    }
    if (f->pend_n) {                                          /* entries + commit */
        for (uint32_t k = 0; k < f->pend_n; k++) {
            ftl_ent_t e = ftl_entry(f, FTL_E_DATA, f->pend_block[k], f->pend_sector[k], f->pend_crc[k]);
            if (ftl_put_entry(f, &e)) goto fail;
        }
        ftl_ent_t c = ftl_entry(f, FTL_E_COMMIT, 0xFF, 0xFF, 0);
        if (ftl_put_entry(f, &c)) goto fail;
        for (uint32_t k = 0; k < f->pend_n; k++) {
            uint32_t b = f->pend_block[k];
            if (f->map_of[b] != FTL_NONE) f->state[f->map_of[b]] = FTL_DIRTY;
            f->map_of[b] = f->pend_sector[k]; f->crc_of[b] = f->pend_crc[k];
            f->state[f->pend_sector[k]] = FTL_LIVE;
        }
        f->pend_n = 0; f->saves++;
        return 1;
    }
    if (silent) {                                             /* reclaim one sector */
        for (uint32_t s = 0; s < FTL_N; s++)
            if (f->state[s] == FTL_DIRTY) {
                if (LSDJ_FL_ERASE(ftl_off(s)) == 0) f->state[s] = FTL_FREE;
                return 0;
            }
    }
    return 0;
fail:
    /* Abandon this save: its sectors become dirty, its blocks are retried by the
       next ftl_begin (the previous save stays valid). */
    if (f->cur_block >= 0) { f->state[f->cur_sector] = FTL_DIRTY; f->retry |= 1u << f->cur_block; f->cur_block = -1; }
    for (uint32_t k = 0; k < f->pend_n; k++) { f->state[f->pend_sector[k]] = FTL_DIRTY; f->retry |= 1u << f->pend_block[k]; }
    f->retry |= f->todo; f->todo = 0; f->pend_n = 0; f->failures++;
    return -1;
}

/* First use (boot, sound off): write ram as the first save. Sectors listed in
   `keep` (an older save format) are left alone until the new save is committed,
   then erased. Erases freely: call only while silent. 0 = ok. */
static int ftl_format(ftl_t *f, const uint8_t *ram, const uint8_t keep[FTL_N]) {
    memset(f, 0, sizeof *f); memset(f->map_of, FTL_NONE, sizeof f->map_of); f->cur_block = -1;
    for (uint32_t s = 0; s < FTL_N; s++) {
        if (keep && keep[s]) { f->state[s] = FTL_LIVE; continue; }   /* protected for now */
        if (LSDJ_FL_ERASE(ftl_off(s))) return -1;
        f->state[s] = FTL_FREE; LSDJ_FL_IDLE();
    }
    /* write all blocks, then a map holding them (header last) */
    for (uint32_t b = 0; b < FTL_BLOCKS; b++) {
        int s = ftl_pick_free(f); if (s < 0) return -1;
        memcpy(f->buf, ram + b * 4096u, 4096u);
        if (LSDJ_FL_WRITE(ftl_off((uint32_t)s), f->buf, 4096u)) return -1;
        f->map_of[b] = (uint8_t)s; f->crc_of[b] = ftl_crc32(0, f->buf, 4096u); f->state[s] = FTL_LIVE;
        LSDJ_FL_IDLE();
    }
    f->map_gen = 0;
    if (ftl_compact(f)) return -1;
    for (uint32_t s = 0; s < FTL_N; s++)
        if (keep && keep[s]) { if (LSDJ_FL_ERASE(ftl_off(s))) return -1; f->state[s] = FTL_FREE; }
    return 0;
}

/* SAVE button: the whole save in one go, with emulation paused (so erases are
   fine). Writes every block whose contents differ from its committed copy, then
   erases the sectors that became stale so the next save needs no erases first.
   1 = committed, 0 = nothing had changed, < 0 = failed (the previous save stays). */
/* Blocks of ram that differ from their committed copy (bit b = block b). */
static uint32_t ftl_changed(const ftl_t *f, const uint8_t *ram) {
    uint32_t mask = 0;
    for (uint32_t b = 0; b < FTL_BLOCKS; b++)
        if (f->map_of[b] == FTL_NONE || ftl_crc32(0, ram + b * 4096u, 4096u) != f->crc_of[b]) mask |= 1u << b;
    return mask;
}
static int ftl_save_now(ftl_t *f, const uint8_t *ram) {
    while (ftl_busy(f)) if (ftl_step(f, ram, 1) < 0) return -1;   /* not expected: finish it */
    uint32_t mask = ftl_changed(f, ram);
    if (!mask && !f->retry) return 0;
    int rc;
    while ((rc = ftl_begin(f, mask)) == -2) {            /* too few erased sectors: erase one */
        mask = 0;                                         /* ftl_begin kept it in f->retry */
        uint32_t before = ftl_count(f, FTL_FREE);
        if (ftl_step(f, ram, 1) < 0 || ftl_count(f, FTL_FREE) == before) return -2;
    }
    if (rc) return -1;
    int committed = 0;
    while (ftl_busy(f)) {
        int r = ftl_step(f, ram, 1);
        if (r < 0) return -1;
        if (r == 1) committed = 1;
    }
    while (ftl_count(f, FTL_DIRTY)) {                     /* tidy up while still paused */
        uint32_t before = ftl_count(f, FTL_DIRTY);
        if (ftl_step(f, ram, 1) < 0 || ftl_count(f, FTL_DIRTY) == before) break;
    }
    return committed ? 1 : -1;
}

/* Keep record n (0 = the settings word, 1.. = effects): one 16-byte map record;
   a fresh map first if this one is full. Programs only, unless no erased sector
   is left for that fresh map. The caller has stopped everything that reads the
   flash. 0 = kept; the previous value stays if it fails. */
static int ftl_put_record(ftl_t *f, unsigned n, uint32_t value) {
    if (ftl_busy(f) || n >= FTL_RECS) return -1;
    uint32_t *slot = n ? &f->rec[n] : &f->settings;
    int had = n ? (int)((f->rec_has >> n) & 1u) : f->has_settings;
    if (had && *slot == value) return 0;
    uint32_t keep = *slot;
    *slot = value;                                     /* a fresh map carries the new value */
    if (n) f->rec_has |= 1u << n; else f->has_settings = 1;
    int rc;
    if (f->state[f->map_sector] != FTL_MAP || f->map_next + 1u > FTL_ENT_PER_MAP) {
        if (!ftl_count(f, FTL_FREE)) {                 /* make room: erase one stale sector */
            for (uint32_t s = 0; s < FTL_N; s++)
                if (f->state[s] == FTL_DIRTY) { if (LSDJ_FL_ERASE(ftl_off(s)) == 0) f->state[s] = FTL_FREE; break; }
        }
        rc = ftl_compact(f);
    } else {
        ftl_ent_t e = ftl_entry(f, FTL_E_SETTINGS, n ? (uint8_t)n : 0xFF, 0xFF, value);
        rc = ftl_put_entry(f, &e);
    }
    if (rc) {
        *slot = keep;
        if (!had) { if (n) f->rec_has &= ~(1u << n); else f->has_settings = 0; }
        return -1;
    }
    return 0;
}
static int ftl_put_settings(ftl_t *f, uint32_t value) { return ftl_put_record(f, 0, value); }
