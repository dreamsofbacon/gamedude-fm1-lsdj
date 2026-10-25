/* SPDX-License-Identifier: GPL-3.0-only */
/* Bluetooth MIDI for LSDJ Burrito: the BLE unit (see lsdj_ble.h). Its own
   compilation unit: it reaches the board only through lsdj_ble.h and
   vendor/hortator-ble/ble/ble_core.h (core_*), so board.c compiles the same
   with or without it.

   The BT runtime is DEADACTIVE's Hortator port of the JieLi AC79 BT libraries
   (vendor/hortator-ble, GPL-3.0-only): the cooperative OS, the SDK glue, the
   radio tables. The peripheral side (advertising, the ATT callbacks, the
   connection events) follows GameDude's FM-1 Bluetooth loader
   (experiments/bluetooth/loader_peripheral.c, diagnostic_gatt.c), which runs
   on the FM-1. The service is the standard BLE-MIDI one. */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_ble_hal.h"
#include "ble/ble_core.h"
#include "ble/ble_sdk_abi.h"
#include "ble/ble_sdk.h"
#include "lsdj_ble.h"
#include "lsdj_blemidi.h"
#define BLE_RF_FROM_CORE 1

uint32_t ble_os_now_ms(void) { return core_ms(); }
#define BLE_DIAG_MAGIC 0xB1E0D1A6u
static struct {
    uint32_t magic, stage, task, starts, trim, delays, last_us, t4_stalls, irq_leaks, fatal;
    uint32_t heap_high, alloc_fails, irq_lowered;
} ble_diag __attribute__((section(".noinit"), unused));
static __attribute__((unused)) void ble_stage(uint32_t s)
{
    if (ble_diag.magic != BLE_DIAG_MAGIC) { ble_diag.magic = BLE_DIAG_MAGIC; ble_diag.starts = 0; }
    if (s == 1u) {
        ble_diag.starts++;
        ble_diag.trim = ble_diag.delays = ble_diag.last_us = ble_diag.t4_stalls = ble_diag.irq_leaks = ble_diag.fatal = 0;
        ble_diag.heap_high = ble_diag.alloc_fails = ble_diag.irq_lowered = 0;
    }
    ble_diag.stage = s;
}
#include "ble/ble_os.c"
#include "ble/ble_vm.c"
#include "ble/ble_port.c"

/* the BT libraries' heap: Hortator's measured high-water + 25 % (vendor/hortator-ble/ble/ble.c) */
#define BLE_HEAP_BYTES 13824u
static uint8_t ble_heap[BLE_HEAP_BYTES] __attribute__((section(".pool")));
static uint8_t ble_on;

extern void att_server_init(const uint8_t *, uint16_t (*)(uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t),
                            int (*)(uint16_t, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t));
extern void att_server_register_packet_handler(btstack_packet_handler_t);
extern void sm_set_request_security(int);
extern void bt_max_pwr_set(uint8_t, uint8_t, uint8_t, uint8_t);

/* ---- the GATT table and advertising data: firmware/lsdj_ble_gatt.h ---- */
#include "lsdj_ble_gatt.h"
#define gatt_db lsdj_ble_gatt_db
#define ble_name lsdj_ble_name
#define adv_data lsdj_ble_adv
#define rsp_data lsdj_ble_rsp
enum { H_NAME = LSDJ_BLE_H_NAME, H_IO = LSDJ_BLE_H_IO, H_CCC = LSDJ_BLE_H_CCC };

/* ---- state ---- */
#define ATT_MTU 247u
static uint8_t att_ram[BLE_SDK_ATT_CTRL_BLOCK_SIZE + ATT_MTU + 512u] __attribute__((aligned(4)));
static uint32_t st, err, deadline_ms, tune_due_ms;
static uint16_t con_handle;
static uint8_t connected, notify_on, advertise, tune_pending;
static uint32_t mtu_payload = 20;                  /* ATT MTU - 3 (23 until the client asks for more) */
static uint32_t pk_in, pk_out, sx_in, out_dropped;
static lsdj_bm_rx_t rx;
static lsdj_bm_tx_t tx;
static lsdj_bm_clock_t clk;
static uint8_t pend[ATT_MTU];                      /* a packet the stack refused (its buffer full): sent again */
static uint32_t pend_n;
/* what the BT task received, for the main loop (lsdj_ble_service): short messages, and whole SysEx */
static struct { uint8_t m[3], n; uint32_t sent_ms; } in_q[32];
static uint32_t in_h, in_t;
static uint8_t sx_q[2][LSDJ_BM_SX_MAX];
static uint16_t sx_len[2];
static uint32_t sx_h, sx_t;

uint32_t lsdj_ble_state(void) { return st; }
uint32_t lsdj_ble_error(void) { return err; }
static void fail(uint32_t e) { err = e; st = LSDJ_BLE_FAILED; }

static void on_msg(void *ctx, const uint8_t *m, uint32_t n, uint32_t ts)
{
    (void)ctx;
    if (in_h - in_t >= 32u) return;
    uint32_t i = in_h & 31u;
    for (uint32_t k = 0; k < n; k++) in_q[i].m[k] = m[k];
    in_q[i].n = (uint8_t)n; in_q[i].sent_ms = lsdj_bm_clock(&clk, ts, core_ms());
    in_h++;
}
static void on_sysex(void *ctx, const uint8_t *m, uint32_t n)
{
    (void)ctx;
    if (sx_h - sx_t >= 2u || n > LSDJ_BM_SX_MAX) return;
    uint32_t i = sx_h & 1u;
    for (uint32_t k = 0; k < n; k++) sx_q[i][k] = m[k];
    sx_len[i] = (uint16_t)n; sx_h++; sx_in++;
}

/* ---- the ATT callbacks (the stack's task) ---- */
static uint16_t att_read(uint16_t h, uint16_t attr, uint16_t off, uint8_t *buf, uint16_t n)
{
    uint8_t ccc[2] = {notify_on, 0};
    const uint8_t *p;
    uint32_t size;
    if (!connected || h != con_handle) return 0;
    if (attr == H_NAME) { p = (const uint8_t *)ble_name; size = sizeof ble_name - 1u; }
    else if (attr == H_CCC) { p = ccc; size = 2; }
    else return 0;                                 /* H_IO reads as empty (the BLE-MIDI spec) */
    if (!buf) return (uint16_t)size;               /* (the stack asks for the length first) */
    if (off >= size) return 0;
    size -= off;
    if (n < size) size = n;
    for (uint32_t i = 0; i < size; i++) buf[i] = p[off + i];
    return (uint16_t)size;
}
static int att_write(uint16_t h, uint16_t attr, uint16_t mode, uint16_t off, uint8_t *p, uint16_t n)
{
    if (!connected || h != con_handle) return 3;   /* write not permitted */
    if (mode) return 6;                            /* no prepared writes */
    if (off) return 7;
    if (attr == H_CCC) {
        notify_on = n >= 1u && (p[0] & 1u);
        return 0;
    }
    if (attr != H_IO) return 3;
    pk_in++;
    lsdj_bm_rx(&rx, p, n, on_msg, on_sysex, 0);
    return 0;
}

/* ---- connection events: the HCI and L2CAP callbacks report event[1] + 1 for these two (GameDude's
   diagnostic_gatt.c, tested on the FM-1); allowed only at that boundary, never reading past the buffer ---- */
static void on_event(uint8_t type, uint8_t *p, uint16_t n, int controller)
{
    if (type != BLE_SDK_HCI_EVENT_PACKET || !p || n < 2u) return;
    if (p[0] == 0xB5u && n >= 6u) {                /* ATT_EVENT_MTU_EXCHANGE_COMPLETE: handle, MTU */
        uint32_t mtu = (uint32_t)p[4] | (uint32_t)p[5] << 8;
        if (mtu > ATT_MTU) mtu = ATT_MTU;
        if (mtu >= 23u) {
            mtu_payload = mtu - 3u;
            ble_user_cmd_prepare(8 /* BLE_CMD_ATT_MTU_SIZE */, 1, (int)mtu_payload);
        }
        return;
    }
    uint32_t declared = (uint32_t)p[1] + 2u;
    int connection = p[0] == 0x3Eu && n >= 6u && ((p[2] == 1u && declared == 21u) || (p[2] == 10u && declared == 33u));
    int disconnect = p[0] == 5u && declared == 6u && n >= 5u;
    if (n < declared && !(controller && n + 1u == declared && (connection || disconnect))) return;
    if (connection) {
        if (p[3]) { if (!connected) advertise = 1; return; }   /* a failed connection: advertise again */
        if (connected) return;                     /* (the HCI and the L2CAP callbacks both report it) */
        con_handle = (uint16_t)(p[4] | p[5] << 8);
        connected = 1; notify_on = 0; mtu_payload = 20; pend_n = 0;
        lsdj_bm_rx_reset(&rx); lsdj_bm_tx_reset(&tx); clk.valid = 0;
        if (ble_op_att_send_init(con_handle, att_ram, sizeof att_ram, ATT_MTU)) {
            err = LSDJ_BLE_ERR_ATT; ble_op_disconnect(con_handle); return;
        }
        advertise = 0; st = LSDJ_BLE_CONNECTED;
        tune_pending = 1; tune_due_ms = core_ms() + 1500u;   /* after the client's discovery */
        return;
    }
    if (disconnect && !p[2] && connected && con_handle == (uint16_t)(p[3] | p[4] << 8)) {
        connected = 0; notify_on = 0; tune_pending = 0; pend_n = 0;
        ble_op_att_send_init(0, 0, 0, 0);
        advertise = 1; st = LSDJ_BLE_READY;
    }
}
static void att_packet(uint8_t t, uint16_t c, uint8_t *p, uint16_t n) { (void)c; on_event(t, p, n, 0); }
static void ctl_packet(uint8_t t, uint16_t c, uint8_t *p, uint16_t n) { (void)c; on_event(t, p, n, 1); }

/* called by the stack while it starts (btstack_main.c): a GATT server, no pairing required */
void ble_profile_init(void)
{
    ble_stage(6);
    le_device_db_init();
    ble_stack_gatt_role(0);
    sm_init();
    sm_set_io_capabilities(BLE_SDK_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0);
    sm_set_encryption_key_size_range(7, 16);
    sm_set_request_security(0);
    att_server_init(gatt_db, att_read, att_write);
    att_server_register_packet_handler(att_packet);
    hci_event_callback_set(ctl_packet);
    le_l2cap_register_packet_handler(ctl_packet);
    ble_vendor_set_default_att_mtu(ATT_MTU);
}
static void ble_on_event(int from, struct bt_event *e)   /* bt_event_notify (ble_port.c) */
{
    if (from == BLE_SDK_BT_EVENT_FROM_CON && e && e->event == BLE_SDK_BT_STATUS_INIT_OK && st == LSDJ_BLE_STARTING) {
        ble_stage(7);
        st = LSDJ_BLE_READY; advertise = 1;
    }
}

/* ---- the main loop's side ---- */
void lsdj_ble_start(const uint8_t mac[6])
{
    if (st != LSDJ_BLE_OFF) return;
    if (!ble_rf_trim_load()) { fail(LSDJ_BLE_ERR_NO_TRIM); return; }   /* never without this unit's calibration */
    for (unsigned i = 0; i < 6u; i++) ble_mac[i] = mac[i];
    ble_os_init(ble_heap, sizeof ble_heap);
    ble_delay_calibrate();
    ble_os_trace = &ble_diag.task;
    ble_stage(1);
    ble_stage(2);                                  /* (no stack-limit window to widen: this board sets none) */
    ble_on = 1;
    st = LSDJ_BLE_STARTING;
    deadline_ms = core_ms() + 10000u;
    bt_max_pwr_set(8, 6, 6, 11);                   /* the SDK's cfg_file_parse would: the FM-1 reference level */
    if (le_controller_set_mac(ble_mac)) { fail(LSDJ_BLE_ERR_MAC); return; }
    ble_stage(3);
    ble_os_after_run = ble_irq_check;
    if (task_create(ble_app_core, 0, "app_core")) fail(LSDJ_BLE_ERR_TASK);
}

int lsdj_ble_send(const uint8_t *m, uint32_t n)
{
    if (!connected || !notify_on) { out_dropped++; return 0; }
    if (!lsdj_bm_tx_put(&tx, m, n)) { out_dropped++; return 0; }
    return 1;
}

static void flush_out(void)
{
    while (connected && notify_on) {
        if (!pend_n) pend_n = lsdj_bm_tx_packet(&tx, core_ms(), pend, mtu_payload);
        if (!pend_n) return;
        uint32_t room = 0;
        ble_op_att_get_remain(&room);
        if (room < pend_n) return;                 /* the stack's buffer: next pass */
        if (ble_op_att_send_data(H_IO, pend, pend_n, 1 /* ATT_OP_NOTIFY */)) return;
        pend_n = 0; pk_out++;
    }
}

void lsdj_ble_service(void)
{
    if (!ble_on) return;
    ble_os_service(3000);                          /* up to ~3 ms of the BT tasks per pass */
    ble_diag.heap_high = ble_os_heap_high();
    ble_diag.alloc_fails = ble_os_alloc_fails();
    uint32_t now = core_ms();
    if (st == LSDJ_BLE_STARTING && (int32_t)(now - deadline_ms) >= 0) fail(LSDJ_BLE_ERR_TIMEOUT);
    if (st == LSDJ_BLE_FAILED) return;
    if (tune_pending && connected && (int32_t)(now - tune_due_ms) >= 0) {
        /* 15-30 ms, no latency, 4 s timeout (Apple's rules allow it); a refusal leaves the link as it is */
        static const uint16_t fast[4] = {12, 24, 0, 400};
        tune_pending = 0;
        (void)ble_user_cmd_prepare(11 /* BLE_CMD_REQ_CONN_PARAM_UPDATE */, 2, (int)con_handle, (void *)fast);
    }
    if (advertise && !connected) {
        advertise = 0;
        int r = ble_user_cmd_prepare(2 /* ADV_PARAM */, 3, 160 /* 100 ms */, 0 /* ADV_IND */, 7 /* all channels */);
        if (!r) r = ble_user_cmd_prepare(3 /* ADV_DATA */, 2, (int)sizeof adv_data, (void *)adv_data);
        if (!r) r = ble_user_cmd_prepare(4 /* RSP_DATA */, 2, (int)sizeof rsp_data, (void *)rsp_data);
        if (!r) r = ble_user_cmd_prepare(1 /* ADV_ENABLE */, 1, 1);
        if (r) fail(LSDJ_BLE_ERR_ADV); else st = LSDJ_BLE_ADVERTISING;
    }
    flush_out();
    while (in_t != in_h) { uint32_t i = in_t & 31u; core_ble_midi(in_q[i].m, in_q[i].n, in_q[i].sent_ms); in_t++; }
    while (sx_t != sx_h) { uint32_t i = sx_t & 1u; core_ble_sysex(sx_q[i], sx_len[i]); sx_t++; }
}

/* the last start, kept over a reset (.noinit ble_diag): 1 = none recorded, or it reached BT_STATUS_INIT_OK (stage 7)
   with no fatal error; 0 = it stopped short (a hang or a crash in it) or ended in a library fault (Hortator's
   AUTOSTART safeguard, ble_start_rec_ok) */
int lsdj_ble_prev_ok(void)
{
    return ble_diag.magic != BLE_DIAG_MAGIC || !ble_diag.starts || (ble_diag.stage == 7u && !ble_diag.fatal);
}

uint32_t lsdj_ble_stat(unsigned i)
{
    switch (i) {
    case 0: return st;
    case 1: return err;
    case 2: return pk_in;
    case 3: return pk_out;
    case 4: return sx_in;
    case 5: return rx.bad;
    case 6: return mtu_payload;
    case 7: return ble_diag.heap_high;
    case 8: return ble_diag.alloc_fails;
    case 9: return ble_os_run_max_us(0);
    case 10: return ble_diag.stage;
    case 11: return ble_diag.fatal;
    case 12: return out_dropped + tx.dropped;
    default: return 0;
    }
}
