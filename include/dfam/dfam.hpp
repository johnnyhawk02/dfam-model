// DFAM-style percussion voice: two triangle-core VCOs, noise, mixer, ladder
// VCF (LP/HP), three decay envelopes, OTA VCA and an 8-step pitch/velocity
// sequencer, wired as described in the DFAM manual. The circuit blocks are
// modelled from Moog's published Werkstatt-01 schematic and the ladder
// literature; DFAM's own component values are not public, so the values here
// are plausible engineering choices, chosen to land the panel ranges.
#pragma once
#include <array>
#include <vector>

#include "components.hpp"
#include "envelope_vca.hpp"
#include "ladder.hpp"
#include "vco.hpp"

namespace dfam {

enum class Wave { Triangle = 0, Square = 1 };
enum class SeqPitchMod { Off = 0, Both = 1, Vco2 = 2 };
enum class VcfMode { LowPass = 0, HighPass = 1 };

// Estimated component values and scalings. Moog has not published DFAM
// schematics, so every value here is an engineering guess chosen to land
// DFAM-like panel ranges. They are exposed so they can be fitted against a
// real unit. SI units throughout (F, ohm, V, s, Hz). "_tol" values are
// relative tolerances treated as about 3 sigma; "_sd" values are 1 sigma.
// Changing a tolerance to 0 still draws from the RNG, so the other parts of a
// seeded unit stay the same.
struct Circuit {
  // VCOs (triangle core, f = Ic / (4 C Vth))
  double vcoCap = 4.7e-9;            // integrator timing capacitor
  double vcoCapTol = 0.02;
  double vcoThreshold = 5.0;         // Schmitt comparator thresholds, +/- V
  double vcoCompDelay = 150e-9;      // comparator + switch delay, s
  double vcoCompDelayTol = 0.2;
  double vcoMaxHz = 22000.0;         // expo current limit, as a frequency
  double pitchEgOctaves = 5.0;       // pitch EG amount at full knob, octaves

  // Expo converters (matched NPN pair)
  double vcoExpoRBulk = 2.0;         // bulk emitter resistance, ohm (HF flattening)
  double vcfExpoRBulk = 2.0;
  double expoRBulkTol = 0.2;
  double expoVbeOffsetSd = 0.0003;   // pair Vbe mismatch, V
  double expoScaleErrSd = 0.002;     // 1 V/oct scale trim error (relative)
  double trimTemperatureC = 25.0;    // temperature the scale was trimmed at

  // Mixer and noise
  double mixerGain = 1.6;            // inverting summer gain
  double opampRail = 10.5;           // op-amp output swing, +/- V (mixer and VCF out)
  double noiseAmp = 2.0;             // noise source level before the mixer, V rms
  double noiseBandwidth = 16000.0;   // noise low-pass corner, Hz

  // VCF (transistor ladder, wc = I / (4 C Vt))
  double vcfCap = 68e-9;             // ladder stage capacitors
  double vcfCapTol = 0.05;
  double vcfMaxHz = 45000.0;         // control current limit, as a cutoff
  double vcfEgOctaves = 8.0;         // VCF EG amount at full knob, octaves
  double noiseVcfScale = 1.5;        // noise -> VCF CV, volts per unit of knob
  double resonanceMax = 1.15;        // loop gain k at full resonance (self-osc ~1)
  double ladderInputScale = 0.005;   // mixer volts -> ladder base volts
  double ladderNoise = 2e-7;         // input thermal noise, V rms per sample

  // Envelopes (capacitor, attack resistor + diode, decay pot)
  double egCap = 2.2e-6;
  double egCapTol = 0.1;
  double egSupply = 12.0;            // attack charges towards this supply, V
  double egDiodeDrop = 0.6;          // steering diode drop, V
  double egPeak = 8.0;               // comparator level ending the attack, V (must be below supply - diode)
  double egAttackR = 470.0;          // fast attack resistor, ohm (~1 ms)
  double vcaSlowAttackR = 22e3;      // VCA slow attack resistor, ohm (~50 ms)
  double decayPotR = 1e6;            // decay pot, ohm (audio taper)
  double decayMinR = 1e3;            // end-stop resistor in series with the pot, ohm
  double decayTaper = 100.0;         // pot law R = Rpot (base^knob - 1) / (base - 1); ~9% at mid-travel

  // VCA (LM13700 OTA)
  double vcaRabc = 47e3;             // envelope -> Iabc resistor, ohm
  double vcaDivTop = 33e3;           // input divider, ohm
  double vcaDivBottom = 220.0;
  double vcaRf = 30e3;               // I-to-V feedback resistor, ohm
  double vcaFeedthroughSd = 0.004;   // control feedthrough (thump), relative to Iabc

  // Output
  double outputHpHz = 8.0;           // output coupling high-pass, Hz
};

// Panel settings. Knobs are 0..1 unless noted; "amount" knobs are -1..1.
struct Patch {
  // Sequencer
  double tempoBpm = 120.0;
  int stepsPerBeat = 4;
  std::array<double, 8> pitch{0, 0, 0, 0, 0, 0, 0, 0};  // volts (1 V/oct), -3..3
  std::array<double, 8> velocity{1, 1, 1, 1, 1, 1, 1, 1};  // 0..1
  SeqPitchMod seqPitchMod = SeqPitchMod::Both;

  // Oscillators (frequency knobs given in Hz at 0 V of modulation)
  double vco1FreqHz = 55.0, vco2FreqHz = 110.0;
  Wave vco1Wave = Wave::Triangle, vco2Wave = Wave::Square;
  double vcoDecay = 0.3;                  // pitch envelope decay knob
  double vco1EgAmount = 0.4, vco2EgAmount = 0.0;
  double fm12Amount = 0.0;                // VCO1 triangle -> VCO2 pitch
  bool hardSync = false;                  // VCO2 reset by VCO1

  // Mixer
  double vco1Level = 0.8, vco2Level = 0.0, noiseLevel = 0.0;

  // Filter
  VcfMode vcfMode = VcfMode::LowPass;
  double cutoffHz = 600.0;
  double resonance = 0.3;
  double vcfEgAmount = 0.5;
  double vcfDecay = 0.3;
  double noiseVcfMod = 0.0;

  // Amplifier
  bool vcaAttackSlow = false;
  double vcaDecay = 0.35;
  double volume = 0.8;

  // "Analog" conditions
  double temperatureC = 25.0;
  uint64_t seed = 1;

  // Estimated component values (not panel controls).
  Circuit circuit;
};

// Windowed-sinc decimator for the oversampled core.
class Decimator {
 public:
  void setup(int factor, int tapsPerPhase = 24) {
    factor_ = factor;
    const int n = factor * tapsPerPhase + 1;
    h_.assign(n, 0.0);
    const double fc = 0.45 / factor;  // passband to 0.45 * output rate
    double sum = 0;
    for (int i = 0; i < n; ++i) {
      const double m = i - (n - 1) / 2.0;
      const double sinc = m == 0 ? 2 * fc : std::sin(2 * kPi * fc * m) / (kPi * m);
      const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (n - 1)) +
                       0.08 * std::cos(4 * kPi * i / (n - 1));  // Blackman
      h_[i] = sinc * w;
      sum += h_[i];
    }
    for (double& x : h_) x /= sum;
    buf_.assign(2 * n, 0.0);
    pos_ = 0;
  }
  // Push one oversampled sample; returns true when an output sample is ready.
  bool push(double x, double& out) {
    const int n = static_cast<int>(h_.size());
    buf_[pos_] = buf_[pos_ + n] = x;
    pos_ = (pos_ + 1) % n;
    if (++count_ < factor_) return false;
    count_ = 0;
    double acc = 0;
    const double* b = &buf_[pos_];  // oldest .. newest
    for (int i = 0; i < n; ++i) acc += h_[i] * b[i];
    out = acc;
    return true;
  }

 private:
  int factor_ = 4, pos_ = 0, count_ = 0;
  std::vector<double> h_, buf_;
};

class Dfam {
 public:
  static constexpr int kOversample = 4;

  void setup(double sampleRate, const Patch& patch) {
    patch_ = patch;
    const Circuit& c = patch.circuit;
    fs_ = sampleRate * kOversample;
    const double tempK = 273.15 + patch.temperatureC;
    const double trimK = 273.15 + c.trimTemperatureC;
    Rng rng(patch.seed);

    // VCOs: timing cap, +/-Vth thresholds, comparator delay, expo pair with
    // bulk emitter resistance and a few mV of mismatch.
    for (int i = 0; i < 2; ++i) {
      const double cap = rng.tol(c.vcoCap, c.vcoCapTol);
      vco_[i].setup(fs_, cap, c.vcoThreshold, rng.tol(c.vcoCompDelay, c.vcoCompDelayTol));
      const double hz = i == 0 ? patch.vco1FreqHz : patch.vco2FreqHz;
      const double iRef = hz * 4.0 * cap * c.vcoThreshold;
      // Draw in a fixed order (argument evaluation order is unspecified).
      const double rE = rng.tol(c.vcoExpoRBulk, c.expoRBulkTol);
      const double vbeOff = rng.gaussian() * c.expoVbeOffsetSd;
      const double scaleErr = rng.gaussian() * c.expoScaleErrSd;
      vcoExpo_[i].setup(iRef, tempK, trimK, rE, vbeOff, scaleErr);
      vcoIMax_[i] = c.vcoMaxHz * 4.0 * cap * c.vcoThreshold;
    }

    // Ladder: stage caps (spread), expo converter as above.
    ladder_.setup(fs_, rng.tol(c.vcfCap, c.vcfCapTol), tempK, patch.seed + 17, c.ladderNoise);
    {
      const double rE = rng.tol(c.vcfExpoRBulk, c.expoRBulkTol);
      const double vbeOff = rng.gaussian() * c.expoVbeOffsetSd;
      const double scaleErr = rng.gaussian() * c.expoScaleErrSd;
      vcfExpo_.setup(ladder_.currentForCutoff(patch.cutoffHz), tempK, trimK, rE, vbeOff,
                     scaleErr);
    }
    vcfIMax_ = ladder_.currentForCutoff(std::min(c.vcfMaxHz, 0.45 * fs_));

    // Envelopes: caps, audio-taper decay pots with end-stop resistors.
    pitchEg_.setup(fs_, rng.tol(c.egCap, c.egCapTol), c.egSupply, c.egDiodeDrop, c.egPeak);
    vcfEg_.setup(fs_, rng.tol(c.egCap, c.egCapTol), c.egSupply, c.egDiodeDrop, c.egPeak);
    vcaEg_.setup(fs_, rng.tol(c.egCap, c.egCapTol), c.egSupply, c.egDiodeDrop, c.egPeak);
    vca_.setup(tempK, rng, c.vcaRabc, c.vcaDivTop, c.vcaDivBottom, c.vcaRf, c.vcaFeedthroughSd);

    // EG volts -> CV volts so that a full knob at peak gives N octaves.
    pitchEgScale_ = c.pitchEgOctaves / c.egPeak;
    vcfEgScale_ = c.vcfEgOctaves / c.egPeak;

    noiseLp_.setup(c.noiseBandwidth, fs_);
    dcBlock_.setup(c.outputHpHz, sampleRate);
    decim_.setup(kOversample);
    noiseRng_ = Rng(patch.seed + 99);

    samplesPerStep_ = fs_ * 60.0 / (patch.tempoBpm * patch.stepsPerBeat);
    stepClock_ = samplesPerStep_;  // fire step 0 on the first sample
    step_ = -1;
  }

  // Render n output samples (mono, roughly +/-1 full scale).
  void render(float* out, int n) {
    for (int i = 0; i < n; ++i) {
      double y = 0.0;
      bool ready = false;
      while (!ready) ready = decim_.push(tick(), y);
      out[i] = static_cast<float>(dcBlock_.process(y) * patch_.volume / 4.0);
    }
  }

  int currentStep() const { return step_; }

 private:
  double decayR(double knob) const {
    const Circuit& c = patch_.circuit;
    return c.decayMinR + audioTaper(knob, c.decayPotR, c.decayTaper);
  }

  double tick() {
    const Patch& p = patch_;
    const Circuit& c = p.circuit;
    // Sequencer clock.
    if (++stepClock_ >= samplesPerStep_) {
      stepClock_ -= samplesPerStep_;
      step_ = (step_ + 1) % 8;
      stepPitch_ = p.pitch[step_];
      stepVel_ = p.velocity[step_];
      // A step at zero velocity is a rest: no trigger, earlier hits ring on.
      // (Assumption: velocity sets the VCF/VCA envelope peak.)
      if (stepVel_ > 0.0) {
        pitchEg_.trigger();
        vcfEg_.trigger(stepVel_);
        vcaEg_.trigger(stepVel_);
      }
    }

    // Envelopes (fast attack ~1 ms; VCA slow attack ~50 ms).
    const double egPitch = pitchEg_.process(c.egAttackR, decayR(p.vcoDecay));
    const double egVcf = vcfEg_.process(c.egAttackR, decayR(p.vcfDecay));
    const double egVca =
        vcaEg_.process(p.vcaAttackSlow ? c.vcaSlowAttackR : c.egAttackR, decayR(p.vcaDecay));

    // Pitch CVs (1 V/oct). Envelope amount: full knob = pitchEgOctaves.
    const double seq1 = p.seqPitchMod == SeqPitchMod::Both ? stepPitch_ : 0.0;
    const double seq2 = p.seqPitchMod != SeqPitchMod::Off ? stepPitch_ : 0.0;
    const double cv1 = seq1 + p.vco1EgAmount * egPitch * pitchEgScale_;
    const double cv2 = seq2 + p.vco2EgAmount * egPitch * pitchEgScale_ +
                       p.fm12Amount * vco_[0].triangleNow();

    const double rise = vco_[0].process(vcoExpo_[0].current(cv1, vcoIMax_[0]));
    vco_[1].process(vcoExpo_[1].current(cv2, vcoIMax_[1]), p.hardSync ? rise : -1.0);

    const double o1 = p.vco1Wave == Wave::Triangle ? vco_[0].triangle() : vco_[0].square();
    const double o2 = p.vco2Wave == Wave::Triangle ? vco_[1].triangle() : vco_[1].square();
    const double noise = noiseLp_.process(noiseRng_.gaussian() * c.noiseAmp);

    // Mixer: inverting summer with gain, clipping into the op-amp rails.
    const double mix = opampRail(
        c.mixerGain * (p.vco1Level * o1 + p.vco2Level * o2 + p.noiseLevel * noise), c.opampRail);

    // Filter: CV = envelope (1 V/oct, full knob = vcfEgOctaves) + noise mod.
    const double vcfCv = p.vcfEgAmount * egVcf * vcfEgScale_ + p.noiseVcfMod * noise * c.noiseVcfScale;
    const double k = c.resonanceMax * std::clamp(p.resonance, 0.0, 1.0);
    const double lin = c.ladderInputScale;
    ladder_.process(mix * lin, vcfExpo_.current(vcfCv, vcfIMax_), k);
    const double filt =
        (p.vcfMode == VcfMode::LowPass ? ladder_.lowpass() : ladder_.highpass()) / lin;

    return vca_.process(opampRail(filt, c.opampRail), egVca);
  }

  Patch patch_;
  double fs_ = 192000;
  TriangleCoreVco vco_[2];
  ExpoConverter vcoExpo_[2], vcfExpo_;
  double vcoIMax_[2] = {1, 1}, vcfIMax_ = 1;
  double pitchEgScale_ = 5.0 / 8.0, vcfEgScale_ = 1.0;
  LadderFilter ladder_;
  DecayEnvelope pitchEg_, vcfEg_, vcaEg_;
  OtaVca vca_;
  OnePoleLp noiseLp_;
  DcBlocker dcBlock_;
  Decimator decim_;
  Rng noiseRng_;
  double samplesPerStep_ = 1, stepClock_ = 0, stepPitch_ = 0, stepVel_ = 1;
  int step_ = -1;
};

}  // namespace dfam
