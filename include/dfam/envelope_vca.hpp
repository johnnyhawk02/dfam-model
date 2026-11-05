// Attack/decay envelope generator and OTA VCA.
#pragma once
#include "components.hpp"

namespace dfam {

// Capacitor envelope: a trigger charges C through the attack resistor (and a
// steering diode) towards the supply; a comparator ends the attack at vPeak,
// then C discharges through the decay pot. Retriggers start from wherever the
// capacitor is, as on hardware. Each phase uses the exact RC step response.
// Velocity scales the attack target and comparator level (the peak voltage).
class DecayEnvelope {
 public:
  void setup(double fs, double cap, double vSupply = 12.0, double diodeDrop = 0.6,
             double vPeak = 8.0) {
    dt_ = 1.0 / fs;
    c_ = cap;
    vSrc_ = vSupply - diodeDrop;
    vPeak_ = vPeak;
  }
  void trigger(double level = 1.0) {
    level = std::clamp(level, 0.0, 1.0);
    srcNow_ = vSrc_ * level;
    peakNow_ = vPeak_ * level;
    attacking_ = v_ < peakNow_;
  }
  // rAttack/rDecay in ohms. Returns the capacitor voltage (0..~8 V).
  double process(double rAttack, double rDecay) {
    if (attacking_) {
      v_ = srcNow_ + (v_ - srcNow_) * std::exp(-dt_ / (rAttack * c_));
      if (v_ >= peakNow_) {
        v_ = peakNow_;
        attacking_ = false;
      }
    } else {
      v_ *= std::exp(-dt_ / (rDecay * c_));
    }
    return v_;
  }
  double value() const { return v_; }

 private:
  double dt_ = 1.0 / 192000, c_ = 2.2e-6, vSrc_ = 11.4, vPeak_ = 8.0, v_ = 0.0;
  double srcNow_ = 11.4, peakNow_ = 8.0;
  bool attacking_ = false;
};

// LM13700-style VCA: the envelope sets the bias current Iabc through a
// resistor; the audio goes through a divider into the OTA's differential
// input, whose output current is Iabc * tanh(Vd / 2Vt). Loud inputs soft-clip,
// and a small control feedthrough (input offset) gives the hardware's thump.
class OtaVca {
 public:
  // rabc: CV to Iabc resistor; divTop/divBottom: input divider; rf: I-to-V
  // feedback resistor; feedthroughSd: control feedthrough spread (1 sigma).
  void setup(double tempK, Rng& rng, double rabc = 47e3, double divTop = 33e3,
             double divBottom = 220.0, double rf = 30e3, double feedthroughSd = 0.004) {
    vt_ = thermalVoltage(tempK);
    feedthrough_ = rng.gaussian() * feedthroughSd;
    rabc_ = rabc;
    divider_ = divBottom / (divTop + divBottom);
    rf_ = rf;
  }
  double process(double vin, double vCtrl) const {
    const double iabc = std::max(vCtrl, 0.0) / rabc_;
    const double vd = vin * divider_;
    const double iout = iabc * (std::tanh(vd / (2.0 * vt_)) + feedthrough_);
    return iout * rf_;
  }

 private:
  double rabc_ = 47e3, divider_ = 220.0 / 33220.0, rf_ = 30e3;
  double vt_ = 0.0257, feedthrough_ = 0.0;
};

}  // namespace dfam
