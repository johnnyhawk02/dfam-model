// Triangle-core VCO: an op-amp integrator charged by the expo converter's
// current, reversed by a Schmitt comparator at +/-Vth (the topology on sheet 3
// of Moog's Werkstatt-01 schematic). Frequency is f = Ic / (4 C Vth).
//
// Component effects that are modelled:
//  * comparator + switch delay: the ramp overshoots the threshold by Ic*td/C
//    before turning, so the top octaves run flat and the triangle grows;
//  * hard sync resets the capacitor to the bottom threshold;
//  * the comparator edges are band-limited with a 2-point polyBLEP placed at
//    the exact sub-sample switching instant (one sample of latency).
#pragma once
#include "components.hpp"

namespace dfam {

class TriangleCoreVco {
 public:
  void setup(double fs, double cap, double vth, double compDelay) {
    dt_ = 1.0 / fs;
    c_ = cap;
    vth_ = vth;
    td_ = compDelay;
    v_ = -vth;
    dir_ = 1;
  }

  // Advance one sample with charge current ic (A). syncFrac in [0,1) resets the
  // core at that point of the sample; pass a negative value for no sync.
  // Returns the sub-sample position where the square went high, or -1.
  double process(double ic, double syncFrac = -1.0) {
    const double slope = ic / c_;
    const double dvTotal = std::max(slope * dt_, 1e-12);
    const double vthEff = vth_ + slope * td_;
    double t = 0.0;
    double rising = -1.0;

    auto advanceTo = [&](double tEnd) {
      for (int guard = 0; guard < 32 && t < tEnd; ++guard) {
        const double remaining = (tEnd - t) * dvTotal;
        const double target = dir_ * vthEff;
        const double dist = (target - v_) * dir_;
        if (dist > remaining) {
          v_ += dir_ * remaining;
          t = tEnd;
          return;
        }
        const double frac = t + std::max(dist, 0.0) / dvTotal;
        v_ = target;
        dir_ = -dir_;
        addBlep(kSq, frac, 2.0 * kOutAmp * dir_);
        if (dir_ > 0 && rising < 0) rising = frac;
        t = frac;
      }
      t = tEnd;
    };

    if (syncFrac >= 0.0 && syncFrac < 1.0) {
      advanceTo(syncFrac);
      const double oldTri = v_;
      v_ = -vthEff;
      if (dir_ < 0) {
        dir_ = 1;
        addBlep(kSq, syncFrac, 2.0 * kOutAmp);
      }
      addBlep(kTri, syncFrac, (v_ - oldTri) * kOutAmp / vth_);
      advanceTo(1.0);
    } else {
      advanceTo(1.0);
    }

    // Emit the held (previous) sample with its corrections; hold this one.
    out_[kTri] = held_[kTri];
    out_[kSq] = held_[kSq];
    held_[kTri] = v_ * kOutAmp / vth_ + next_[kTri];
    held_[kSq] = kOutAmp * dir_ + next_[kSq];
    next_[kTri] = next_[kSq] = 0.0;
    return rising;
  }

  double triangle() const { return out_[kTri]; }  // volts, about +/-5 V
  double square() const { return out_[kSq]; }     // volts, +/-5 V
  // Unfiltered instantaneous triangle, used as the FM source (no latency).
  double triangleNow() const { return v_ * kOutAmp / vth_; }

  double frequencyFor(double ic) const { return ic / (4.0 * c_ * (vth_ + ic / c_ * td_)); }

 private:
  static constexpr int kTri = 0, kSq = 1;
  static constexpr double kOutAmp = 5.0;

  void addBlep(int ch, double d, double delta) {
    held_[ch] += delta * 0.5 * (1.0 - d) * (1.0 - d);
    next_[ch] -= delta * 0.5 * d * d;
  }

  double dt_ = 1.0 / 192000, c_ = 4.7e-9, vth_ = 5.0, td_ = 2e-7;
  double v_ = 0;
  int dir_ = 1;
  double held_[2] = {0, 0}, next_[2] = {0, 0}, out_[2] = {0, 0};
};

}  // namespace dfam
