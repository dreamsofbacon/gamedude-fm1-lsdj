// SPDX-License-Identifier: GPL-3.0-only
// web/lsdj/lsdj-save.js against the real firmware handler (firmware/lsdj_sysex.h,
// via tests/lsdj_sysex_bridge.c). Needs gcc; skipped without it.
import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {exportSave, importSave, checkSav, saveInfo, SAVE_SIZE} from '../web/lsdj/lsdj-save.js';
import {crc32} from '../web/lsdj/lsdj-rom.js';

const root = new URL('..', import.meta.url).pathname;
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lsdj-save-'));
const bridge = path.join(tmp, 'bridge');
if (spawnSync('gcc', ['-std=c99', '-O2', '-Wall', '-Werror', '-I' + root + 'firmware', root + 'tests/lsdj_sysex_bridge.c', '-o', bridge]).status !== 0) {
  console.log('SKIP: lsdj_save.mjs needs gcc'); process.exit(0);
}

// A fake LSDj save: 128 KiB with the "jk" marker.
function sav(seed) {
  const b = new Uint8Array(SAVE_SIZE);
  for (let i = 0; i < b.length; i++) b[i] = (i * seed + (i >> 9)) & 255;
  b[0x813e] = 0x6a; b[0x813f] = 0x6b; return b;
}
const first = sav(13), second = sav(29);
fs.writeFileSync(path.join(tmp, 'start.sav'), first);

// A vendor/fm1ota.js-style Link over the bridge's pipes.
function link(child) {
  let buf = [], packets = [], waiter = null;
  child.stdout.on('data', d => {
    for (const b of d) {
      if (b === 0xf0) buf = [];
      buf.push(b);
      if (b === 0xf7) { packets.push(buf); buf = []; waiter?.(); }
    }
  });
  return {
    send(bytes) { return child.stdin.write(Buffer.from(bytes)); },
    drain() { packets = []; },
    read(ms) {
      if (packets.length) return Promise.resolve(packets.shift());
      return new Promise(resolve => {
        const t = setTimeout(() => { waiter = null; resolve(null); }, ms);
        waiter = () => { clearTimeout(t); waiter = null; resolve(packets.shift()); };
      });
    },
  };
}

const child = spawn(bridge, [path.join(tmp, 'start.sav')]);
try {
  const l = link(child);
  const info = await saveInfo(l);
  assert.equal(info.size, SAVE_SIZE); assert.equal(info.crc, crc32(first)); assert.equal(info.importing, false);

  let ticks = 0;
  const exported = await exportSave(l, () => ticks++);
  assert.deepEqual(exported, first); assert.equal(ticks, SAVE_SIZE / 128);

  await importSave(l, second);
  assert.deepEqual(await exportSave(l), second);

  assert.throws(() => checkSav(second.subarray(0, 1000)), /128 KiB/);
  const notLsdj = second.slice(); notLsdj[0x813e] = 0;
  assert.throws(() => checkSav(notLsdj), /not an LSDj save/);
  await assert.rejects(importSave(l, notLsdj), /not an LSDj save/);
  assert.deepEqual(await exportSave(l), second);       // a rejected import changes nothing

  // The real jelly.sav, if it is here (never committed).
  const real = path.join(root, '../../roms/jelly.sav');
  if (fs.existsSync(real)) {
    const bytes = new Uint8Array(fs.readFileSync(real));
    checkSav(bytes); await importSave(l, bytes); assert.deepEqual(await exportSave(l), bytes);
    console.log('  real jelly.sav: import + export round trip ok');
  }
  console.log('PASS: LSDj save export/import against the firmware handler, marker/size checks, rejected import leaves the save alone.');
} finally {
  child.stdin.end(); child.kill(); fs.rmSync(tmp, {recursive: true, force: true});
}
