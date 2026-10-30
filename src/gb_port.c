/* SPDX-License-Identifier: GPL-3.0-only */
#include "gb_port.h"
#include "smk37_game_controls.h"
#include <string.h>
/* FM-1 firmware: the hot path runs from RAM (section .ram_text, copied at boot).
   From flash, through the small XIP cache, LSDj playing cost ~170 CPU cycles per
   emulated Game Boy instruction (CPU meter: EMU 96%, sound only 4%). */
#if defined(GB_HARDWARE) && !GB_SMK37
#define GP_RAM __attribute__((section(".ram_text.gb")))
struct gb_s;
GP_RAM uint8_t __gb_read(struct gb_s *gb, uint16_t addr);
GP_RAM void __gb_write(struct gb_s *gb, uint_fast16_t addr, uint8_t val);
GP_RAM uint8_t __gb_execute_cb(struct gb_s *gb);
GP_RAM void __gb_draw_line(struct gb_s *gb);
GP_RAM void __gb_step_cpu(struct gb_s *gb);
/* gb_reset writes registers through __gb_write, and gb_init calls gb_reset. */
GP_RAM void gb_reset(struct gb_s *gb);
enum gb_error_e;
GP_RAM enum gb_init_error_e gb_init(struct gb_s *gb,
    uint8_t (*gb_rom_read)(struct gb_s*, const uint_fast32_t),
    uint8_t (*gb_cart_ram_read)(struct gb_s*, const uint_fast32_t),
    void (*gb_cart_ram_write)(struct gb_s*, const uint_fast32_t, const uint8_t),
    void (*gb_error)(struct gb_s*, const enum gb_error_e, const uint16_t),
    void *priv);
/* A call between flash (0x02000000) and RAM (0x01C00000) is out of the 23-bit
   jump range in both directions: flash code calls RAM code through a pointer,
   and RAM code gets its own memcpy / memset. */
#define GP_FAR(fn) ((__typeof__(&fn))gp_far((void *)&fn))
static void *gp_far(void *p) { void *volatile q = p; return q; }
GP_RAM static void *gp_memcpy(void *d, const void *s, size_t n) {
    uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d;
}
GP_RAM static void *gp_memset(void *d, int v, size_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)v; return d; }
#define memcpy gp_memcpy
#define memset gp_memset
#define GP_BANK0_RAM 1
#else
#define GP_RAM
#define GP_FAR(fn) (fn)
#define GP_BANK0_RAM 0
#endif
#ifndef MINIGB_APU_AUDIO_FORMAT_S16SYS
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#endif
#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE GB_PORT_RATE
#endif
#include "minigb_apu.h"
void minigb_apu_render(struct minigb_apu_ctx *, audio_sample_t *, unsigned);
static struct minigb_apu_ctx apu;
GP_RAM static uint8_t audio_read(uint16_t addr) { return minigb_apu_audio_read(&apu, addr); }
static uint32_t note_count;
static void apu_catch_up(void);
#ifdef GP_CLOCK_LOG
static void gp_note_log_now(void);   /* host tests: sound position of each note start */
#endif
GP_RAM static void audio_write(uint16_t addr, uint8_t v) {
    apu_catch_up();
    /* NRx4 with the trigger bit set: a note starts (autosave waits for silence). */
    if ((addr == 0xFF14 || addr == 0xFF19 || addr == 0xFF1E || addr == 0xFF23) && (v & 0x80)) {
        note_count++;
#ifdef GP_CLOCK_LOG
        gp_note_log_now();
#endif
    }
    minigb_apu_audio_write(&apu, addr, v);
}
#define ENABLE_SOUND 1
#define PEANUT_GB_12_COLOUR 0
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#define PEANUT_GB_USE_INTRINSICS 0
#include "peanut_gb.h"
#if defined(GB_HARDWARE)
#define POOL __attribute__((section(".pool"), aligned(4)))
#define BARRIER() __asm__ volatile("csync" ::: "memory")
#else
#define POOL
#define BARRIER() __asm__ volatile("" ::: "memory")
#endif
#ifndef GP_RENDER_SKIP
#if defined(GB_HARDWARE)
#define GP_RENDER_SKIP 1
#else
#define GP_RENDER_SKIP 0
#endif
#endif
static struct gb_s gb;
static uint8_t save[GB_PORT_SAVE_MAX] POOL;
static uint16_t pixels[160*144] POOL;
/* The ring: GP_AUDIO_TARGET queued plus a frame (740) must fit. 4096 samples (93 ms);
   the Bluetooth build's 2048 (46 ms) was crunchy on the device and stays only there. */
#if LSDJ_BLE
#define RING 2048u
#else
#define RING 4096u
#endif
typedef char gp_ring_fits_target[GP_AUDIO_TARGET + 740u <= RING ? 1 : -1];
/* GP_SPLIT (FM-1 firmware): the ring holds, per sample, the four channels and the
   panning word (see minigb_apu_split); gp_audio_read mixes them on the consumer
   side, where the per-channel effects run. Otherwise the ring is stereo. */
#ifndef GP_SPLIT
#if defined(GB_HARDWARE) && !GB_SMK37
#define GP_SPLIT 1
#else
#define GP_SPLIT 0
#endif
#endif
#if GP_SPLIT
#define RING_W 5u
void minigb_apu_split(int16_t *out);
static int16_t chans[740*5];
#else
#define RING_W 2u
#endif
static int16_t ring[RING*RING_W] POOL;
static int16_t pcm[740*2];
static volatile uint32_t write_pos, read_pos;
static const uint8_t *cart;
static uint32_t cart_size, save_size, dirty, error, ready, paused, frames;
/* AUTOSAVE (FM-1_981): cartridge RAM changes outside [ignore_lo, ignore_hi] (LSDj's play-time
   clock) are real changes; the frame of the last one. */
static uint32_t ignore_lo = 1, ignore_hi = 0, dirty_sig, last_sig_frame;
static uint32_t silent_frames;  /* consecutive frames whose output was exactly zero */
static uint32_t sample_acc, palette_id, volume = 65;
static char title[17];
static const uint16_t palettes[4][4] = {
    {0xe7da,0x9d50,0x5308,0x18e3}, {0xffff,0xad55,0x52aa,0},
    {0xffba,0xd42e,0x786d,0x2087}, {0xdfff,0x6e9c,0x3274,0x1108}
};
/* The four colours in use (shade 0 lightest .. 3 darkest): a built-in palette or
   one set with gp_palette_colors (themes). Swapped as a pointer: the emulating
   core reads it per line. */
static uint16_t custom_palette[4];
static const uint16_t *volatile pal = palettes[0];
/* Optional sparse cartridge: bank_map[logical bank] = stored bank, 0xff = absent
   (reads 0xff, e.g. an empty LSDj kit slot). Bank 0 must be stored first.
   bank_start / bank_len (NULL: 16 KiB banks back to back): where each stored bank
   starts and how much of it is stored; past that it reads 0xff (LSDj slot v2). */
static const uint8_t *bank_map;
static uint32_t bank_map_n;
static const uint32_t *bank_start;
static const uint16_t *bank_len;
#if GP_BANK0_RAM
/* LSDj's bank 0 takes 60-74% of its ROM reads while a song plays: keep it in RAM.
   (Bank 2 in RAM as well, alpha 26, made no measurable difference on the FM-1:
   the time goes to interpreting instructions, not to reading them.) */
static uint8_t bank0_ram[16384];
#endif
GP_RAM static uint8_t rom_read(struct gb_s *g, uint_fast32_t a) {
    (void)g;
#if GP_BANK0_RAM
    if (a < 0x4000u) return bank0_ram[a];
#endif
    if (bank_map) {
        uint32_t b = (uint32_t)a >> 14, o = (uint32_t)a & 0x3fff;
        uint8_t s = b < bank_map_n ? bank_map[b] : 0xff;
        if (s == 0xff) return 0xff;
        if (bank_len) return o < bank_len[s] ? cart[bank_start[s] + o] : 0xff;
        return cart[((uint32_t)s << 14) | o];
    }
    return a < cart_size ? cart[a] : 0xff;
}
void gp_set_bank_map(const uint8_t *map, uint32_t banks, const uint32_t *start, const uint16_t *len) {
    bank_map = map; bank_map_n = banks; bank_start = start; bank_len = start ? len : 0;
}
GP_RAM static uint8_t ram_read(struct gb_s *g, uint_fast32_t a) {
    (void)g; return a < save_size ? save[a] : 0xff;
}
GP_RAM static void ram_write(struct gb_s *g, uint_fast32_t a, uint8_t v) {
    (void)g;
    if (a < save_size && save[a] != v) {
        save[a] = v; dirty = 1;
        if (a < ignore_lo || a > ignore_hi) { dirty_sig = 1; last_sig_frame = frames; }
    }
}
static void cpu_error(struct gb_s *g, enum gb_error_e e, uint16_t a) {
    (void)e; (void)a; error = GP_CPU; g->gb_frame = 1;
}
GP_RAM static void line(struct gb_s *g, const uint8_t *p, const uint_fast8_t y) {
    (void)g; if (y >= 144) return;
    for (unsigned x=0;x<160;x++) pixels[y*160+x]=pal[p[x]&3];
}
/* LSDj leading: its clock bytes' sound positions (master_tx, below) */
#define MT_Q 32u   /* 1.3 beats: CPU0 takes them every pass */
static uint32_t mt_q[MT_Q];
static volatile uint32_t mt_head, mt_tail;
GP_RAM static void master_tx(struct gb_s *g, const uint8_t b);   /* LSDj leading: below */
int gp_load(const uint8_t *rom, uint32_t n) {
    ready=0; error=0; paused=0; frames=0; dirty=0; save_size=0;
    title[0]=0; cart=0; cart_size=0; write_pos=read_pos=0;
    memset(pixels,0,sizeof pixels);
    if (!rom || n<0x150) return error=GP_SHORT;
    if (rom[0x143]==0xc0) return error=GP_COLOR;
    /* Validate before gb_init, which indexes its size tables directly. */
    if (rom[0x148]>8 || n != (32768u<<rom[0x148])) return error=GP_ROM_SIZE;
    if (rom[0x149]>5) return error=GP_RAM_SIZE;
    uint8_t sum=0;
    for (unsigned i=0x134;i<=0x14c;i++) sum=(uint8_t)(sum-rom[i]-1);
    if (sum!=rom[0x14d]) return error=GP_CHECKSUM;
    switch(rom[0x147]) {
      case 0: case 1: case 2: case 3: case 5: case 6: case 8: case 9:
      case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13:
      case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: break;
      default: return error=GP_MAPPER;
    }
    cart=rom; cart_size=n;
#if GP_BANK0_RAM
    {   /* bank 0 as the emulator reads it (sparse map: stored bank 0 comes first) */
        uint32_t k = n < sizeof bank0_ram ? n : (uint32_t)sizeof bank0_ram;
        if (bank_map && bank_len && k > bank_len[0]) k = bank_len[0];   /* slot v2: the rest reads 0xff */
        for (uint32_t i = 0; i < sizeof bank0_ram; i++) bank0_ram[i] = i < k ? rom[i] : 0xff;
    }
#endif
    memset(&gb,0,sizeof gb); memset(&apu,0,sizeof apu);
    minigb_apu_audio_init(&apu);
    if (GP_FAR(gb_init)(&gb,rom_read,ram_read,ram_write,cpu_error,0)!=GB_INIT_NO_ERROR)
        return error=GP_MAPPER;
    gb.gb_serial_tx = master_tx;   /* LSDj leading: MIDI clock out */
    mt_head = mt_tail = 0;
    /* MBC2 RAM is internal to the mapper even with header RAM size zero. */
    if (gb.mbc==2) gb.cart_ram=1;
    size_t s=0;
    if (gb_get_save_size_s(&gb,&s) || s>sizeof save) return error=GP_RAM_SIZE;
    save_size=gb.cart_ram ? (uint32_t)s : 0;
    memset(save,0xff,sizeof save);
    gb_init_lcd(&gb,line); gb.direct.joypad=0xff;
    gb.direct.frame_skip=GP_RENDER_SKIP; gb.direct.interlace=0;
    for (unsigned i=0;i<16;i++) {
        uint8_t c=rom[0x134+i];
        if (i==15 && (rom[0x143]&0x80)) c=0;
        title[i]=(c>=32 && c<=126)?(char)c:0;
    }
    title[16]=0; sample_acc=0; ready=1; return GP_OK;
}
void gp_buttons(uint32_t p) {
    /* Opposite directions cancel. Ownership is handled by each frontend. */
    if ((p&48)==48) p&=~48u;
    if ((p&192)==192) p&=~192u;
    gb.direct.joypad=(uint8_t)~p;
}
void gp_pause(uint32_t p) { paused=!!p; gp_buttons(0); }
void gp_reset(void) {
    if (!ready) return;
    memset(&apu,0,sizeof apu); minigb_apu_audio_init(&apu); GP_FAR(gb_reset)(&gb);
    gb.direct.frame_skip=GP_RENDER_SKIP;
    error=0; sample_acc=0; frames=0;
    gp_buttons(0);
}
/* Cycle-synced APU: before each APU register write, render the samples up to the
   CPU's position in the frame. LSDj's kits rewrite wave RAM many times per frame;
   rendering once per frame kept only the frame's last state (kicks lost their
   pitch sweep, short hits vanished). Frames end at LY 144 (vblank). */
static uint32_t frame_samples, rendered_samples;
/* Sound rendering time for the CPU meter (24 MHz ticks; FM-1 firmware only). */
#if defined(GB_HARDWARE) && !GB_SMK37
#include "fm1_time.h"
#define GP_TICKS() fm1_ticks()
#else
#define GP_TICKS() 0u
#endif
static uint32_t snd_ticks, snd_renders;
GP_RAM static void apu_render(uint32_t from, uint32_t n) {
    uint32_t t0 = GP_TICKS();
#if GP_SPLIT
    minigb_apu_split(chans + from * 5);
#endif
    minigb_apu_render(&apu, pcm + from * 2, n);
    snd_ticks += GP_TICKS() - t0; snd_renders++;
}
uint32_t gp_snd_take_ticks(void) { uint32_t t = snd_ticks; snd_ticks = 0; return t; }
uint32_t gp_snd_take_renders(void) { uint32_t n = snd_renders; snd_renders = 0; return n; }
GP_RAM static void apu_catch_up(void) {
#ifdef GP_NO_CATCHUP
    return;
#endif
    if (!frame_samples) return;
    uint32_t ly = gb.hram_io[IO_LY];
    uint32_t cyc = ((ly + LCD_VERT_LINES - LCD_HEIGHT) % LCD_VERT_LINES) * LCD_LINE_CYCLES + gb.counter.lcd_count;
    if (cyc > LCD_FRAME_CYCLES) cyc = LCD_FRAME_CYCLES;
    uint32_t target = cyc * frame_samples / LCD_FRAME_CYCLES;   /* < 2^27: 32-bit */
    if (target > rendered_samples) {
        apu_render(rendered_samples, target - rendered_samples);
        rendered_samples = target;
    }
}
/* Software PS/2 keyboard on the link port (LSDj SYNC=KEYBD). LSDj reads a PS/2
   keyboard through the 8-bit serial register in external-clock mode, which keeps
   only the start bit and the low 7 data bits, reversed (LSDj's author, keyjazz;
   RetroPlug's lsdjKeyboardMap.ts). So each key code goes in as ONE serial byte:
   the low 7 bits reversed, MSB first, clocked ~100 us per bit, then a 4 ms gap.
   A bit is only clocked in while a transfer is armed with the external clock. */
#define KB_BIT_CYCLES 1u       /* bits of a byte back to back, as RetroPlug does */
#define KB_GAP_CYCLES 16777u   /* 4 ms between keys, as Arduinoboy does */
#define KB_GAP 2u
#ifdef GP_CLOCK_LOG
uint32_t gp_clock_log[4096], gp_clock_logged;   /* host tests: sound position each clock byte went in */
#define GP_LOG_POS() do { if (gp_clock_logged < 4096u) gp_clock_log[gp_clock_logged++] = write_pos + kb_frame_cycle() * frame_samples / LCD_FRAME_CYCLES; } while (0)
uint32_t gp_note_log[8192], gp_note_logged;
static uint32_t kb_frame_cycle(void);
static void gp_note_log_now(void) { if (gp_note_logged < 8192u && frame_samples) gp_note_log[gp_note_logged++] = write_pos + kb_frame_cycle() * frame_samples / LCD_FRAME_CYCLES; }
#else
#define GP_LOG_POS() do { } while (0)
#endif
static uint8_t kb_q[512];   /* 3 bytes a key, 4 ms each: 2 s of backlog */
/* One producer (gp_keybd_send, CPU0) and one consumer (kb_clock, the emulating
   core, which may be CPU1): the entry is written before the tail moves. */
static volatile uint32_t kb_head, kb_tail;
static uint32_t kb_shift, kb_budget, kb_prev_cyc, kb_unarmed;
static void kb_push(uint8_t v) {
    uint32_t t = kb_tail;
    if (t - kb_head >= sizeof kb_q) return;
    kb_q[t & (sizeof kb_q - 1u)] = v; BARRIER(); kb_tail = t + 1u;
}
void gp_keybd_send(uint8_t code) {
    uint8_t r = 0;
    for (unsigned i = 0; i < 7; i++) r = (uint8_t)(r << 1 | ((code >> i) & 1u));   /* low 7 bits, reversed */
    for (int i = 7; i >= 0; i--) kb_push((r >> i) & 1u);                            /* MSB first */
    kb_push(KB_GAP);
}
void gp_link_send(uint8_t byte) {
    for (int i = 7; i >= 0; i--) kb_push((byte >> i) & 1u);
    kb_push(KB_GAP);
}
uint32_t gp_keybd_pending(void) { return kb_tail - kb_head; }
static uint32_t kb_frame_cycle(void) {
    uint32_t ly = gb.hram_io[IO_LY];
    return ((ly + LCD_VERT_LINES - LCD_HEIGHT) % LCD_VERT_LINES) * LCD_LINE_CYCLES + gb.counter.lcd_count;
}
GP_RAM static void kb_clock(void) {
    uint32_t cyc = kb_frame_cycle();
    kb_budget += cyc >= kb_prev_cyc ? cyc - kb_prev_cyc : cyc + LCD_FRAME_CYCLES - kb_prev_cyc;
    kb_prev_cyc = cyc;
    while (kb_head != kb_tail) {
        uint8_t v = kb_q[kb_head & (sizeof kb_q - 1u)];
        uint32_t need = v == KB_GAP ? KB_GAP_CYCLES : KB_BIT_CYCLES;
        if (kb_budget < need) return;
        if (v != KB_GAP) {
            if ((gb.hram_io[IO_SC] & 0x81u) != 0x80u) { kb_budget = need; return; }  /* wait until armed */
            gb.hram_io[IO_SB] = (uint8_t)(gb.hram_io[IO_SB] << 1 | v);
            if (++kb_shift == 8u) { kb_shift = 0; gb.hram_io[IO_SC] &= 0x7Fu; gb.hram_io[IO_IF] |= SERIAL_INTR; GP_LOG_POS(); }
        }
        kb_budget -= need; kb_head++;
    }
    kb_budget = 0;
}
/* MIDI clock ticks on the link port, each at a sound position (samples written
   to the ring): one 0x00 byte, clocked in when emulation reaches that sample, so
   every tick is heard the same time after it arrived (gp_link_tick_at). Checked
   once per scanline (~0.1 ms). One producer (CPU0), one consumer, as kb_q. */
#define CLK_Q 128u
static uint32_t clk_q[CLK_Q];
static volatile uint32_t clk_head, clk_tail;
static uint32_t clk_ly = 0xFFu, clk_unarmed;
/* gp_wram_poke queue: one producer (CPU0), the emulating core drains it before a
   frame, so the writes of one call batch land together. Entry: offset << 8 | value. */
#define POKE_Q 16u
static uint32_t poke_q[POKE_Q];
static volatile uint32_t poke_head, poke_tail;
void gp_link_tick_at(uint32_t pos) {
    uint32_t t = clk_tail;
    if (t - clk_head >= CLK_Q) return;
    clk_q[t & (CLK_Q - 1u)] = pos; BARRIER(); clk_tail = t + 1u;
}
uint32_t gp_audio_consumed(void) { return read_pos; }
/* LSDj leading (PROJECT > SYNC = LSDJ, playing) sends one link byte per MIDI tick on
   its own clock, as an Arduinoboy reads it in its LSDj master mode. Each such
   transfer's sound position goes here, for MIDI clock out (gp_master_tick_peek /
   _drop). One producer (the emulating core), one consumer (CPU0), as kb_q. */
GP_RAM static void master_tx(struct gb_s *g, const uint8_t b) {
    (void)b;
    if (!(g->hram_io[IO_SC] & 0x01u)) return;          /* internal clock only (the keyboard and MIDI clock use external) */
    uint32_t t = mt_tail;
    if (t - mt_head >= MT_Q) return;
    uint32_t ly = g->hram_io[IO_LY];
    uint32_t cyc = ((ly + LCD_VERT_LINES - LCD_HEIGHT) % LCD_VERT_LINES) * LCD_LINE_CYCLES + g->counter.lcd_count;
    mt_q[t & (MT_Q - 1u)] = write_pos + cyc * frame_samples / LCD_FRAME_CYCLES;
    BARRIER(); mt_tail = t + 1u;
}
int gp_master_tick_peek(uint32_t *pos) {
    if (mt_head == mt_tail) return 0;
    BARRIER(); *pos = mt_q[mt_head & (MT_Q - 1u)];
    return 1;
}
void gp_master_tick_drop(void) { if (mt_head != mt_tail) mt_head++; }
GP_RAM static void clk_check(void) {
    clk_ly = gb.hram_io[IO_LY];
    uint32_t pos = write_pos + kb_frame_cycle() * frame_samples / LCD_FRAME_CYCLES;
    if ((int32_t)(pos - clk_q[clk_head & (CLK_Q - 1u)]) < 0) return;   /* not yet */
    if ((gb.hram_io[IO_SC] & 0x81u) != 0x80u) return;                   /* LSDj not listening yet */
    gb.hram_io[IO_SB] = 0; gb.hram_io[IO_SC] &= 0x7Fu; gb.hram_io[IO_IF] |= SERIAL_INTR;
    GP_LOG_POS();
    clk_head++;
}
/* One frame of Game Boy CPU (in RAM; gp_frame enters it once per frame). */
GP_RAM static void run_cpu(unsigned *steps) {
    while (!gb.gb_frame && !error && (*steps)++ < 80000) {
        __gb_step_cpu(&gb);
        if (kb_head != kb_tail) kb_clock();
        if (clk_head != clk_tail && gb.hram_io[IO_LY] != clk_ly) clk_check();
    }
}
GP_RAM static int gp_frame_ram(void) {
    if (!ready || error || paused) return 0;
    /* Backpressure: never advance and discard sound when the consumer stalls. */
    if (RING-gp_audio_queued()<740) return 0;
    /* Exact rational sample count: no fixed 738-sample-per-frame drift. */
    sample_acc += GB_PORT_RATE * 70224u;
    uint32_t count=sample_acc/4194304u; sample_acc%=4194304u;
    frame_samples=count; rendered_samples=0;
    gb.gb_frame=0;
    unsigned steps=0;
    while (poke_head != poke_tail) { uint32_t p = poke_q[poke_head % POKE_Q]; gb.wram[(p >> 8) & 0x1FFFu] = (uint8_t)p; BARRIER(); poke_head++; }   /* gp_wram_poke */
    kb_prev_cyc = kb_frame_cycle();
    /* Not in LSDj's keyboard mode (nothing arms the serial port): drop queued keys
       after ~1 s instead of firing them in a burst later. */
    if (kb_head != kb_tail && (gb.hram_io[IO_SC] & 0x81u) != 0x80u) { if (++kb_unarmed > 60u) { kb_head = kb_tail; kb_shift = 0; } }
    else kb_unarmed = 0;
    /* Clock ticks LSDj never takes (not armed): dropped after ~1 s, not a burst later. */
    if (clk_head != clk_tail && (gb.hram_io[IO_SC] & 0x81u) != 0x80u) { if (++clk_unarmed > 60u) clk_head = clk_tail; }
    else clk_unarmed = 0;
    run_cpu(&steps);   /* RAM to RAM: a direct call */
    if (error) { frame_samples=0; return 0; }
    if (!gb.gb_frame) { frame_samples=0; error=GP_TIMEOUT; return 0; }
    if (!(gb.hram_io[0x40]&0x80)) {
        for (unsigned i=0;i<160*144;i++) pixels[i]=pal[0];
    }
    if (rendered_samples < count)
        apu_render(rendered_samples, count - rendered_samples);
    frame_samples=0;
    {   /* silence detector: SAVE is only allowed while LSDj is stopped */
        int quiet=1;
#if GP_SPLIT
        for (uint32_t i=0;i<count*5u && quiet;i++) if (i%5u!=4u && chans[i]) quiet=0;
#else
        for (uint32_t i=0;i<count*2u;i++) if (pcm[i]) { quiet=0; break; }
#endif
        silent_frames = quiet ? silent_frames + 1u : 0u;
    }
    uint32_t w=write_pos;
#if GP_SPLIT
    for (uint32_t i=0;i<count;i++)
        for (uint32_t k=0;k<5u;k++) ring[((w+i)&(RING-1))*5u+k]=chans[i*5u+k];
#else
    for (uint32_t i=0;i<count;i++) {
        ring[((w+i)&(RING-1))*2]=(int16_t)((int32_t)pcm[i*2]*(int32_t)volume/100);
        ring[((w+i)&(RING-1))*2+1]=(int16_t)((int32_t)pcm[i*2+1]*(int32_t)volume/100);
    }
#endif
    BARRIER(); write_pos=w+count;
    frames++; return 1;
}
int gp_frame(void) { return GP_FAR(gp_frame_ram)(); }
/* One byte of the Game Boy's work RAM (C000-DFFF), e.g. an LSDj variable; 0xFF otherwise. */
uint8_t gp_wram_peek(uint16_t addr) { return addr >= 0xC000u && addr < 0xC000u + WRAM_SIZE ? gb.wram[addr - 0xC000u] : 0xFFu; }
/* A write to work RAM, done by the emulating core before its next frame (the
   emulator may run on CPU1). Writes queued before a frame all land before it,
   in order; a full queue drops the write. */
void gp_wram_poke(uint16_t addr, uint8_t v) {
    if (addr < 0xC000u || addr >= 0xC000u + WRAM_SIZE) return;
    uint32_t t = poke_tail;
    if (t - poke_head >= POKE_Q) return;
    poke_q[t % POKE_Q] = (uint32_t)(addr - 0xC000u) << 8 | v; BARRIER(); poke_tail = t + 1u;
}
/* Several writes that land in the same frame (published together), or none if
   they do not all fit. Returns 1 if queued. */
int gp_wram_poke_many(const uint16_t *addr, const uint8_t *v, uint32_t n) {
    uint32_t t = poke_tail;
    if (t - poke_head + n > POKE_Q) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (addr[i] < 0xC000u || addr[i] >= 0xC000u + WRAM_SIZE) return 0;
        poke_q[(t + i) % POKE_Q] = (uint32_t)(addr[i] - 0xC000u) << 8 | v[i];
    }
    BARRIER(); poke_tail = t + n;
    return 1;
}

/* Emulator on CPU1 (FM-1). CPU1 calls gp_core1_step() in a loop; CPU0 handles
   input, sound output, the display and saving. See gb_port.h for the fields. */
gp_core1_t gp_core1 = { .audio_target = GP_AUDIO_TARGET, .pic_every = 6u };
static uint32_t pic_frame;
GP_RAM void gp_core1_step(void) {
    gp_core1_t *s = &gp_core1;
    s->heartbeat++;
    if (s->pause_req) {                       /* parked between frames */
        BARRIER(); s->paused_gen = s->pause_gen; BARRIER();
        return;
    }
    if (write_pos - read_pos >= s->audio_target) return;   /* sound buffer full: wait */
    uint32_t t0 = GP_TICKS();
    /* Draw this frame only if CPU0 has taken the last picture and pic_every frames passed. */
    int render = s->pic_ack == s->pic_seq && frames - pic_frame >= s->pic_every;
    gb.direct.frame_skip = 0; gb.display.lcd_draw_line = render ? line : 0;
    if (gp_frame_ram()) {
        if (render) { pic_frame = frames; BARRIER(); s->pic_seq++; }
        uint32_t dt = GP_TICKS() - t0;
        s->last_ticks = dt; s->busy_ticks += dt; BARRIER(); s->frames = frames;
    }
}
#if GP_SPLIT
/* Effects (FM-1): when set, they mix the split sample instead of gp_mix; with
   all effects off they return exactly gp_mix's sums. */
static void (*gp_fx_fn)(const int16_t *s, int32_t *l, int32_t *r);
void gp_set_fx(void (*fn)(const int16_t *s, int32_t *l, int32_t *r)) { gp_fx_fn = fn; }
/* The stereo mix of one split sample, exactly as MiniGB adds the channels:
   pulse 1, pulse 2, wave, noise, each into int16, then the volume. */
static void gp_mix(const int16_t *s, int16_t *l, int16_t *r) {
    uint32_t pan = (uint16_t)s[4], vl = (pan >> 8) & 7u, vr = (pan >> 11) & 7u;
    int16_t L = 0, R = 0;
    for (uint32_t c = 0; c < 4; c++) {
        L = (int16_t)(L + s[c] * (int32_t)((pan >> (4 + c)) & 1u) * (int32_t)vl);
        R = (int16_t)(R + s[c] * (int32_t)((pan >> c) & 1u) * (int32_t)vr);
    }
    *l = (int16_t)((int32_t)L * (int32_t)volume / 100);
    *r = (int16_t)((int32_t)R * (int32_t)volume / 100);
}
#endif
void gp_audio_read(int16_t *out,uint32_t n) {
    uint32_t r=read_pos,w=write_pos; BARRIER();
    for(uint32_t i=0;i<n;i++) {
        if(r!=w && !paused && !error) {
#if GP_SPLIT
            const int16_t *s=&ring[(r&(RING-1))*5u];
            if(gp_fx_fn){
                int32_t l,rr;gp_fx_fn(s,&l,&rr);
                out[2*i]=(int16_t)(l*(int32_t)volume/100);out[2*i+1]=(int16_t)(rr*(int32_t)volume/100);
            }else gp_mix(s,&out[2*i],&out[2*i+1]);
            r++;
#else
            out[2*i]=ring[(r&(RING-1))*2];out[2*i+1]=ring[(r&(RING-1))*2+1];r++;
#endif
        } else {out[2*i]=out[2*i+1]=0;if(r!=w)r++;}
    }
    BARRIER(); read_pos=r;
}
uint32_t gp_audio_queued(void) { return write_pos-read_pos; }
void gp_palette(uint32_t p) {palette_id=p%4;pal=palettes[palette_id];}
void gp_palette_colors(const uint16_t c[4]) {
    for (unsigned i=0;i<4;i++) custom_palette[i]=c[i];
    pal=custom_palette;
}
void gp_volume(uint32_t v) {volume=v>100?100:v;}
uint16_t *gp_pixels(void) {return pixels;}
/* Draw the next frame or not (the FM-1 shows every 6th frame): skipping the
   line drawing changes nothing else in the emulation. Replaces frame_skip. */
void gp_render(uint32_t on) { gb.direct.frame_skip = 0; gb.display.lcd_draw_line = on ? line : 0; }
uint8_t *gp_save_data(void) {return save;}
uint32_t gp_save_size(void) {return save_size;}
uint32_t gp_save_dirty(void) {return dirty;}
void gp_save_clean(void) {dirty=0; dirty_sig=0;}
void gp_save_ignore(uint32_t lo, uint32_t hi) {ignore_lo=lo; ignore_hi=hi;}
uint32_t gp_save_dirty_sig(void) {return dirty_sig;}
void gp_save_mark_dirty(void) {dirty=1; dirty_sig=1;}
uint32_t gp_save_last_change(void) {return last_sig_frame;}
uint32_t gp_note_count(void) {return note_count;}
uint32_t gp_silent_frames(void) {return silent_frames;}
uint32_t gp_error(void) {return error;}
uint32_t gp_frame_count(void) {return frames;}
const char *gp_title(void) {return title;}
uint32_t gp_input_keys(uint32_t lo,uint32_t hi) {
#if GB_SMK37
    return gp_smk37_keys(lo,hi);
#else
    (void)hi;
    /* FM-1 starts at F3. White keys: left 1/2, down 3, right 4/5,
       Select 7, Start 8, whites 15/16 B/A. Black keys 2/3
       both mean Up; count black and white keys separately from the left.
       These are zero-based chromatic scan indices, not white-key numbers. */
    static const uint8_t keys[8]={26,24,11,12,7,0,3,4};
    uint32_t p=0;
    for(unsigned i=0;i<8;i++) if(lo&(1u<<keys[i]))p|=1u<<i;
    if(lo&(1u<<2))p|=GP_LEFT;  /* Inner white 2. */
    if(lo&(1u<<6))p|=GP_RIGHT; /* Inner white 4. */
    if(lo&(1u<<5))p|=GP_UP; /* Third black key, A#3; second is G#3. */
    return p;
#endif
}
/* For drawing in LSDj's own style (firmware/lsdj_native.h): its tiles (video RAM),
   LCDC (which tile addressing), BGP (shade of each colour index), the shades. */
const uint8_t *gp_vram(void) { return gb.vram; }
uint8_t gp_lcdc(void) { return gb.hram_io[IO_LCDC]; }
uint8_t gp_bgp(void) { return gb.hram_io[IO_BGP]; }
const uint16_t *gp_shades(void) { return pal; }
