"""Build browser, native host tests and FM-1/SMK37 compile-only GB candidates."""
import argparse, hashlib, json, os, re, subprocess, sys, zipfile
from pathlib import Path
from diagnostic import make_rom
from prepare_apu import prepare
ROOT=Path(__file__).resolve().parents[1]
LSDJ_SLOT_PRODUCT='FM-1_991'
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
    p.add_argument('target',choices=['web','test','fm1','smk37'])
    p.add_argument('--zig',default=os.environ.get('ZIG',str(DEFAULT_ZIG)))
    p.add_argument('--toolchain',default=os.environ.get('JIELI_TOOLCHAIN',DEFAULT_TC))
    p.add_argument('--rom',type=Path)
    p.add_argument('--rom-capacity',type=int,default=0,help='Reserve cartridge bytes for the browser ROM packer')
    p.add_argument('--lsdj-slot',type=int,default=0,help='FM-1: empty browser-patchable LSDj slot (bytes, a multiple of 256, at least 256 + 16384)')
    p.add_argument('--kits',type=int,default=None,help='Sparse cartridge (automatic above 512 KiB): LSDj kit banks to keep (default 3)')
    p.add_argument('--ble',action='store_true',help='FM-1: Bluetooth MIDI (firmware/lsdj_ble.c, the JieLi BT libraries)')
    p.add_argument('--ble-sdk',default=os.environ.get('AC79_BT_SDK','/root/ac79-sdk-bt'),help='AC79 SDK with the pinned BT libraries (vendor/hortator-ble/tools/ble_libs.py)')
    a=p.parse_args()
    if a.lsdj_slot and (a.target!='fm1' or a.rom or a.kits is not None or a.rom_capacity or a.ble or
                       a.lsdj_slot<16640 or a.lsdj_slot>0x8dfbc or a.lsdj_slot%256):
        p.error('--lsdj-slot requires FM-1, a multiple of 256 bytes (at least 16640), and no ROM, kits, capacity or BLE option')
    (ROOT/'build').mkdir(exist_ok=True);(ROOT/'web').mkdir(exist_ok=True)
    prepare(ROOT)
    includes=['-Isrc','-Ivendor/peanut-gb']
    flags=['-std=c99','-O2','-fno-builtin','-DMINIGB_APU_AUDIO_FORMAT_S16SYS','-DAUDIO_SAMPLE_RATE=44100',*includes]
    sources=['src/gb_port.c','build/minigb_apu.c']
    if a.target=='web':
        exports=['gp_upload','gp_load_upload','gp_frame','gp_buttons','gp_pause','gp_reset','gp_palette','gp_volume',
                 'gp_pixels','gp_save_data','gp_save_size','gp_save_dirty','gp_save_clean','gp_audio_queued',
                 'gp_render_audio','gp_error','gp_frame_count','gp_title']
        run([a.zig,'cc','-target','wasm32-freestanding',*flags,'-Isrc/freestanding','-nostdlib',
             '-Wl,--no-entry','-Wl,-z,stack-size=0x10000','-Wl,--initial-memory=0x1000000','-Wl,--max-memory=0x1000000',
             *['-Wl,--export='+x for x in exports],*sources,'src/wasm.c','src/libc.c','-o','web/gb.wasm'])
        (ROOT/'web/diagnostic.gb').write_bytes(make_rom())
        binary=ROOT/'web/gb.wasm'
    elif a.target=='test':
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
        return
    (ROOT/'web/build.json').write_text(json.dumps({'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'bytes':binary.stat().st_size})+'\n')
    print('Built',binary,binary.stat().st_size,'bytes')

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

def ble_build(a,flags,rel,tc):
    """The Bluetooth unit and the JieLi pieces: firmware/lsdj_ble.c (with Hortator's BT runtime), the context switch,
    the SDK's BT config sources and log stubs as bitcode, the pinned libraries and archive members."""
    import importlib.util,shutil
    spec=importlib.util.spec_from_file_location('ble_libs',ROOT/'vendor/hortator-ble/tools/ble_libs.py')
    ble_libs=importlib.util.module_from_spec(spec);spec.loader.exec_module(ble_libs)
    ble_libs.SDK=Path(a.ble_sdk)
    libs=ble_libs.libs()   # stops, naming it, on a missing SDK or a library that is not the pinned one
    out=ROOT/rel/'ble';out.mkdir(exist_ok=True)
    orel=f'{rel}/ble'
    objs=[]
    hdr=['-Ivendor/hortator-ble','-Ivendor/hortator-ble/ble','-Ivendor/hortator-ble/hal','-Ifirmware']
    tc('pi32v2/bin/clang',*flags,'-mllvm','-pi32v2-large-program=true',*hdr,'-c','firmware/lsdj_ble.c','-o',f'{orel}/lsdj_ble.o')
    objs.append(f'{orel}/lsdj_ble.o')
    tc('pi32v2/bin/clang','-target','pi32v2','-c','vendor/hortator-ble/hal/fm1_ctx.S','-o',f'{orel}/fm1_ctx.o')
    objs.append(f'{orel}/fm1_ctx.o')
    sdk=str(ble_libs.SDK)
    cfl=[('-I'+sdk+s[len('-I/sdk'):] if s.startswith('-I/sdk/') else '-Ivendor/hortator-ble/ble/sdkcfg' if s=='-Ifirmware/src/ble/sdkcfg' else s)
         for s in ble_libs.SDK_CFLAGS]
    for src in ble_libs.SDK_CONFIG_SOURCES:
        o=f'{orel}/{Path(src).stem}.o'
        tc('pi32v2/bin/clang','-target','pi32v2',*cfl,'-c',f'{sdk}/{src}','-o',o);objs.append(o)
    tc('pi32v2/bin/clang','-target','pi32v2','-mcpu=r3','-Os','-flto','-Wall','-c','vendor/hortator-ble/ble/ble_lto_stubs.c','-o',f'{orel}/ble_lto_stubs.o')
    objs.append(f'{orel}/ble_lto_stubs.o')
    paths=[]
    for p in libs:
        shutil.copy(p,out/Path(p).name);paths.append(f'{orel}/{Path(p).name}')
    for m in ble_libs.members(out):
        objs.append(f'{orel}/{Path(m).name}')
    return objs,paths,ble_libs

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
    if a.lsdj_slot:sparse=f'#define CART_SLOT_CAPACITY {capacity}u\n#define CART_SLOT_PRODUCT "{LSDJ_SLOT_PRODUCT}"\n'
    (out/'cartridge.h').write_text('/* Local cartridge; never publish game ROMs. */\n'+sparse+'static const unsigned char cartridge[] = {\n'+
        ',\n'.join(','.join(str(b) for b in storage[i:i+32]) for i in range(0,len(storage),32))+'\n};\n')
    if a.target=='smk37':
        from build_smk_loader import build as build_loade
        gen,loader=build_loader(a.toolchain,out)
    rel=out.relative_to(ROOT).as_posix()
    def tc(tool,*args):
        run(['wsl','-e',f'{a.toolchain}/{tool}',*args])
    flags=['-target','pi32v2','-std=c99','-Os','-fno-builtin','-ffunction-sections','-fdata-sections','-DGB_HARDWARE=1','-DMUSICCOACH_DUAL_CORE=1',
        '-DMINIGB_APU_AUDIO_FORMAT_S16SYS','-DAUDIO_SAMPLE_RATE=44100',
        '-Isrc','-Isrc/freestanding','-Ivendor/peanut-gb','-Ivendor/felucca/firmware/hal','-I'+rel,
        '-DGB_SMK37='+str(int(a.target=='smk37')),'-Wall','-Wno-unused-function']
    if a.target=='smk37':flags+=['-I'+str(gen.relative_to(ROOT)).replace('\\','/')]
    ble=getattr(a,'ble',False)
    if ble and a.target!='fm1':raise SystemExit('--ble is for the FM-1')
    if ble:flags+=['-DLSDJ_BLE=1']
    inputs=[*sources,'src/libc.c','firmware/board.c','firmware/start.S','vendor/felucca/firmware/hal/fm1_vec.S']
    objects=[]
    for i,src in enumerate(inputs):
        obj=f'{rel}/{i}.o'
        source_flags=[('-O2' if f=='-Os' else f) for f in flags] if src in sources else flags
        tc('pi32v2/bin/clang',*source_flags,'-c',src,'-o',obj);objects.append(obj)
    elf=f'{rel}/gb-{a.target}.elf'
    if not ble:
        tc('pi32v2/bin/ld','--gc-sections','-T','vendor/felucca/firmware/app.ld',*objects,'-o',elf)
    else:
        ble_objects,ble_libs_paths,ble_libs=ble_build(a,flags,rel,tc)
        # the BT libraries are LLVM bitcode: linked through LTO with the SDK demo's codegen options (Hortator's
        # tools/build.py). Hortator sends the libraries' memcpy to a RAM copy (--wrap) for the radio trim, which
        # this build never runs (the stored calibration only): ours stays, so LSDj's own calls keep theirs
        gold=f'{a.toolchain}/pi32v2/bin/LLVMgold.so'
        tc('pi32v2/bin/ld','--gc-sections','--plugin',gold,'--plugin-opt=mcpu=r3','--plugin-opt=-pi32v2-large-program=true',
           '--plugin-opt=-pi32v2-always-use-itblock=false','--plugin-opt=-dont-used-symbol-list='+','.join(ble_libs.LOG_FUNCS),
           '--orphan-handling=error','-Map',f'{rel}/gb-{a.target}.map','-T','firmware/app_ble.ld',
           *objects,*ble_objects,'--start-group',*ble_libs_paths,'--end-group','-o',elf)
        ok={'.volatile_ram_code','.bt_updata_ram_code'}
        ram_code=sorted(set(re.findall(r'^\s*(\.\S*ram_code\S*)',(ROOT/rel/f'gb-{a.target}.map').read_text(),re.M))-ok)
        if ram_code:raise SystemExit(f'BT library RAM code sections linked into XIP: {ram_code}')
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
    assert len(image)<=0x8dfbc and sym('_bss_end')<=(sym('_pool_start') if ble else 0x1c1a000) and sym('_pool_end')<=0x1c74000
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
        result.update(lsdj_slot=1,product=LSDJ_SLOT_PRODUCT,cartridge='empty LSDj slot',
                      compatibility='experimental generic; verified cursor profile only, other memory helpers disabled')
    if ble:result.update(bluetooth='BLE-MIDI device (firmware/lsdj_ble.c)',ram_text_bytes=sym('_dsp_end')-0x1c00000,
        sram_free=0x1c74000-sym('_cpu1_stacks_hi'))
    if a.target=='smk37':result.update(usb_protocol='smk37-ram-v2',product=loader['product'],loader=loader)
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
