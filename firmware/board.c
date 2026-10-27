/* SPDX-License-Identifier: GPL-3.0-only
 * FM-1 and SMK37 adapters using MusicCoach/Felucca HAL. Compile-only candidate.
 * Audio IRQ drains PCM; CPU0 emulates GB/APU; CPU1 scales immutable display frames. */
#include <stdint.h>
#include "gb_port.h"
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "smk37_bootguard.h"
#include "cartridge.h"
#ifdef CART_SLOT_CAPACITY
#include "lsdj_slot.h"
static lsdj_slot_t cart_slot;
static uint8_t cart_slot_valid;
#endif
#include "boot_brand.h"
extern void gb_isr_audio(void),gb_isr_scan(void);
static int32_t dma[256*4] __attribute__((aligned(4)));
static int16_t samples[256*2];
static uint16_t screen[240*216] __attribute__((section(".pool"),aligned(4)));
static volatile uint32_t scan_count,audio_count,key_lo,key_hi,audio_underruns;
static uint32_t display_count;
static uint32_t input_buttons,input_seen;
#include "lsdj_cpu.h"
static lsdj_cpu_t cpu;   /* CPU meter (FM-1: GLO shows it) */
static volatile uint32_t splash_active,ding_position;
static void read_samples(void){
    if(!splash_active){gp_audio_read(samples,256);return;}
    uint32_t p=ding_position;
    for(unsigned i=0;i<256;i++){
        int16_t v=gd_ding(p);if(p<GD_DING_FRAMES)p++;
        samples[2*i]=samples[2*i+1]=v;
    }
    ding_position=p;
}
#include "multicore_video.h"
volatile smk37_bootguard_t gb_bootguard __attribute__((section(".noinit")));

#if GB_SMK37
/* The scanner also includes the LED encoder: configure it before either HAL. */
#define SMK37_LIGHT_MAX_LEVEL 255u /* Readable game pads; diagnostics keep their dim default. */
#include "smk37_boot.h"
#include "smk37_time.h"
#include "smk37_audio.h"
#include "smk37_lcd_hw.h"
#include "smk37_panel.h"
#include "smk37_scan_config.h"
#include "smk37_scanner.h"
#include "smk37_contacts.h"
#include "smk37_keyscan.h"
#include "smk37_controls.h"
#include "smk37_adc.h"
#define SMK37_LIGHT_RESET() fm1_delay_us(350)
#define SMK37_LIGHT_DMA_BARRIER() __asm__ volatile("csync" ::: "memory")
#include "smk37_lights_hw.h"
_Static_assert(SMK37_LIGHT_COMPILED_MAX_LEVEL==255u,"SMK pad encoder must use application brightness");
#include "smk37_game_controls.h"
#include "smk37_game_pads.h"
#include "smk37_volume.h"
#include "smk37_brightness.h"
static smk_brightness_t smk_brightness_fader;
static volatile uint32_t smk_brightness=150;
static volatile uint32_t smk_volume_target=64;
static uint32_t smk_volume_current=64;
static smk37_keyscan_t keys[37];
static smk37_adc_t pad_adc;
static smk37_controls_t panel_controls;
static gb_smk37_pads_t game_pads;
static volatile uint32_t smk_buttons;
static uint8_t pad_lights[SMK37_LIGHT_BYTES] __attribute__((aligned(4)));
static int lcd_command(uint8_t c){return smk37_lcd_command(c,4000000);}
static int lcd_data(const void*p,uint32_t n){return smk37_lcd_data(p,n,4000000);}
static const smk37_panel_io_t panel={lcd_command,lcd_data,fm1_delay_ms};
static const smk37_audio_hooks_t audio_hooks={fm1_delay_ms,fm1_irq_attach};
void gb_scan_irq(void) {
    if(!(SMK37_SCAN_CON&0x8000u))return;
    unsigned index=pad_adc.scan.index;
    int result=smk37_adc_spi_done(&pad_adc);
    if(result>0)smk_volume_target=smk_volume_sample(smk_volume_target,index,pad_adc.scan.raw[index]);
    if(result>0)smk_brightness=smk_brightness_sample(&smk_brightness_fader,smk_brightness,index,pad_adc.scan.raw[index]);
    if(result>0)for(unsigned p=0;p<16;p++)if(index==smk37_pad_input[p])
        gb_smk37_pad_sample(&game_pads,p,pad_adc.scan.raw[index]);
    if(result<0)game_pads.held=game_pads.pressed=0;
    smk37_scan_set_analog(smk37_analog_mask(&pad_adc.scan));
    if(!smk37_scan_tick())return;
    (void)smk37_controls_tick(&panel_controls,smk37_scan_frame);
    smk_buttons=panel_controls.buttons;
    scan_count++;
    for(unsigned k=0;k<37;k++) {
        unsigned raw=smk37_contact_closed(smk37_scan_frame,smk37_key_contacts[k][0])|
            smk37_contact_closed(smk37_scan_frame,smk37_key_contacts[k][1])<<1;
        uint32_t duration;
        if(!smk37_keyscan_tick(&keys[k],raw,&duration))continue;
        if(k<32) {if(duration)key_lo|=1u<<k;else key_lo&=~(1u<<k);}
        else {if(duration)key_hi|=1u<<(k-32);else key_hi&=~(1u<<(k-32));}
    }
}
void gb_audio_irq(void) {
    uint8_t p=smk37_audio_pending();smk37_audio_ack_aux(p);
    if(!(p&SMK37_AUDIO_HALF))return;
    unsigned h=smk37_audio_free_half();read_samples();
    uint32_t target=smk_volume_target;
    smk_volume_render(dma+h*512,samples,smk_volume_current,target);smk_volume_current=target;
    audio_count++;smk37_audio_ack_half();
}
static int display_init(void){smk37_lcd_hw_init(4);return smk37_panel_init(&panel);}
static void display_frame(void){
    if(smk37_panel_window(&panel,0,12,240,216))lcd_data(screen,sizeof screen);
}
static int display_ready(void){return 1;}
static void display_stop(void){}
/* Main-loop only: leave the shared SPI peripheral to any active LCD DMA. */
static void smk_pad_lights(uint32_t held){
    static uint32_t previous=UINT32_MAX,last;
    uint32_t lit=gp_smk37_pad_lights()&~held,level=smk_brightness;
    uint32_t state=lit|(level<<16);
    if(state==previous||!display_ready())return;
    uint32_t now=fm1_ticks();
    /* Coalesce fader changes to 50 Hz; pad press/release feedback stays immediate. */
    if(lit==(previous&0xffffu)&&(uint32_t)(now-last)<480000u)return;
    uint32_t rgb[16]={0};
    for(unsigned p=0;p<16;p++)if(lit&(1u<<p))
        rgb[p]=p>=14?(level<<16)|level:((level*2/3)<<8)|level;
    smk37_light_encode_rgb(pad_lights,rgb);
    if(smk37_lights_send(pad_lights,4000000)){previous=state;last=now;}
}
static void input_init(void){
    smk37_adc_init(&pad_adc);
    SMK37_LIGHT_REG(0x50080u)&=~(1u<<10);
    SMK37_LIGHT_REG(0x50088u)&=~(1u<<10);
    smk37_scan_set_analog(smk37_analog_mask(&pad_adc.scan));
    smk37_scan_indicator_bits=(1u<<16)|(1u<<18); /* PLAY green, REC red. */
    smk37_scan_init(fm1_irq_attach,gb_isr_scan);
    smk_pad_lights(0);
}
static uint32_t smk_input_frame(void){
    fm1_irq_off();
    uint32_t lo=key_lo,hi=key_hi,pads=gb_smk37_pads_take(&game_pads),buttons=smk_buttons;
    uint32_t held=game_pads.held;
    fm1_irq_on();
    smk_pad_lights(held);
    return gp_input_keys(lo,hi)|gp_smk37_pads(pads)|gp_smk37_panel(buttons);
}
static void audio_start(void){(void)smk37_audio_init(dma,512,gb_isr_audio,3,&audio_hooks);}
static void audio_stop(void){smk37_audio_stop();}
#include "smk37_recovery.h"
#else
#include "fm1_timer.h"
#include "fm1_input.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "master_volume.h"
#include "fm1_lcd_hw.h"
#include "../vendor/felucca/firmware/src/lcd.c"
static volatile uint32_t master_gain;
static int32_t master_raw=-1;
static uint32_t master_audio_gain;
static void master_boot(void){
    fm1_adc_init();(void)fm1_adc_read(FM1_ADC_MASTER);
    for(unsigned i=0;i<3;i++){
        int32_t raw=fm1_adc_read(FM1_ADC_MASTER);
        if(raw>=0){master_raw=raw;master_gain=master_audio_gain=gb_master_gain((uint32_t)raw);return;}
    }
    /* ADC failure starts muted; a later successful conversion recovers. */
    master_gain=master_audio_gain=0;
}
static void master_poll(void){
    static uint32_t last;
    uint32_t now=fm1_ticks();if(now-last<240000u)return;last=now;
    int32_t raw=fm1_adc_read(FM1_ADC_MASTER);
    master_raw=raw;if(raw>=0)master_gain=gb_master_gain((uint32_t)raw);
}
static void recovery_tick(void);
void gb_scan_irq(void){
    uint32_t t0=fm1_ticks();
    fm1_timer5_ack();fm1_input_tick();key_lo=fm1_in.notes;scan_count++;recovery_tick();
    cpu.isr+=fm1_ticks()-t0;
}
/* Sound clock: when the audio interrupt last took a block, and the position. */
static volatile uint32_t aud_seq,aud_at_ticks,aud_at_pos;
void gb_audio_irq(void){
    uint32_t t0=fm1_ticks();
    uint32_t p=fm1_audio_pending();fm1_audio_ack_aux(p);
    if(!(p&FM1_AUDIO_HALF))return;
    unsigned h=fm1_audio_free_half();if(!splash_active&&gp_audio_queued()<256)audio_underruns++;
    read_samples();
    aud_seq++;aud_at_ticks=fm1_ticks();aud_at_pos=gp_audio_consumed();aud_seq++;   /* sound clock, for MIDI sync */
    int32_t start=(int32_t)master_audio_gain,target=(int32_t)master_gain;
    for(unsigned i=0;i<256;i++){
        uint32_t gain=(uint32_t)(start+(target-start)*(int32_t)(i+1)/256);
        dma[h*512+i*2]=gb_master_sample(samples[i*2],gain);
        dma[h*512+i*2+1]=gb_master_sample(samples[i*2+1],gain);
    }
    master_audio_gain=(uint32_t)target;
    audio_count++;fm1_audio_ack_half();
    cpu.isr+=fm1_ticks()-t0;
}
static int display_init(void){lcd_init();return 1;}
/* Transfer asynchronously so enlarging the image cannot block emulation for
   the duration of a full LCD DMA. Never overwrite a buffer still in flight. */
static int display_ready(void){
    if(lcd_busy&&!(FM1_LCD_SPI_CON&0x8000u))return 0;
    lcd_sync();return 1;
}
static void display_frame(void){lcd_blit(0,12,240,216,screen);}
#include "fm1_game_lights.h"
/* Note mode: piano keys -> LSDj keyboard (PROJECT > SYNC = KEYBD) through the
   software PS/2 link port in gb_port.c. Mapping as Arduinoboy / RetroPlug: MIDI note
   48 = LSDj keyboard C, two rows of note keys, LSDj's octave keys slide its octave
   to the note's; key presses only (LSDj's keyboard has no note-off). Notes below 48
   are ignored, so low keys never turn into LSDj's mute/cursor commands. */
static void lsdj_keybd_note(int note,int on);
#define LSDJ_KEYBD_NOTE(n,on) lsdj_keybd_note((n),(on))
#include "fm1_lsdj_input.h"
static const uint8_t kb_note_map[24]={0x1A,0x1B,0x22,0x23,0x21,0x2A,0x34,0x32,0x33,0x31,0x3B,0x3A,
                                      0x15,0x1E,0x1D,0x26,0x24,0x2D,0x2E,0x2C,0x36,0x35,0x3D,0x3C};
#define KB_OCT_DN 0x05u   /* F1 */
#define KB_OCT_UP 0x06u   /* F2 */
static int kb_octave=-1;  /* LSDj's keyboard octave as we last set it; -1 = not synced yet */
static uint8_t sync_midi;   /* LSDj's SYNC is MIDI: the link port carries clock, not keys */
static void lsdj_keybd_note(int note,int on){
    if(!on||note<48||sync_midi)return;
    if(kb_octave<0){for(int i=0;i<5;i++)gp_keybd_send(KB_OCT_DN);kb_octave=0;}  /* clamp to 0, as Arduinoboy */
    int n=note-48,oct=n/12;
    while(kb_octave<oct){gp_keybd_send(KB_OCT_UP);kb_octave++;}
    while(kb_octave>oct){gp_keybd_send(KB_OCT_DN);kb_octave--;}
    gp_keybd_send(kb_note_map[n>=0x3C?(n%12)+12:n%12]);
}
static lsdj_input_t lsdj_in;
/* A key's LED at the soft level: the HAL's steady mid level plus the glow. */
static void led_key_soft(uint32_t id,int on){
    for(uint32_t p=0;p<FM1_NCOL;p++)
        for(uint32_t r=1;r<5u;r++)
            if(FM1_KEYMAP[r][p]==(int8_t)id){
                if(on){fm1_led_mid[p]|=(uint8_t)(1u<<r);fm1_led_dim[p]|=(uint8_t)(1u<<r);}
                else{fm1_led_mid[p]&=(uint8_t)~(1u<<r);fm1_led_dim[p]&=(uint8_t)~(1u<<r);}
            }
}
static uint32_t lights_held_prev=UINT32_MAX,lights_shown,soft_shown,lights_base;
static uint8_t lights_valid;   /* 0: the LEDs are unknown (boot, after the boot sweep): write every key */
static uint8_t led_anim;       /* the boot sweep owns the LEDs: write none */
static uint32_t piano_soft;   /* keys to show soft instead of lit (the D-pad on the beat: play_light) */
static void piano_set2(uint32_t lights,uint32_t soft){   /* bit k = key k */
    if(led_anim)return;
    if(lights_valid&&lights==lights_shown&&soft==soft_shown)return;
    if(!lights_valid){lights_shown=~lights;soft_shown=~soft;lights_valid=1;}
    for(unsigned k=0;k<27;k++){
        uint32_t b=1u<<k;
        if((lights^lights_shown)&b)fm1_led_key(14+k,(lights&b)!=0);
        if((soft^soft_shown)&b)led_key_soft(14+k,(soft&b)!=0);
    }
    lights_shown=lights;soft_shown=soft;
}
static void piano_set(uint32_t lights){   /* key lights as given (bit k = key k) */
    lights_held_prev=UINT32_MAX;
    piano_set2(lights,0);
}
static void piano_lights(uint32_t held_actions){
    if(held_actions!=lights_held_prev){lights_base=gp_fm1_key_lights(held_actions);lights_held_prev=held_actions;}
    piano_set2(lights_base&~piano_soft,lights_base&piano_soft);
}
static void input_init(void){
    fm1_input_init();
    lsdj_input_init(&lsdj_in);
    piano_lights(0);
    fm1_led_key(12,1); /* PLAY / Start */
    fm1_led_key(13,1); /* REC / Select */
    fm1_timer5_start(gb_isr_scan,4);
}
static void audio_start(void){fm1_audio_init(dma,512,gb_isr_audio,3);}
static void audio_stop(void){fm1_audio_stop();}
#if !GB_SMK37
static void midi_in(uint32_t pkt,uint32_t at_ms,uint32_t src);   /* MIDI in (lsdj_midi.h), below */
#define FM1_MIDI_IN(p,at,src) midi_in((p),(at),(src))
#endif
#include "fm1_recovery.h"
/* LSDj songs (cartridge RAM) in flash: loaded at boot, written on SAVE. */
static int lsdj_fl_erase(uint32_t off){uint32_t took;return fl_erase4k(off,&took);}
#define LSDJ_FL_ERASE(off) lsdj_fl_erase(off)
#define LSDJ_FL_WRITE(off,p,n) fl_write(off,p,n)
#define LSDJ_FL_READ(off,p,n) ota_fread(off,p,n)
#define LSDJ_FL_IDLE() fm1_wdt_feed()
/* The import's 4 KiB bounce buffer: borrowed from the screen buffer, past the 32 KiB+
   the compressed-save import also borrows (boot only, before anything is drawn there). */
#define LSDJ_SAVE_BUF ((uint8_t*)screen+65536u)
typedef char lsdj_save_buf_inside_screen[65536u+4096u<=sizeof screen?1:-1];
#include "lsdj_save.h"   /* v5 A/B save format: only read once, to import it */
#include "lsdj_state.h"  /* compressed save store: the autosave (and later the slots) */
/* Build-time check: the flash driver must accept the whole save pool (a narrower
   range made autosaves that landed in the pool's top sectors fail silently). */
typedef char lst_pool_inside_flash_range[FL_RANGE_OK(LST_POOL_LO,LST_POOL_HI-LST_POOL_LO)?1:-1];
#include "lsdj_ftl.h"    /* power-cut-safe save store, written only by the SAVE button */
#define SAVE_BUTTON 9u            /* BTN_SAVE */
#define GLO_BUTTON 7u             /* BTN_GLO: settings menu */
#define FX_BUTTON 2u              /* BTN_FX: effects page */
#define SEQ_BUTTON 11u            /* BTN_SEQ: LSDj live / song mode, any screen */
#define ARP_BUTTON 10u            /* BTN_ARP: song row cue (hold, K1/K2 row, let go) */
#define EDIT_BUTTON 6u            /* BTN_EDIT: tempo (hold + K1/K2, or tap) */
#define LSDJ_STATE_BUILD 1u       /* cartridge-RAM-only compressed autosave (v6..v10, imported) */
#define SILENT_FRAMES 30u            /* 0.5 s of exactly-zero output: LSDj is stopped */
static ftl_t ftl;
static int save_display_ok;
#include "lsdj_overlay.h"
#include "lsdj_splash.h"   /* startup screen */
static lsdj_ov_t ov;
/* Settings menu: GLO opens and closes it; firmware/lsdj_settings.h. */
#include "lsdj_settings.h"
static lsdj_settings_t settings;
static lsdj_midi_t midi;   /* the MIDI options (lsdj_midi.h): save-store record 16 */
#define MIDI_REC 16u
static lsdj_menu_t menu;
/* The settings (record 0) and the MIDI options (record 16), only what changed. The
   flash must be safe (emulator parked). 0 = ok. */
static int settings_store(void){
    int rc=ftl_put_settings(&ftl,lsdj_set_pack(&settings));
    uint32_t w=lsdj_midi_pack(&midi);
    if(!rc&&!(((ftl.rec_has>>MIDI_REC)&1u)&&ftl.rec[MIDI_REC]==w))rc=ftl_put_record(&ftl,MIDI_REC,w);
    return rc;
}
static char menu_hint[20];
static uint32_t menu_hint_until;

/* Effects (FX button: page; KNOB 1-4: live). firmware/lsdj_fx.h, lsdj_fx_ui.h */
#include "lsdj_fx_ui.h"
static lsdj_fx_t fx;
static lsdj_fx_ui_t fxu;
static uint8_t fx_echo[LSDJ_FX_ECHO_LEN] __attribute__((section(".pool"),aligned(4)));
static void fx_hook(const int16_t *s,int32_t *l,int32_t *r){lsdj_fx_mix(&fx,s,l,r);}   /* audio interrupt */
/* FX button: a tap shows the channel pages; held, the master pages (and the
   channel strip: C4-F4 mute, lsdj_fx_hold_keys), from FX_HOLD_MS or the first knob / key. */
#define FX_HOLD_MS 250u
static uint8_t fx_held,fx_used;
static uint32_t fx_held_since,fx_hold_keys_prev;
/* Where LSDj keeps the channel under its cursor (kept on the CHAIN and PHRASE
   screens too) and its live tempo, per version, found with harness/find_cursor.c
   and harness/find_tempo.c; the SYNC setting (song RAM offset) and the armed /
   playing flags (4 WRAM bytes, one per channel) with harness/lsdj_drive.c.
   0 = unknown. */
static uint16_t cursor_addr,tempo_addr,sync_off,armed_addr,row_addr,live_addr,songrow_addr,chainpos_addr,songtab_off,
                period_addr,songtempo_off,screen_addr;
#define LSDJ_SYNC_MIDI 3u
#define LSDJ_SYNC_LSDJ 1u
#define LSDJ_SYNC_KEYBD 5u
static void cursor_detect(void){
    /* row: 4 phrase rows (0..15, one per channel), live: 1 in live mode, songrow: 4 song
       rows, chainpos: 4 places in the chain, songtab: the song table in cartridge RAM
       (harness/lsdj_drive.c rec / peek / sdump, find_chainpos.py) */
    /* period: the tempo period (16 bits), songtempo: the song's tempo in cartridge RAM */
    static const struct {const char *title;uint16_t cursor,tempo,sync,armed,row,live,songrow,chainpos,songtab,period,songtempo,screen;} known[]={
        {"LSDj-v9.3.9",0xC41D,0xC529,0x3FBD,0xC0E0,0xC16C,0xC528,0xC200,0xC17C,0x1290,0xC954,0x3FB4,0xC401}};
#ifdef CART_SLOT_CAPACITY
    /* Generic builds: the browser marks the slot with profile 1 only when the program
       banks' SHA-256 matches the verified LSDj 9.3.9 program (lsdj_slot.h byte 148), never
       by title. Every address below comes from that program, so all of them apply;
       unknown programs keep every helper off. */
    const int verified=cart_slot.cursor_addr==known[0].cursor;
#endif
    const char *t=gp_title();
    for(unsigned i=0;i<sizeof known/sizeof known[0];i++){
        unsigned k=0;while(known[i].title[k]&&t[k]==known[i].title[k])k++;
#ifdef CART_SLOT_CAPACITY
        if(i==0&&verified){
#else
        if(!known[i].title[k]&&!t[k]){
#endif
            cursor_addr=known[i].cursor;tempo_addr=known[i].tempo;sync_off=known[i].sync;armed_addr=known[i].armed;
            row_addr=known[i].row;live_addr=known[i].live;
            songrow_addr=known[i].songrow;chainpos_addr=known[i].chainpos;songtab_off=known[i].songtab;
            period_addr=known[i].period;songtempo_off=known[i].songtempo;screen_addr=known[i].screen;return;
        }
    }
}
/* MIDI clock sync (firmware/lsdj_midisync.h): USB MIDI clock and transport. */
#include "lsdj_midisync.h"
static lsdj_ms_t msync;
static uint32_t sync_btn;   /* START held by the sync this frame */
static int lsdj_armed(void){
    if(!armed_addr)return 0;
    for(unsigned c=0;c<4;c++)if(gp_wram_peek((uint16_t)(armed_addr+c)))return 1;
    return 0;
}
/* Tick timing: a tick is heard SYNC DELAY (the MIDI options: 100, 75 or 50 ms) after
   it arrived, always. The sound clock is the output's sample count, interpolated since
   the last audio interrupt; emulation runs at most GP_AUDIO_TARGET + one frame (2040
   samples, 46 ms) ahead of it, so a tick placed 50 ms or more later is never already past. */
static uint32_t sound_clock(void){
    uint32_t s,t,p;
    do{s=aud_seq;t=aud_at_ticks;p=aud_at_pos;}while((s&1u)||s!=aud_seq);
    uint32_t dt=fm1_ticks()-t;if(dt>288000u)dt=288000u;   /* at most two blocks (12 ms) */
    return p+dt*441u/240000u;                             /* 24 MHz ticks -> 44.1 kHz samples */
}
static uint32_t tick_target;
static void sync_link_send(uint8_t b){(void)b;gp_link_tick_at(tick_target);}
static int16_t midi_pc_row=-1;   /* a program change to cue (cue_frame takes it) */
static uint8_t midi_note_told;
static void midi_in(uint32_t pkt,uint32_t at_ms,uint32_t src){
    uint8_t st=(uint8_t)(pkt>>8),d1=(uint8_t)(pkt>>16);
    switch(lsdj_midi_route(&midi,pkt,src)){
    case LSDJ_MIDI_CLOCK:case LSDJ_MIDI_TRANSPORT:break;   /* below */
    case LSDJ_MIDI_NOTE:
        if(sync_off&&gp_save_data()[sync_off]!=LSDJ_SYNC_KEYBD&&!midi_note_told){
            midi_note_told=1;lsdj_ov_show(&ov,"MIDI NOTES: SET","LSDJ SYNC TO KEYBD",fm1_ms);
        }
        lsdj_keybd_note(d1,1);return;
    case LSDJ_MIDI_CC:(void)lsdj_midi_cc(&midi,&fx,pkt);return;
    case LSDJ_MIDI_PC:midi_pc_row=d1;return;
    default:return;
    }
    uint32_t age=fm1_ms-at_ms;if(age>50u)age=50u;   /* waited in the ring (ms; Bluetooth: since it was sent) */
    tick_target=sound_clock()-age*441u/10u+lsdj_midi_delay_samples(&midi);
    sync_midi=(uint8_t)(sync_off&&gp_save_data()[sync_off]==LSDJ_SYNC_MIDI);
    int said=lsdj_ms_byte(&msync,st,sync_midi,lsdj_armed(),sync_link_send);
    static const char *const msg[5][2]={{0,0},{"MIDI START",""},{"MIDI CONTINUE",""},{"MIDI STOP",""},
                                        {"MIDI START: SET","LSDJ SYNC TO MIDI"}};
    if(said)lsdj_ov_show(&ov,msg[said][0],msg[said][1],fm1_ms);
}
/* The effects pop-up: the page's four knobs as bars under LSDj's four channel
   columns (lsdj_overlay.h). hot = the knob just turned (amber), -1 none. While
   it shows, it is refilled every frame (cursor moves, synced times...). */
static lsdj_ov_bars_t bars;
static void fx_bars_fill(void){
    lsdj_fx_bars(&fx,&fxu,bars.hot,bars.title,bars.big,bars.label,bars.value,bars.caption,bars.level);
    lsdj_fx_bar_choices(&fx,&fxu,bars.nchoice,bars.pick,bars.choice);
    bars.amount_scale=1;
    bars.strip=fxu.held?(uint8_t)(0x80u|fx.mute):0u;
    { unsigned t=lsdj_fx_eff(&fxu);bars.chan=(int8_t)(t<4u?(int)t:-1); }   /* frame the channel the effects act on */
}
static void fx_bars_show(int hot){
    bars.hot=(int8_t)hot;fx_bars_fill();lsdj_ov_bars_keep(&bars,fm1_ms);
}
/* The effects setup into the save store (records 1..11), only what changed.
   The flash must be safe (emulator parked or stopped). Records written, or -1. */
/* the record of effects word i: words 0..10 in records 1..11, words 11..16 (alpha 66) in 17..22 */
#define FX_REC(i) ((i)<LSDJ_FX_WORDS_OLD?(i)+1u:(i)+6u)
static int fx_store(void){
    uint32_t w[LSDJ_FX_WORDS];lsdj_fx_pack(&fx,&fxu,w);int n=0;
    for(unsigned i=0;i<LSDJ_FX_WORDS;i++){
        unsigned r=FX_REC(i);
        if(((ftl.rec_has>>r)&1u)&&ftl.rec[r]==w[i])continue;
        if(ftl_put_record(&ftl,r,w[i]))return -1;
        n++;
    }
    return n;
}
static void fx_load(void){
    uint32_t w[LSDJ_FX_WORDS];unsigned n=LSDJ_FX_WORDS;
    for(unsigned i=0;i<LSDJ_FX_WORDS;i++){
        unsigned r=FX_REC(i);
        if((ftl.rec_has>>r)&1u)w[i]=ftl.rec[r];
        else if(i==10u)w[i]=0;   /* alphas 27-29 kept 10 words */
        else if(i>=LSDJ_FX_WORDS_OLD){n=LSDJ_FX_WORDS_OLD;break;}   /* saved before alpha 66: the new effects off */
        else return;
    }
    (void)lsdj_fx_unpack_n(&fx,&fxu,w,n);
}
/* Themes (firmware/lsdj_theme.h): the screen's four colours and the overlay's. */
#include "lsdj_theme.h"
typedef char lsdj_theme_count_ok[LSDJ_THEMES==LSDJ_SET_THEMES?1:-1];
static void settings_apply(void){
#if !LSDJ_BLE
    settings.bluetooth=0;   /* (this build has no Bluetooth: BLUETOOTH stays OFF) */
#endif
    static const uint32_t sel[3]={0,1u<<LSDJ_KEY_FS5,1u<<LSDJ_KEY_DS5};
    lsdj_input_config(&lsdj_in,settings.knobs_swapped,settings.presets_rev,settings.algo_rev,
                      settings.play_is_select,sel[settings.select_key%3u]);
    const lsdj_theme_t *th=lsdj_theme_of(settings.theme);
    lsdj_theme_overlay(th);gp_palette_colors(th->gb);
    lsdj_ov_ms=lsdj_set_popup_ms[settings.popup%LSDJ_SET_POPUPS];   /* POPUP TIME */
}
/* Everything the firmware shows, in LSDj's own style (firmware/lsdj_native.h):
   drawn into the Game Boy picture before it is scaled. native_draw is below. */
#include "lsdj_native.h"
static void native_draw(void);
/* Menu open: SELECT picks a setting, KNOB 1-4 change it;
   PRESETS / ALGORITHM show what they now do (to try the directions). */
static void menu_input(void){
    int32_t enc[FM1_NENC];
    for(unsigned e=0;e<FM1_NENC;e++)enc[e]=fm1_enc_take(e);
    int32_t st[4]={enc[2],enc[3],enc[4],enc[5]};
    if(lsdj_menu_knobs(&menu,&settings,&midi,st,enc[0])==1)settings_apply();
    if(enc[0]||st[0]||st[1]||st[2]||st[3])menu_hint_until=0;
    static const char *const knob_name[2]={"PRESETS: ","ALGO: "};
    static const int enc_of[2]={LSDJ_ENC_PRESETS,LSDJ_ENC_ALGORITHM};
    for(unsigned k=0;k<2;k++){
        int32_t d=enc[enc_of[k]];if(!d)continue;
        uint8_t b=lsdj_in.knob_out[k][d>0];
        const char *dir=b==GP_UP?"UP":b==GP_DOWN?"DOWN":b==GP_LEFT?"LEFT":"RIGHT";
        unsigned n=0;
        for(const char *c=knob_name[k];*c;c++)menu_hint[n++]=*c;
        for(const char *c=dir;*c;c++)menu_hint[n++]=*c;
        menu_hint[n]=0;menu_hint_until=(fm1_ms+1500u)|1u;
    }
    gp_buttons(sync_btn);
}
/* Show a message now, while the emulator (and the CPU1 video job) is stopped. */
static void ov_now(unsigned msg){
    lsdj_ov_say(&ov,msg,fm1_ms);
    if(!save_display_ok)return;
    while(!display_ready())fm1_wdt_feed();
    native_draw();video_scale_local(gp_pixels(),screen);display_frame();
    while(!display_ready())fm1_wdt_feed();
}
static void save_wait(uint32_t ticks){uint32_t t=fm1_ticks();while(fm1_ticks()-t<ticks)fm1_wdt_feed();}
static void save_blink(unsigned n,uint32_t ticks){
    for(unsigned i=0;i<n;i++){fm1_led_key(SAVE_BUTTON,0);save_wait(ticks);fm1_led_key(SAVE_BUTTON,1);save_wait(ticks);}
}
/* SAVE button. Nothing writes flash in the background: SAVE pauses LSDj (and
   the CPU1 video job), writes the changed 4 KiB blocks of cartridge RAM through
   the power-cut-safe store, and resumes. Only while LSDj is stopped (0.5 s of
   silent output); while it plays, SAVE is refused with 2 slow blinks. SAVE light
   off while writing; 5 fast blinks = flash error (the previous save stays). */
/* SAVE while LSDj plays: the save store writes one step (a 256-byte page, a
   block snapshot, the commit) per emulated frame, the emulator parked for each
   step (~1-2 ms), never an erase (those wait for a SAVE with the song stopped).
   Each block is snapshotted when its turn comes. */
static int play_save,settings_pending;
static void play_save_done(int ok){
    if(ok){   /* the effects setup with it */
        if(core1_park(960000u)){if(fx_store()<0)ok=0;core1_unpark();}else ok=0;
    }
    play_save=0;fm1_led_key(SAVE_BUTTON,1);
    if(ok)gp_save_clean();
    if(settings_pending){   /* settings changed while the save ran */
        settings_pending=0;video_hold();(void)settings_store();video_release();
    }
    lsdj_ov_say(&ov,ok?LSDJ_MSG_SAVED:LSDJ_MSG_SAVE_FAILED,fm1_ms);
    if(!ok)save_blink(5,1200000u);
}
static void play_save_step(void){
    if(!play_save)return;
    if(!core1_park(960000u))return;          /* 40 ms: try again on the next frame */
    int r=ftl_step(&ftl,gp_save_data(),0);
    core1_unpark();
    if(r<0)play_save_done(0);
    else if(!ftl_busy(&ftl))play_save_done(1);
}
static int play_save_start(void){
    uint32_t mask=ftl_changed(&ftl,gp_save_data());
    if(!mask&&!ftl.retry){   /* the song is saved; maybe the effects are not */
        int fr=-1;
        if(core1_park(960000u)){fr=fx_store();core1_unpark();}
        lsdj_ov_say(&ov,fr<0?LSDJ_MSG_SAVE_FAILED:fr?LSDJ_MSG_SAVED:LSDJ_MSG_NO_CHANGES,fm1_ms);
        return 1;
    }
    int rc=ftl_begin(&ftl,mask);
    if(rc==-2){ftl.retry=0;return 0;}         /* would need an erase: only with the song stopped */
    if(rc){lsdj_ov_say(&ov,LSDJ_MSG_SAVE_FAILED,fm1_ms);return 1;}
    play_save=1;fm1_led_key(SAVE_BUTTON,0);
    lsdj_ov_say(&ov,LSDJ_MSG_SAVING_PLAYING,fm1_ms);
    return 1;
}
static void lsdj_save_button(void){
    if(play_save)return;                      /* already saving */
    if(gp_silent_frames()<SILENT_FRAMES){
        if(!recovery_flash_ok){lsdj_ov_say(&ov,LSDJ_MSG_SAVE_FAILED,fm1_ms);save_blink(5,1200000u);return;}
        if(play_save_start())return;
        lsdj_ov_say(&ov,LSDJ_MSG_SAVE_PLAYING,fm1_ms);save_blink(2,7200000u);return;   /* flash full: stop first */
    }
    if(!recovery_flash_ok){lsdj_ov_say(&ov,LSDJ_MSG_SAVE_FAILED,fm1_ms);save_blink(5,1200000u);return;}
    fm1_led_key(SAVE_BUTTON,0);
    video_hold();   /* CPU1 parked, not stopped (multicore_video.h) */
    ov_now(LSDJ_MSG_SAVING);
    int rc=ftl_save_now(&ftl,gp_save_data());
    if(rc>=0){int fr=fx_store();if(fr<0)rc=-1;else if(rc==0&&fr>0)rc=1;}   /* and the effects */
    video_release();
    if(rc>=0)gp_save_clean();
    lsdj_ov_say(&ov,rc>0?LSDJ_MSG_SAVED:rc==0?LSDJ_MSG_NO_CHANGES:LSDJ_MSG_SAVE_FAILED,fm1_ms);
    fm1_led_key(SAVE_BUTTON,1);
    if(rc<0)save_blink(5,1200000u);
}
/* At boot, before LSDj's first frame (sound is silent here, so erases are
   fine): load the autosave (SAVE light blinks 3 times). Without one, import the
   previous format (v6..v10 compressed autosave, or the v5 A/B save; blinks
   twice) into a fresh autosave area, keeping the old copy until the new one is
   committed. */
static int songs_loaded;   /* shown once the game picture is up */
static void lsdj_save_boot(void){
    unsigned blinks=0;
    if(ftl_boot(&ftl,gp_save_data()))blinks=3;
    else if(recovery_flash_ok){
        uint8_t keep[FTL_N];memset(keep,0,sizeof keep);
        lst_region_t r={gp_save_data(),gp_save_size()};
        lst_entry_t e[LST_KINDS];lst_scan(e,NULL);
        if(e[LST_AUTO].valid&&lst_load(LST_AUTO,&r,1,LSDJ_STATE_BUILD,(uint8_t*)screen,LSDJ_SAVE_BUF,NULL)==LST_OK){
            for(uint32_t k=0;k<e[LST_AUTO].nsect;k++)keep[e[LST_AUTO].sector+k]=1;
            blinks=2;
        }else if(lsdj_save_load(gp_save_data(),gp_save_size())){
            for(uint32_t k=0;k<FTL_N&&k<0x42u;k++)keep[k]=1;   /* v5 slots: 0x97000..0xD9000 */
            blinks=2;
        }
        if(ftl_format(&ftl,gp_save_data(),keep))blinks=0;
    }
    gp_save_clean();   /* what was just loaded is saved */
    for(unsigned i=0;i<blinks;i++){fm1_led_key(SAVE_BUTTON,1);save_wait(4800000u);fm1_led_key(SAVE_BUTTON,0);save_wait(4800000u);}
    if(blinks)songs_loaded=1;
}
/* Buttons, keys and knobs, once per emulated frame (knob taps are timed in
   frames): src/fm1_lsdj_input.h. PLAY=Start, REC=Select. */
/* PLAY's light on the beat (firmware/lsdj_lights.h): song mode flashes, live mode dips. */
#include "lsdj_lights.h"
/* Button lights as last written (-1 unknown): reset after the boot sweep. */
static int play_shown=-1,seq_shown=-1,arp_shown=-1;
static void play_light(void){
    int on=1;
    if(armed_addr&&row_addr){
        uint8_t playing[4],rows[4];
        for(unsigned c=0;c<4;c++){playing[c]=gp_wram_peek((uint16_t)(armed_addr+c));rows[c]=gp_wram_peek((uint16_t)(row_addr+c));}
        on=lsdj_play_light(playing,rows,live_addr&&gp_wram_peek(live_addr));
        piano_soft=lsdj_in.mode==LSDJ_KEYS_BUTTONS?lsdj_dpad_soft(playing,rows):0u;   /* D-pad keys take turns */
    }
    if(!led_anim&&on!=play_shown){play_shown=on;fm1_led_key(LSDJ_BTN_PLAY,on);}
    int live=live_addr&&gp_wram_peek(live_addr);   /* SEQ lit in live mode */
    if(!led_anim&&live!=seq_shown){seq_shown=live;fm1_led_key(SEQ_BUTTON,live);}
}
/* ARP: song row cue (firmware/lsdj_cue.h). */
#include "lsdj_cue.h"
static lsdj_cue_t cue;
static void cue_lsdj_state(uint8_t playing[4],uint8_t rows[4]){
    for(unsigned c=0;c<4;c++){playing[c]=gp_wram_peek((uint16_t)(armed_addr+c));rows[c]=gp_wram_peek((uint16_t)(songrow_addr+c));}
}
static void cue_button(int down){
    uint8_t playing[4],rows[4];cue_lsdj_state(playing,rows);
    if(down){lsdj_cue_open(&cue,playing,rows);bars.until_ms=0;return;}
    if(lsdj_cue_release(&cue)){
        static const char hex[]="0123456789ABCDEF";
        static char l1[20]="CUE ROW 00";l1[8]=hex[cue.row>>4];l1[9]=hex[cue.row&15u];
        lsdj_ov_show(&ov,l1,lsdj_cue_first(playing)<0?"PLAYS WHEN STARTED":"AT THE NEXT BAR",fm1_ms);
    }
    bars.until_ms=0;
}
/* EDIT: tempo (firmware/lsdj_tempo.h): the period LSDj counts with, its tempo
   setting and the song's tempo, all at once. */
#include "lsdj_tempo.h"
static lsdj_tempo_t tempo;
static void tempo_apply(unsigned bpm){
    uint16_t p=lsdj_tempo_period(bpm);uint8_t b=lsdj_tempo_byte(bpm);
    uint16_t a[3]={period_addr,(uint16_t)(period_addr+1u),tempo_addr};uint8_t v[3]={(uint8_t)p,(uint8_t)(p>>8),b};
    if(gp_wram_poke_many(a,v,3))gp_save_data()[songtempo_off]=b;
    lsdj_fx_set_bpm(&fx,bpm);
}
static void tempo_button(int down){
    if(!period_addr){if(down)lsdj_ov_show(&ov,"EDIT: TEMPO","NEEDS LSDJ 9.3.9",fm1_ms);return;}
    if(!down){lsdj_tempo_release(&tempo);bars.until_ms=0;return;}
    unsigned tap=lsdj_tempo_press(&tempo,lsdj_tempo_of_byte(gp_wram_peek(tempo_addr)),fm1_ms);
    if(tap)tempo_apply(tap);
    bars.until_ms=0;
}
static void cue_frame(void){   /* once per emulated frame: the writes, ARP's light */
    if(!songrow_addr)return;
    if(midi_pc_row>=0){   /* MIDI program change n: cue song row n (PC CUE) */
        static const char hx[]="0123456789ABCDEF";char l[20]="CUE ROW ";
        cue.row=(uint8_t)midi_pc_row;cue.pending=1;cue.waiting=0;
        l[8]=hx[cue.row>>4];l[9]=hx[cue.row&15u];l[10]=0;lsdj_ov_show(&ov,"MIDI PROGRAM CHANGE",l,fm1_ms);
        midi_pc_row=-1;
    }
    uint8_t playing[4],rows[4];cue_lsdj_state(playing,rows);
    uint16_t a[8];uint8_t v[8];
    unsigned n=lsdj_cue_frame(&cue,playing,rows,songrow_addr,chainpos_addr,a,v);
    if(n&&!gp_wram_poke_many(a,v,n))cue.pending=1;   /* queue full: next frame */
    int l=lsdj_cue_lit(&cue);
    if(!led_anim&&l!=arp_shown){arp_shown=l;fm1_led_key(ARP_BUTTON,l);}
}
/* SEQ: LSDj's live / song mode from any screen, without touching the player's
   controls: LSDj's mode flag is written (the same switch as its SELECT+LEFT on the
   SONG screen, also while playing: harness/live_poke_check.sh). LSDj only redraws
   its mode display (the S / L in the screen map, the SONG / LIVE title) when the
   screen changes, so until then the firmware covers it with the right one
   (lsdj_ov_mode_cover). */
static uint8_t seq_cover;      /* 0 none, else LSDj's display is stale: 1 song, 2 live */
static uint8_t seq_poked;      /* SEQ wrote the flag; LSDj has not redrawn since */
static uint8_t seq_screen=0xFF;
static void seq_frame(void){   /* once per emulated frame */
    if(!live_addr||!screen_addr){seq_cover=0;return;}
    uint8_t scr=gp_wram_peek(screen_addr),live=gp_wram_peek(live_addr)!=0;
    if(scr!=seq_screen){seq_screen=scr;seq_poked=0;}   /* a new screen: LSDj drew it with the mode as it is */
    seq_cover=seq_poked?(uint8_t)(live?2u:1u):0u;
}
static void seq_button(void){
    if(!live_addr||!screen_addr){lsdj_ov_show(&ov,"SEQ: LIVE/SONG","NEEDS LSDJ 9.3.9",fm1_ms);return;}
    int live=!gp_wram_peek(live_addr);
    gp_wram_poke(live_addr,(uint8_t)live);seq_poked=1;
    lsdj_ov_show(&ov,live?"LIVE MODE":"SONG MODE",live?"SEQ: BACK TO SONG":"SEQ: LIVE MODE",fm1_ms);
}
/* The mode cover (SEQ), the effects / cue / tempo panel, the settings list and
   messages, in that order, into the picture: CPU0, while CPU1 waits for it to be
   taken (or the emulator on CPU0 is between frames, or stopped). */
static void native_draw(void){
    lsdj_nat_t n={gp_pixels(),gp_vram(),gp_lcdc(),gp_bgp(),gp_shades()};
    lsdj_nat_mode(&n,seq_cover,seq_screen==4u);
    /* the panel, with the knobs' names on LSDj's bottom two rows: the effects
       page's, or the cue / tempo panel's own while ARP / EDIT is held. Only
       while the panel shows: otherwise LSDj's screen is left whole. */
    int panel=lsdj_ov_bars_active(&bars,fm1_ms),own=panel&&(cue.held||tempo.held);
    if(panel){
        char names[4][7];
        if(own)for(unsigned k=0;k<4u;k++){unsigned j=0;for(;j<6u&&bars.label[k][j];j++)names[k][j]=bars.label[k][j];names[k][j]=0;}
        else lsdj_fx_names(&fxu,names);
        lsdj_nat_bars(&n,&bars,lsdj_theme_of(settings.theme)->accent);
        if(!own)lsdj_nat_pagemap(&n,lsdj_fx_group_letters[fxu.held?1:0],lsdj_fx_pos(&fxu));   /* the pages of the group, in the screen map */
        lsdj_nat_names(&n,(const char(*)[7])names,bars.hot);
    }else bars.until_ms=0;
    if(menu.open){
        int hint=menu_hint_until&&(int32_t)(menu_hint_until-fm1_ms)>0;
        char lines[LSDJ_MENU_LINES][20];
        unsigned cnt=lsdj_menu_lines(&menu,&settings,&midi,hint?menu_hint:0,lines);   /* the open page */
        lsdj_nat_list(&n,(const char(*)[20])lines,cnt,(int)menu.cursor);
    }
    if(lsdj_ov_active(&ov,fm1_ms))lsdj_nat_message(&n,ov.line[0],ov.line[1]);
    else ov.until_ms=0;
}
/* The boot sweep (Felucca's power-on light animation, started when the startup
   screen ends: LSDj is ready) owns the LEDs while it runs; then every light of
   ours is written again. Returns 1 while it runs. */
static int leds_busy(void){
    static int was;
    int on=fm1_led_anim_on();
    if(was&&!on){lights_valid=0;lights_held_prev=UINT32_MAX;play_shown=seq_shown=arp_shown=-1;fm1_led_key(LSDJ_BTN_REC,1);}
    was=on;
    return on;
}
/* The web page's link (firmware/lsdj_link.h): songs, settings, effects, themes
   and live data over USB-MIDI. Never firmware. Flash writes park CPU1 first, as
   SAVE does. */
#include "lsdj_link.h"
static lsdj_link_t link_state;
static uint8_t *lk_save(void){return gp_save_data();}
static uint32_t lk_size(void){return gp_save_size();}
static void lk_pause(int on){gp_pause((uint32_t)on);}
static int lk_store(void){
    if(!recovery_flash_ok||play_save)return -1;
    video_hold();int rc=ftl_save_now(&ftl,gp_save_data());video_release();
    if(rc>=0)gp_save_clean();
    return rc<0?-1:0;
}
static int lk_load(void){return ftl_boot(&ftl,gp_save_data())?0:-1;}
static void lk_reset(void){gp_reset();}
static const char *lk_firmware(void){return FELUCCA_ID;}
static const char *lk_title(void){return gp_title();}
static int lk_live_known(void){return cursor_addr!=0;}
static uint32_t lk_settings_get(void){return lsdj_set_pack(&settings);}
static int lk_settings_set(uint32_t v){
    lsdj_settings_t t;if(!lsdj_set_unpack(v,&t))return 0;
    settings=t;settings_apply();return 1;
}
static void lk_fx_get(uint32_t w[LSDJ_LINK_FX_WORDS]){lsdj_fx_pack(&fx,&fxu,w);}
/* Older clients retain effects they cannot expose; saved page numbers migrate. */
static int lk_fx_set(const uint32_t *w,unsigned n){
    return lsdj_fx_set_words(&fx,&fxu,w,n);
}
typedef char lk_fx_words_ok[LSDJ_LINK_FX_WORDS==LSDJ_FX_WORDS&&LSDJ_LINK_FX_WORDS_OLD==LSDJ_FX_WORDS_OLD?1:-1];
static void lk_theme_get(uint16_t c[8]){lsdj_theme_to8(&lsdj_theme_custom,c);}
static void lk_theme_set(const uint16_t c[8]){lsdj_theme_from8(&lsdj_theme_custom,c);if(settings.theme==LSDJ_THEME_CUSTOM)settings_apply();}
static int theme_store(void){   /* the custom theme: records 12..15, only what changed */
    uint16_t c[8];lsdj_theme_to8(&lsdj_theme_custom,c);
    for(unsigned i=0;i<4u;i++){
        uint32_t w=(uint32_t)c[2*i]|(uint32_t)c[2*i+1]<<16;unsigned r=12u+i;
        if(((ftl.rec_has>>r)&1u)&&ftl.rec[r]==w)continue;
        if(ftl_put_record(&ftl,r,w))return -1;
    }
    return 0;
}
static void theme_load(void){
    uint16_t c[8];
    for(unsigned i=0;i<4u;i++){unsigned r=12u+i;if(!((ftl.rec_has>>r)&1u))return;c[2*i]=(uint16_t)ftl.rec[r];c[2*i+1]=(uint16_t)(ftl.rec[r]>>16);}
    lsdj_theme_from8(&lsdj_theme_custom,c);
}
static int lk_store_config(void){
    if(!recovery_flash_ok||play_save)return -1;
    video_hold();
    int rc=settings_store();
    if(!rc&&fx_store()<0)rc=-1;
    if(!rc)rc=theme_store();
    video_release();
    return rc?-1:0;
}
static void lk_live(lsdj_link_live_t *l){
    l->page=(uint8_t)lsdj_fx_cur(&fxu);l->cursor_ch=fxu.cursor_ch;l->bpm=fx.bpm;l->mute=fx.mute;l->freeze=fx.freeze;l->playing=0;
    if(armed_addr)for(unsigned c=0;c<4;c++)if(gp_wram_peek((uint16_t)(armed_addr+c)))l->playing|=(uint8_t)(1u<<c);
}
#if LSDJ_BLE
#include "lsdj_ble.h"
#define LK_BT_STAT lsdj_ble_stat
#else
#define LK_BT_STAT 0
#endif
static uint32_t lk_midi_get(void){return lsdj_midi_pack(&midi);}
static int lk_midi_set(uint32_t v){lsdj_midi_t t;if(!lsdj_midi_unpack(v,&t))return 0;midi=t;return 1;}
static const volatile uint8_t *lk_cartridge(uint32_t *len){
#ifdef CART_SLOT_CAPACITY
    *len=LSDJ_SLOT_HDR+cart_slot.stored_banks*LSDJ_SLOT_BANK;
    if(cart_slot_valid&&cart_slot.stored_banks&&*len<=CART_SLOT_CAPACITY)return cartridge;
#else
    (void)len;
#endif
    return 0;
}
static int lk_mute_set(uint8_t m,uint8_t f){   /* the web page's channel buttons and freeze */
    if(m>15u||f>1u)return 0;
    fx.mute=m;fx.freeze=f;lsdj_fx_update(&fx);return 1;
}
static const lsdj_link_io_t link_io={lk_save,lk_size,lk_pause,lk_store,lk_load,lk_reset,lk_firmware,lk_title,lk_live_known,
    lk_settings_get,lk_settings_set,lk_fx_get,lk_fx_set,lk_theme_get,lk_theme_set,lk_store_config,lk_live,LK_BT_STAT,
    lk_midi_get,lk_midi_set,lk_cartridge,lk_mute_set};
static int app_sysex_frame(const uint8_t *frame,uint32_t size){
    if(!lsdj_link_match(frame,size))return 0;
    static uint8_t reply[LSDJ_LINK_REPLY_MAX];
    ota_wire_send(reply,lsdj_link_handle(&link_state,&link_io,frame,size,fm1_ms,reply));
    return 1;
}
#if LSDJ_BLE
/* Bluetooth MIDI (firmware/lsdj_ble.c, tools/build.py --ble). BLUETOOTH = DEVICE: the
   FM-1 is a BLE-MIDI device. Clock and transport join USB / TRS (midi_in, timed by the
   sender); the web page's link answers over Bluetooth as over USB.
   This unit's radio calibration (the stock firmware's config sector, 0xE8000) is read
   once at boot, before the emulator runs on CPU1, so the radio never reads flash later. */
#include "../experiments/bluetooth/rf_calibration.c"   /* GameDude's validated reader */
static np_rf_calibration rf_cal;
static int rf_ok;
static int rf_read(void *ctx,uint32_t off,uint8_t *p,uint32_t n){(void)ctx;fm1_wdt_feed();return ota_fread(off,p,n);}
static void ble_rf_boot(void){rf_ok=np_rf_load_fm1(rf_read,0,&rf_cal)==NP_RF_OK;}
int core_rf_trim(uint8_t trim[68],uint8_t xosc[2],uint8_t pa[7]){
    if(!rf_ok)return 0;
    for(unsigned i=0;i<68u;i++)trim[i]=rf_cal.trim[i];
    xosc[0]=rf_cal.xosc[0];xosc[1]=rf_cal.xosc[1];
    for(unsigned i=0;i<7u;i++)pa[i]=rf_cal.pa[i];
    return 1;
}
uint32_t core_ms(void){return fm1_ms;}
void core_wdt_feed(void){fm1_wdt_feed();}
void core_reboot(void){fm1_reboot();}
void core_audio_stop(void){audio_stop();}
void core_irq_attach(uint32_t n,void(*h)(void),uint32_t prio){if(n<128u)fm1_irq_attach(n,h,prio);}
void core_irq_mask(uint32_t n){if(n<128u)fm1_irq_mask(n);}
void core_irq_unmask(uint32_t n){if(n<128u)FM1_ICFG(n)|=1u<<((n&7u)*4u);}
uint32_t core_irq_is_masked(uint32_t n){return n<128u?!(FM1_ICFG(n)&(1u<<((n&7u)*4u))):1u;}
void core_ble_midi(const uint8_t *m,uint32_t n,uint32_t sent_ms){   /* as a USB-MIDI packet */
    if(n==1u&&m[0]>=0xF8u)midi_in(0x0Fu|(uint32_t)m[0]<<8,sent_ms,LSDJ_MIDI_SRC_BT);
    else if(n>=2u&&m[0]>=0x80u&&m[0]<0xF0u)
        midi_in((uint32_t)(m[0]>>4)|(uint32_t)m[0]<<8|(uint32_t)m[1]<<16|(uint32_t)(n>2u?m[2]:0u)<<24,sent_ms,LSDJ_MIDI_SRC_BT);
}
void core_ble_sysex(const uint8_t *m,uint32_t n){
    if(!lsdj_link_match(m,n))return;
    static uint8_t reply[LSDJ_LINK_REPLY_MAX];
    uint32_t k=lsdj_link_handle(&link_state,&link_io,m,n,fm1_ms,reply);
    if(k)(void)lsdj_ble_send(reply,k);
}
/* The address: random static, the same every start (from this unit's calibration),
   so phones and computers find the FM-1 they paired with. */
static void ble_mac(uint8_t mac[6]){
    uint32_t h=2166136261u;
    for(unsigned i=0;i<68u;i++){h^=rf_cal.trim[i];h*=16777619u;}
    for(unsigned i=0;i<6u;i++){h^=h>>13;h*=0x5bd1e995u;mac[i]=(uint8_t)(h>>24);}
    mac[0]|=0xC0u;mac[5]|=0xC0u;   /* (the stack keeps the most significant byte at either end) */
}
/* Start, and wait (up to 3 s) until the stack is up. The emulator is parked
   meanwhile (as SAVE does): the radio starting while CPU1 runs from flash is untried. */
static void ble_start_wait(void){
    uint8_t mac[6];ble_mac(mac);
    video_hold();
    lsdj_ble_start(mac);
    uint32_t t0=fm1_ms;
    while(lsdj_ble_state()==LSDJ_BLE_STARTING&&fm1_ms-t0<3000u){fm1_wdt_feed();lsdj_ble_service();}
    video_release();
}
static uint32_t ble_shown=LSDJ_BLE_OFF;
static uint8_t ble_off_told;
static uint8_t ble_skipped;   /* the last start failed (before a reset): wait for the setting to go off and on */
static void ble_frame(void){
    if(ble_skipped&&!settings.bluetooth)ble_skipped=0;
    if(settings.bluetooth==1u&&!ble_skipped&&lsdj_ble_state()==LSDJ_BLE_OFF){lsdj_ov_show(&ov,"BLUETOOTH","STARTING",fm1_ms);ble_start_wait();}
    lsdj_ble_service();
    uint32_t s=lsdj_ble_state();
    if(!settings.bluetooth&&s!=LSDJ_BLE_OFF&&!ble_off_told){ble_off_told=1;lsdj_ov_show(&ov,"BLUETOOTH OFF","AFTER A RESTART",fm1_ms);}
    if(settings.bluetooth)ble_off_told=0;
    if(s==ble_shown)return;
    static const char *const why[]={"","NO RADIO TRIM","START TIMED OUT","NO TASK","ADDRESS REFUSED","ADVERTISING REFUSED","LINK REFUSED"};
    uint32_t e=lsdj_ble_error();
    if(s==LSDJ_BLE_CONNECTED)lsdj_ov_show(&ov,"BLUETOOTH","CONNECTED",fm1_ms);
    else if(s==LSDJ_BLE_ADVERTISING)lsdj_ov_show(&ov,"BLUETOOTH",ble_shown==LSDJ_BLE_CONNECTED?"DISCONNECTED":"LSDJ BURRITO READY",fm1_ms);
    else if(s==LSDJ_BLE_FAILED)lsdj_ov_show(&ov,"BLUETOOTH FAILED",e<sizeof why/sizeof why[0]?why[e]:"",fm1_ms);
    if(s!=LSDJ_BLE_READY)ble_shown=s;   /* (READY: on the way to advertising) */
}
#endif
static void fm1_input_frame(void){
    led_anim=(uint8_t)leds_busy();
    play_light();cue_frame();seq_frame();
    /* MIDI sync: START presses for the transport (lsdj_midisync.h), whatever is open */
    sync_midi=(uint8_t)(sync_off&&gp_save_data()[sync_off]==LSDJ_SYNC_MIDI);
    sync_btn=lsdj_ms_frame(&msync,lsdj_armed())?GP_START:0u;
    if(menu.open){menu_input();return;}
    /* Once per emulated frame: knob taps are timed in frames.
       Keys, PLAY=Start, REC=Select: src/fm1_lsdj_input.h. */
    int32_t enc[FM1_NENC];
    for(unsigned e=0;e<FM1_NENC;e++)enc[e]=fm1_enc_take(e);
    if(cursor_addr){uint8_t c=gp_wram_peek(cursor_addr);fxu.cursor_ch=c<4u?c:0xFFu;}
    if(tempo_addr){uint8_t b=gp_wram_peek(tempo_addr);lsdj_fx_set_bpm(&fx,b<40u?b+256u:b);}   /* LSDj: 40..295 */
    if(tempo.held){   /* EDIT held: K1 / K2 set the tempo, not effects */
        unsigned b=lsdj_tempo_turn(&tempo,(int)enc[2],(int)enc[3]);enc[2]=enc[3]=enc[4]=enc[5]=0;
        if(b)tempo_apply(b);
        lsdj_tempo_bars(&tempo,sync_midi,bars.title,bars.big,bars.label,bars.value,bars.caption,bars.level);
        for(unsigned k=0;k<4u;k++)bars.nchoice[k]=0;
        bars.amount_scale=0;
        bars.hot=-1;bars.chan=-1;lsdj_ov_bars_keep(&bars,fm1_ms);
    }
    if(cue.held){   /* ARP held: K1 / K2 choose the song row to cue, not effects */
        lsdj_cue_turn(&cue,(int)enc[2],(int)enc[3]);enc[2]=enc[3]=enc[4]=enc[5]=0;
        lsdj_cue_bars(&cue,gp_save_data()+songtab_off,bars.title,bars.big,bars.label,bars.value,bars.caption,bars.level);
        for(unsigned k=0;k<4u;k++)bars.nchoice[k]=0;
        bars.amount_scale=0;
        bars.hot=-1;bars.chan=-1;lsdj_ov_bars_keep(&bars,fm1_ms);
    }
    if(fx_held&&!fxu.held&&(fm1_ms-fx_held_since>=FX_HOLD_MS||enc[0]||enc[2]||enc[3]||enc[4]||enc[5]||(key_lo&~fx_hold_keys_prev)))
        {fxu.held=1;bars.hot=-1;}   /* held (or used): the master pages, the hint until a knob turns */
    {   /* KNOB 1-4 (encoders 2-5): the page's four effects */
        int32_t st[4]={enc[2],enc[3],enc[4],enc[5]};
        int k=lsdj_fx_knobs(&fx,&fxu,st);
        if(k>=0){fx_bars_show(k);if(fx_held)fx_used=1;}   /* FX + knob is not a tap */
    }
    if(enc[0]){   /* SELECT knob (encoder 0): effects pages */
        lsdj_fx_page_turn(&fxu,(int)enc[0]);fx_bars_show(-1);
        if(fx_held)fx_used=1;
    }
    if(!cue.held&&!tempo.held&&lsdj_ov_bars_active(&bars,fm1_ms))fx_bars_fill();
    if(fx_held){   /* FX held: the master pages and the channel strip; LSDj gets no input meanwhile */
        uint32_t down=key_lo&~fx_hold_keys_prev;fx_hold_keys_prev=key_lo;
        if(down&&lsdj_fx_hold_keys(&fx,down))fx_used=1;
        if(fxu.held){fx_bars_fill();lsdj_ov_bars_keep(&bars,fm1_ms);}
        piano_set(lsdj_fx_hold_lights(&fx));
        gp_buttons(sync_btn);return;
    }
    uint32_t panel_now=fm1_in.buttons,panel_was=lsdj_in.prev_buttons;
    uint8_t mode_was=lsdj_in.mode;int8_t oct_was=lsdj_in.octave;
    input_buttons=lsdj_input_frame(&lsdj_in,key_lo,panel_now,enc);
    /* On-screen messages for what the FM-1's own buttons just did. */
    if(lsdj_in.mode!=mode_was)
        lsdj_ov_say(&ov,lsdj_in.mode==LSDJ_KEYS_NOTES?LSDJ_MSG_NOTE_MODE:LSDJ_MSG_BUTTON_MODE,fm1_ms);
    else if(lsdj_in.octave!=oct_was)
        lsdj_ov_octave(&ov,lsdj_in.octave<oct_was,lsdj_in.octave,fm1_ms);
    else if(lsdj_in.mode==LSDJ_KEYS_BUTTONS&&(panel_now&~panel_was&3u))
        lsdj_ov_say(&ov,LSDJ_MSG_OCT_BUTTONS,fm1_ms);
    /* Note mode: every key light off. */
    piano_lights(lsdj_in.mode==LSDJ_KEYS_NOTES?0xFFu:input_buttons);
    input_seen|=input_buttons;gp_buttons(input_buttons|sync_btn);
}
#endif
static void boot_splash(int display_ok){
#if !GB_SMK37
    const lsdj_theme_t *th=lsdj_theme_of(settings.theme);   /* loaded before the splash: it takes the theme's colours */
    const lsdj_splash_colors_t splash_col=lsdj_splash_of_theme(th->gb,th->accent);
#endif
    uint32_t start=fm1_ticks(),landed=0,landing_frame=0,landed_at=0,next_draw=0;
    for(;;){
        uint32_t ms=(fm1_ticks()-start)/24000u;
        fm1_wdt_feed();
#if !GB_SMK37
        master_poll();
#endif
        int ready=!display_ok||display_ready();
        /* Start the chime after the landing frame has finished its LCD transfer. */
        if(!landed&&ready&&(landing_frame||(!display_ok&&ms>=GD_SPLASH_LAND_MS))){
            ding_position=0;landed=1;landed_at=ms;
        }
        if(landed&&ms>=GD_SPLASH_END_MS&&ms-landed_at>=750u)break;
        if(display_ok&&ready&&ms>=next_draw){
#if GB_SMK37
            gd_splash_draw(screen,ms);
#else
            lsdj_splash_draw(screen,ms,gd_splash_y(ms)-84,FELUCCA_ID,&splash_col);
#endif
            display_frame();next_draw=ms+33u;
            if(ms>=GD_SPLASH_LAND_MS)landing_frame=1;
        }
    }
    while(display_ok&&!display_ready())fm1_wdt_feed();
}
/* MIDI clock out (CLOCK OUT): LSDj leading (SYNC = LSDJ, playing) sends one link byte a
   tick; each goes out as a MIDI clock (F8) when that moment of the sound is heard, Start
   before the first, Stop when they stop (lsdj_midi.h). */
static lsdj_mco_t mco;
static void midi_out_byte(uint8_t b){
    if(midi.clock_out==LSDJ_OUT_USB||midi.clock_out==LSDJ_OUT_BOTH){uint32_t f=irq_save();midi_out_event(0x0Fu|(uint32_t)b<<8);irq_restore(f);}
#if LSDJ_BLE
    if(midi.clock_out==LSDJ_OUT_BT||midi.clock_out==LSDJ_OUT_BOTH)(void)lsdj_ble_send(&b,1);
#endif
}
static void midi_clock_out(void){
    uint32_t pos,now=sound_clock();
    while(gp_master_tick_peek(&pos)&&(int32_t)(now-pos)>=0){
        gp_master_tick_drop();
        if(!midi.clock_out)continue;
        uint8_t b[2];unsigned n=lsdj_mco_tick(&mco,fm1_ms,b);
        for(unsigned i=0;i<n;i++)midi_out_byte(b[i]);
    }
    if(lsdj_mco_poll(&mco,fm1_ms)&&midi.clock_out)midi_out_byte(0xFCu);
    if(!midi.clock_out)mco.running=0;
    static uint8_t told;   /* CLOCK OUT on, LSDj playing, but not leading: say how, once */
    if(midi.clock_out&&!told&&sync_off&&lsdj_armed()&&gp_save_data()[sync_off]!=LSDJ_SYNC_LSDJ){
        told=1;lsdj_ov_show(&ov,"MIDI CLOCK OUT: SET","LSDJ SYNC TO LSDJ",fm1_ms);
    }
}
static void fm1_fault(const fm1_crash_t *c){(void)c;video_retire();audio_stop();fm1_reboot();}
extern uint32_t _data_start[],_data_end[],_data_load[],_bss_start[],_bss_end[];
extern uint32_t _pool_start[],_pool_end[],_rt_start[],_rt_end[],_rt_load[];
extern uint32_t _dsp_start[],_dsp_end[],_dsp_load[];
static void copy(uint32_t*d,uint32_t*end,const uint32_t*s){while(d<end)*d++=*s++;}
static void zero(uint32_t*d,uint32_t*end){while(d<end)*d++=0;}
void gb_cstart(const uint32_t *parameters){
    fm1_core1_stop();fm1_p33_timeouts=0;fm1_reset_reason();
    uint32_t cold=fm1_boot.p3_rst&1u;
    fm1_wdt_arm(0x0d);
    if(smk37_bootguard_begin(&gb_bootguard,cold))fm1_enter_uboot();
#if GB_SMK37
    smk37_boot_capture(parameters);
#else
    (void)parameters;
#endif
    zero(_bss_start,_bss_end);zero(_pool_start,_pool_end);
    copy(_data_start,_data_end,_data_load);copy(_rt_start,_rt_end,_rt_load);
    copy(_dsp_start,_dsp_end,_dsp_load);__asm__ volatile("csync":::"memory");
#if GB_SMK37
    smk37_time_init();
#else
    fm1_time_init();
#endif
    fm1_irq_init();fm1_mailbox_clear();
    int display_ok=display_init();
#if !GB_SMK37
    save_display_ok=display_ok;
#endif
    /* Volatile header read: the public template's ROM slot is patched in the
       browser, so the compiler must not fold its original header bytes. */
    const volatile uint8_t *cart_header=cartridge;
#if defined(CART_SLOT_CAPACITY)
    int slot_error=lsdj_slot_parse(cart_header,CART_SLOT_CAPACITY,&cart_slot);
    cart_slot_valid=(uint8_t)(slot_error==LSDJ_SLOT_OK);
    if(!slot_error)gp_set_bank_map(cart_slot.bank_map,cart_slot.logical_banks);
    if(slot_error||gp_load(cartridge+LSDJ_SLOT_HDR,cart_slot.logical_banks*LSDJ_SLOT_BANK)){
#elif defined(CART_SPARSE_BANKS)
    /* Sparse cartridge (tools/build.py): only code + chosen kit banks are stored. */
    (void)cart_header;gp_set_bank_map(cart_bank_map,CART_SPARSE_BANKS);
    uint32_t cart_size=CART_SPARSE_BANKS*16384u;
    if(gp_load(cartridge,cart_size)) {
#else
    uint32_t cart_size=cart_header[0x148]<=4?(32768u<<cart_header[0x148]):0;
    if(!cart_size||cart_size>sizeof cartridge||gp_load(cartridge,cart_size)) {
#endif
        /* Invalid embedded cartridge: allow bootguard/watchdog recovery. */
        for(;;);
    }
    input_init();
#if !GB_SMK37
    master_boot();
#endif
    splash_active=1;ding_position=GD_DING_FRAMES;
    audio_start();fm1_irq_enable_all();
#if !GB_SMK37
    /* Before LSDj's first frame (after the splash): it reads its songs from
       cartridge RAM at boot. Needs the panel scan running for the light blinks. */
    lsdj_save_boot();
    theme_load();   /* the custom theme, before the settings choose a theme */
    lsdj_set_unpack(ftl.has_settings?ftl.settings:0u,&settings);settings_apply();
    lsdj_midi_unpack(((ftl.rec_has>>MIDI_REC)&1u)?ftl.rec[MIDI_REC]:0u,&midi);   /* (defaults if none) */
#if LSDJ_BLE
    ble_rf_boot();   /* this unit's radio calibration, while nothing else reads flash */
#endif
    lsdj_fx_init(&fx,fx_echo);lsdj_fx_ui_init(&fxu);fx_load();gp_set_fx(fx_hook);cursor_detect();
    fm1_led_key(SAVE_BUTTON,1);
#endif
    boot_splash(display_ok);
    fm1_led_anim_start(0);   /* boot done: the light sweep (leds_busy) */
    for(unsigned i=0;i<4;i++)gp_frame();
    splash_active=0;
    recovery_init();
#if LSDJ_BLE
    if(settings.bluetooth==1u){   /* before the emulator moves to CPU1: no parking */
        if(lsdj_ble_prev_ok())ble_start_wait();
        else{ble_skipped=1;lsdj_ov_show(&ov,"BLUETOOTH SKIPPED","LAST START FAILED",fm1_ms);}
    }
#endif
#if !GB_SMK37
    core1_emu=1;   /* the emulator moves to CPU1; if CPU1 does not start, CPU0 keeps it */
#endif
    video_start(cold);
    uint32_t drawn=0,last=fm1_ticks(),elapsed=0,seconds=0;
#if !GB_SMK37
    if(songs_loaded)lsdj_ov_say(&ov,LSDJ_MSG_LOADED,fm1_ms);
    cpu.start=fm1_ticks();
#endif
    for(;;){
        uint32_t now=fm1_ticks();elapsed+=now-last;last=now;
        int worked=0;   /* CPU meter: a pass that does nothing counts as idle */
        if(elapsed>=24000000){elapsed-=24000000;seconds++;}
        fm1_wdt_feed();
#if !GB_SMK37
        master_poll();
#endif
        recovery_service();
        lsdj_link_poll(&link_state,&link_io,fm1_ms);   /* an import whose page went away */
#if LSDJ_BLE
        ble_frame();
#endif
#if !GB_SMK37
        midi_clock_out();
#endif
#if !GB_SMK37
        {
            static uint32_t prev_panel;
            uint32_t panel=fm1_in.buttons;
            if(panel&~prev_panel&(1u<<SAVE_BUTTON))lsdj_save_button();
            if(panel&~prev_panel&(1u<<SEQ_BUTTON))seq_button();
            if(songrow_addr&&((panel^prev_panel)&(1u<<ARP_BUTTON)))cue_button((panel>>ARP_BUTTON)&1u);
            if((panel^prev_panel)&(1u<<EDIT_BUTTON))tempo_button((panel>>EDIT_BUTTON)&1u);
            if(panel&~prev_panel&(1u<<FX_BUTTON)&&!menu.open){
                fx_held=1;fx_used=0;fx_held_since=fm1_ms;fx_hold_keys_prev=key_lo;
            }
            if(fx_held&&!(panel&(1u<<FX_BUTTON))){   /* released: a tap toggles the page */
                int tap=!fx_used&&fm1_ms-fx_held_since<FX_HOLD_MS;
                fx_held=0;fxu.held=0;
                if(tap&&!menu.open)fx_bars_show(-1);   /* a tap: the channel pages */
                else bars.until_ms=0;                  /* let go: the master pages close */
            }
            if(panel&~prev_panel&(1u<<GLO_BUTTON)){
                if(!menu.open){lsdj_menu_open(&menu);bars.until_ms=0;}   /* the effects pop-up goes */
                else{
                    menu.open=0;
                    if(menu.changed){   /* keep them: a 16-byte record, the emulator parked meanwhile */
                        int rc=-8;
                        if(play_save){settings_pending=1;rc=0;}   /* written when the save is done */
                        else if(recovery_flash_ok){video_hold();rc=settings_store();video_release();}
                        lsdj_ov_say(&ov,rc?LSDJ_MSG_SETTINGS_FAILED:LSDJ_MSG_SETTINGS_SAVED,fm1_ms);
                    }
                }
            }
            prev_panel=panel;
            {   /* CPU meter: underruns since the last pass */
                static uint32_t underruns_seen;
                uint32_t u=audio_underruns;cpu.underrun_count+=u-underruns_seen;underruns_seen=u;
            }
            cpu.core1=(uint8_t)(core1_emu&&video_online);cpu.reason=(uint8_t)video_retire_reason;
            {   /* the emulator fell back to CPU0 (CPU1 did not start, or stopped): too slow with the
                   effects, the sound crackles. Say so once each time, and how to get CPU1 back. */
                static uint32_t told=0;
                if(!(core1_emu&&video_online)&&video_fallbacks!=told&&fm1_ms>6000u){
                    told=video_fallbacks;lsdj_ov_show(&ov,"CPU1 STOPPED: SOUND","MAY CRACKLE. OFF+ON",fm1_ms);
                }
            }
            if(lsdj_cpu_poll(&cpu,fm1_ticks())&&settings.meter&&!menu.open){
                static char l1[20],l2[20];
                lsdj_cpu_lines(&cpu,l1,l2);lsdj_ov_show(&ov,l1,l2,fm1_ms);
            }
        }
#endif
#if !GB_SMK37
        int emu1=core1_emu&&video_online;   /* the emulator runs on CPU1 */
#else
        int emu1=0;
#endif
        static uint32_t seen_frames,seen_busy,hb,hb_at;
        if(!emu1){hb_at=fm1_ticks();seen_frames=gp_core1.frames;seen_busy=gp_core1.busy_ticks;}
#if !GB_SMK37
        if(emu1){
            /* Input for the frame CPU1 is about to emulate, and the CPU meter. */
            uint32_t f=gp_core1.frames;
            if(f!=seen_frames){
                fm1_input_frame();
                play_save_step();
                uint32_t busy=gp_core1.busy_ticks;
                cpu.emu+=busy-seen_busy;cpu.frames+=f-seen_frames;
                if(gp_core1.last_ticks>cpu.worst)cpu.worst=gp_core1.last_ticks;
                seen_busy=busy;seen_frames=f;
                cpu.snd+=gp_snd_take_ticks();cpu.renders+=gp_snd_take_renders();
                worked=1;
            }
            /* A finished picture: scale it here (CPU0), then let CPU1 draw again. */
            if(display_ok&&gp_core1.pic_seq!=gp_core1.pic_ack&&display_ready()){
                uint32_t t0=fm1_ticks();video_barrier();
                native_draw();video_scale_local(gp_pixels(),screen);
                video_barrier();gp_core1.pic_ack=gp_core1.pic_seq;
                /* panels and messages: in the picture (native_draw) */
                display_frame();display_count++;
                cpu.lcd+=fm1_ticks()-t0;worked=1;
            }
            /* CPU1 watchdog: no progress for 0.5 s, or a CPU1 fault: emulate on CPU0. */
            uint32_t h=gp_core1.heartbeat;
            if(h!=hb){hb=h;hb_at=fm1_ticks();}
            else if((uint32_t)(fm1_ticks()-hb_at)>12000000u||fm1_core1_fault.count!=video_faults){
                video_retire_reason=5;video_retire();
            }
        }
#endif
        if(!emu1&&gp_audio_queued()<GP_AUDIO_TARGET){
#if GB_SMK37
            input_buttons=smk_input_frame();input_seen|=input_buttons;gp_buttons(input_buttons);
#else
            fm1_input_frame();
#endif
            uint32_t t0=fm1_ticks();
            /* Only the frame that will be sent to the LCD is drawn (every 6th). */
            gp_render(gp_frame_count()+1u-drawn>=6u);
            gp_frame();
            lsdj_cpu_frame(&cpu,fm1_ticks()-t0);worked=1;
#if !GB_SMK37
            play_save_step();
#endif
            cpu.snd+=gp_snd_take_ticks();cpu.renders+=gp_snd_take_renders();
        }
        if(!emu1 && display_ok && video_complete()){
            uint32_t t0=fm1_ticks();
#if !GB_SMK37
            /* panels and messages: in the picture (native_draw) */
#endif
            display_frame();display_count++;
            cpu.lcd+=fm1_ticks()-t0;worked=1;
        }
        if(!emu1 && display_ok && !video_pending && gp_frame_count()-drawn>=6 && display_ready()){
            uint32_t t0=fm1_ticks();
            native_draw();video_submit(gp_pixels());drawn=gp_frame_count();
            if(!video_online){
#if !GB_SMK37
                /* panels and messages: in the picture (native_draw) */
#endif
                display_frame();display_count++;
            }
            cpu.lcd+=fm1_ticks()-t0;worked=1;
        }
        if(!worked)cpu.idle+=fm1_ticks()-now;
        if(seconds>=30 && scan_count>100 && audio_count>100 && gp_frame_count()>100)
            smk37_bootguard_healthy(&gb_bootguard);
        if(gp_error()) {audio_stop();fm1_reboot();}
    }
}
