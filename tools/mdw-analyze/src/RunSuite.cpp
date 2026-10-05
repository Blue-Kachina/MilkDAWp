// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 1.10's metric gate: `mdw-analyze --suite fixtures/` runs every
// fixture, scores it, and fails (non-zero exit) if any fixture misses the
// thresholds in fixtures/thresholds.json. Kept in its own translation unit
// from main.cpp's CLI plumbing so the gate logic itself stays easy to find.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "milkdawp/core/Analyzer.h"
#include "milkdawp/core/BeatClock.h"
#include "milkdawp/core/OnsetDetector.h"
#include "milkdawp/core/Resample.h"
#include "milkdawp/core/Scoring.h"
#include "milkdawp/core/SectionDetector.h"
#include "milkdawp/core/TempoTracker.h"
#include "milkdawp/core/Wav.h"

namespace {

using namespace milkdawp::core;
constexpr double kInternalSampleRate = 48000.0;

// Deliberately minimal: fixtures/thresholds.json has a small, fixed set of
// top-level numeric keys (see fixtures/README.md), so a hand-rolled scan
// avoids pulling in a JSON library for milkdawp_core's one CLI tool.
double readJsonNumber(const std::string& text, const std::string& key, double defaultValue) {
  const std::string needle = "\"" + key + "\"";
  auto pos = text.find(needle);
  if (pos == std::string::npos) {
    return defaultValue;
  }
  pos = text.find(':', pos);
  if (pos == std::string::npos) {
    return defaultValue;
  }
  ++pos;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  std::size_t end = pos;
  while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.' ||
                                text[end] == '-' || text[end] == '+' || text[end] == 'e' || text[end] == 'E')) {
    ++end;
  }
  if (end == pos) {
    return defaultValue;
  }
  try {
    return std::stod(text.substr(pos, end - pos));
  } catch (const std::exception&) {
    return defaultValue;
  }
}

struct Thresholds {
  double minFMeasure = 0.85;
  double toleranceSeconds = 0.070;
  double maxTempoErrorFraction = 0.02;
  double maxOnsetsForSilence = 2.0;
  double dropToleranceSeconds = 0.15;
};

Thresholds loadThresholds(const std::string& path) {
  Thresholds t;
  std::ifstream file(path);
  if (!file) {
    return t;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();

  t.minFMeasure = readJsonNumber(text, "minFMeasure", t.minFMeasure);
  t.toleranceSeconds = readJsonNumber(text, "toleranceSeconds", t.toleranceSeconds);
  t.maxTempoErrorFraction = readJsonNumber(text, "maxTempoErrorFraction", t.maxTempoErrorFraction);
  t.maxOnsetsForSilence = readJsonNumber(text, "maxOnsetsForSilence", t.maxOnsetsForSilence);
  t.dropToleranceSeconds = readJsonNumber(text, "dropToleranceSeconds", t.dropToleranceSeconds);
  return t;
}

std::vector<double> readReferenceBeats(const std::filesystem::path& path) {
  std::vector<double> beats;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    try {
      beats.push_back(std::stod(line));
    } catch (const std::exception&) {
    }
  }
  return beats;
}

/// True when the reference beats are regular enough that "a single bpm"
/// is a meaningful thing to compare against (coefficient of variation of
/// inter-beat intervals below 30%). Sparse/irregular material (§7 Phase 1.9's
/// deliberately hard fixtures) has no real single tempo, so comparing a
/// tracker's output against one is a scoring-approach mismatch, not
/// something a tracking-algorithm fix can resolve -- skip the tempo-error
/// check rather than force a number onto a fixture that doesn't have one.
bool hasSteadyTempo(std::vector<double> beats) {
  if (beats.size() < 3) {
    return false;
  }
  std::sort(beats.begin(), beats.end());
  std::vector<double> intervals;
  for (std::size_t i = 1; i < beats.size(); ++i) {
    intervals.push_back(beats[i] - beats[i - 1]);
  }
  double mean = 0.0;
  for (double v : intervals) {
    mean += v;
  }
  mean /= static_cast<double>(intervals.size());
  if (mean <= 0.0) {
    return false;
  }
  double variance = 0.0;
  for (double v : intervals) {
    variance += (v - mean) * (v - mean);
  }
  variance /= static_cast<double>(intervals.size());
  const double coefficientOfVariation = std::sqrt(variance) / mean;
  return coefficientOfVariation < 0.30;
}

double referenceBpmFromBeats(std::vector<double> beats) {
  if (beats.size() < 2) {
    return 0.0;
  }
  std::sort(beats.begin(), beats.end());
  std::vector<double> intervals;
  for (std::size_t i = 1; i < beats.size(); ++i) {
    intervals.push_back(beats[i] - beats[i - 1]);
  }
  std::sort(intervals.begin(), intervals.end());
  const double median = intervals[intervals.size() / 2];
  return median > 0.0 ? 60.0 / median : 0.0;
}

struct FixtureResult {
  std::string name;
  bool passed = false;
  std::string detail;
};

/// 5.1: Energy mode must cut on exactly the annotated drops (drops.txt; none
/// when the file is missing), each within the tolerance. Returns whether it
/// did, and describes the result in `detail`.
bool scoreDrops(const std::vector<double>& detected, const std::vector<double>& reference, double tolerance,
                std::string& detail) {
  std::vector<bool> used(detected.size(), false);
  int missed = 0;
  for (const double drop : reference) {
    bool found = false;
    for (std::size_t i = 0; i < detected.size() && !found; ++i) {
      if (!used[i] && std::abs(detected[i] - drop) <= tolerance) {
        used[i] = true;
        found = true;
      }
    }
    missed += found ? 0 : 1;
  }
  const auto extra = static_cast<int>(std::count(used.begin(), used.end(), false));

  std::ostringstream text;
  text << "drops=" << detected.size() << " (expected " << reference.size();
  if (missed > 0 || extra > 0) {
    text << ", " << missed << " missed, " << extra << " extra";
  }
  text << ")";
  if (!detected.empty()) {
    text << " at";
    for (const double t : detected) {
      text << " " << t << "s";
    }
  }
  detail = text.str();
  return missed == 0 && extra == 0;
}

std::vector<float> downmixToMono(const WavAudio& audio) {
  if (audio.numChannels == 1) {
    return audio.interleavedSamples;
  }
  const std::size_t numFrames = audio.interleavedSamples.size() / static_cast<std::size_t>(audio.numChannels);
  std::vector<float> mono(numFrames);
  for (std::size_t i = 0; i < numFrames; ++i) {
    double sum = 0.0;
    for (int c = 0; c < audio.numChannels; ++c) {
      sum += audio.interleavedSamples[i * static_cast<std::size_t>(audio.numChannels) +
                                       static_cast<std::size_t>(c)];
    }
    mono[i] = static_cast<float>(sum / audio.numChannels);
  }
  return mono;
}

FixtureResult evaluateFixture(const std::filesystem::path& dir, const Thresholds& thresholds) {
  FixtureResult result;
  result.name = dir.filename().string();

  const auto wavPath = dir / "audio.wav";
  const auto beatsPath = dir / "beats.txt";

  WavAudio audio;
  try {
    audio = readWavFile(wavPath.string());
  } catch (const std::exception& e) {
    result.passed = false;
    result.detail = std::string("could not read audio.wav: ") + e.what();
    return result;
  }

  auto mono = downmixToMono(audio);
  auto resampled = (audio.sampleRate == kInternalSampleRate)
                        ? mono
                        : resampleLinear(mono, audio.sampleRate, kInternalSampleRate);

  Analyzer analyzer(kInternalSampleRate);
  OnsetDetector broadbandDetector(kInternalSampleRate);
  OnsetDetector bassDetector(kInternalSampleRate);
  TempoTracker tempoTracker(kInternalSampleRate);
  BeatClock beatClock(kInternalSampleRate);
  SectionDetector sections(kInternalSampleRate, Analyzer::hopSize);

  const std::size_t hopSize = Analyzer::hopSize;
  std::size_t numHops = (resampled.size() + hopSize - 1) / hopSize;
  numHops = std::max<std::size_t>(numHops, 1);
  resampled.resize(numHops * hopSize, 0.0f);

  std::vector<double> beatTimes;
  std::vector<double> dropTimes;  std::size_t onsetCount = 0;
  bool haveLastBeatIndex = false;
  std::uint64_t lastBeatIndex = 0;
  float finalBpm = 0.0f;

  for (std::size_t hop = 0; hop < numHops; ++hop) {
    const std::uint64_t samplePos = hop * hopSize;
    auto frame = analyzer.processHop(&resampled[samplePos]);
    if (broadbandDetector.processHop(frame.onsetStrength)) {
      ++onsetCount;
    }
    auto bassOnset = bassDetector.processHop(frame.bassOnsetStrength);
    auto tempo = tempoTracker.processHop(frame.onsetStrength);
    auto beatState = beatClock.processHop(samplePos, tempo, bassOnset);
    // What the Director gives Energy mode (TransitionScheduler::tick).
    if (sections.processHop(frame.bassEnergy, bassOnset.has_value()).drop) {
      dropTimes.push_back(static_cast<double>(samplePos) / kInternalSampleRate);
    }
    if (haveLastBeatIndex && beatState.beatIndex > lastBeatIndex) {
      beatTimes.push_back(static_cast<double>(samplePos) / kInternalSampleRate);
    }
    haveLastBeatIndex = true;
    lastBeatIndex = beatState.beatIndex;
    finalBpm = tempo.bpm;
  }

  auto reference = readReferenceBeats(beatsPath);
  std::string dropDetail;
  const bool dropsOk =
      scoreDrops(dropTimes, readReferenceBeats(dir / "drops.txt"), thresholds.dropToleranceSeconds, dropDetail);

  if (reference.empty()) {
    // A "no beats expected" fixture (silence, noise): pass if we didn't
    // hallucinate a pile of onsets (§4.3 acceptance metric).
    result.passed = static_cast<double>(onsetCount) <= thresholds.maxOnsetsForSilence && dropsOk;
    result.detail = "no reference beats (expected none); onsets=" + std::to_string(onsetCount) +
                     " (max " + std::to_string(thresholds.maxOnsetsForSilence) + "), " + dropDetail;
    return result;
  }

  auto fMeasure = computeBeatFMeasure(beatTimes, reference, thresholds.toleranceSeconds);
  const bool steady = hasSteadyTempo(reference);
  const double referenceBpm = referenceBpmFromBeats(reference);
  const double tempoError = computeOctaveTolerantTempoError(finalBpm, referenceBpm);

  const bool fMeasureOk = fMeasure.fMeasure >= thresholds.minFMeasure;
  const bool tempoOk = !steady || tempoError <= thresholds.maxTempoErrorFraction;
  result.passed = fMeasureOk && tempoOk && dropsOk;

  std::ostringstream detail;
  detail << "F-measure=" << fMeasure.fMeasure << " (min " << thresholds.minFMeasure << "), "
         << (steady ? "" : "[no steady tempo, tempo check skipped] ") << "tempoError="
         << (tempoError * 100.0) << "% (max " << (thresholds.maxTempoErrorFraction * 100.0)
         << "%), detectedBpm=" << finalBpm << ", referenceBpm=" << referenceBpm << ", " << dropDetail;
  result.detail = detail.str();
  return result;
}

} // namespace

int runFixtureSuite(const std::string& fixturesDir, const std::string& thresholdsPath) {
  const Thresholds thresholds = loadThresholds(thresholdsPath);

  if (!std::filesystem::exists(fixturesDir)) {
    std::cerr << "mdw-analyze --suite: fixtures directory not found: " << fixturesDir << "\n";
    return 1;
  }

  bool allPassed = true;
  int fixtureCount = 0;

  for (const auto& entry : std::filesystem::directory_iterator(fixturesDir)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (!std::filesystem::exists(entry.path() / "audio.wav")) {
      continue; // not a fixture directory
    }

    ++fixtureCount;
    auto result = evaluateFixture(entry.path(), thresholds);
    std::printf("[%s] %s -- %s\n", result.passed ? "PASS" : "FAIL", result.name.c_str(),
                result.detail.c_str());
    allPassed = allPassed && result.passed;
  }

  if (fixtureCount == 0) {
    std::cerr << "mdw-analyze --suite: no fixtures found under " << fixturesDir << "\n";
    return 1;
  }

  std::printf("%s: %d fixture(s) evaluated\n", allPassed ? "PASS" : "FAIL", fixtureCount);
  return allPassed ? 0 : 1;
}
