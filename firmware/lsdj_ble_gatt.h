/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's GATT table and advertising data as a BLE-MIDI device
   (firmware/lsdj_ble.c). The table is in the JieLi SDK's profile_data format,
   as its generated tables are (apps/common/ble/include/le_net_cfg.h): per
   attribute u16 length (of the whole entry), u16 flags, u16 handle, the type
   (2 or 16 bytes), the value; a 0 length ends it. Flags: 0x02 read, 0x04 write
   without response, 0x08 write, 0x10 notify, 0x100 dynamic (the callbacks
   answer), 0x200 128-bit type. 128-bit UUIDs are stored little endian.
   Pure data: tests/lsdj_ble_gatt.c checks every entry. */
#pragma once
#include <stdint.h>

/* BLE-MIDI service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, I/O characteristic
   7772E5DB-3868-4112-A1A9-F2669D106BF3 (MMA "MIDI over Bluetooth LE" 1.0) */
#define LSDJ_BLE_MIDI_SVC 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
#define LSDJ_BLE_MIDI_IO  0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77
enum { LSDJ_BLE_H_NAME = 3, LSDJ_BLE_H_IO = 6, LSDJ_BLE_H_CCC = 7 };
static const uint8_t lsdj_ble_gatt_db[] = {
    /* 0x0001 GAP service 1800 */
    10, 0, 0x02, 0x00, 1, 0, 0x00, 0x28, 0x00, 0x18,
    /* 0x0002 characteristic: read, value 0x0003, device name 2A00 */
    13, 0, 0x02, 0x00, 2, 0, 0x03, 0x28, 0x02, LSDJ_BLE_H_NAME, 0, 0x00, 0x2A,
    /* 0x0003 the name: read, dynamic */
    8, 0, 0x02, 0x01, LSDJ_BLE_H_NAME, 0, 0x00, 0x2A,
    /* 0x0004 BLE-MIDI service */
    24, 0, 0x02, 0x00, 4, 0, 0x00, 0x28, LSDJ_BLE_MIDI_SVC,
    /* 0x0005 characteristic: read, write without response, notify; value 0x0006, BLE-MIDI I/O */
    27, 0, 0x02, 0x00, 5, 0, 0x03, 0x28, 0x16, LSDJ_BLE_H_IO, 0, LSDJ_BLE_MIDI_IO,
    /* 0x0006 BLE-MIDI I/O: read, write without response, notify, dynamic, 128-bit */
    22, 0, 0x16, 0x03, LSDJ_BLE_H_IO, 0, LSDJ_BLE_MIDI_IO,
    /* 0x0007 its client configuration 2902: read, write, dynamic */
    10, 0, 0x0A, 0x01, LSDJ_BLE_H_CCC, 0, 0x02, 0x29, 0x00, 0x00,
    0, 0};
static const char lsdj_ble_name[] = "LSDJ Burrito";
/* advertising: flags (LE general discoverable, no classic), the BLE-MIDI service;
   the scan response: the name */
static const uint8_t lsdj_ble_adv[] = {2, 0x01, 0x06, 17, 0x07, LSDJ_BLE_MIDI_SVC};
static const uint8_t lsdj_ble_rsp[] = {sizeof lsdj_ble_name, 0x09, 'L', 'S', 'D', 'J', ' ', 'B', 'u', 'r', 'r', 'i', 't', 'o'};
_Static_assert(sizeof lsdj_ble_rsp == sizeof lsdj_ble_name + 1u, "scan response: the whole name");
_Static_assert(sizeof lsdj_ble_adv <= 31u && sizeof lsdj_ble_rsp <= 31u, "advertising: 31 bytes at most");
