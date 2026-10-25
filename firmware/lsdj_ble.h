/* SPDX-License-Identifier: GPL-3.0-only */
/* Bluetooth MIDI for LSDJ Burrito (firmware/lsdj_ble.c, its own compilation unit
   with the JieLi BT libraries; built with tools/build.py --ble).

   DEVICE mode: the FM-1 is a standard BLE-MIDI device ("LSDJ Burrito"). Phones,
   tablets, computers and the web page connect to it. MIDI clock and transport
   join the USB / TRS path (each message timed by the sender's timestamp), and
   the web page's link SysEx (firmware/lsdj_link.h) works as over USB.

   Everything runs on CPU0, from the main loop (lsdj_ble_service): the BT
   libraries' tasks are cooperative (vendor/hortator-ble/ble/ble_os.c); their
   interrupts are below the audio's. The radio is never started without this
   unit's stored calibration (read once at boot, before the emulator runs). */
#pragma once
#include <stdint.h>

enum { LSDJ_BLE_OFF, LSDJ_BLE_STARTING, LSDJ_BLE_READY, LSDJ_BLE_ADVERTISING, LSDJ_BLE_CONNECTED, LSDJ_BLE_FAILED };
enum { LSDJ_BLE_ERR_NONE, LSDJ_BLE_ERR_NO_TRIM, LSDJ_BLE_ERR_TIMEOUT, LSDJ_BLE_ERR_TASK, LSDJ_BLE_ERR_MAC,
       LSDJ_BLE_ERR_ADV, LSDJ_BLE_ERR_ATT };

/* the unit: start once (DEVICE mode), then service every main-loop pass */
void lsdj_ble_start(const uint8_t mac[6]);
void lsdj_ble_service(void);
uint32_t lsdj_ble_state(void);
uint32_t lsdj_ble_error(void);
/* one whole MIDI message out (a SysEx with its F0 and F7); 0 = not connected or queue full (dropped whole) */
int lsdj_ble_send(const uint8_t *m, uint32_t n);
/* diagnostics: 0 state, 1 error, 2 packets in, 3 packets out, 4 SysEx in, 5 bad packets, 6 MTU payload,
   7 heap high-water, 8 heap refusals, 9 longest task run (us), 10 start stage, 11 fatal, 12 dropped out */
uint32_t lsdj_ble_stat(unsigned i);
/* 0 if the last start (before a reset) stopped short or faulted: the board does not start again by itself */
int lsdj_ble_prev_ok(void);

/* the board (board.c) provides these to the unit */
uint32_t core_ms(void);
int core_rf_trim(uint8_t trim[68], uint8_t xosc[2], uint8_t pa[7]);   /* the boot-time read; 1 = valid */
/* in the main loop, after the BT tasks ran: a short or real-time message (1..3 bytes) with our
   millisecond time the sender sent it; a whole SysEx (the bytes between F0 and F7) */
void core_ble_midi(const uint8_t *m, uint32_t n, uint32_t sent_ms);
void core_ble_sysex(const uint8_t *m, uint32_t n);
