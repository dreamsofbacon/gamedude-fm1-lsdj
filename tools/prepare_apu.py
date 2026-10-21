"""Generate a minimal variable-frame-count MiniGB APU adaptation; vendor stays intact."""
from pathlib import Path
def prepare(root):
    text=(root/'vendor/peanut-gb/minigb_apu.c').read_text()
    text=text.replace('const bool ch2)', 'const bool ch2, unsigned frames)')
    for name in ('wave','noise'):
        text=text.replace(f'update_{name}(struct minigb_apu_ctx *ctx, audio_sample_t *samples)',
                          f'update_{name}(struct minigb_apu_ctx *ctx, audio_sample_t *samples, unsigned frames)')
    assert text.count('i < AUDIO_NSAMPLES')==3
    text=text.replace('i < AUDIO_NSAMPLES', 'i < frames * 2u')
    text=text.replace('void minigb_apu_audio_callback(struct minigb_apu_ctx *ctx,\n\t\taudio_sample_t *stream)',
                      'void minigb_apu_render(struct minigb_apu_ctx *ctx,\n\t\taudio_sample_t *stream, unsigned frames)')
    text=text.replace('AUDIO_SAMPLES_TOTAL * sizeof(audio_sample_t)', 'frames * 2u * sizeof(audio_sample_t)')
    text=text.replace('update_square(ctx, stream, 0);', 'update_square(ctx, stream, 0, frames);')
    text=text.replace('update_square(ctx, stream, 1);', 'update_square(ctx, stream, 1, frames);')
    text=text.replace('update_wave(ctx, stream);', 'update_wave(ctx, stream, frames);')
    text=text.replace('update_noise(ctx, stream);', 'update_noise(ctx, stream, frames);')
    text=fix_zombie_mode(text)
    text=fix_boot_state(text)
    text+='\nvoid minigb_apu_audio_callback(struct minigb_apu_ctx *ctx, audio_sample_t *stream) { minigb_apu_render(ctx, stream, AUDIO_SAMPLES); }\n'
    path=root/'build/minigb_apu.c';path.parent.mkdir(exist_ok=True)
    path.write_text(text)
    return path

ZOMBIE_NRX2='''\t\tif (ctx->chans[i].powered && ctx->chans[i].enabled) {
\t\t\tint vol = ctx->chans[i].volume;
\t\t\tint tick = (val & 7) && !(old & 7);
\t\t\tif ((val & 0xF) == 8 && (old & 0xF) == 8) tick = 1;
\t\t\tif ((val ^ old) & 8) {
\t\t\t\tif (val & 8) vol = !(old & 7) ? (vol ^ 0xF) : (0xE - vol);
\t\t\t\telse vol = 0x10 - vol;
\t\t\t\tif (val & 8) tick = 0;
\t\t\t}
\t\t\tif (tick) vol += (val & 8) ? 1 : -1;
\t\t\tctx->chans[i].volume = vol & 0xF;
\t\t}
\t\t{
\t\t\t/* (FREQ_INC_MULT * 64) / period, as in chan_trigger(); FREQ_INC_MULT == 105 */
\t\t\tstatic const uint32_t inc_lut[8] = { 840, 6720, 3360, 2240, 1680, 1344, 1120, 960 };
\t\t\tctx->chans[i].env.step = val & 7;
\t\t\tctx->chans[i].env.up = val & 8;
\t\t\tctx->chans[i].env.inc = inc_lut[val & 7];
\t\t}
'''

def fix_zombie_mode(text):
    """DMG NRx2 "zombie mode" after SameBoy's nrx2_glitch(). LSDj 9 builds its volume
    envelopes from NRx2 rewrites (09, 11, 18 per tick = volume -1); MiniGB's rule pushed
    the volume up instead, so notes swelled instead of decaying."""
    store='\tctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] = val;\n\ti = '
    assert store in text and '# define FREQ_INC_MULT\t\t105' in text
    text=text.replace(store,'\tconst uint8_t old = ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION];\n'+store,1)
    start=text.index('\t\t// "zombie mode" stuff')
    end=text.index('\t} break;\n\n\tcase 0xFF1C:')
    return text[:start]+ZOMBIE_NRX2+text[end:]

def fix_boot_state(text):
    """Leave the APU as the DMG boot ROM does: powered, post-boot registers, channel 1
    still enabled at volume 0 so NR52 reads 0xF1. MiniGB wrote the registers before
    powering on (so they were dropped) and left NR52 = 0xF0, which LSDj reads as a
    Super Game Boy and then plays 250/256 slow."""
    loop='\t\tfor(uint_fast8_t i = 0; i < sizeof(regs_init); ++i)\n\t\t\tminigb_apu_audio_write(ctx, 0xFF10 + i, regs_init[i]);\n\t}\n'
    assert loop in text
    text=text.replace(loop,'\t\tminigb_apu_audio_write(ctx, 0xFF26, 0x80);\n'+loop+
                      '\n\tchan_enable(ctx, 0, 1);\n\tctx->chans[0].volume = 0;\n',1)
    # NR52 bits 0-3 are read-only channel status: a write (Peanut-GB's gb_reset writes
    # 0xF1) must not clear them; only powering off does.
    store='\t\tctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] = val & 0x80;\n'
    assert text.count(store)==1
    return text.replace(store,'\t\tctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] = (val & 0x80) ?\n'
                        '\t\t\t(0x80 | (ctx->audio_mem[addr - AUDIO_ADDR_COMPENSATION] & 0x0F)) : 0x00;\n')
