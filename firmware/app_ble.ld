/* SPDX-License-Identifier: GPL-3.0-only */
/* LSDJ Burrito with Bluetooth (tools/build.py --ble): vendor/felucca/firmware/app.ld's memory map (Leo Kuroshita,
 * Hügelton Instruments) plus the JieLi BT libraries' sections, placed as Hortator's app_ble.ld does
 * (vendor/hortator-ble/app_ble.ld, DEADACTIVE; after the AC79 SDK's cpu/wl82/sdk_ld_sfc.c, Apache-2.0):
 * their code and constants in XIP, their data in .data, their zeroed data in .bss, the radio calibration's code in
 * RAM (.ram_text). No classic BT, no TWS: their pool symbols are given with zero size. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    /* app.ld splits this SRAM into RAM (72 KiB) and POOL (360 KiB); "the boundary only splits the budget"
       (its own words), so here it floats: .data, .bss, .pool and the CPU1 stacks in turn */
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 0x6C000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0)) *(.text.memset) *(.text.memcpy) *(.text.memcmp) *(.text*) *(.rodata*)
        . = ALIGN(4);
        a2dp_source_media_codec_begin = .; KEEP(*(.a2dp_source_media_codec)) a2dp_source_media_codec_end = .;
        a2dp_sink_media_probe_begin = .; KEEP(*(.a2dp_sink_media_probe)) a2dp_sink_media_probe_end = .;
        a2dp_sink_media_codec_begin = .; KEEP(*(.a2dp_sink_media_codec)) a2dp_sink_media_codec_end = .;
        a2dp_event_handler_begin = .; KEEP(*(.a2dp_event_handler)) a2dp_event_handler_end = .;
        sdp_record_item_begin = .; KEEP(*(.sdp_record_item)) sdp_record_item_end = .;
        bt_sleep_begin = .; KEEP(*(.bt_sleep)) bt_sleep_end = .;
        tws_func_stub_begin = .; KEEP(*(.tws_func_stub)) tws_func_stub_end = .;
        tws_sync_channel_begin = .; KEEP(*(.tws_sync_channel)) tws_sync_channel_end = .;
        _sys_event_handler_begin = .; PROVIDE(sys_event_handler_begin = .); KEEP(*(.sys_event.*))
        _sys_event_handler_end = .; PROVIDE(sys_event_handler_end = .);
        lp_target_begin = .; KEEP(*(.lp_target)) lp_target_end = .;
        /* every library *_code section but .volatile_ram_code (in .ram_text below; first match wins) */
        *(.[a-uw-z]*_code) *(.v[a-np-z]*_code) *(.*_const) *(.LOG_TAG_CONST)
        . = ALIGN(4); _etext = .;
    } > XIP
    .ram_text : {
        _rt_start = .; KEEP(*(.ram_text*))
        *(.volatile_ram_code) *(*.rf.text.cache.L2.rf_trim)
        . = ALIGN(4); _rt_end = .;
    } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .dsp_text : { _dsp_start = .; *(.dsp_text*) . = ALIGN(4); _dsp_end = .; } > RAMTEXT AT > XIP
    _dsp_load = LOADADDR(.dsp_text);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.*_data) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        _bss_start = .; *(.bss*) *(.sbss*) *(COMMON)
        *(.bd_base) *(.*_bulk) *(.*_bss) *(.btstack_pool)
        acl_tx_pool = .; acl_tx_pool_end = .; acl_rx_pool = .; acl_rx_pool_end = .;
        tws_bulk_pool = .; tws_bulk_pool_end = .;
        . = ALIGN(4); _bss_end = .;
    } > RAM
    .pool (NOLOAD) : { . = ALIGN(32); _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > RAM
    .cpu1_stacks (NOLOAD) : {
        . = ALIGN(32); _cpu1_stacks_lo = .;
        KEEP(*(.cpu1_stacks))
        _cpu1_stacks_hi = .;
    } > RAM
    _cpu1_ustack_top = _cpu1_stacks_lo + 4096;
    _cpu1_sstack_top = _cpu1_stacks_lo + 8192;
    ASSERT(_cpu1_stacks_hi <= 0x01C74000, "CPU1 stacks exceed SRAM (the CPU0 stacks start at 0x01C74000)")
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) *(.debug*) }
}
