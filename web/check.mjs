// Compares the WASM generator against the repository's other builds of the
// same C code:
//
//  1. The golden boards in test/host/golden/ (byte-exact).
//  2. The gcc-built oracle (test/host/build/run_tests, BOARD_SEED mode)
//     across many seeds, bingo targets, and disabled-objective sets.
//
// Run via `make check` (which builds both sides first).

import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import createModule from './site/dist/bingo64gen.mjs';

const ORACLE_CWD = '../test/host';
const ORACLE = 'build/run_tests'; // relative to ORACLE_CWD
const SWEEP_SEEDS = 200;
const OPTION_SWEEP_SEEDS = 60;

const M = await createModule();
const NUM_TYPES = M._gen_num_objective_types();

// The dump contains raw in-game glyph bytes (e.g. 0xFA), which are not valid
// UTF-8, so read the C string byte-by-byte instead of via UTF8ToString.
function cStringBytes(ptr) {
  const heap = M.HEAPU8;
  let end = ptr;
  while (heap[end] !== 0) end++;
  return Buffer.from(heap.subarray(ptr, end));
}

function wasmDump(seed, target, disabledTypes) {
  let disabledPtr = 0;
  if (disabledTypes.length > 0) {
    disabledPtr = M._malloc(NUM_TYPES);
    M.HEAPU8.fill(0, disabledPtr, disabledPtr + NUM_TYPES);
    for (const t of disabledTypes) M.HEAPU8[disabledPtr + t] = 1;
  }
  const bytes = cStringBytes(M._gen_board_dump(seed, target, disabledPtr));
  if (disabledPtr) M._free(disabledPtr);
  return bytes;
}

function oracleDump(seed, target, disabledTypes) {
  const env = { ...process.env, BOARD_SEED: String(seed) };
  if (target !== 1) env.BOARD_TARGET = String(target);
  if (disabledTypes.length > 0) env.BOARD_DISABLE = disabledTypes.join(',');
  return execFileSync(ORACLE, { cwd: ORACLE_CWD, env });
}

// Deterministic PRNG so failures reproduce.
let rngState = 0x12345678;
function rng() {
  rngState = (Math.imul(rngState, 1103515245) + 12345) >>> 0;
  return rngState;
}

let failures = 0;
function compare(name, a, b) {
  if (Buffer.compare(a, b) === 0) return;
  failures++;
  console.error(`MISMATCH: ${name}`);
  const al = a.toString('latin1').split('\n');
  const bl = b.toString('latin1').split('\n');
  for (let i = 0; i < Math.max(al.length, bl.length); i++) {
    if (al[i] !== bl[i]) {
      console.error(`  wasm  : ${JSON.stringify(al[i])}`);
      console.error(`  oracle: ${JSON.stringify(bl[i])}`);
      break;
    }
  }
}

// 1. Goldens.
for (const seed of [1, 12345, 314159]) {
  const golden = readFileSync(`../test/host/golden/board_${seed}.txt`);
  compare(`golden seed ${seed}`, wasmDump(seed, 1, []), golden);
}
console.log('golden boards: checked 3');

// 2. Seed sweep against the oracle, default options.
for (let i = 0; i < SWEEP_SEEDS; i++) {
  const seed = rng() % 1000000000;
  compare(`seed ${seed}`, wasmDump(seed, 1, []), oracleDump(seed, 1, []));
}
console.log(`seed sweep: checked ${SWEEP_SEEDS}`);

// 3. Option sweep: random target and random disabled set per seed.
const TARGETS = [1, 2, 3, 12];
for (let i = 0; i < OPTION_SWEEP_SEEDS; i++) {
  const seed = rng() % 1000000000;
  const target = TARGETS[rng() % TARGETS.length];
  const disabled = [];
  const nDisable = rng() % 8;
  for (let j = 0; j < nDisable; j++) disabled.push(rng() % NUM_TYPES);
  const name = `seed ${seed} target ${target} disable [${disabled.join(',')}]`;
  compare(name, wasmDump(seed, target, disabled), oracleDump(seed, target, disabled));
}
console.log(`option sweep: checked ${OPTION_SWEEP_SEEDS}`);

if (failures > 0) {
  console.error(`FAILED: ${failures} mismatch(es)`);
  process.exit(1);
}
console.log('all checks passed');
