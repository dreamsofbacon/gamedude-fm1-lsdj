/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define GB_PORT_RATE 44100u
#define GB_PORT_WIDTH 160u
#define GB_PORT_HEIGHT 144u
#define GB_PORT_SAVE_MAX 131072u
/* Pressed bits, matching Peanut-GB: A B SELECT START RIGHT LEFT UP DOWN. */
enum { GP_A=1, GP_B=2, GP_SELECT=4, GP_START=8, GP_RIGHT=16,
       GP_LEFT=32, GP_UP=64, GP_DOWN=128 };
enum { GP_OK=0, GP_SHORT=1, GP_COLOR=2, GP_ROM_SIZE=3, GP_RAM_SIZE=4,
       GP_CHECKSUM=5, GP_MAPPER=6, GP_CPU=7, GP_TIMEOUT=8 };
int gp_load(const uint8_t *rom, uint32_t size);
/* Sparse cartridge: call before gp_load, which then takes the logical size. */
/* start/len: per stored bank (LSDj slot v2), or NULL for 16 KiB banks back to back */
void gp_set_bank_map(const uint8_t *map, uint32_t banks, const uint32_t *start, const uint16_t *len);
int gp_frame(void);
/* Sound samples the emulator keeps queued (44.1 kHz): 2955 = 67 ms, four frames, the
   room slow frames and the effects need (alpha 56-59 had 1300, 29 ms: crunchy on the device). */
#define GP_AUDIO_TARGET 2955u
/* LSDj leading (SYNC = LSDJ): the sound position of each tick it sent (24 a beat), oldest first. */
int gp_master_tick_peek(uint32_t *pos);
void gp_master_tick_drop(void);
/* Emulator on CPU1 (FM-1). Each field has one writer:
     CPU0: pause_req, pause_gen, pic_ack, audio_target, pic_every
     CPU1: paused_gen, pic_seq, heartbeat, busy_ticks, frames, last_ticks
   Pause: CPU0 bumps pause_gen, sets pause_req, waits for paused_gen == pause_gen
   (CPU1 is then between frames and touches nothing but this struct); clearing
   pause_req resumes. Picture: after a drawn frame pic_seq moves on; gp_pixels()
   stays unchanged until CPU0 sets pic_ack = pic_seq. */
typedef struct {
    volatile uint32_t pause_req, pause_gen, paused_gen;
    volatile uint32_t pic_seq, pic_ack;
    volatile uint32_t heartbeat, busy_ticks, frames, last_ticks;
    volatile uint32_t audio_target;
    volatile uint32_t pic_every;   /* draw a picture every this many frames (6: 10/s) */
} gp_core1_t;
extern gp_core1_t gp_core1;
void gp_core1_step(void);   /* CPU1: one pass of its loop (in RAM) */
/* FM-1 effects: mixes one split sample (4 channels + panning word) into stereo,
   before the volume; called from gp_audio_read. */
void gp_set_fx(void (*fn)(const int16_t *s, int32_t *l, int32_t *r));
uint8_t gp_wram_peek(uint16_t addr);   /* work RAM byte (C000-DFFF) */
void gp_wram_poke(uint16_t addr, uint8_t v);   /* written before the next emulated frame */
int gp_wram_poke_many(const uint16_t *addr, const uint8_t *v, uint32_t n);   /* all in the same frame */
void gp_buttons(uint32_t pressed);
void gp_pause(uint32_t paused);
void gp_reset(void);
void gp_palette(uint32_t palette);
void gp_palette_colors(const uint16_t colors[4]);   /* RGB565, lightest first (themes) */
const uint8_t *gp_vram(void);      /* video RAM (tiles), for drawing in LSDj's style */
uint8_t gp_lcdc(void);
uint8_t gp_bgp(void);
const uint16_t *gp_shades(void);   /* the four colours in use */
void gp_volume(uint32_t volume); /* 0..100 */
uint16_t *gp_pixels(void);
void gp_render(uint32_t on);     /* draw the next frame's picture (default: as set at load) */       /* native RGB565, 160x144 */
uint8_t *gp_save_data(void);
uint32_t gp_save_size(void);
uint32_t gp_save_dirty(void);
void gp_save_clean(void);
/* Autosave support: real changes ignore cartridge RAM [lo, hi]; whether there was one since
   the last clean; mark one; the frame of the last one (gp_frame_count). */
void gp_save_ignore(uint32_t lo, uint32_t hi);
uint32_t gp_save_dirty_sig(void);
void gp_save_mark_dirty(void);
uint32_t gp_save_last_change(void);
/* Autosave support: real changes ignore cartridge RAM [lo, hi]; frame of the last
   real change; notes started so far (NRx4 triggers). */
uint32_t gp_note_count(void);
/* Sound rendering since the last call (CPU meter): 24 MHz ticks and render calls. */
uint32_t gp_snd_take_ticks(void);
uint32_t gp_snd_take_renders(void);
/* Frames in a row whose audio output was exactly zero (LSDj stopped). */
uint32_t gp_silent_frames(void);
/* Software PS/2 keyboard on the link port (LSDj SYNC=KEYBD): queue one key code. */
void gp_keybd_send(uint8_t code);
uint32_t gp_keybd_pending(void);
/* One byte into the link port, clocked in MSB first by an external clock
   (Arduinoboy MIDI sync: one 0x00 byte per MIDI clock tick). Same queue. */
void gp_link_send(uint8_t byte);
/* One MIDI clock tick (a 0x00 link byte) at sound position pos: clocked in when
   emulation has written that many samples, so ticks keep their spacing exactly.
   gp_audio_consumed: samples taken by the sound output so far (same count). */
void gp_link_tick_at(uint32_t pos);
uint32_t gp_audio_consumed(void);
void gp_audio_read(int16_t *out, uint32_t frames);
uint32_t gp_audio_queued(void);
uint32_t gp_error(void);
uint32_t gp_frame_count(void);
const char *gp_title(void);
uint32_t gp_input_keys(uint32_t lo, uint32_t hi);
