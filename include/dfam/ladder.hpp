// Moog transistor ladder, solved at the component level.
//
// Each of the four stages is a differential transistor pair loaded by a
// capacitor C between the collectors. With control (tail) current I, Ebers-Moll
// transistors give, per stage (Huovilainen 2004; D'Angelo & Valimaki 2013):
//
//   dV1/dt = I/(2C) * ( tanh((Vin - 4k V4)/(2Vt)) - tanh(V1/(2Vt)) )
//   dVn/dt = I/(2C) * ( tanh(V(n-1)/(2Vt))       - tanh(Vn/(2Vt)) )
//
// so the small-signal cutoff is wc = I / (4 C Vt). The system is integrated
// with the trapezoidal rule and a full Newton solve (no unit-delay in the
// resonance loop), which keeps tuning and resonance correct up to high cutoff.
//
// DFAM's VCF has an LP/HP switch. Moog has not published how the HP is made;
// here it is the binomial tap mix of the ladder stages (a 4-pole high-pass
// sharing the same resonance loop). Treat that as an assumption.
#pragma once
#include "components.hpp"

namespace dfam {

class LadderFilter {
 public:
  void setup(double fs, double cap, double tempK, uint64_t seed = 1, double inputNoise = 2e-7) {
    dt_ = 1.0 / fs;
    noise_ = inputNoise;
    c_ = cap;
    vt_ = thermalVoltage(tempK);
    rng_ = Rng(seed);
    for (double& x : v_) x = 0.0;
    vinPrev_ = 0.0;
  }

  double cutoffHz(double current) const { return current / (4.0 * c_ * vt_) / (2.0 * kPi); }
  double currentForCutoff(double hz) const { return 2.0 * kPi * hz * 4.0 * c_ * vt_; }

  // vin: differential input voltage at the ladder base (tens of mV is "clean").
  // current: control current in A. k: resonance (self-oscillates near 1).
  void process(double vin, double current, double k) {
    // Thermal noise at the input pair so resonance can start oscillating.
    vin += rng_.gaussian() * noise_;
    const double g = current / (2.0 * c_);
    const double h = 0.5 * dt_;

    double fOld[4];
    deriv(vinPrev_, v_, g, k, fOld);

    double x[4];
    for (int i = 0; i < 4; ++i) x[i] = v_[i] + dt_ * fOld[i];  // Euler predictor

    for (int it = 0; it < 8; ++it) {
      double f[4], J[4][4] = {};
      deriv(vin, x, g, k, f);
      const double u = vin - 4.0 * k * x[3];
      const double du = dT(u), d0 = dT(x[0]), d1 = dT(x[1]), d2 = dT(x[2]), d3 = dT(x[3]);
      // Residual F = x - v - h*(fOld + f); Jacobian = I - h * df/dx.
      double F[4];
      for (int i = 0; i < 4; ++i) F[i] = x[i] - v_[i] - h * (fOld[i] + f[i]);
      J[0][0] = 1.0 + h * g * d0;
      J[0][3] = h * g * 4.0 * k * du;
      J[1][0] = -h * g * d0;
      J[1][1] = 1.0 + h * g * d1;
      J[2][1] = -h * g * d1;
      J[2][2] = 1.0 + h * g * d2;
      J[3][2] = -h * g * d2;
      J[3][3] = 1.0 + h * g * d3;
      double dx[4];
      solve4(J, F, dx);
      double maxStep = 0.0;
      for (int i = 0; i < 4; ++i) {
        x[i] -= dx[i];
        maxStep = std::max(maxStep, std::fabs(dx[i]));
      }
      if (maxStep < 1e-10) break;
    }
    for (int i = 0; i < 4; ++i) v_[i] = x[i];
    vinPrev_ = vin;
    u_ = vin - 4.0 * k * v_[3];
  }

  double lowpass() const { return v_[3]; }
  double highpass() const { return u_ - 4.0 * v_[0] + 6.0 * v_[1] - 4.0 * v_[2] + v_[3]; }

 private:
  double T(double x) const { return std::tanh(x / (2.0 * vt_)); }
  double dT(double x) const {
    double t = std::tanh(x / (2.0 * vt_));
    return (1.0 - t * t) / (2.0 * vt_);
  }
  void deriv(double vin, const double* x, double g, double k, double* f) const {
    const double t0 = T(x[0]), t1 = T(x[1]), t2 = T(x[2]), t3 = T(x[3]);
    f[0] = g * (T(vin - 4.0 * k * x[3]) - t0);
    f[1] = g * (t0 - t1);
    f[2] = g * (t1 - t2);
    f[3] = g * (t2 - t3);
  }
  static void solve4(double A[4][4], const double* b, double* xOut) {
    double M[4][5];
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) M[i][j] = A[i][j];
      M[i][4] = b[i];
    }
    for (int c = 0; c < 4; ++c) {
      int p = c;
      for (int r = c + 1; r < 4; ++r)
        if (std::fabs(M[r][c]) > std::fabs(M[p][c])) p = r;
      if (p != c)
        for (int j = 0; j < 5; ++j) std::swap(M[c][j], M[p][j]);
      for (int r = c + 1; r < 4; ++r) {
        double m = M[r][c] / M[c][c];
        for (int j = c; j < 5; ++j) M[r][j] -= m * M[c][j];
      }
    }
    for (int i = 3; i >= 0; --i) {
      double s = M[i][4];
      for (int j = i + 1; j < 4; ++j) s -= M[i][j] * xOut[j];
      xOut[i] = s / M[i][i];
    }
  }

  double dt_ = 1.0 / 192000, c_ = 6.8e-8, vt_ = 0.0257, noise_ = 2e-7;
  double v_[4] = {0, 0, 0, 0};
  double vinPrev_ = 0.0, u_ = 0.0;
  Rng rng_;
};

}  // namespace dfam
