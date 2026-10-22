// SPDX-License-Identifier: GPL-3.0-only
// LSDj packed by the web installer boots in GameDude's core like the full ROM.
// Needs gcc and a local LSDj ROM (LSDJ_ROM, default ../../roms/lsdj9_3_9.gb); skipped otherwise.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {buildSlot, listKits, SLOT_HEADER, BANK} from '../web/lsdj/lsdj-rom.js';

const root = new URL('..', import.meta.url).pathname;
const romPath = process.env.LSDJ_ROM || path.join(root, '../../roms/lsdj9_3_9.gb');
if (!fs.existsSync(romPath)) { console.log('SKIP: lsdj_boot.mjs needs a local LSDj ROM'); process.exit(0); }
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lsdj-boot-')), exe = path.join(tmp, 'boot');
// The firmware's patched APU (tools/prepare_apu.py), as tools/build.py makes it.
spawnSync('python3', ['-c', 'import sys;from pathlib import Path;sys.path.insert(0,"tools");from prepare_apu import prepare;prepare(Path("."))'], {cwd: root});
const cc = spawnSync('gcc', ['-std=gnu99', '-O2', '-w', '-DMINIGB_APU_AUDIO_FORMAT_S16SYS', '-DAUDIO_SAMPLE_RATE=44100',
  '-I' + root + 'src', '-I' + root + 'vendor/peanut-gb', root + 'tests/lsdj_slot_boot.c', root + 'src/gb_port.c', root + 'build/minigb_apu.c', '-o', exe], {encoding: 'utf8'});
if (cc.status !== 0) { console.log('SKIP: lsdj_boot.mjs could not build (gcc / build/minigb_apu.c):\n' + cc.stderr); process.exit(0); }

const run = (...args) => {
  const r = spawnSync(exe, args, {encoding: 'utf8'});
  assert.equal(r.status, 0, r.stdout + r.stderr);
  return r.stdout;
};
const screen = out => out.match(/screen (\w+)/)[1];
const rom = new Uint8Array(fs.readFileSync(romPath)), kits = listKits(rom).map(k => k.bank);
const full = run('rom', romPath, '1500', path.join(tmp, 'full.ppm'));
for (const keep of [[], kits.slice(0, 3), kits.slice(5, 7)]) {
  const {slot, stored} = buildSlot(rom, keep, SLOT_HEADER + 64 * BANK);
  const file = path.join(tmp, 'slot.bin'); fs.writeFileSync(file, slot);
  const out = run('slot', file, '1500', path.join(tmp, 'slot.ppm'));
  assert.match(out, new RegExp(`${stored} stored, ${keep.length} kits`));
  assert.equal(screen(out), screen(full), `LSDj screen after 1500 frames, kits ${keep}`);
}
// A damaged slot is refused, as the firmware refuses it.
const {slot} = buildSlot(rom, [], SLOT_HEADER + 64 * BANK); slot[SLOT_HEADER + 100] ^= 1;
fs.writeFileSync(path.join(tmp, 'bad.bin'), slot);
const bad = spawnSync(exe, ['slot', path.join(tmp, 'bad.bin'), '1', path.join(tmp, 'x.ppm')], {encoding: 'utf8'});
assert.equal(bad.status, 1); assert.match(bad.stdout, /slot parse error -4/);
if (process.env.KEEP_PPM) fs.copyFileSync(path.join(tmp, 'slot.ppm'), process.env.KEEP_PPM);
fs.rmSync(tmp, {recursive: true, force: true});
console.log(`PASS: LSDj from a web-packed slot boots to the same screen as the full ROM (0, 3 and 2 kits); damaged slot refused.`);
