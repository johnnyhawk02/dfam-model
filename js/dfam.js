// DFAM component model, JavaScript port of include/dfam/*.hpp.
//
// Same circuits, constants and random draws as the C++ code, so a patch and
// seed render (almost) the same samples in both. Runs in Node and in an
// AudioWorklet. Unlike the C++ version, setPatch() can change knobs live
// without resetting the circuit state, which is what you want for prototyping.
//
// Patch keys match the preset files (see DEFAULT_PATCH and CIRCUIT_INFO).

// Estimated component values (the C++ dfam::Circuit struct). Moog has not
// published DFAM schematics, so these are guesses, exposed for fitting against
// a real unit. SI units. "_tol" is relative tolerance (~3 sigma), "_sd" is
// 1 sigma. live: true means setPatch() applies it without rebuilding the
// circuit; the others are used when a Dfam is constructed.
// [key, default, group, description, live]
export const CIRCUIT_INFO = Object.freeze([
  ['vco_cap', 4.7e-9, 'VCO', 'VCO timing capacitor, F', false],
  ['vco_cap_tol', 0.02, 'VCO', 'VCO cap tolerance (relative, ~3 sigma)', false],
  ['vco_threshold', 5, 'VCO', 'VCO comparator thresholds, +/- V', false],
  ['vco_comp_delay', 150e-9, 'VCO', 'VCO comparator + switch delay, s', false],
  ['vco_comp_delay_tol', 0.2, 'VCO', 'comparator delay tolerance', false],
  ['vco_max_hz', 22000, 'VCO', 'VCO expo current limit, Hz', true],
  ['pitch_eg_octaves', 5, 'VCO', 'pitch EG amount at full knob, octaves', true],
  ['vco_expo_rbulk', 2, 'Expo', 'VCO expo bulk emitter resistance, ohm', false],
  ['vcf_expo_rbulk', 2, 'Expo', 'VCF expo bulk emitter resistance, ohm', false],
  ['expo_rbulk_tol', 0.2, 'Expo', 'bulk resistance tolerance', false],
  ['expo_vbe_offset_sd', 0.0003, 'Expo', 'expo pair Vbe mismatch, V (1 sigma)', false],
  ['expo_scale_err_sd', 0.002, 'Expo', '1 V/oct trim error (relative, 1 sigma)', false],
  ['trim_temperature', 25, 'Expo', 'temperature the expo scale was trimmed at, C', false],
  ['mixer_gain', 1.6, 'Mixer', 'mixer summer gain', true],
  ['opamp_rail', 10.5, 'Mixer', 'op-amp output swing (mixer, VCF out), +/- V', true],
  ['noise_amp', 2, 'Mixer', 'noise source level, V rms', true],
  ['noise_bw', 16000, 'Mixer', 'noise low-pass corner, Hz', false],
  ['vcf_cap', 68e-9, 'VCF', 'ladder stage capacitors, F', false],
  ['vcf_cap_tol', 0.05, 'VCF', 'ladder cap tolerance', false],
  ['vcf_max_hz', 45000, 'VCF', 'VCF control current limit, Hz', true],
  ['vcf_eg_octaves', 8, 'VCF', 'VCF EG amount at full knob, octaves', true],
  ['noise_vcf_scale', 1.5, 'VCF', 'noise -> VCF CV, V per unit knob', true],
  ['resonance_max', 1.15, 'VCF', 'ladder loop gain at full resonance', true],
  ['ladder_in', 0.005, 'VCF', 'mixer V -> ladder input V (drive)', true],
  ['ladder_noise', 2e-7, 'VCF', 'ladder input thermal noise, V rms', true],
  ['eg_cap', 2.2e-6, 'Envelopes', 'envelope capacitors, F', false],
  ['eg_cap_tol', 0.1, 'Envelopes', 'envelope cap tolerance', false],
  ['eg_supply', 12, 'Envelopes', 'envelope attack supply, V', false],
  ['eg_diode', 0.6, 'Envelopes', 'envelope steering diode drop, V', false],
  ['eg_peak', 8, 'Envelopes', 'envelope peak (comparator), V; keep below supply - diode', false],
  ['eg_attack_r', 470, 'Envelopes', 'fast attack resistor, ohm', true],
  ['vca_slow_attack_r', 22e3, 'Envelopes', 'VCA slow attack resistor, ohm', true],
  ['decay_pot_r', 1e6, 'Envelopes', 'decay pot, ohm', true],
  ['decay_min_r', 1e3, 'Envelopes', 'decay end-stop resistor, ohm', true],
  ['decay_taper', 100, 'Envelopes', 'decay pot audio-taper base', true],
  ['vca_rabc', 47e3, 'VCA', 'VCA envelope -> Iabc resistor, ohm', true],
  ['vca_div_top', 33e3, 'VCA', 'VCA input divider top, ohm', true],
  ['vca_div_bottom', 220, 'VCA', 'VCA input divider bottom, ohm', true],
  ['vca_rf', 30e3, 'VCA', 'VCA I-to-V feedback resistor, ohm', true],
  ['vca_feedthrough_sd', 0.004, 'VCA', 'VCA control feedthrough (1 sigma)', false],
  ['output_hp', 8, 'Output', 'output coupling high-pass, Hz', false],
].map(Object.freeze));
export const DEFAULT_CIRCUIT = Object.freeze(Object.fromEntries(CIRCUIT_INFO.map(([k, v]) => [k, v])));
// Circuit keys that need a new Dfam to take effect.
// Number as preset text: 4.7e-9 rather than 0.0000000047.
export const formatValue = (v) => (v !== 0 && Math.abs(v) < 1e-4 ? v.toExponential() : String(v));
export const CIRCUIT_REBUILD_KEYS = Object.freeze(CIRCUIT_INFO.filter((e) => !e[4]).map((e) => e[0]));

export const DEFAULT_PATCH = Object.freeze({
  tempo: 120, steps_per_beat: 4,
  pitch: [0, 0, 0, 0, 0, 0, 0, 0],        // volts, 1 V/oct
  velocity: [1, 1, 1, 1, 1, 1, 1, 1],     // 0..1, 0 = rest
  seq_pitch_mod: 'both',                  // 'both' | 'off' | 'vco2'
  vco1_freq: 55, vco2_freq: 110,          // Hz at 0 V
  vco1_wave: 'triangle', vco2_wave: 'square',
  vco_decay: 0.3, vco1_eg_amount: 0.4, vco2_eg_amount: 0,
  fm_1_2: 0, hard_sync: false,
  vco1_level: 0.8, vco2_level: 0, noise_level: 0,
  vcf_mode: 'lp', cutoff: 600, resonance: 0.3,
  vcf_eg_amount: 0.5, vcf_decay: 0.3, noise_vcf_mod: 0,
  vca_attack: 'fast', vca_decay: 0.35, volume: 0.8,
  temperature: 25, seed: 1,
  ...DEFAULT_CIRCUIT,
});

// ---------------------------------------------------------------- components

const K_BOLTZMANN = 1.380649e-23, K_CHARGE = 1.602176634e-19, PI = Math.PI;
export const thermalVoltage = (tempK) => (K_BOLTZMANN * tempK) / K_CHARGE;
const clamp = (x, a, b) => Math.min(Math.max(x, a), b);

// xorshift64 on two uint32 halves; bit-identical to the C++ Rng.
export class Rng {
  constructor(seed = 1) {
    const s = (BigInt.asUintN(64, BigInt(seed)) * 0x9E3779B97F4A7C15n + 1n) & 0xFFFFFFFFFFFFFFFFn;
    this.hi = Number(s >> 32n) >>> 0;
    this.lo = Number(s & 0xFFFFFFFFn) >>> 0;
    this.spare = 0;
    this.haveSpare = false;
  }
  next() {
    let hi = this.hi, lo = this.lo, h, l;
    h = ((hi << 13) | (lo >>> 19)) >>> 0; l = (lo << 13) >>> 0;   // s ^= s << 13
    hi = (hi ^ h) >>> 0; lo = (lo ^ l) >>> 0;
    l = ((lo >>> 7) | (hi << 25)) >>> 0; h = hi >>> 7;             // s ^= s >> 7
    hi = (hi ^ h) >>> 0; lo = (lo ^ l) >>> 0;
    h = ((hi << 17) | (lo >>> 15)) >>> 0; l = (lo << 17) >>> 0;   // s ^= s << 17
    this.hi = (hi ^ h) >>> 0; this.lo = (lo ^ l) >>> 0;
  }
  uniform() {
    this.next();
    return (this.hi * 2097152 + (this.lo >>> 11)) / 9007199254740992;
  }
  gaussian() {
    if (this.haveSpare) { this.haveSpare = false; return this.spare; }
    const u1 = Math.max(this.uniform(), 1e-300), u2 = this.uniform();
    const r = Math.sqrt(-2 * Math.log(u1));
    this.spare = r * Math.sin(2 * PI * u2);
    this.haveSpare = true;
    return r * Math.cos(2 * PI * u2);
  }
  tol(nominal, relTol) { return nominal * (1 + (this.gaussian() * relTol) / 3); }
}

// Matched-pair expo converter with bulk emitter resistance (see components.hpp).
export class ExpoConverter {
  setup(iRef, tempK, trimTempK, rE, vbeOffset, scaleErr) {
    this.iRef = iRef;
    this.vt = thermalVoltage(tempK);
    this.vPerVolt = thermalVoltage(trimTempK) * Math.log(2) * (1 + scaleErr);
    this.rE = rE;
    this.vbeOffset = vbeOffset;
  }
  current(cv, iMax) {
    const dV = cv * this.vPerVolt + this.vbeOffset, vt = this.vt, rI = this.rE * this.iRef;
    let x = Math.min(dV / vt, Math.log(iMax / this.iRef));
    for (let k = 0; k < 6; k++) {
      const e = Math.exp(x);
      const step = (vt * x + rI * (e - 1) - dV) / (vt + rI * e);
      x -= step;
      if (Math.abs(step) < 1e-9) break;
    }
    return Math.min(this.iRef * Math.exp(x), iMax);
  }
}

export const audioTaper = (knob, rMax, base = 100) =>
  (rMax * (Math.pow(base, clamp(knob, 0, 1)) - 1)) / (base - 1);

export function opampRail(v, rail) {
  const knee = 0.85 * rail, a = Math.abs(v);
  if (a <= knee) return v;
  return Math.sign(v) * (knee + (rail - knee) * Math.tanh((a - knee) / (rail - knee)));
}

class DcBlocker {
  constructor(fc, fs) { this.a = Math.exp((-2 * PI * fc) / fs); this.x1 = 0; this.y = 0; }
  process(x) { this.y = this.a * (this.y + x - this.x1); this.x1 = x; return this.y; }
}

class OnePoleLp {
  constructor(fc, fs) { this.a = 1 - Math.exp((-2 * PI * fc) / fs); this.y = 0; }
  process(x) { return (this.y += this.a * (x - this.y)); }
}

// ---------------------------------------------------------------- VCO

// Triangle core: integrator + Schmitt comparator, f = Ic / (4 C Vth).
export class TriangleCoreVco {
  constructor(fs, cap, vth, compDelay) {
    this.dt = 1 / fs; this.c = cap; this.vth = vth; this.td = compDelay;
    this.v = -vth; this.dir = 1;
    this.held = [0, 0]; this.nxt = [0, 0]; this.out = [0, 0];
  }
  addBlep(ch, d, delta) {
    this.held[ch] += delta * 0.5 * (1 - d) * (1 - d);
    this.nxt[ch] -= delta * 0.5 * d * d;
  }
  // Returns the sub-sample position where the square went high, or -1.
  process(ic, syncFrac = -1) {
    const slope = ic / this.c;
    const dvTotal = Math.max(slope * this.dt, 1e-12);
    const vthEff = this.vth + slope * this.td;
    let t = 0, rising = -1;
    const advanceTo = (tEnd) => {
      for (let guard = 0; guard < 32 && t < tEnd; guard++) {
        const remaining = (tEnd - t) * dvTotal;
        const target = this.dir * vthEff;
        const dist = (target - this.v) * this.dir;
        if (dist > remaining) { this.v += this.dir * remaining; t = tEnd; return; }
        const frac = t + Math.max(dist, 0) / dvTotal;
        this.v = target;
        this.dir = -this.dir;
        this.addBlep(1, frac, 10 * this.dir);
        if (this.dir > 0 && rising < 0) rising = frac;
        t = frac;
      }
      t = tEnd;
    };
    if (syncFrac >= 0 && syncFrac < 1) {
      advanceTo(syncFrac);
      const oldTri = this.v;
      this.v = -vthEff;
      if (this.dir < 0) { this.dir = 1; this.addBlep(1, syncFrac, 10); }
      this.addBlep(0, syncFrac, ((this.v - oldTri) * 5) / this.vth);
      advanceTo(1);
    } else {
      advanceTo(1);
    }
    this.out[0] = this.held[0]; this.out[1] = this.held[1];
    this.held[0] = (this.v * 5) / this.vth + this.nxt[0];
    this.held[1] = 5 * this.dir + this.nxt[1];
    this.nxt[0] = this.nxt[1] = 0;
    return rising;
  }
  triangle() { return this.out[0]; }
  square() { return this.out[1]; }
  triangleNow() { return (this.v * 5) / this.vth; }
}

// ---------------------------------------------------------------- ladder

// Four tanh differential-pair stages, trapezoidal rule + Newton (see ladder.hpp).
export class LadderFilter {
  constructor(fs, cap, tempK, seed = 1, inputNoise = 2e-7) {
    this.dt = 1 / fs; this.c = cap; this.vt = thermalVoltage(tempK); this.noise = inputNoise;
    this.rng = new Rng(seed);
    this.v = new Float64Array(4); this.x = new Float64Array(4);
    this.fOld = new Float64Array(4); this.f = new Float64Array(4);
    this.vinPrev = 0; this.u = 0;
    this.M = [new Float64Array(5), new Float64Array(5), new Float64Array(5), new Float64Array(5)];
    this.dx = new Float64Array(4);
  }
  currentForCutoff(hz) { return 2 * PI * hz * 4 * this.c * this.vt; }
  deriv(vin, x, g, k, f) {
    const s = 1 / (2 * this.vt);
    const t0 = Math.tanh(x[0] * s), t1 = Math.tanh(x[1] * s);
    const t2 = Math.tanh(x[2] * s), t3 = Math.tanh(x[3] * s);
    f[0] = g * (Math.tanh((vin - 4 * k * x[3]) * s) - t0);
    f[1] = g * (t0 - t1); f[2] = g * (t1 - t2); f[3] = g * (t2 - t3);
  }
  dT(x) { const t = Math.tanh(x / (2 * this.vt)); return (1 - t * t) / (2 * this.vt); }
  process(vin, current, k) {
    vin += this.rng.gaussian() * this.noise;  // input thermal noise
    const g = current / (2 * this.c), h = 0.5 * this.dt;
    const v = this.v, x = this.x, fOld = this.fOld, f = this.f, M = this.M, dx = this.dx;
    this.deriv(this.vinPrev, v, g, k, fOld);
    for (let i = 0; i < 4; i++) x[i] = v[i] + this.dt * fOld[i];
    for (let it = 0; it < 8; it++) {
      this.deriv(vin, x, g, k, f);
      const du = this.dT(vin - 4 * k * x[3]);
      const d0 = this.dT(x[0]), d1 = this.dT(x[1]), d2 = this.dT(x[2]), d3 = this.dT(x[3]);
      for (let i = 0; i < 4; i++) { M[i].fill(0); M[i][4] = x[i] - v[i] - h * (fOld[i] + f[i]); }
      M[0][0] = 1 + h * g * d0; M[0][3] = h * g * 4 * k * du;
      M[1][0] = -h * g * d0; M[1][1] = 1 + h * g * d1;
      M[2][1] = -h * g * d1; M[2][2] = 1 + h * g * d2;
      M[3][2] = -h * g * d2; M[3][3] = 1 + h * g * d3;
      solve4(M, dx);
      let maxStep = 0;
      for (let i = 0; i < 4; i++) { x[i] -= dx[i]; maxStep = Math.max(maxStep, Math.abs(dx[i])); }
      if (maxStep < 1e-10) break;
    }
    v.set(x);
    this.vinPrev = vin;
    this.u = vin - 4 * k * v[3];
  }
  lowpass() { return this.v[3]; }
  highpass() { const v = this.v; return this.u - 4 * v[0] + 6 * v[1] - 4 * v[2] + v[3]; }
}

// Gaussian elimination with partial pivoting on a 4x5 augmented matrix.
function solve4(M, out) {
  for (let c = 0; c < 4; c++) {
    let p = c;
    for (let r = c + 1; r < 4; r++) if (Math.abs(M[r][c]) > Math.abs(M[p][c])) p = r;
    if (p !== c) { const tmp = M[c]; M[c] = M[p]; M[p] = tmp; }
    for (let r = c + 1; r < 4; r++) {
      const m = M[r][c] / M[c][c];
      for (let j = c; j < 5; j++) M[r][j] -= m * M[c][j];
    }
  }
  for (let i = 3; i >= 0; i--) {
    let s = M[i][4];
    for (let j = i + 1; j < 4; j++) s -= M[i][j] * out[j];
    out[i] = s / M[i][i];
  }
}

// ---------------------------------------------------------------- EG and VCA

export class DecayEnvelope {
  constructor(fs, cap, vSupply = 12, diodeDrop = 0.6, vPeak = 8) {
    this.dt = 1 / fs; this.c = cap; this.vSrc = vSupply - diodeDrop; this.vPeak = vPeak;
    this.v = 0; this.srcNow = this.vSrc; this.peakNow = vPeak; this.attacking = false;
  }
  trigger(level = 1) {
    level = clamp(level, 0, 1);
    this.srcNow = this.vSrc * level;
    this.peakNow = this.vPeak * level;
    this.attacking = this.v < this.peakNow;
  }
  process(rAttack, rDecay) {
    if (this.attacking) {
      this.v = this.srcNow + (this.v - this.srcNow) * Math.exp(-this.dt / (rAttack * this.c));
      if (this.v >= this.peakNow) { this.v = this.peakNow; this.attacking = false; }
    } else {
      this.v *= Math.exp(-this.dt / (rDecay * this.c));
    }
    return this.v;
  }
}

export class OtaVca {
  constructor(tempK, rng, feedthroughSd = 0.004) {
    this.vt = thermalVoltage(tempK);
    this.feedthrough = rng.gaussian() * feedthroughSd;
    this.setResistors();
  }
  setResistors(rabc = 47e3, divTop = 33e3, divBottom = 220, rf = 30e3) {
    this.rabc = rabc; this.divider = divBottom / (divTop + divBottom); this.rf = rf;
  }
  process(vin, vCtrl) {
    const iabc = Math.max(vCtrl, 0) / this.rabc;
    const vd = vin * this.divider;
    return iabc * (Math.tanh(vd / (2 * this.vt)) + this.feedthrough) * this.rf;
  }
}

// ---------------------------------------------------------------- decimator

class Decimator {
  constructor(factor, tapsPerPhase = 24) {
    const n = factor * tapsPerPhase + 1, fc = 0.45 / factor;
    this.h = new Float64Array(n);
    let sum = 0;
    for (let i = 0; i < n; i++) {
      const m = i - (n - 1) / 2;
      const sinc = m === 0 ? 2 * fc : Math.sin(2 * PI * fc * m) / (PI * m);
      const w = 0.42 - 0.5 * Math.cos((2 * PI * i) / (n - 1)) + 0.08 * Math.cos((4 * PI * i) / (n - 1));
      this.h[i] = sinc * w;
      sum += this.h[i];
    }
    for (let i = 0; i < n; i++) this.h[i] /= sum;
    this.n = n; this.factor = factor;
    this.buf = new Float64Array(2 * n); this.pos = 0; this.count = 0; this.out = 0;
  }
  push(x) {
    const n = this.n;
    this.buf[this.pos] = this.buf[this.pos + n] = x;
    this.pos = (this.pos + 1) % n;
    if (++this.count < this.factor) return false;
    this.count = 0;
    let acc = 0;
    const h = this.h, b = this.buf, p = this.pos;
    for (let i = 0; i < n; i++) acc += h[i] * b[p + i];
    this.out = acc;
    return true;
  }
}

// ---------------------------------------------------------------- the voice


export class Dfam {
  // oversample: 4 matches the C++ code; 2 halves the CPU for live tweaking.
  constructor(sampleRate, patch = {}, oversample = 4) {
    this.oversample = oversample;
    this.fs = sampleRate * oversample;
    // knobs: the panel settings. p: what the circuit uses now, which is knobs
    // with this step's parameter locks (patch.locks, see setLocks) applied.
    this.knobs = { ...DEFAULT_PATCH, ...patch };
    delete this.knobs.locks;
    this.p = { ...this.knobs };
    this.locks = {};
    const p = this.p, fs = this.fs;
    const tempK = 273.15 + p.temperature, trimK = 273.15 + p.trim_temperature;
    const rng = new Rng(p.seed);

    this.vco = []; this.vcoExpo = []; this.vcoCap = []; this.vcoIMax = [0, 0];
    for (let i = 0; i < 2; i++) {
      const cap = rng.tol(p.vco_cap, p.vco_cap_tol);
      this.vco.push(new TriangleCoreVco(fs, cap, p.vco_threshold, rng.tol(p.vco_comp_delay, p.vco_comp_delay_tol)));
      const rE = rng.tol(p.vco_expo_rbulk, p.expo_rbulk_tol);
      const vbeOff = rng.gaussian() * p.expo_vbe_offset_sd, scaleErr = rng.gaussian() * p.expo_scale_err_sd;
      const expo = new ExpoConverter();
      expo.setup(1, tempK, trimK, rE, vbeOff, scaleErr);
      this.vcoExpo.push(expo);
      this.vcoCap.push(cap);
    }
    this.vth = p.vco_threshold;
    this.ladder = new LadderFilter(fs, rng.tol(p.vcf_cap, p.vcf_cap_tol), tempK, BigInt(p.seed) + 17n, p.ladder_noise);
    this.vcfExpo = new ExpoConverter();
    {
      const rE = rng.tol(p.vcf_expo_rbulk, p.expo_rbulk_tol);
      const vbeOff = rng.gaussian() * p.expo_vbe_offset_sd, scaleErr = rng.gaussian() * p.expo_scale_err_sd;
      this.vcfExpo.setup(1, tempK, trimK, rE, vbeOff, scaleErr);
    }
    const eg = () => new DecayEnvelope(fs, rng.tol(p.eg_cap, p.eg_cap_tol), p.eg_supply, p.eg_diode, p.eg_peak);
    this.pitchEg = eg();
    this.vcfEg = eg();
    this.vcaEg = eg();
    this.egPeak = p.eg_peak;
    this.vca = new OtaVca(tempK, rng, p.vca_feedthrough_sd);
    this.noiseLp = new OnePoleLp(p.noise_bw, fs);
    this.dcBlock = new DcBlocker(p.output_hp, sampleRate);
    this.decim = new Decimator(oversample);
    this.noiseRng = new Rng(BigInt(p.seed) + 99n);

    this.step = -1; this.stepPitch = 0; this.stepVel = 1;
    this.running = true;
    this.applyKnobs();
    if (patch.locks) this.setLocks(patch.locks);
    this.stepClock = this.samplesPerStep;  // fire step 0 on the first sample
  }

  // Live knob changes. Circuit state (caps, phases, envelopes) is kept;
  // seed, temperature and CIRCUIT_REBUILD_KEYS only take effect in a new Dfam.
  setPatch(changes) {
    const { locks, ...knobs } = changes;
    Object.assign(this.knobs, knobs);
    if (locks) this.setLocks(locks);
    else this.applyStep();
  }

  // Parameter locks: { key: [8 values, null = use the knob] }. On each step
  // the locked keys take that step's value. Only live keys make sense here.
  setLocks(locks) {
    this.locks = {};
    for (const [k, lane] of Object.entries(locks))
      if (k in DEFAULT_PATCH && lane.some((v) => v !== null && v !== undefined)) this.locks[k] = lane;
    this.applyStep();
  }

  applyStep() {
    const changes = {};
    for (const [k, v] of Object.entries(this.knobs)) {
      const lv = this.step >= 0 ? this.locks[k]?.[this.step] : undefined;
      const want = lv === null || lv === undefined ? v : lv;
      if (this.p[k] !== want) changes[k] = want;
    }
    if (Object.keys(changes).length) { Object.assign(this.p, changes); this.applyKnobs(); }
  }

  applyKnobs() {
    const p = this.p;
    for (let i = 0; i < 2; i++) {
      const hz = i === 0 ? p.vco1_freq : p.vco2_freq;
      this.vcoExpo[i].iRef = hz * 4 * this.vcoCap[i] * this.vth;
      this.vcoIMax[i] = p.vco_max_hz * 4 * this.vcoCap[i] * this.vth;
    }
    this.vcfExpo.iRef = this.ladder.currentForCutoff(p.cutoff);
    this.vcfIMax = this.ladder.currentForCutoff(Math.min(p.vcf_max_hz, 0.45 * this.fs));
    this.ladder.noise = p.ladder_noise;
    this.vca.setResistors(p.vca_rabc, p.vca_div_top, p.vca_div_bottom, p.vca_rf);
    // EG volts -> CV volts so a full knob at the envelope peak gives N octaves.
    this.pitchEgScale = p.pitch_eg_octaves / this.egPeak;
    this.vcfEgScale = p.vcf_eg_octaves / this.egPeak;
    const old = this.samplesPerStep;
    this.samplesPerStep = (this.fs * 60) / (p.tempo * p.steps_per_beat);
    if (old) this.stepClock *= this.samplesPerStep / old;  // keep phase in the step
    const decayR = (knob) => p.decay_min_r + audioTaper(knob, p.decay_pot_r, p.decay_taper);
    this.rDecayVco = decayR(p.vco_decay);
    this.rDecayVcf = decayR(p.vcf_decay);
    this.rDecayVca = decayR(p.vca_decay);
    this.rAttack = p.eg_attack_r;
    this.rAttackVca = p.vca_attack === 'slow' ? p.vca_slow_attack_r : p.eg_attack_r;
    this.k = p.resonance_max * clamp(p.resonance, 0, 1);
  }

  // Fire one hit now, outside the sequencer (e.g. a "trigger" button).
  trigger(velocity = 1) {
    this.pitchEg.trigger();
    this.vcfEg.trigger(velocity);
    this.vcaEg.trigger(velocity);
  }

  tick() {
    const p = this.p;
    if (this.running && ++this.stepClock >= this.samplesPerStep) {
      this.stepClock -= this.samplesPerStep;
      this.step = (this.step + 1) % 8;
      for (const k in this.locks) { this.applyStep(); break; }
      this.stepPitch = p.pitch[this.step];
      this.stepVel = p.velocity[this.step];
      if (this.stepVel > 0) this.trigger(this.stepVel);
    }

    const egPitch = this.pitchEg.process(this.rAttack, this.rDecayVco);
    const egVcf = this.vcfEg.process(this.rAttack, this.rDecayVcf);
    const egVca = this.vcaEg.process(this.rAttackVca, this.rDecayVca);

    const seq1 = p.seq_pitch_mod === 'both' ? this.stepPitch : 0;
    const seq2 = p.seq_pitch_mod !== 'off' ? this.stepPitch : 0;
    const cv1 = seq1 + p.vco1_eg_amount * egPitch * this.pitchEgScale;
    const cv2 = seq2 + p.vco2_eg_amount * egPitch * this.pitchEgScale + p.fm_1_2 * this.vco[0].triangleNow();

    const v1 = this.vco[0], v2 = this.vco[1];
    const rise = v1.process(this.vcoExpo[0].current(cv1, this.vcoIMax[0]));
    v2.process(this.vcoExpo[1].current(cv2, this.vcoIMax[1]), p.hard_sync ? rise : -1);

    const o1 = p.vco1_wave === 'square' ? v1.square() : v1.triangle();
    const o2 = p.vco2_wave === 'square' ? v2.square() : v2.triangle();
    const noise = this.noiseLp.process(this.noiseRng.gaussian() * p.noise_amp);

    const rail = p.opamp_rail, lin = p.ladder_in;
    const mix = opampRail(p.mixer_gain * (p.vco1_level * o1 + p.vco2_level * o2 + p.noise_level * noise), rail);
    const vcfCv = p.vcf_eg_amount * egVcf * this.vcfEgScale + p.noise_vcf_mod * noise * p.noise_vcf_scale;
    this.ladder.process(mix * lin, this.vcfExpo.current(vcfCv, this.vcfIMax), this.k);
    const filt = (p.vcf_mode === 'hp' ? this.ladder.highpass() : this.ladder.lowpass()) / lin;
    return this.vca.process(opampRail(filt, rail), egVca);
  }

  // Fill a Float32Array (mono, about +/-1 full scale).
  render(out, offset = 0, length = out.length - offset) {
    const d = this.decim, gain = this.p.volume / 4;
    for (let i = 0; i < length; i++) {
      while (!d.push(this.tick()));
      out[offset + i] = this.dcBlock.process(d.out) * gain;
    }
    return out;
  }
}

// ---------------------------------------------------------------- presets

// Parse the "key = value" preset format used by presets/*.txt.
export function parsePreset(text) {
  const patch = {};
  for (const raw of text.split('\n')) {
    const line = raw.split('#')[0];
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    let key = line.slice(0, eq).trim(), val = line.slice(eq + 1).trim();
    // "lock.cutoff = 300,-,-,900,-,-,-,-": a per-step lane, "-" = use the knob.
    const lock = key.startsWith('lock.');
    if (lock) key = key.slice(5);
    if (!(key in DEFAULT_PATCH)) throw new Error(`unknown key: ${key}`);
    const def = DEFAULT_PATCH[key];
    const conv = (v) => (typeof def === 'boolean' ? v === 'on' || v === '1' || v === 'true'
      : typeof def === 'number' ? Number(v) : v);
    if (lock) {
      if (Array.isArray(def)) throw new Error(`${key} can't be locked`);
      const lane = val.split(',').map((v) => v.trim()).map((v) => (v === '-' || v === '' ? null : conv(v)));
      (patch.locks ??= {})[key] = Array.from({ length: 8 }, (_, i) => lane[i] ?? null);
    } else if (Array.isArray(def)) patch[key] = val.split(',').map(Number);
    else patch[key] = conv(val);
  }
  return patch;
}

// Preset lines for parameter locks, in the format parsePreset reads.
export function locksToText(locks = {}, fmt = String) {
  const one = (v) => (v === null || v === undefined ? '-' : typeof v === 'boolean' ? (v ? 'on' : 'off')
    : typeof v === 'number' ? fmt(v) : String(v));
  return Object.entries(locks).filter(([, l]) => l.some((v) => v !== null && v !== undefined))
    .map(([k, l]) => `lock.${k} = ${l.map(one).join(',')}`);
}
