# DFAM component model

A component-level emulation of a Moog DFAM-style percussion synth, as a header-only C++17 library and a command-line renderer.

Moog has not published DFAM schematics. Each circuit block here is modelled on the matching block in Moog's published Werkstatt-01 schematic and on the Moog-ladder modelling papers, then wired the way the DFAM manual describes. Component values are plausible engineering choices tuned to land DFAM-like panel ranges. They are not measured from a DFAM. See `research/dfam-circuit-sources.md`.

## Build and run

```sh
cmake -S . -B build && cmake --build build -j
./build/dfam_tests                      # model sanity checks
./build/dfam_render presets/kick.txt kick.wav --seconds 8
./build/dfam_render presets/metal.txt out.wav cutoff=1500 resonance=0.9   # override knobs
./build/dfam_render --keys              # list every patch key
./build/dfam_render --circuit           # estimated component values and their defaults
```

The output is a 24-bit mono WAV at 48 kHz (`--rate` changes it). The core runs at 4x oversampling and renders about 5x faster than real time on one core.

## JavaScript version (for prototyping)

`js/dfam.js` is a dependency-free ES module port of the same model. The same patch and seed give **bit-identical** output to the C++ renderer at 24-bit (checked on all five presets). It renders about 2.7x real time in Node at 4x oversampling.

```sh
node js/render.mjs presets/metal.txt metal.wav --seconds 8 cutoff=1500   # CLI, same flags as dfam_render
```

**Playground:** open `js/index.html` directly in a browser, with no server needed. It's a self-contained file with the model and presets inlined. It has every panel control, the 8-step pitch and velocity grid, presets, a scope, a manual trigger, copy/paste of preset text, and a collapsible panel of the estimated circuit values. Knobs change live without resetting the circuit. The temperature, unit (seed) and oversampling controls rebuild it. Served over http it runs in an AudioWorklet. Opened from a file it falls back to a main-thread ScriptProcessor, which is shown next to the sample rate. Switch to 2x oversampling if it crackles.

`js/index.html` is generated. Edit `js/playground.src.html`, `js/dfam.js` or `presets/*.txt`, then run `node js/build-standalone.mjs`.

In code:

```js
import { Dfam, parsePreset } from './js/dfam.js';
const synth = new Dfam(48000, { cutoff: 300, resonance: 0.8 });
synth.setPatch({ vcf_decay: 0.5 });          // live change
synth.trigger(1);                            // manual hit
const out = synth.render(new Float32Array(48000));
```

## Tuning the estimated component values

Every component value and scaling that is a guess, rather than a panel control, can be set like a patch key. That covers capacitors, resistors, expo bulk resistance, comparator delay, envelope voltages, EG amount ranges, the VCA divider, tolerances and so on. They live in `dfam::Circuit` (`patch.circuit` in C++) and in `CIRCUIT_INFO` / `DEFAULT_CIRCUIT` in `js/dfam.js`, where the keys are flat patch keys. The defaults are the values the model always used, so existing presets and renders are unchanged, bit for bit.

Put them in a preset file, or pass them on the command line:

```sh
./build/dfam_render presets/kick.txt kick.wav pitch_eg_octaves=4 vco_expo_rbulk=4 eg_peak=7.5
./build/dfam_render --circuit > my_unit.txt   # a starting point to edit; append a patch's keys after it
node js/render.mjs --circuit                  # same listing from the JS model
```

In the playground, open **Circuit: estimated component values** under the scope. Each value has a log slider from a quarter to four times its default, and a box for an exact value. Changed values are highlighted and get copied into the preset text, under a `# circuit` comment. Values used when the circuit is built (caps, tolerances, thresholds, expo mismatch, envelope voltages, noise bandwidth, VCA feedthrough, output high-pass) rebuild the engine. The rest change live.

Units are SI: F, Ω, V, s and Hz. A `_tol` value is a relative tolerance treated as about 3σ, and a `_sd` value is 1σ. Both set the unit-to-unit spread that the seed draws. Setting one to 0 still draws from the random generator, so the other parts of a seeded unit stay the same.

Some things to know while tuning:

- The frequency and cutoff knobs are calibrated in Hz, so a timing cap or threshold change mostly cancels out at low pitch. It still shows up through the expo bulk resistance, which acts on the larger current, and through the comparator delay.
- `pitch_eg_octaves` and `vcf_eg_octaves` are the CV at full knob with the envelope at `eg_peak`. Keep `eg_peak` below `eg_supply - eg_diode`, or the attack never reaches the comparator and the envelope never decays.
- `ladder_in` sets how hard the mixer drives the ladder. Higher is more tanh grit, and the output gain is compensated.
- The decay time constant is `eg_cap · (decay_min_r + decay_pot_r · (decay_taper^knob − 1) / (decay_taper − 1))`.

| Key | Default | What it is |
|---|---|---|
| `vco_cap` | 4.7e-9 | VCO timing capacitor, F |
| `vco_cap_tol` | 0.02 | VCO cap tolerance (relative, ~3 sigma) |
| `vco_threshold` | 5 | VCO comparator thresholds, +/- V |
| `vco_comp_delay` | 1.5e-7 | VCO comparator + switch delay, s |
| `vco_comp_delay_tol` | 0.2 | comparator delay tolerance |
| `vco_max_hz` | 22000 | VCO expo current limit, Hz |
| `pitch_eg_octaves` | 5 | pitch EG amount at full knob, octaves |
| `vco_expo_rbulk` | 2 | VCO expo bulk emitter resistance, ohm |
| `vcf_expo_rbulk` | 2 | VCF expo bulk emitter resistance, ohm |
| `expo_rbulk_tol` | 0.2 | bulk resistance tolerance |
| `expo_vbe_offset_sd` | 0.0003 | expo pair Vbe mismatch, V (1 sigma) |
| `expo_scale_err_sd` | 0.002 | 1 V/oct trim error (relative, 1 sigma) |
| `trim_temperature` | 25 | temperature the expo scale was trimmed at, C |
| `mixer_gain` | 1.6 | mixer summer gain |
| `opamp_rail` | 10.5 | op-amp output swing (mixer, VCF out), +/- V |
| `noise_amp` | 2 | noise source level, V rms |
| `noise_bw` | 16000 | noise low-pass corner, Hz |
| `vcf_cap` | 6.8e-8 | ladder stage capacitors, F |
| `vcf_cap_tol` | 0.05 | ladder cap tolerance |
| `vcf_max_hz` | 45000 | VCF control current limit, Hz |
| `vcf_eg_octaves` | 8 | VCF EG amount at full knob, octaves |
| `noise_vcf_scale` | 1.5 | noise -> VCF CV, V per unit knob |
| `resonance_max` | 1.15 | ladder loop gain at full resonance |
| `ladder_in` | 0.005 | mixer V -> ladder input V (drive) |
| `ladder_noise` | 2e-7 | ladder input thermal noise, V rms |
| `eg_cap` | 2.2e-6 | envelope capacitors, F |
| `eg_cap_tol` | 0.1 | envelope cap tolerance |
| `eg_supply` | 12 | envelope attack supply, V |
| `eg_diode` | 0.6 | envelope steering diode drop, V |
| `eg_peak` | 8 | envelope peak (comparator), V; keep below supply - diode |
| `eg_attack_r` | 470 | fast attack resistor, ohm |
| `vca_slow_attack_r` | 22000 | VCA slow attack resistor, ohm |
| `decay_pot_r` | 1000000 | decay pot, ohm |
| `decay_min_r` | 1000 | decay end-stop resistor, ohm |
| `decay_taper` | 100 | decay pot audio-taper base |
| `vca_rabc` | 47000 | VCA envelope -> Iabc resistor, ohm |
| `vca_div_top` | 33000 | VCA input divider top, ohm |
| `vca_div_bottom` | 220 | VCA input divider bottom, ohm |
| `vca_rf` | 30000 | VCA I-to-V feedback resistor, ohm |
| `vca_feedthrough_sd` | 0.004 | VCA control feedthrough (1 sigma) |
| `output_hp` | 8 | output coupling high-pass, Hz |

## What is modelled

| Block | Model | File |
|---|---|---|
| Expo converters (VCOs, VCF) | Matched NPN pair, Ebers-Moll with bulk emitter resistance, solved by Newton. Captures 1 V/oct scaling set by Vt at trim temperature, HF flattening and temperature drift. | `components.hpp` |
| VCO 1 and 2 | Triangle core: integrator cap charged by expo current, Schmitt comparator at ±5 V with switching delay. Sub-sample-accurate switching, polyBLEP square edges. Hard sync resets the cap. | `vco.hpp` |
| Mixer | Inverting summer into soft op-amp rails, so high levels overdrive the filter as on the hardware. | `dfam.hpp` |
| Ladder VCF | 4 differential-pair stages with tanh nonlinearity and cutoff = I / (4 C Vt). Trapezoidal integration with a full Newton solve (zero-delay feedback). Self-oscillates, with thermal noise at the input. | `ladder.hpp` |
| Envelopes | Capacitor charged through an attack resistor and diode, comparator at the peak, then discharge through a 1 M audio-taper pot. Uses exact RC steps. VCA attack has fast and slow settings. | `envelope_vca.hpp` |
| VCA | LM13700 OTA: Iabc from the envelope, tanh input stage behind a 33k/220R divider, small control feedthrough. | `envelope_vca.hpp` |
| Noise | Gaussian, band-limited. Feeds the mixer and the VCF mod. | `dfam.hpp` |
| Sequencer | 8 steps of pitch (V) and velocity. A step with zero velocity is a rest. | `dfam.hpp` |

The seed sets per-unit component spread: cap tolerances, expo mismatch and comparator delay. Different seeds behave like different units.

## Assumptions to check against a real DFAM

- **HP mode:** a 4-pole binomial tap mix of the ladder. DFAM's actual HP circuit is unknown.
- **Velocity:** sets the VCF and VCA envelope peak. Zero velocity is treated as a rest.
- **FM routing:** "1-2 FM" is read as VCO1's triangle modulating VCO2's exponential CV, and hard sync as VCO2 reset by VCO1.
- **Scaling and ranges:** these are guesses that need tuning against recordings. They are full pitch EG amount = 5 octaves, full VCF EG amount = 8 octaves, decay range about 2 ms to 2.2 s tau, and slow attack about 50 ms. All of them are settable (see "Tuning the estimated component values").
- **Pitch EG:** not velocity-scaled.

## Test results (`dfam_tests`)

- VCO tracks exactly 1 V/oct with an ideal expo. It runs about 84 cents flat at 7 V with 2 Ω bulk resistance, and drifts when 10 °C above trim.
- Ladder LP and HP are at −12.04 dB at cutoff with no resonance, matching theory. It self-oscillates within 50 cents of cutoff at k = 1.15.
- Envelope decay τ equals R·C.
- The full voice renders with no NaNs.
- Circuit values are wired through: `pitch_eg_octaves = 0` and `resonance_max = 0` disable their knobs, and `vco_expo_rbulk` changes the sound.

## Next steps

- Wrap `dfam::Dfam` in a JUCE or CLAP plugin. The core is header-only with no allocation in the audio path.
- Add the patch bay inputs and outputs (they map directly onto the CV sums in `Dfam::tick`).
- Fit the circuit values against recordings of a real DFAM. They are all exposed as patch keys, so a fitting script can drive `dfam_render` or the JS model directly.
