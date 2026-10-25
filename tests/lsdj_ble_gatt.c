/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for firmware/lsdj_ble_gatt.h: every entry's length matches its
   parts, handles run 1..7 with none skipped, each characteristic declaration
   points at the next handle with the same type, the flags match the declared
   properties, the UUIDs are the BLE-MIDI ones (byte order checked against the
   spec's strings), and the advertising data is well-formed AD structures. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lsdj_ble_gatt.h"

/* "03B80E5A-EDE8-..." -> 16 bytes, little endian (as ATT stores them) */
static void uuid_le(const char *s, uint8_t out[16]) {
    uint8_t be[16]; unsigned n = 0;
    for (const char *c = s; *c && n < 16; ) {
        if (*c == '-') { c++; continue; }
        unsigned v; sscanf(c, "%2x", &v); be[n++] = (uint8_t)v; c += 2;
    }
    assert(n == 16);
    for (unsigned i = 0; i < 16; i++) out[i] = be[15 - i];
}

int main(void) {
    uint8_t svc[16], io[16];
    uuid_le("03B80E5A-EDE8-4B33-A751-6CE34EC4C700", svc);
    uuid_le("7772E5DB-3868-4112-A1A9-F2669D106BF3", io);
    const uint8_t *p = lsdj_ble_gatt_db;
    unsigned handle = 0, entries = 0, pending_value = 0, pending_props = 0;
    const uint8_t *pending_type = 0; unsigned pending_type_len = 0;
    for (;;) {
        unsigned len = p[0] | p[1] << 8;
        if (!len) break;
        unsigned flags = p[2] | p[3] << 8, h = p[4] | p[5] << 8;
        int t128 = (flags & 0x200) != 0;
        unsigned tlen = t128 ? 16u : 2u;
        assert(len >= 6u + tlen);
        assert(h == handle + 1u);   /* consecutive, from 1 */
        handle = h; entries++;
        const uint8_t *type = p + 6, *val = p + 6 + tlen; unsigned vlen = len - 6u - tlen;
        if (pending_value) {         /* the value a declaration announced */
            assert(h == pending_value && tlen == pending_type_len && !memcmp(type, pending_type, tlen));
            assert((flags & 0xFFu) == pending_props);   /* its flags carry the declared properties */
            assert(flags & 0x100u);                      /* answered by the callbacks */
            pending_value = 0;
        }
        if (!t128 && type[0] == 0x00 && type[1] == 0x28) {        /* primary service */
            assert(vlen == 2u || vlen == 16u);
            if (vlen == 16u) assert(!memcmp(val, svc, 16));
            else assert(val[0] == 0x00 && val[1] == 0x18);         /* GAP */
        } else if (!t128 && type[0] == 0x03 && type[1] == 0x28) { /* characteristic declaration */
            assert(vlen == 5u || vlen == 19u);
            pending_props = val[0]; pending_value = val[1] | val[2] << 8;
            pending_type = val + 3; pending_type_len = vlen - 3u;
            assert(pending_value == h + 1u);
        } else if (!t128 && type[0] == 0x02 && type[1] == 0x29) { /* CCCD */
            assert(vlen == 2u && (flags & 0x0Au) == 0x0Au && h == LSDJ_BLE_H_CCC && h == LSDJ_BLE_H_IO + 1u);
        }
        p += len;
    }
    assert(entries == 7 && handle == LSDJ_BLE_H_CCC && !pending_value);
    /* the I/O value: read, write without response, notify (the BLE-MIDI spec), 128-bit, the I/O UUID */
    { const uint8_t *q = lsdj_ble_gatt_db; unsigned off = 0, h = 0;
      while (h != LSDJ_BLE_H_IO) { unsigned len = q[off] | q[off + 1] << 8; h = q[off + 4]; if (h != LSDJ_BLE_H_IO) off += len; }
      assert((q[off + 2] & 0x16) == 0x16 && !memcmp(q + off + 6, io, 16)); }
    /* advertising: AD structures; flags, then the complete 128-bit service list = BLE-MIDI */
    { unsigned i = 0, seen_svc = 0;
      while (i < sizeof lsdj_ble_adv) {
          unsigned l = lsdj_ble_adv[i]; assert(l && i + 1u + l <= sizeof lsdj_ble_adv);
          if (lsdj_ble_adv[i + 1] == 0x07) { assert(l == 17u && !memcmp(lsdj_ble_adv + i + 2, svc, 16)); seen_svc = 1; }
          i += 1u + l;
      }
      assert(i == sizeof lsdj_ble_adv && seen_svc && lsdj_ble_adv[1] == 0x01 && lsdj_ble_adv[2] == 0x06); }
    assert(lsdj_ble_rsp[1] == 0x09 && lsdj_ble_rsp[0] == strlen(lsdj_ble_name) + 1u && !memcmp(lsdj_ble_rsp + 2, lsdj_ble_name, strlen(lsdj_ble_name)));
    puts("PASS lsdj_ble_gatt: 7 entries, consecutive handles, declarations match values, BLE-MIDI UUIDs, CCCD, advertising");
    return 0;
}
