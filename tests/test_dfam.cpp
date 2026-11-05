// Checks that the component models behave like the circuits they model.
#include <cstdio>
#include <vector>

#include "dfam/dfam.hpp"

using namespace dfam;

static int failures = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    std::printf("%s  ", (cond) ? "ok  " : "FAIL"); \
    std::printf(__VA_ARGS__);                    \
    std::printf("\n");                           \
    if (!(cond)) ++failures;                     \
  } while (0)

static double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

// Measure VCO frequency from square-wave rising edges.
static double measureVco(double cv, double baseHz, double rE, double tempC = 25.0) {
  const double fs = 192000, cap = 4.7e-9;
  TriangleCoreVco vco;
  vco.setup(fs, cap, 5.0, 0.0);
  ExpoConverter expo;
  expo.setup(baseHz * 4 * cap * 5.0, 273.15 + tempC, 298.15, rE, 0.0, 0.0);
  const double ic = expo.current(cv, 1.0);
  double first = -1, last = -1;
  int edges = 0;
  for (int n = 0; n < 192000; ++n) {
    double r = vco.process(ic);
    if (r >= 0) {
      double t = n + r;
      if (first < 0) first = t;
      last = t;
      ++edges;
    }
  }
  return (edges - 1) / ((last - first) / fs);
}

static void testVco() {
  // Ideal expo (no bulk resistance): exact 1 V/oct tracking.
  for (int v = 0; v <= 6; v += 2) {
    double f = measureVco(v, 55.0, 0.0), want = 55.0 * std::pow(2.0, v);
    CHECK(std::fabs(cents(f, want)) < 1.0, "VCO ideal expo %d V: %.2f Hz (want %.2f, %+.2f c)",
          v, f, want, cents(f, want));
  }
  // With 2 ohm bulk resistance the top octaves run flat, as in hardware.
  double fHi = measureVco(7, 55.0, 2.0), wantHi = 55.0 * 128;
  CHECK(cents(fHi, wantHi) < -5.0 && cents(fHi, wantHi) > -150.0,
        "VCO with 2 ohm rE at 7 V runs flat: %+.1f c", cents(fHi, wantHi));
  // Temperature drift: 10 C warmer than trim should sharpen/flatten scale ~3%/oct.
  double fWarm = measureVco(4, 55.0, 0.0, 35.0), want4 = 55.0 * 16;
  CHECK(std::fabs(cents(fWarm, want4)) > 50.0, "VCO 10 C above trim at 4 V drifts %+.1f c",
        cents(fWarm, want4));
}

static void testLadder() {
  const double fs = 192000, fc = 1000.0;
  // Small-signal gain at cutoff, no resonance: |1/(1+j)^4| = -12.04 dB.
  {
    LadderFilter lf;
    lf.setup(fs, 68e-9, 298.15);
    const double I = lf.currentForCutoff(fc);
    double peak = 0;
    for (int n = 0; n < 96000; ++n) {
      lf.process(0.001 * std::sin(2 * kPi * fc * n / fs), I, 0.0);
      if (n > 48000) peak = std::max(peak, std::fabs(lf.lowpass()));
    }
    double db = 20 * std::log10(peak / 0.001);
    CHECK(std::fabs(db + 12.04) < 0.5, "Ladder LP gain at cutoff: %.2f dB (want -12.04)", db);
  }
  // Same for the HP taps.
  {
    LadderFilter lf;
    lf.setup(fs, 68e-9, 298.15);
    const double I = lf.currentForCutoff(fc);
    double peak = 0;
    for (int n = 0; n < 96000; ++n) {
      lf.process(0.001 * std::sin(2 * kPi * fc * n / fs), I, 0.0);
      if (n > 48000) peak = std::max(peak, std::fabs(lf.highpass()));
    }
    double db = 20 * std::log10(peak / 0.001);
    CHECK(std::fabs(db + 12.04) < 0.5, "Ladder HP gain at cutoff: %.2f dB (want -12.04)", db);
  }
  // Self-oscillation above k = 1 with no input, near the cutoff frequency.
  {
    LadderFilter lf;
    lf.setup(fs, 68e-9, 298.15, 3);
    const double I = lf.currentForCutoff(fc);
    double peak = 0, prev = 0, firstX = -1, lastX = -1;
    int xs = 0;
    for (int n = 0; n < 192000 * 2; ++n) {
      lf.process(0.0, I, 1.15);
      double y = lf.lowpass();
      if (n > 192000) {
        peak = std::max(peak, std::fabs(y));
        if (prev < 0 && y >= 0) {
          if (firstX < 0) firstX = n;
          lastX = n;
          ++xs;
        }
      }
      prev = y;
    }
    double f = xs > 1 ? (xs - 1) / ((lastX - firstX) / fs) : 0;
    CHECK(peak > 0.005 && std::fabs(cents(f, fc)) < 100,
          "Ladder self-oscillates at k=1.15: %.0f mV at %.1f Hz", peak * 1000, f);
  }
}

static void testEnvelope() {
  const double fs = 192000, C = 2.2e-6, R = 100e3;
  DecayEnvelope eg;
  eg.setup(fs, C);
  eg.trigger();
  int n = 0;
  while (eg.process(470, R) < 8.0 && n < 100000) ++n;
  double attackMs = 1000.0 * n / fs;
  int m = 0;
  while (eg.process(470, R) > 8.0 / std::exp(1.0)) ++m;
  double tau = m / fs;
  CHECK(attackMs > 0.5 && attackMs < 3.0, "EG fast attack %.2f ms", attackMs);
  CHECK(std::fabs(tau - R * C) / (R * C) < 0.01, "EG decay tau %.4f s (want %.4f)", tau, R * C);
}

static void testVoice() {
  Patch p;
  p.noiseLevel = 0.3;
  p.fm12Amount = 0.5;
  p.vco2Level = 0.6;
  p.hardSync = true;
  p.resonance = 0.9;
  Dfam d;
  d.setup(48000, p);
  std::vector<float> buf(48000 * 2);
  d.render(buf.data(), static_cast<int>(buf.size()));
  float peak = 0;
  bool finite = true;
  for (float v : buf) {
    peak = std::max(peak, std::fabs(v));
    finite &= std::isfinite(v);
  }
  CHECK(finite && peak > 0.05 && peak < 1.5, "Full voice renders, peak %.2f", peak);
}

// The estimated component values are wired through: with the pitch EG range
// set to 0 octaves the VCO EG amount knob must have no effect.
static void testCircuitParams() {
  auto render = [](Patch p) {
    Dfam d;
    d.setup(48000, p);
    std::vector<float> buf(24000);
    d.render(buf.data(), static_cast<int>(buf.size()));
    return buf;
  };
  Patch a;
  a.circuit.pitchEgOctaves = 0.0;
  Patch b = a;
  b.vco1EgAmount = 1.0;
  CHECK(render(a) == render(b), "pitch_eg_octaves = 0 disables the pitch EG");
  Patch c;
  c.circuit.resonanceMax = 0.0;
  Patch d = c;
  d.resonance = 1.0;
  CHECK(render(c) == render(d), "resonance_max = 0 disables the resonance knob");
  Patch e;
  e.circuit.vcoExpoRBulk = 20.0;
  CHECK(render(e) != render(Patch()), "vco_expo_rbulk changes the sound");
}

int main() {
  testVco();
  testLadder();
  testEnvelope();
  testVoice();
  testCircuitParams();
  std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures ? 1 : 0;
}
