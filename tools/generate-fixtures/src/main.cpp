// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Generates the Phase 1.9 fixture set: short, synthesized, licence-free
// audio.wav + beats.txt pairs under fixtures/<name>/, per fixtures/README.md.
// Run: generate-fixtures <fixturesDir>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "milkdawp/core/Wav.h"

using milkdawp::core::WavAudio;
using milkdawp::core::writeWavFile;

namespace {

constexpr double kSampleRate = 48000.0;

WavAudio makeSilentBuffer(double durationSeconds) {
  WavAudio audio;
  audio.numChannels = 1;
  audio.sampleRate = kSampleRate;
  audio.interleavedSamples.assign(static_cast<std::size_t>(durationSeconds * kSampleRate), 0.0f);
  return audio;
}

/// Adds a short percussive "kick" burst (decaying low-frequency tone plus a
/// brief noise transient) at `startSample`, mixed additively.
void addKick(std::vector<float>& buffer, std::size_t startSample, std::mt19937& rng, float gain = 0.9f) {
  constexpr double toneFreqHz = 80.0;
  constexpr double toneDecaySeconds = 0.12;
  constexpr double noiseDecaySeconds = 0.01;
  const auto toneLength = static_cast<std::size_t>(toneDecaySeconds * kSampleRate * 5);
  std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

  for (std::size_t i = 0; i < toneLength && startSample + i < buffer.size(); ++i) {
    const double t = static_cast<double>(i) / kSampleRate;
    const float toneEnv = static_cast<float>(std::exp(-t / toneDecaySeconds));
    const float tone = toneEnv * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * toneFreqHz * t));
    const float noiseEnv = static_cast<float>(std::exp(-t / noiseDecaySeconds));
    const float noise = noiseEnv * noiseDist(rng);
    buffer[startSample + i] += gain * (0.85f * tone + 0.5f * noise);
  }
}

void writeFixture(const std::filesystem::path& fixturesDir, const std::string& name, const WavAudio& audio,
                   const std::vector<double>& beatTimes, const std::string& sourceNote) {
  const auto dir = fixturesDir / name;
  std::filesystem::create_directories(dir);
  writeWavFile((dir / "audio.wav").string(), audio);

  std::ofstream beatsFile(dir / "beats.txt");
  for (double t : beatTimes) {
    beatsFile << t << "\n";
  }

  std::ofstream sourceFile(dir / "SOURCE.md");
  sourceFile << "Synthesized by tools/generate-fixtures (Phase 1.9). " << sourceNote << "\n"
             << "Licence: CC0 (generated, no external source material).\n";

  std::printf("wrote %s (%zu beats)\n", dir.string().c_str(), beatTimes.size());
}

std::vector<double> fourOnTheFloor(WavAudio& audio, double bpm, double durationSeconds, std::mt19937& rng) {
  audio = makeSilentBuffer(durationSeconds);
  const double periodSeconds = 60.0 / bpm;
  std::vector<double> beats;
  for (double t = periodSeconds; t < durationSeconds - periodSeconds; t += periodSeconds) {
    addKick(audio.interleavedSamples, static_cast<std::size_t>(t * kSampleRate), rng);
    beats.push_back(t);
  }
  return beats;
}

std::vector<double> syncopated(WavAudio& audio, double bpm, double durationSeconds, std::mt19937& rng) {
  // 4/4 at `bpm`, but skip beat 1 of every other bar and add an off-beat
  // (halfway between beats 3 and 4) instead -- a genuinely syncopated
  // pattern rather than a plain grid.
  audio = makeSilentBuffer(durationSeconds);
  const double beatSeconds = 60.0 / bpm;
  std::vector<double> beats;
  int beatIndex = 0;
  for (double t = beatSeconds; t < durationSeconds - beatSeconds; t += beatSeconds, ++beatIndex) {
    const int barBeat = beatIndex % 4;
    const int bar = beatIndex / 4;
    const bool skipThisBeat = (barBeat == 0) && (bar % 2 == 1);
    if (!skipThisBeat) {
      addKick(audio.interleavedSamples, static_cast<std::size_t>(t * kSampleRate), rng);
      beats.push_back(t);
    }
    if (barBeat == 2) { // between beat 3 and beat 4: add a syncopated hit
      const double offBeatTime = t + beatSeconds * 0.5;
      addKick(audio.interleavedSamples, static_cast<std::size_t>(offBeatTime * kSampleRate), rng, 0.6f);
      beats.push_back(offBeatTime);
    }
  }
  return beats;
}

std::vector<double> tempoChange(WavAudio& audio, double bpmStart, double bpmEnd, double durationSeconds,
                                 std::mt19937& rng) {
  audio = makeSilentBuffer(durationSeconds);
  const double switchTime = durationSeconds / 2.0;
  std::vector<double> beats;

  double t = 60.0 / bpmStart;
  while (t < switchTime) {
    addKick(audio.interleavedSamples, static_cast<std::size_t>(t * kSampleRate), rng);
    beats.push_back(t);
    t += 60.0 / bpmStart;
  }
  t = switchTime + 60.0 / bpmEnd;
  while (t < durationSeconds - (60.0 / bpmEnd)) {
    addKick(audio.interleavedSamples, static_cast<std::size_t>(t * kSampleRate), rng);
    beats.push_back(t);
    t += 60.0 / bpmEnd;
  }
  return beats;
}

std::vector<double> sparseAcoustic(WavAudio& audio, double durationSeconds, std::mt19937& rng) {
  // Irregular, sparse onsets -- a deliberately hard case (§7 Phase 1.9), not
  // expected to hit the same F-measure bar as steady electronic material.
  audio = makeSilentBuffer(durationSeconds);
  const std::vector<double> times{0.6, 1.9, 2.3, 3.8, 5.5, 6.0, 7.4, 9.1};
  std::vector<double> beats;
  for (double t : times) {
    if (t < durationSeconds) {
      addKick(audio.interleavedSamples, static_cast<std::size_t>(t * kSampleRate), rng, 0.5f);
      beats.push_back(t);
    }
  }
  return beats;
}

WavAudio makeWhiteNoise(double durationSeconds, std::mt19937& rng, float amplitude = 0.2f) {
  WavAudio audio;
  audio.numChannels = 1;
  audio.sampleRate = kSampleRate;
  const auto n = static_cast<std::size_t>(durationSeconds * kSampleRate);
  audio.interleavedSamples.resize(n);
  std::uniform_real_distribution<float> dist(-amplitude, amplitude);
  for (auto& s : audio.interleavedSamples) {
    s = dist(rng);
  }
  return audio;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: generate-fixtures <fixturesDir>\n");
    return 1;
  }
  const std::filesystem::path fixturesDir = argv[1];
  std::mt19937 rng(12345); // fixed seed: fixtures are reproducible byte-for-byte

  WavAudio audio;

  audio = {};
  auto beats = fourOnTheFloor(audio, 128.0, 8.0, rng);
  writeFixture(fixturesDir, "four_on_the_floor", audio, beats,
               "128 BPM kick on every beat for 8 seconds -- the easy case.");

  audio = {};
  beats = syncopated(audio, 120.0, 8.0, rng);
  writeFixture(fixturesDir, "syncopated", audio, beats,
               "120 BPM with a skipped downbeat every other bar plus an off-beat hit.");

  audio = {};
  beats = tempoChange(audio, 100.0, 140.0, 10.0, rng);
  writeFixture(fixturesDir, "tempo_change", audio, beats,
               "100 BPM for the first 5 seconds, 140 BPM for the second 5.");

  audio = {};
  beats = sparseAcoustic(audio, 10.0, rng);
  writeFixture(fixturesDir, "sparse_acoustic", audio, beats,
               "Irregular sparse onsets, no steady tempo -- a deliberately hard case.");

  audio = makeSilentBuffer(5.0);
  writeFixture(fixturesDir, "silence", audio, {}, "5 seconds of digital silence.");

  audio = makeWhiteNoise(5.0, rng);
  writeFixture(fixturesDir, "noise", audio, {}, "5 seconds of white noise at low amplitude.");

  std::printf("done.\n");
  return 0;
}
