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
#include "boot_brand.h"
extern void gb_isr_audio(void),gb_isr_scan(void);
static int32_t dma[256*4] __attribute__((aligned(4)));
static int16_t samples[256*2];
static uint16_t screen[240*216] __attribute__((section(".pool"),aligned(4)));
static volatile uint32_t scan_count,audio_count,key_lo,key_hi,audio_underruns;
static uint32_t display_count;
static uint32_t input_buttons,input_seen;
static volatile uint32_t splash_active,ding_position;
/* Flash speed test (board.c lsdj_flash_test_*): a steady 1 kHz sine replaces the
   game audio, so any interrupt gap caused by a flash erase shows up as a jump in
   the recorded tone; afterwards short beeps report the slowest erase in ms. */
static volatile uint32_t tone_on,tone_phase;
static int16_t tone_table[64]={   /* RAM (.data): 8000*sin(2*pi*k/64) */
    0,784,1561,2322,3061,3771,4445,5075,5657,6184,6652,7055,7391,7656,7846,7961,8000,7961,7846,7656,7391,7055,6652,6184,5657,5075,4445,3771,3061,2322,1561,784,0,-784,-1561,-2322,-3061,-3771,-4445,-5075,-5657,-6184,-6652,-7055,-7391,-7656,-7846,-7961,-8000,-7961,-7846,-7656,-7391,-7055,-6652,-6184,-5657,-5075,-4445,-3771,-3061,-2322,-1561,-784};
static void read_samples(void){
    if(tone_on){
        for(unsigned i=0;i<256;i++){
            int16_t v=tone_table[tone_phase>>26];tone_phase+=97391549u;   /* 1000 Hz at 44.1 kHz */
            samples[2*i]=samples[2*i+1]=v;
        }
        return;
    }
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
void gb_scan_irq(void){fm1_timer5_ack();fm1_input_tick();key_lo=fm1_in.notes;scan_count++;recovery_tick();}
void gb_audio_irq(void){
    uint32_t p=fm1_audio_pending();fm1_audio_ack_aux(p);
    if(!(p&FM1_AUDIO_HALF))return;
    unsigned h=fm1_audio_free_half();if(!splash_active&&gp_audio_queued()<256)audio_underruns++;
    read_samples();
    int32_t start=(int32_t)master_audio_gain,target=(int32_t)master_gain;
    for(unsigned i=0;i<256;i++){
        uint32_t gain=(uint32_t)(start+(target-start)*(int32_t)(i+1)/256);
        dma[h*512+i*2]=gb_master_sample(samples[i*2],gain);
        dma[h*512+i*2+1]=gb_master_sample(samples[i*2+1],gain);
    }
    master_audio_gain=(uint32_t)target;
    audio_count++;fm1_audio_ack_half();
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
#include "fm1_lsdj_input.h"
static lsdj_input_t lsdj_in;
static void piano_lights(uint32_t held_actions){
    static uint32_t previous=UINT32_MAX;
    if(held_actions==previous)return;
    previous=held_actions;
    uint32_t lights=gp_fm1_key_lights(held_actions);
    for(unsigned k=0;k<27;k++)fm1_led_key(14+k,(lights&(1u<<k))!=0);
}
static void input_init(void){
    fm1_input_init();
    lsdj_input_init(&lsdj_in);
    piano_lights(0);
    fm1_led_key(12,1); /* PLAY / Select */
    fm1_led_key(13,1); /* REC / Start */
    fm1_timer5_start(gb_isr_scan,4);
}
static void audio_start(void){fm1_audio_init(dma,512,gb_isr_audio,3);}
static void audio_stop(void){fm1_audio_stop();}
#include "fm1_recovery.h"
/* LSDj songs (cartridge RAM) in flash: loaded at boot, written on SAVE. */
static int lsdj_fl_erase(uint32_t off){uint32_t took;return fl_erase4k(off,&took);}
#define LSDJ_FL_ERASE(off) lsdj_fl_erase(off)
#define LSDJ_FL_WRITE(off,p,n) fl_write(off,p,n)
#define LSDJ_FL_READ(off,p,n) ota_fread(off,p,n)
#define LSDJ_FL_IDLE() fm1_wdt_feed()
#include "lsdj_save.h"   /* v5 A/B save format: only read once, to import it */
#include "lsdj_state.h"  /* compressed save store: the autosave (and later the slots) */
/* Build-time check: the flash driver must accept the whole save pool (a narrower
   range made autosaves that landed in the pool's top sectors fail silently). */
typedef char lst_pool_inside_flash_range[FL_RANGE_OK(LST_POOL_LO,LST_POOL_HI-LST_POOL_LO)?1:-1];
#include "lsdj_ftl.h"    /* glitch-free autosave: programs while sound plays, erases in silence */
#define SAVE_BUTTON 9u            /* BTN_SAVE */
#define LSDJ_STATE_BUILD 1u       /* cartridge-RAM-only compressed autosave (v6..v10, imported) */
/* LSDj 9.3.9 updates its play-time clock in cartridge RAM about once a minute;
   that alone is not a reason to autosave (it is saved with the next real change). */
#define LSDJ_CLOCK_LO 0x3FB3u
#define LSDJ_CLOCK_HI 0x3FB8u
#define AUTOSAVE_QUIET_FRAMES 180u   /* save 3 s after the last real change */
#define SILENT_FRAMES 30u            /* 0.5 s of exactly-zero output = silent: erases allowed */
static ftl_t ftl;
static int save_display_ok;
static void save_wait(uint32_t ticks){uint32_t t=fm1_ticks();while(fm1_ticks()-t<ticks)fm1_wdt_feed();}
/* Autosave, never pausing anything: changed 4 KiB blocks of cartridge RAM are
   written page by page (<= 1.6 ms each) into erased sectors and committed
   together; old sectors are erased only while the output is silent. Starts 3 s
   after the last real change (SAVE: at once). The SAVE light is off while a save
   is being written, on again when it is committed. */
static void lsdj_autosave_poll(uint32_t held,uint32_t save_now){
    static uint32_t led_off;
    uint32_t f=gp_frame_count();
    int silent=gp_silent_frames()>=SILENT_FRAMES;
    if(!ftl_busy(&ftl)&&(gp_save_blocks()||ftl.retry)){
        int quiet=f-gp_save_last_change()>=AUTOSAVE_QUIET_FRAMES&&!held;
        if((quiet||save_now)&&recovery_flash_ok){
            uint32_t m=gp_save_take_blocks();
            if(ftl_begin(&ftl,m)==0&&ftl_busy(&ftl)&&!led_off){fm1_led_key(SAVE_BUTTON,0);led_off=1;}
        }
    }
    if(recovery_flash_ok&&ftl_step(&ftl,gp_save_data(),silent)<0){
        /* a failed write never loses the previous save; blink fast so it is seen */
        for(unsigned i=0;i<5;i++){fm1_led_key(SAVE_BUTTON,1);save_wait(1200000u);fm1_led_key(SAVE_BUTTON,0);save_wait(1200000u);}
    }
    if(led_off&&!ftl_busy(&ftl)){fm1_led_key(SAVE_BUTTON,1);led_off=0;}
}
/* Flash speed test (OCT- held + SAVE), runs while emulation continues:
   1 s of steady test tone (read_samples), then 5 rounds one second apart, each on
   a FREE pool sector (never a saved entry): erase, program 16 pages, erase again.
   1 s later the tone stops; then beeps report the slowest erase (one beep per
   started millisecond) and, after a pause, the slowest page program (one beep per
   started 0.1 ms). Record it from the line-out to hear any gap in the tone. */
enum { FT_IDLE, FT_LEAD, FT_ROUNDS, FT_TAIL, FT_GAP, FT_BEEP_E, FT_PAUSE, FT_BEEP_P };
static uint32_t ft_state,ft_next,ft_round,ft_erase_us,ft_prog_us,ft_beeps;
static int ft_tone;
static void lsdj_flash_test_start(void){
    if(!recovery_flash_ok||ft_state)return;
    ft_state=FT_LEAD;ft_next=fm1_ticks()+24000000u;ft_round=0;ft_erase_us=ft_prog_us=0;
    tone_phase=0;tone_on=1;fm1_led_key(SAVE_BUTTON,0);
}
static void lsdj_flash_test_round(void){
    lst_entry_t e[LST_KINDS];uint8_t owner[LST_SECTORS];
    lst_scan(e,owner);
    uint32_t pick=LST_SECTORS,skip=ft_round;
    for(uint32_t s=LST_SECTORS;s-->0;)if(!owner[s]&&skip--==0){pick=s;break;}
    if(pick==LST_SECTORS)return;
    uint32_t off=lst_sector_off(pick),took=0;
    if(!fl_erase4k(off,&took)&&took>ft_erase_us)ft_erase_us=took;
    static uint8_t zeros[256];
    for(uint32_t pg=0;pg<16;pg++){
        uint32_t f=irq_save(),pt=0;
        int rc=FL_FAR(fl_prog_ram)(off+pg*256u,zeros,256u,&pt);
        irq_restore(f);
        if(!rc&&pt>ft_prog_us)ft_prog_us=pt;
    }
    took=0;
    if(!fl_erase4k(off,&took)&&took>ft_erase_us)ft_erase_us=took;   /* leave it erased */
}
static void lsdj_flash_test_poll(void){
    if(!ft_state)return;
    uint32_t now=fm1_ticks();
    if((int32_t)(now-ft_next)<0)return;
    switch(ft_state){
    case FT_LEAD: ft_state=FT_ROUNDS; ft_next=now; break;
    case FT_ROUNDS:
        lsdj_flash_test_round();
        ft_next=now+24000000u;
        if(++ft_round==5)ft_state=FT_TAIL;
        break;
    case FT_TAIL: tone_on=0; ft_state=FT_GAP; ft_next=now+24000000u; break;
    case FT_GAP:
        ft_beeps=(ft_erase_us+999u)/1000u; if(ft_beeps>60)ft_beeps=60; if(!ft_beeps)ft_beeps=1;
        ft_state=FT_BEEP_E; ft_tone=0; ft_next=now; break;
    case FT_BEEP_E: case FT_BEEP_P:
        if(!ft_tone&&ft_beeps){tone_phase=0;tone_on=1;ft_tone=1;ft_next=now+2880000u;break;}  /* 120 ms on */
        tone_on=0;ft_tone=0;
        if(ft_beeps)ft_beeps--;
        if(ft_beeps){ft_next=now+5520000u;break;}                                              /* 230 ms off */
        if(ft_state==FT_BEEP_E){
            ft_beeps=(ft_prog_us+99u)/100u; if(ft_beeps>60)ft_beeps=60; if(!ft_beeps)ft_beeps=1;
            ft_state=FT_PAUSE; ft_next=now+36000000u; break;                                   /* 1.5 s */
        }
        ft_state=FT_IDLE; fm1_led_key(SAVE_BUTTON,1); break;
    case FT_PAUSE: ft_state=FT_BEEP_P; ft_tone=0; ft_next=now; break;
    }
}
/* At boot, before LSDj's first frame (sound is silent here, so erases are
   fine): load the autosave (SAVE light blinks 3 times). Without one, import the
   previous format (v6..v10 compressed autosave, or the v5 A/B save; blinks
   twice) into a fresh autosave area, keeping the old copy until the new one is
   committed. */
static void lsdj_save_boot(void){
    gp_save_ignore(LSDJ_CLOCK_LO,LSDJ_CLOCK_HI);
    unsigned blinks=0;
    if(ftl_boot(&ftl,gp_save_data()))blinks=3;
    else if(recovery_flash_ok){
        uint8_t keep[FTL_N];memset(keep,0,sizeof keep);
        lst_region_t r={gp_save_data(),gp_save_size()};
        lst_entry_t e[LST_KINDS];lst_scan(e,NULL);
        if(e[LST_AUTO].valid&&lst_load(LST_AUTO,&r,1,LSDJ_STATE_BUILD,(uint8_t*)screen,lsdj_save_buf,NULL)==LST_OK){
            for(uint32_t k=0;k<e[LST_AUTO].nsect;k++)keep[e[LST_AUTO].sector+k]=1;
            blinks=2;
        }else if(lsdj_save_load(gp_save_data(),gp_save_size())){
            for(uint32_t k=0;k<FTL_N&&k<0x42u;k++)keep[k]=1;   /* v5 slots: 0x97000..0xD9000 */
            blinks=2;
        }
        if(ftl_format(&ftl,gp_save_data(),keep))blinks=0;
    }
    gp_save_take_blocks();   /* what was just loaded is saved */
    for(unsigned i=0;i<blinks;i++){fm1_led_key(SAVE_BUTTON,1);save_wait(4800000u);fm1_led_key(SAVE_BUTTON,0);save_wait(4800000u);}
}
#endif
static void boot_splash(int display_ok){
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
            gd_splash_draw(screen,ms);display_frame();next_draw=ms+33u;
            if(ms>=GD_SPLASH_LAND_MS)landing_frame=1;
        }
    }
    while(display_ok&&!display_ready())fm1_wdt_feed();
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
#ifdef CART_SPARSE_BANKS
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
    fm1_led_key(SAVE_BUTTON,1);
#endif
    boot_splash(display_ok);
    for(unsigned i=0;i<4;i++)gp_frame();
    splash_active=0;
    recovery_init();
    video_start(cold);
    uint32_t drawn=0,last=fm1_ticks(),elapsed=0,seconds=0;
    for(;;){
        uint32_t now=fm1_ticks();elapsed+=now-last;last=now;
        if(elapsed>=24000000){elapsed-=24000000;seconds++;}
        fm1_wdt_feed();
#if !GB_SMK37
        master_poll();
#endif
        recovery_service();
#if !GB_SMK37
        {
            static uint32_t prev_panel;
            uint32_t panel=fm1_in.buttons;
            uint32_t save_now=0;
            if(panel&~prev_panel&(1u<<SAVE_BUTTON)){
                if(panel&1u)lsdj_flash_test_start();   /* OCT- held: flash speed test */
                else save_now=1;                       /* SAVE: save now (still no pause) */
            }
            prev_panel=panel;
            lsdj_flash_test_poll();
            if(!ft_state)lsdj_autosave_poll(panel|key_lo|key_hi,save_now);
        }
#endif
        if(gp_audio_queued()<2955){
#if GB_SMK37
            input_buttons=smk_input_frame();input_seen|=input_buttons;gp_buttons(input_buttons);
#else
            /* Once per emulated frame: knob taps are timed in frames.
               Keys, PLAY=Select, REC=Start: src/fm1_lsdj_input.h. */
            int32_t enc[FM1_NENC];
            for(unsigned e=0;e<FM1_NENC;e++)enc[e]=fm1_enc_take(e);
            input_buttons=lsdj_input_frame(&lsdj_in,key_lo,fm1_in.buttons,enc);
            /* Note mode: every key light off. */
            piano_lights(lsdj_in.mode==LSDJ_KEYS_NOTES?0xFFu:input_buttons);
            input_seen|=input_buttons;gp_buttons(input_buttons);
#endif
            gp_frame();
        }
        if(display_ok && video_complete()){display_frame();display_count++;}
        if(display_ok && !video_pending && gp_frame_count()-drawn>=6 && display_ready()){
            video_submit(gp_pixels());drawn=gp_frame_count();
            if(!video_online){display_frame();display_count++;}
        }
        if(seconds>=30 && scan_count>100 && audio_count>100 && gp_frame_count()>100)
            smk37_bootguard_healthy(&gb_bootguard);
        if(gp_error()) {audio_stop();fm1_reboot();}
    }
}
