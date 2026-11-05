#!/usr/bin/env node
// Render a preset to a 24-bit mono WAV with the JS model.
//   node js/render.mjs presets/kick.txt kick.wav [--seconds 8] [--rate 48000] [key=value ...]
//   node js/render.mjs --keys | --circuit
import { readFileSync, writeFileSync } from 'node:fs';
import { CIRCUIT_INFO, DEFAULT_PATCH, Dfam, formatValue, parsePreset } from './dfam.js';

const args = process.argv.slice(2);
if (args[0] === '--keys') {
  console.log(Object.keys(DEFAULT_PATCH).sort().join('\n'));
  process.exit(0);
}
if (args[0] === '--circuit') {  // estimated component values, as preset lines
  for (const [k, v, , help] of CIRCUIT_INFO) console.log(`${k} = ${formatValue(v)}`.padEnd(31) + ' # ' + help);
  process.exit(0);
}
if (args.length < 2) {
  console.error('usage: node js/render.mjs <patch.txt> <out.wav> [--seconds N] [--rate HZ] [key=value ...]\n       node js/render.mjs --keys | --circuit');
  process.exit(1);
}
const [presetPath, outPath, ...rest] = args;
let text = readFileSync(presetPath, 'utf8'), seconds = 8, rate = 48000;
for (let i = 0; i < rest.length; i++) {
  if (rest[i] === '--seconds') seconds = Number(rest[++i]);
  else if (rest[i] === '--rate') rate = Number(rest[++i]);
  else text += '\n' + rest[i];
}
const synth = new Dfam(rate, parsePreset(text));
const out = new Float32Array(Math.round(seconds * rate));
const t0 = performance.now();
synth.render(out);
const ms = performance.now() - t0;

const n = out.length, buf = Buffer.alloc(44 + n * 3);
buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 3, 4); buf.write('WAVEfmt ', 8);
buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(1, 22);
buf.writeUInt32LE(rate, 24); buf.writeUInt32LE(rate * 3, 28); buf.writeUInt16LE(3, 32);
buf.writeUInt16LE(24, 34); buf.write('data', 36); buf.writeUInt32LE(n * 3, 40);
let peak = 0;
for (let i = 0; i < n; i++) {
  const s = Math.max(-1, Math.min(1, out[i]));
  peak = Math.max(peak, Math.abs(s));
  buf.writeIntLE(Math.round(s * 8388607), 44 + i * 3, 3);
}
writeFileSync(outPath, buf);
console.log(`wrote ${outPath} (${seconds} s, peak ${peak.toFixed(2)}, ${(seconds * 1000 / ms).toFixed(1)}x real time)`);
