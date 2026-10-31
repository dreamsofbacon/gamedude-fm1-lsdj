"""Build the public LSDj firmware template: GameDude for FM-1 with an EMPTY ROM slot.

The sibling Burrito website writes each user's own LSDj into the slot locally.
The generic experimental template has identity FM-1_5370995, short name FM-1_995 (note mode sets LSDj's SYNC to KEYBD; no Bluetooth; its own firmware number; every sample kit fits: banks stored without their 0xFF filler; the FM-1 pushes each new screen picture to the web page; SCREEN RATE setting: the FM-1 draws 10, 15, 20 or 30 pictures a second; screen mirror rows sent back to back; screen mirror from the LCD image; other LSDj versions: cursor FX and LFO tempo, custom palettes accepted; faster screen mirror; knobs / SELECT: channel pages, FX: master pages with the echo; screen mirror for the web page; AUTOSAVE; echo mix and time on MASTER, settings knob fix; echo send on FILTER K2, FX+LIGHTS switches; hold-FX master page; all LSDj 9.3.9 helpers for the
verified program, so SEQ, tempo, cues and MIDI sync work) with a master FX bypass
and reorganized controls. It supports read-only cartridge export for ROM-free updates
and retains cursor FX support
for a verified LSDj 9.3.9 program, without the channel tint. It includes the
alpha 71 display and CPU1 save fixes. Local ROM builds (never published) keep FM-1_9NN in firmware/fm1_recovery.h.
This template contains no LSDj ROM.

Run in WSL from the GameDude root, with the FM-1 toolchain and AC79 SDK:
  python3 tools/lsdj_template.py --toolchain /root/.jieli/toolchain --sdk /root/ac79-sdk
writes build/lsdj-template/fm1-lsdj-template.fwsc and template.json.
Packaging follows tools/package_fm1.py (same checks), without its author-private
restore-package round trip. Never accesses a device."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from package_fm1 import wrap,sha,SDK_HASHES,pkg,wire
APP_MAX=0x8dfbc   # FM-1 app limit (tools/build.py)
HDR,BANK=256,16384

def main():
    ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--toolchain',required=True);ap.add_argument('--sdk',required=True,type=Path)
    ap.add_argument('--output-dir',type=Path,default=ROOT/'build/lsdj-template')
    a=ap.parse_args()
    for rel,digest in SDK_HASHES.items():
        assert sha((a.sdk/'cpu/wl82/tools'/rel).read_bytes())==digest,rel+' SDK revision mismatch'
    def build(slot):
        subprocess.run([sys.executable,'tools/build.py','fm1','--toolchain',a.toolchain,'--lsdj-slot',str(slot)],cwd=ROOT,check=True,
                       stdout=subprocess.DEVNULL)
        return json.loads((ROOT/'build/fm1/manifest.json').read_text())
    # Pass 1: a one-bank slot, to see how much room is left. (Not an empty one: with
    # nothing after the header the compiler drops the code that reads past it.)
    code=build(HDR+BANK)['app_bytes']-BANK
    # FM-1_991 (slot version 2): banks are stored without their 0xFF filler, so the slot is
    # every byte left over (in 256-byte steps), not a whole number of banks.
    slot=(APP_MAX-code-64)//256*256
    assert slot>=HDR+13*BANK*3//4,'Not even LSDj\'s code banks fit'
    m=build(slot)
    print(f'LSDj slot: {slot} bytes ({(slot-HDR)/BANK:.2f} banks\' worth); app {m["app_bytes"]} of {APP_MAX} bytes')

    # Package (as tools/package_fm1.py).
    def tc(tool,*args):return subprocess.check_output([a.toolchain+'/'+tool,*map(str,args)],cwd=ROOT,text=True)
    out=ROOT/'build/fm1';fw='vendor/felucca/firmware'
    flags=['-target','pi32v2','-Os','-ffunction-sections','-fno-builtin','-Wall','-Wno-unused-function','-I'+fw+'/hal','-I'+fw+'/src']
    for stem in ['crt0_ldr','loader']:
        tc('pi32v2/bin/clang',*flags,'-c',fw+'/loader/'+stem+('.S' if stem=='crt0_ldr' else '.c'),'-o','build/fm1/'+stem+'.o')
    tc('pi32v2/bin/ld','--gc-sections','-e','_start','-T',fw+'/loader/loader.ld','build/fm1/crt0_ldr.o','build/fm1/loader.o','-o','build/fm1/loader.elf')
    for line in tc('common/bin/objdump','-h','build/fm1/loader.elf').splitlines():
        parts=line.split()
        if len(parts)>4 and parts[1].startswith('.') and int(parts[2],16):assert 0x1c00000<=int(parts[3],16)<0x1c80000,line
    tc('common/bin/objcopy','-O','binary','-j','.text','build/fm1/loader.elf','build/fm1/loader.bin')
    loader=wrap((out/'loader.bin').read_bytes());assert len(loader)<=0x4f00
    app=(out/'gb-fm1-NOT-FOR-INSTALL.bin').read_bytes()
    assert m['sha256']==sha(app) and m['usb_updater'] and m.get('lsdj_slot')==1
    product=m['product']
    assert product.encode() in app,'Template identity mismatch'
    offset,capacity=m['cartridge_offset'],m['cartridge_capacity']
    assert app[offset:offset+capacity]==b'\xff'*capacity,'The ROM slot must be empty'
    pkg.SDK=a.sdk;flash=pkg.flash_image(app,pkg.KEY);raw=pkg.ufw(flash,loader,product)
    assert wire.product_of(raw)==product
    assert wire.logical_image(raw)[0x400:0x400+len(flash)]==flash
    area=bytearray(flash[0x4000:]);pkg.sfc(area,0,len(area),0,pkg.KEY)
    assert area[0x120:0x120+len(app)]==app
    ota=out/'ota-test'
    subprocess.run(['gcc','-O1','-UNDEBUG','-w','vendor/felucca/tests/ota_test.c','-o',ota],cwd=ROOT,check=True)
    web=a.output_dir;web.mkdir(parents=True,exist_ok=True)
    (web/'fm1-lsdj-template.fwsc').write_bytes(raw)
    print(subprocess.check_output([ota,web/'fm1-lsdj-template.fwsc'],cwd=ROOT,text=True).strip())
    meta={'product':product,'version':m['version'],'package':'fm1-lsdj-template.fwsc','sha256':sha(raw),
          'cartridge_offset':offset,'cartridge_capacity':capacity,'lsdj_slot':1,
          'usb_updater':True,'usb_protocol':'fm1-ota-v1',
          'protected_app_sha256':hashlib.sha256(app[:offset]+app[offset+capacity:]+b'\xff'*(APP_MAX-len(app))).hexdigest(),
          'loader_sha256':sha(loader),'app_bytes':len(app),'experimental':True,
          'hardware_validated':False,'generic_rom':True,
          'notice':'Empty LSDj slot. Each user\'s own LSDj is inserted locally in the browser.'}
    (web/'template.json').write_text(json.dumps(meta,indent=2)+'\n')
    print(json.dumps(meta,indent=2))
if __name__=='__main__':main()
