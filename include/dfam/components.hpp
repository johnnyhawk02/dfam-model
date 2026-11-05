// Shared physical constants and small component models.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dfam {

constexpr double kBoltzmann = 1.380649e-23;  // J/K
constexpr double kCharge = 1.602176634e-19;  // C
constexpr double kPi = 3.14159265358979323846;

// Thermal voltage kT/q at a junction temperature in kelvin (~25.7 mV at 298 K).
inline double thermalVoltage(double tempK) { return kBoltzmann * tempK / kCharge; }

// Small deterministic PRNG so renders are repeatable per seed (component spread, noise).
class Rng {
 public:
  explicit Rng(uint64_t seed = 1) : s_(seed * 0x9E3779B97F4A7C15ull + 1) {}
  uint64_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 7;
    s_ ^= s_ << 17;
    return s_;
  }
  double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }  // [0,1)
  double gaussian() {  // Box-Muller
    if (haveSpare_) { haveSpare_ = false; return spare_; }
    double u1 = std::max(uniform(), 1e-300), u2 = uniform();
    double r = std::sqrt(-2.0 * std::log(u1));
    spare_ = r * std::sin(2 * kPi * u2);
    haveSpare_ = true;
    return r * std::cos(2 * kPi * u2);
  }
  // A component value with a given relative tolerance (treated as ~3 sigma).
  double tol(double nominal, double relTol) { return nominal * (1.0 + gaussian() * relTol / 3.0); }

 private:
  uint64_t s_;
  double spare_ = 0;
  bool haveSpare_ = false;
};

// Matched NPN pair exponential converter (the "expo" in every Moog VCO/VCF).
// The reference transistor runs at iRef; the input divider puts Vt0*ln2 per volt
// across the bases, so 1 V of CV doubles the collector current at the trim
// temperature. The bulk emitter resistance rE makes high currents run flat,
// which is why real oscillators go flat at the top unless HF-compensated.
class ExpoConverter {
 public:
  void setup(double iRef, double tempK, double trimTempK, double rE, double vbeOffset,
             double scaleErr) {
    iRef_ = iRef;
    vt_ = thermalVoltage(tempK);
    vPerVolt_ = thermalVoltage(trimTempK) * std::log(2.0) * (1.0 + scaleErr);
    rE_ = rE;
    vbeOffset_ = vbeOffset;
  }
  // Collector current for a CV in volts (1 V/oct). Solves
  //   Vt*ln(I/Iref) + rE*(I - Iref) = dV   by Newton in the log domain.
  double current(double cv, double iMax) const {
    const double dV = cv * vPerVolt_ + vbeOffset_;
    double x = dV / vt_;  // ideal-diode guess, x = ln(I/Iref)
    x = std::min(x, std::log(iMax / iRef_));
    for (int k = 0; k < 6; ++k) {
      double e = std::exp(x);
      double g = vt_ * x + rE_ * iRef_ * (e - 1.0) - dV;
      double gp = vt_ + rE_ * iRef_ * e;
      double step = g / gp;
      x -= step;
      if (std::fabs(step) < 1e-9) break;
    }
    return std::min(iRef_ * std::exp(x), iMax);
  }
  double iRef() const { return iRef_; }

 private:
  double iRef_ = 1e-5, vt_ = 0.0257, vPerVolt_ = 0.0178, rE_ = 0, vbeOffset_ = 0;
};

// Audio-taper potentiometer: resistance between wiper and end for a 0..1 knob.
inline double audioTaper(double knob, double rMax, double base = 100.0) {
  knob = std::clamp(knob, 0.0, 1.0);
  return rMax * (std::pow(base, knob) - 1.0) / (base - 1.0);
}

// Op-amp output stage: linear inside the rails, rounding off near them.
inline double opampRail(double v, double rail) {
  const double knee = 0.85 * rail;
  if (std::fabs(v) <= knee) return v;
  double s = v > 0 ? 1.0 : -1.0;
  double over = std::fabs(v) - knee;
  return s * (knee + (rail - knee) * std::tanh(over / (rail - knee)));
}

// One-pole high-pass, e.g. an output coupling capacitor.
class DcBlocker {
 public:
  void setup(double fc, double fs) { a_ = std::exp(-2 * kPi * fc / fs); }
  double process(double x) {
    double y = a_ * (y_ + x - x1_);
    x1_ = x;
    y_ = y;
    return y;
  }

 private:
  double a_ = 0.999, x1_ = 0, y_ = 0;
};

// One-pole low-pass, e.g. finite amplifier bandwidth.
class OnePoleLp {
 public:
  void setup(double fc, double fs) { a_ = 1.0 - std::exp(-2 * kPi * fc / fs); }
  double process(double x) { return y_ += a_ * (x - y_); }

 private:
  double a_ = 1, y_ = 0;
};

}  // namespace dfam
