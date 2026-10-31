"""Build native host tests and FM-1/SMK37 compile-only GB candidates."""
import argparse, hashlib, json, os, re, subprocess, sys, zipfile
from pathlib import Path
from diagnostic import make_rom
from prepare_apu import prepare
ROOT=Path(__file__).resolve().parents[1]
# The package identity (Felucca updater, The Firmware Hub): FM-1_ + our prefix 537 + a 4-digit version
# (FM-1_992 on: FM-1_9NN overlaps other firmwares). LSDJ_VERSION is the short name the web page's link
# (8 bytes in INFO) and the splash use. FM-1_993 went to a private Bluetooth build (since removed), so
# FM-1_994 is the next public one.
LSDJ_VERSION='FM-1_995'
LSDJ_SLOT_PRODUCT='FM-1_537'+LSDJ_VERSION[5:].zfill(4)
DEFAULT_ZIG=ROOT.parent/'Felucca/Felucca-website/build/wasm-tools/ziglang/zig.exe'
# JieLi toolchain: $JIELI_TOOLCHAIN, else ~/.jieli/toolchain (Dinghy's get_toolchain.sh), else the upstream path.
DEFAULT_TC=next((str(p) for p in [Path.home()/'.jieli/toolchain'] if p.is_dir()),
                '/mnt/e/AI_Apps/FM1-SMK/Felucca/Felucca-review/.build-deps/jieli/jieli-linux-toolchains-20250324.1')
def run(args):
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True)
def rom_bytes(path):
    if not path:return make_rom()
    p=Path(path)
    if p.suffix.lower()=='.zip':
        with zipfile.ZipFile(p) as z:
            names=[x for x in z.infolist() if x.filename.lower().endswith('.gb') and not x.is_dir()]
            if len(names)!=1:raise ValueError('ZIP must contain exactly one .gb cartridge')
            if names[0].file_size>8*1024*1024:raise ValueError('ROM exceeds 8 MiB')
            return z.read(names[0])
    return p.read_bytes()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('target',choices=['test','fm1','smk37'])
    p.add_argument('--zig',default=os.environ.get('ZIG',str(DEFAULT_ZIG)))
    p.add_argument('--toolchain',default=os.environ.get('JIELI_TOOLCHAIN',DEFAULT_TC))
    p.add_argument('--rom',type=Path)
    p.add_argument('--rom-capacity',type=int,default=0,help='Reserve cartridge bytes for the browser ROM packer')
    p.add_argument('--lsdj-slot',type=int,default=0,help='FM-1: empty browser-patchable LSDj slot (bytes, a multiple of 256, at least 256 + 16384)')
    p.add_argument('--kits',type=int,default=None,help='Sparse cartridge (automatic above 512 KiB): LSDj kit banks to keep (default 3)')
    a=p.parse_args()
    if a.lsdj_slot and (a.target!='fm1' or a.rom or a.kits is not None or a.rom_capacity or
                       a.lsdj_slot<16640 or a.lsdj_slot>0x8dfbc or a.lsdj_slot%256):
        p.error('--lsdj-slot requires FM-1, a multiple of 256 bytes (at least 16640), and no ROM, kits or capacity option')
    (ROOT/'build').mkdir(exist_ok=True)
    prepare(ROOT)
    includes=['-Isrc','-Ivendor/peanut-gb']
    flags=['-std=c99','-O2','-fno-builtin','-DMINIGB_APU_AUDIO_FORMAT_S16SYS','-DAUDIO_SAMPLE_RATE=44100',*includes]
    sources=['src/gb_port.c','build/minigb_apu.c']
    if a.target=='test':
        (ROOT/'build/diagnostic.gb').write_bytes(make_rom())
        run([a.zig,'cc',*flags,'-UNDEBUG','-Wall','-Wextra',*sources,'tests/runtime.c','-o','build/runtime-test.exe'])
        run([ROOT/'build/runtime-test.exe',ROOT/'build/diagnostic.gb'])
        run([a.zig,'cc','-std=c99','-O2','-UNDEBUG','-Isrc','tests/smk37_game_controls.c','-o','build/smk37-controls-test.exe'])
        run([ROOT/'build/smk37-controls-test.exe'])
        run([a.zig,'cc','-std=c99','-O2','-UNDEBUG','tests/smk37_brightness.c','-o','build/smk37-brightness-test.exe'])
        run([ROOT/'build/smk37-brightness-test.exe'])
        run([a.zig,'cc','-std=c99','-O2','-UNDEBUG','tests/smk37_light_transport.c','-o','build/smk37-light-transport-test.exe'])
        run([ROOT/'build/smk37-light-transport-test.exe'])
        run([a.zig,'cc','-std=c99','-O2','-UNDEBUG','-Isrc','tests/video_worker.c','-o','build/video-worker-test.exe'])
        run([ROOT/'build/video-worker-test.exe'])
        run([a.zig,'cc','-std=c99','-O2','-UNDEBUG','-Isrc','tests/boot_brand.c','-o','build/boot-brand-test.exe'])
        run([ROOT/'build/boot-brand-test.exe'])
        return
    else:
        native(a,sources)

def sparse_pack(rom,kits):
    """Store only the banks a large ROM needs: drop empty banks and keep the first `kits`
    LSDj kit banks (signature 60 40); absent banks read as 0xff (an empty kit slot)."""
    banks=[rom[i:i+16384] for i in range(0,len(rom),16384)]
    stored=[];bank_map=[0xff]*len(banks);kept=0
    for i,b in enumerate(banks):
        if b.count(0xff)==len(b) or b.count(0)==len(b):continue
        if b[:2]==b'\x60\x40':
            if kept>=kits:continue
            kept+=1
        bank_map[i]=len(stored);stored.append(b)
    assert bank_map[0]==0
    # LSDj's first-boot cartridge test checks the header's global checksum (sum of all
    # bytes but 0x14E/F), so recompute it for the ROM as the emulator will read it.
    logical=b''.join(stored[s] if s!=0xff else b'\xff'*16384 for s in bank_map)
    total=(sum(logical)-logical[0x14e]-logical[0x14f])&0xffff
    bank0=bytearray(stored[0]);bank0[0x14e:0x150]=total.to_bytes(2,'big');stored[0]=bytes(bank0)
    return b''.join(stored),bank_map,kept

def native(a,sources):
    out=ROOT/'build'/a.target;out.mkdir(exist_ok=True)
    rom=b'' if a.lsdj_slot else rom_bytes(a.rom)
    bank_map=None
    if a.lsdj_slot:
        capacity=a.lsdj_slot;storage=b'\xff'*capacity
    elif a.kits is not None or len(rom)>512*1024:
        storage,bank_map,kept=sparse_pack(rom,3 if a.kits is None else a.kits)
        capacity=len(storage)
        print(f'Sparse cartridge: {len(storage)//16384} of {len(bank_map)} banks stored ({kept} kits), {capacity} bytes')
    else:
        capacity=a.rom_capacity or len(rom)
        if capacity<len(rom) or capacity>524288:raise SystemExit('Invalid cartridge capacity')
        storage=rom+b'\xff'*(capacity-len(rom))
    sparse=('' if bank_map is None else f'#define CART_SPARSE_BANKS {len(bank_map)}u\n'
            'static const unsigned char cart_bank_map[] = {'+','.join(map(str,bank_map))+'};\n')
    if a.lsdj_slot:sparse=f'#define CART_SLOT_CAPACITY {capacity}u\n#define CART_SLOT_PRODUCT "{LSDJ_SLOT_PRODUCT}"\n#define CART_SLOT_VERSION "{LSDJ_VERSION}"\n'
    (out/'cartridge.h').write_text('/* Local cartridge; never publish game ROMs. */\n'+sparse+'static const unsigned char cartridge[] = {\n'+
        ',\n'.join(','.join(str(b) for b in storage[i:i+32]) for i in range(0,len(storage),32))+'\n};\n')
    if a.target=='smk37':
        from build_smk_loader import build as build_loader
        gen,loader=build_loader(a.toolchain,out)
    rel=out.relative_to(ROOT).as_posix()
    def tc(tool,*args):
        run(['wsl','-e',f'{a.toolchain}/{tool}',*args])
    flags=['-target','pi32v2','-std=c99','-Os','-fno-builtin','-ffunction-sections','-fdata-sections','-DGB_HARDWARE=1','-DMUSICCOACH_DUAL_CORE=1',
        '-DMINIGB_APU_AUDIO_FORMAT_S16SYS','-DAUDIO_SAMPLE_RATE=44100',
        '-Isrc','-Isrc/freestanding','-Ivendor/peanut-gb','-Ivendor/felucca/firmware/hal','-I'+rel,
        '-DGB_SMK37='+str(int(a.target=='smk37')),'-Wall','-Wno-unused-function']
    if a.target=='smk37':flags+=['-I'+str(gen.relative_to(ROOT)).replace('\\','/')]
    inputs=[*sources,'src/libc.c','firmware/board.c','firmware/start.S','vendor/felucca/firmware/hal/fm1_vec.S']
    objects=[]
    for i,src in enumerate(inputs):
        obj=f'{rel}/{i}.o'
        source_flags=[('-O2' if f=='-Os' else f) for f in flags] if src in sources else flags
        tc('pi32v2/bin/clang',*source_flags,'-c',src,'-o',obj);objects.append(obj)
    elf=f'{rel}/gb-{a.target}.elf'
    tc('pi32v2/bin/ld','--gc-sections','-T','vendor/felucca/firmware/app.ld',*objects,'-o',elf)
    def capture(tool,*args):
        return subprocess.check_output(['wsl','-e',f'{a.toolchain}/{tool}',*args],cwd=ROOT,text=True)
    symbols=capture('common/bin/objdump','-t',elf);sections=capture('common/bin/objdump','-h',elf)
    (out/'symbols.txt').write_text(symbols);(out/'sections.txt').write_text(sections)
    def sym(n):return int(re.search(r'^([0-9a-f]+) .*\s'+n+r'$',symbols,re.M)[1],16)
    image=bytearray()
    for sec,address in [('.text',0x2000120),('.ram_text',sym('_rt_load')),('.dsp_text',sym('_dsp_load')),('.data',sym('_data_load'))]:
        target=f'{rel}/{sec[1:]}.bin';tc('common/bin/objcopy','-O','binary','-j',sec,elf,target)
        blob=(ROOT/target).read_bytes() if (ROOT/target).exists() else b''
        if not blob:continue
        offset=address-0x2000120
        if offset<len(image):raise RuntimeError('Overlapping XIP sections')
        image.extend(b'\xff'*(offset-len(image)));image.extend(blob)
    image.extend(b'\xff'*(-len(image)%4))
    assert sym('_start')==0x2000120 and image[:4]==bytes.fromhex('04818000')
    assert len(image)<=0x8dfbc and sym('_bss_end')<=0x1c1a000 and sym('_pool_end')<=0x1c74000
    assert sym('_cpu1_stacks_hi')<=0x1c74000 and sym('_cpu1_stacks_hi')-sym('_cpu1_stacks_lo')==8192
    assert 0x1c00000<=sym('gb_core1_main')<sym('_dsp_end')<=0x1c06000
    if a.target=='smk37':
        assert not re.search(r'\b(fm1_input_init|fm1_adc_init|fm1_audio_init|fm1_lcd_hw_init)$',symbols,re.M)
    if a.target=='fm1':
        for required in ['usb_start','ota_session.parm']:
            assert re.search(r'\s'+re.escape(required)+r'$',symbols,re.M), 'Missing USB updater symbol: '+required
    binary=out/f'gb-{a.target}-NOT-FOR-INSTALL.bin';binary.write_bytes(image)
    result={'board':a.target,'installable':False,'hardware_validated':False,'bytes':len(image),
      'ram_bytes':sym('_bss_end')-0x1c08000,'pool_bytes':sym('_pool_end')-0x1c1a000,'cpu1_stack_bytes':sym('_cpu1_stacks_hi')-sym('_cpu1_stacks_lo'),
      'dual_core':'CPU0 GB/APU/audio/input/USB; CPU1 display scaling; timeout/fault fallback',
      'sha256':hashlib.sha256(image).hexdigest(),'rom_bytes':len(rom),'rom_sha256':hashlib.sha256(rom).hexdigest(),
      'cartridge_offset':sym('cartridge')-0x2000120,'cartridge_capacity':capacity,
      'cartridge':'original diagnostic' if a.rom is None else 'private user cartridge',
      'flash_writes':'OTA staging 0xe0000..0xe5000 only' if a.target=='fm1' else False,
      'usb_updater':True,'save_persistence':'session RAM only'}
    result['app_bytes']=len(image)
    if a.lsdj_slot:
        result.update(lsdj_slot=1,product=LSDJ_SLOT_PRODUCT,version=LSDJ_VERSION,cartridge='empty LSDj slot',
                      compatibility='experimental generic; verified cursor profile only, other memory helpers disabled')
    if a.target=='smk37':result.update(usb_protocol='smk37-ram-v2',product=loader['product'],loader=loader)
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
