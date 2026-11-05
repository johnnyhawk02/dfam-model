// dfam_render: render a DFAM patch file to a 24-bit mono WAV.
//
//   dfam_render <patch.txt> <out.wav> [--seconds 8] [--rate 48000] [key=value ...]
//
// Patch files are "key = value" lines; '#' starts a comment. Extra key=value
// arguments override the file. Run with --keys to list every key, and
// --circuit to print the estimated component values with their defaults.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "dfam/dfam.hpp"

using dfam::Patch;

namespace {

std::string trim(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  const auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<double> parseList(const std::string& v) {
  std::vector<double> out;
  std::stringstream ss(v);
  std::string item;
  while (std::getline(ss, item, ',')) out.push_back(std::stod(trim(item)));
  return out;
}

using Setter = std::function<void(Patch&, const std::string&)>;

// Estimated component values, settable like panel keys. SI units.
struct CircuitKey {
  const char* key;
  double dfam::Circuit::*field;
  const char* help;
};
const std::vector<CircuitKey>& circuitKeys() {
  using C = dfam::Circuit;
  static const std::vector<CircuitKey> k = {
      {"vco_cap", &C::vcoCap, "VCO timing capacitor, F"},
      {"vco_cap_tol", &C::vcoCapTol, "VCO cap tolerance (relative, ~3 sigma)"},
      {"vco_threshold", &C::vcoThreshold, "VCO comparator thresholds, +/- V"},
      {"vco_comp_delay", &C::vcoCompDelay, "VCO comparator + switch delay, s"},
      {"vco_comp_delay_tol", &C::vcoCompDelayTol, "comparator delay tolerance"},
      {"vco_max_hz", &C::vcoMaxHz, "VCO expo current limit, Hz"},
      {"pitch_eg_octaves", &C::pitchEgOctaves, "pitch EG amount at full knob, octaves"},
      {"vco_expo_rbulk", &C::vcoExpoRBulk, "VCO expo bulk emitter resistance, ohm"},
      {"vcf_expo_rbulk", &C::vcfExpoRBulk, "VCF expo bulk emitter resistance, ohm"},
      {"expo_rbulk_tol", &C::expoRBulkTol, "bulk resistance tolerance"},
      {"expo_vbe_offset_sd", &C::expoVbeOffsetSd, "expo pair Vbe mismatch, V (1 sigma)"},
      {"expo_scale_err_sd", &C::expoScaleErrSd, "1 V/oct trim error (relative, 1 sigma)"},
      {"trim_temperature", &C::trimTemperatureC, "temperature the expo scale was trimmed at, C"},
      {"mixer_gain", &C::mixerGain, "mixer summer gain"},
      {"opamp_rail", &C::opampRail, "op-amp output swing (mixer, VCF out), +/- V"},
      {"noise_amp", &C::noiseAmp, "noise source level, V rms"},
      {"noise_bw", &C::noiseBandwidth, "noise low-pass corner, Hz"},
      {"vcf_cap", &C::vcfCap, "ladder stage capacitors, F"},
      {"vcf_cap_tol", &C::vcfCapTol, "ladder cap tolerance"},
      {"vcf_max_hz", &C::vcfMaxHz, "VCF control current limit, Hz"},
      {"vcf_eg_octaves", &C::vcfEgOctaves, "VCF EG amount at full knob, octaves"},
      {"noise_vcf_scale", &C::noiseVcfScale, "noise -> VCF CV, V per unit knob"},
      {"resonance_max", &C::resonanceMax, "ladder loop gain at full resonance"},
      {"ladder_in", &C::ladderInputScale, "mixer V -> ladder input V (drive)"},
      {"ladder_noise", &C::ladderNoise, "ladder input thermal noise, V rms"},
      {"eg_cap", &C::egCap, "envelope capacitors, F"},
      {"eg_cap_tol", &C::egCapTol, "envelope cap tolerance"},
      {"eg_supply", &C::egSupply, "envelope attack supply, V"},
      {"eg_diode", &C::egDiodeDrop, "envelope steering diode drop, V"},
      {"eg_peak", &C::egPeak, "envelope peak (comparator), V; keep below supply - diode"},
      {"eg_attack_r", &C::egAttackR, "fast attack resistor, ohm"},
      {"vca_slow_attack_r", &C::vcaSlowAttackR, "VCA slow attack resistor, ohm"},
      {"decay_pot_r", &C::decayPotR, "decay pot, ohm"},
      {"decay_min_r", &C::decayMinR, "decay end-stop resistor, ohm"},
      {"decay_taper", &C::decayTaper, "decay pot audio-taper base"},
      {"vca_rabc", &C::vcaRabc, "VCA envelope -> Iabc resistor, ohm"},
      {"vca_div_top", &C::vcaDivTop, "VCA input divider top, ohm"},
      {"vca_div_bottom", &C::vcaDivBottom, "VCA input divider bottom, ohm"},
      {"vca_rf", &C::vcaRf, "VCA I-to-V feedback resistor, ohm"},
      {"vca_feedthrough_sd", &C::vcaFeedthroughSd, "VCA control feedthrough (1 sigma)"},
      {"output_hp", &C::outputHpHz, "output coupling high-pass, Hz"},
  };
  return k;
}

// Shortest decimal text that reads back as the same double.
std::string shortest(double v) {
  char buf[32];
  for (int prec = 1; prec <= 17; ++prec) {
    std::snprintf(buf, sizeof buf, "%.*g", prec, v);
    if (std::stod(buf) == v) break;
  }
  // Prefer 22000 over 2.2e+04 for whole numbers.
  if (std::strchr(buf, '+') && v == std::floor(v) && std::fabs(v) < 1e15)
    std::snprintf(buf, sizeof buf, "%.0f", v);
  std::string out = buf;  // 4.7e-09 -> 4.7e-9, as JS prints it
  for (const char* pat : {"e-0", "e+0"}) {
    const auto at = out.find(pat);
    if (at != std::string::npos) out.erase(at + 2, 1);
  }
  return out;
}

std::map<std::string, Setter> setters() {
  std::map<std::string, Setter> m;
  auto num = [&](const char* key, double Patch::*field) {
    m[key] = [field](Patch& p, const std::string& v) { p.*field = std::stod(v); };
  };
  num("tempo", &Patch::tempoBpm);
  num("vco1_freq", &Patch::vco1FreqHz);
  num("vco2_freq", &Patch::vco2FreqHz);
  num("vco_decay", &Patch::vcoDecay);
  num("vco1_eg_amount", &Patch::vco1EgAmount);
  num("vco2_eg_amount", &Patch::vco2EgAmount);
  num("fm_1_2", &Patch::fm12Amount);
  num("vco1_level", &Patch::vco1Level);
  num("vco2_level", &Patch::vco2Level);
  num("noise_level", &Patch::noiseLevel);
  num("cutoff", &Patch::cutoffHz);
  num("resonance", &Patch::resonance);
  num("vcf_eg_amount", &Patch::vcfEgAmount);
  num("vcf_decay", &Patch::vcfDecay);
  num("noise_vcf_mod", &Patch::noiseVcfMod);
  num("vca_decay", &Patch::vcaDecay);
  num("volume", &Patch::volume);
  num("temperature", &Patch::temperatureC);
  for (const auto& ck : circuitKeys()) {
    auto field = ck.field;
    m[ck.key] = [field](Patch& p, const std::string& v) { p.circuit.*field = std::stod(v); };
  }
  m["steps_per_beat"] = [](Patch& p, const std::string& v) { p.stepsPerBeat = std::stoi(v); };
  m["seed"] = [](Patch& p, const std::string& v) { p.seed = std::stoull(v); };
  m["hard_sync"] = [](Patch& p, const std::string& v) { p.hardSync = v == "on" || v == "1"; };
  m["vca_attack"] = [](Patch& p, const std::string& v) { p.vcaAttackSlow = v == "slow"; };
  m["vco1_wave"] = [](Patch& p, const std::string& v) {
    p.vco1Wave = v == "square" ? dfam::Wave::Square : dfam::Wave::Triangle;
  };
  m["vco2_wave"] = [](Patch& p, const std::string& v) {
    p.vco2Wave = v == "square" ? dfam::Wave::Square : dfam::Wave::Triangle;
  };
  m["vcf_mode"] = [](Patch& p, const std::string& v) {
    p.vcfMode = v == "hp" ? dfam::VcfMode::HighPass : dfam::VcfMode::LowPass;
  };
  m["seq_pitch_mod"] = [](Patch& p, const std::string& v) {
    p.seqPitchMod = v == "off"    ? dfam::SeqPitchMod::Off
                    : v == "vco2" ? dfam::SeqPitchMod::Vco2
                                  : dfam::SeqPitchMod::Both;
  };
  m["pitch"] = [](Patch& p, const std::string& v) {
    auto l = parseList(v);
    for (size_t i = 0; i < 8 && i < l.size(); ++i) p.pitch[i] = l[i];
  };
  m["velocity"] = [](Patch& p, const std::string& v) {
    auto l = parseList(v);
    for (size_t i = 0; i < 8 && i < l.size(); ++i) p.velocity[i] = l[i];
  };
  return m;
}

bool apply(Patch& p, const std::string& line, const std::map<std::string, Setter>& s) {
  std::string l = line.substr(0, line.find('#'));
  const auto eq = l.find('=');
  if (eq == std::string::npos) return trim(l).empty();
  const std::string key = trim(l.substr(0, eq)), val = trim(l.substr(eq + 1));
  auto it = s.find(key);
  if (it == s.end()) {
    std::cerr << "unknown key: " << key << "\n";
    return false;
  }
  it->second(p, val);
  return true;
}

void writeWav(const std::string& path, const std::vector<float>& x, int rate) {
  std::ofstream f(path, std::ios::binary);
  auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
  auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
  const uint32_t dataBytes = static_cast<uint32_t>(x.size() * 3);
  f.write("RIFF", 4);
  u32(36 + dataBytes);
  f.write("WAVEfmt ", 8);
  u32(16);
  u16(1);
  u16(1);
  u32(rate);
  u32(rate * 3);
  u16(3);
  u16(24);
  f.write("data", 4);
  u32(dataBytes);
  for (float s : x) {
    const double c = std::max(-1.0, std::min(1.0, static_cast<double>(s)));
    const int32_t v = static_cast<int32_t>(std::lround(c * 8388607.0));
    const char b[3] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff)};
    f.write(b, 3);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto s = setters();
  if (argc >= 2 && std::string(argv[1]) == "--keys") {
    for (const auto& kv : s) std::cout << kv.first << "\n";
    return 0;
  }
  if (argc >= 2 && std::string(argv[1]) == "--circuit") {
    // Defaults of the estimated component values, as preset lines.
    const dfam::Circuit def;
    for (const auto& ck : circuitKeys()) {
      std::string line = std::string(ck.key) + " = " + shortest(def.*ck.field);
      line.resize(std::max<size_t>(line.size() + 1, 32), ' ');
      std::cout << line << "# " << ck.help << "\n";
    }
    return 0;
  }
  if (argc < 3) {
    std::cerr << "usage: dfam_render <patch.txt> <out.wav> [--seconds N] [--rate HZ] "
                 "[key=value ...]\n       dfam_render --keys | --circuit\n";
    return 1;
  }
  Patch patch;
  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "cannot open " << argv[1] << "\n";
    return 1;
  }
  std::string line;
  bool ok = true;
  while (std::getline(in, line)) ok &= apply(patch, line, s);

  double seconds = 8.0;
  int rate = 48000;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
    else if (a == "--rate" && i + 1 < argc) rate = std::atoi(argv[++i]);
    else ok &= apply(patch, a, s);
  }
  if (!ok) return 1;

  dfam::Dfam synth;
  synth.setup(rate, patch);
  std::vector<float> out(static_cast<size_t>(seconds * rate));
  synth.render(out.data(), static_cast<int>(out.size()));

  float peak = 0;
  for (float v : out) peak = std::max(peak, std::fabs(v));
  writeWav(argv[2], out, rate);
  std::printf("wrote %s (%.1f s, peak %.2f)\n", argv[2], seconds, peak);
  return 0;
}
