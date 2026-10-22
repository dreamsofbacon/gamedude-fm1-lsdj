// SPDX-License-Identifier: GPL-3.0-only
// Build and run the LSDj host C tests (slot format, save SysEx, flash save slots). Needs gcc.
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const root = new URL('..', import.meta.url).pathname;
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lsdj-c-'));
if (spawnSync('gcc', ['--version']).status !== 0) { console.log('SKIP: lsdj_c.mjs needs gcc'); process.exit(0); }
let failed = 0;
for (const [test, include] of [['lsdj_slot', 'src'], ['lsdj_sysex', 'firmware'], ['lsdj_save', 'firmware']]) {
  const exe = path.join(tmp, test);
  const cc = spawnSync('gcc', ['-std=c99', '-O2', '-Wall', '-Wextra', '-I' + root + include, root + `tests/${test}.c`, '-o', exe], {encoding: 'utf8'});
  const r = cc.status === 0 ? spawnSync(exe, {encoding: 'utf8'}) : cc;
  process.stdout.write(r.stdout + r.stderr);
  if (r.status !== 0) { failed++; console.log(`FAIL: ${test}`); }
}
fs.rmSync(tmp, {recursive: true, force: true});
process.exit(failed ? 1 : 0);
