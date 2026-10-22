// SPDX-License-Identifier: GPL-3.0-only
// web/lsdj: ROM identification, kit listing, trimming (same bytes as
// tools/build.py sparse_pack) and packing into a firmware template.
// Uses a synthetic LSDj-shaped ROM; the real ROM is checked too if present.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {identifyRom, listKits, sparsePack, buildSlot, parseSlot, crc32, BANK, SLOT_HEADER} from '../web/lsdj/lsdj-rom.js';
import {prepareLsdjFirmware} from '../web/lsdj/lsdj-firmware.js';
import {Package, sha256hex} from '../web/vendor/fm1pkg.js';

const root = new URL('..', import.meta.url).pathname;
const versions = JSON.parse(fs.readFileSync(root + 'web/lsdj/versions.json', 'utf8'));

// 1 MiB MBC5 ROM: 4 code banks, kits at 8..10 (+ one at 0x20), the rest empty.
function synthetic() {
  const rom = new Uint8Array(64 * BANK).fill(0xff);
  for (const b of [0, 1, 2, 3, 0x1b]) for (let i = 0; i < BANK; i++) rom[b * BANK + i] = (i * 13 + b) & 255;
  ['TR-606', 'TR-808', 'CR-78 ', 'KIT--4'].forEach((name, k) => {
    const b = [8, 9, 10, 0x20][k], at = b * BANK;
    for (let i = 0; i < BANK; i++) rom[at + i] = (i * 7 + k) & 255;
    rom.set([0x60, 0x40], at); rom.set([...name].map(c => c.charCodeAt(0)), at + 0x52);
  });
  rom.fill(0, 0x134, 0x150); rom.set([...'LSDj-v9.9.9'].map(c => c.charCodeAt(0)), 0x134);
  rom[0x147] = 0x1b; rom[0x148] = 5; rom[0x149] = 4;
  let s = 0; for (let i = 0x134; i < 0x14d; i++) s = (s - rom[i] - 1) & 255; rom[0x14d] = s;
  return rom;
}
const rom = synthetic();

// Identify.
const id = await identifyRom(rom, versions);
assert.equal(id.title, 'LSDj-v9.9.9'); assert.equal(id.tested, false); assert.equal(id.version, '9.9.9');
assert.equal((await identifyRom(rom, [{version: 'x', sha256: await sha256hex(rom)}])).tested, true);
const notLsdj = rom.slice(); notLsdj[0x134] = 0x41; await assert.rejects(identifyRom(notLsdj, versions), /not an LSDj/);
const badSum = rom.slice(); badSum[0x14d] ^= 1; await assert.rejects(identifyRom(badSum, versions), /checksum/);
await assert.rejects(identifyRom(rom.subarray(0, 3 * BANK), versions), /size/);

// Kits.
assert.deepEqual(listKits(rom).map(k => [k.bank, k.name]), [[8, 'TR-606'], [9, 'TR-808'], [10, 'CR-78'], [0x20, 'KIT--4']]);

// Trimming matches the Python packer (first-n kits) byte for byte.
function python(romFile, n) {
  const code = 'import sys,json;sys.path.insert(0,"tools");from build import sparse_pack;' +
    `s,m,k=sparse_pack(open(${JSON.stringify(romFile)},"rb").read(),${n});sys.stdout.write(json.dumps([s.hex(),m,k]))`;
  const r = spawnSync('python3', ['-c', code], {cwd: root, maxBuffer: 1 << 26, encoding: 'utf8'});
  if (r.status !== 0) throw Error(r.stderr);
  const [hex, map, kept] = JSON.parse(r.stdout);
  return {storage: Uint8Array.from(Buffer.from(hex, 'hex')), bankMap: map, kept};
}
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lsdj-rom-'));
function parity(bytes, label) {
  const file = path.join(tmp, 'rom.gb'); fs.writeFileSync(file, bytes);
  const kits = listKits(bytes).map(k => k.bank);
  for (const n of [0, 1, 3, kits.length]) {
    const js = sparsePack(bytes, kits.slice(0, n)), py = python(file, n);
    assert.deepEqual(js.bankMap, py.bankMap, `${label} map, ${n} kits`);
    assert.equal(js.kept, py.kept); assert.ok(Buffer.from(js.storage).equals(Buffer.from(py.storage)), `${label} bytes, ${n} kits`);
  }
}
parity(rom, 'synthetic');
const realRom = path.join(root, '../../roms/lsdj9_3_9.gb');
if (fs.existsSync(realRom)) {
  const real = new Uint8Array(fs.readFileSync(realRom));
  const rid = await identifyRom(real, versions);
  assert.equal(rid.tested, true); assert.equal(rid.version, '9.3.9');
  assert.equal(listKits(real).length, 21); assert.equal(listKits(real)[3].name, 'TR-808');
  parity(real, 'lsdj 9.3.9');
  console.log('  real LSDj 9.3.9: identified as tested, 21 kits, trimming matches Python');
}

// Kits can be chosen freely, not just the first n.
const pick = sparsePack(rom, [10]);
assert.equal(pick.kept, 1); assert.equal(pick.bankMap[8], 0xff); assert.equal(pick.bankMap[9], 0xff); assert.notEqual(pick.bankMap[10], 0xff);

// Slot round trip and the C parser's rules.
const cap = SLOT_HEADER + 8 * BANK;
const {slot, stored, kept} = buildSlot(rom, [8, 0x20], cap);
assert.equal(stored, 7); assert.equal(kept, 2);
const back = parseSlot(slot, cap);
assert.equal(back.stored, 7); assert.equal(back.logical, 64); assert.equal(back.kits, 2);
assert.equal(back.map[0], 0); assert.equal(back.map[9], 0xff);
assert.throws(() => buildSlot(rom, [8, 9, 10, 0x20], cap), /Too many kits/);
const flipped = slot.slice(); flipped[SLOT_HEADER + 5] ^= 1; assert.equal(parseSlot(flipped, cap).error, 'data');
const header = slot.slice(); header[30] ^= 1; assert.equal(parseSlot(header, cap).error, 'header');
assert.equal(parseSlot(new Uint8Array(cap).fill(0xff), cap).error, 'empty');

// The same bytes as the C slot test layout expects: CRC fields cover what they say.
assert.equal(new DataView(slot.buffer).getUint32(16, true), crc32(slot.subarray(SLOT_HEADER)));

// Pack into a firmware template. Until tools/lsdj_template.py has been run on a
// machine with the FM-1 toolchain, GameDude's public template stands in: same
// package format and checks, a 512 KiB window.
const lsdjMeta = root + 'web/lsdj/template.json';
const [metaFile, pkgDir] = fs.existsSync(lsdjMeta) ? [lsdjMeta, 'web/lsdj/'] : [root + 'web/firmware/template.json', 'web/firmware/'];
const meta = {...JSON.parse(fs.readFileSync(metaFile, 'utf8')), lsdj_slot: 1};
const template = new Uint8Array(fs.readFileSync(root + pkgDir + meta.package));
const result = await prepareLsdjFirmware(rom, [8, 9, 10, 0x20], template, meta);
const before = new Package(template), after = new Package(result.bytes);
const a = before.dec.subarray(before.appOff, before.appOff + before.app.size), b = after.dec.subarray(after.appOff, after.appOff + after.app.size);
const off = meta.cartridge_offset, end = off + meta.cartridge_capacity;
assert.ok(Buffer.from(a.subarray(0, off)).equals(Buffer.from(b.subarray(0, off))), 'code before the slot unchanged');
assert.ok(Buffer.from(a.subarray(end)).equals(Buffer.from(b.subarray(end))), 'code after the slot unchanged');
assert.equal(parseSlot(b.subarray(off, end), meta.cartridge_capacity).stored, 9);
assert.ok(b.subarray(off + result.slotBytes, end).every(x => x === 0xff));
await assert.rejects(prepareLsdjFirmware(rom, [], template, {...meta, lsdj_slot: undefined}), /not an LSDj firmware template/);
const broken = template.slice(); broken[broken.length - 1] ^= 1;
await assert.rejects(prepareLsdjFirmware(rom, [], broken, meta), /integrity/);
fs.rmSync(tmp, {recursive: true, force: true});
console.log(`PASS: LSDj ROM identify/kits/trim (matches Python), slot format, template packing (${pkgDir === 'web/lsdj/' ? 'LSDj' : 'stand-in GameDude'} template).`);
