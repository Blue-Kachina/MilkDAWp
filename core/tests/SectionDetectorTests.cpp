// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/SectionDetector.h"

using namespace milkdawp::core;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kHopSize = 512;
constexpr double kHopSeconds = static_cast<double>(kHopSize) / kSampleRate;
/// `AnalysisFrame::bassEnergy` of a kick near full scale (measured on the
/// fixtures: about 1.5e5). The tracks below are built relative to 1.0.
constexpr float kKick = 1.0e5f;

/// A synthetic bass track, built one stretch at a time: hits that jump the
/// envelope up and let it fall back with the Analyzer's 200 ms release, the
/// way `AnalysisFrame::bassEnergy` behaves around a kick.
class BassTrack {
public:
  /// `seconds` of hits every `periodSeconds` at `peak` energy, over a floor
  /// of `floor` (a pad's or a bassline's leakage into the bass band).
  BassTrack& hits(double seconds, double periodSeconds, float peak, float floor = 0.0f) {
    const auto hops = hopsFor(seconds);
    const auto period = std::max<std::size_t>(1, hopsFor(periodSeconds));
    for (std::size_t i = 0; i < hops; ++i) {
      const bool hit = i % period == 0;
      if (hit) {
        envelope_ = std::max(envelope_, peak);
      } else {
        envelope_ = static_cast<float>(envelope_ * std::exp(-kHopSeconds / 0.2));
      }
      energy_.push_back(std::max(envelope_, floor));
      onset_.push_back(hit);
    }
    return *this;
  }
  /// `seconds` with no hits, the envelope falling to `floor`.
  BassTrack& quiet(double seconds, float floor = 0.0f) {
    for (std::size_t i = 0; i < hopsFor(seconds); ++i) {
      envelope_ = static_cast<float>(envelope_ * std::exp(-kHopSeconds / 0.2));
      energy_.push_back(std::max(envelope_, floor));
      onset_.push_back(false);
    }
    return *this;
  }
  [[nodiscard]] std::size_t hop() const { return energy_.size(); }

  /// The hops `SectionDetector` called drops.
  [[nodiscard]] std::vector<std::size_t> drops(SectionDetectorConfig config = {}) const {
    SectionDetector detector(kSampleRate, kHopSize, config);
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < energy_.size(); ++i) {
      if (detector.processHop(energy_[i] * kKick, onset_[i]).drop) {
        result.push_back(i);
      }
    }
    return result;
  }

private:
  static std::size_t hopsFor(double seconds) {
    return static_cast<std::size_t>(std::lround(seconds / kHopSeconds));
  }

  float envelope_ = 0.0f;
  std::vector<float> energy_;
  std::vector<bool> onset_;
};

constexpr double kBeat = 0.5; // 120 bpm

} // namespace

TEST_CASE("Steady kicks are never a drop", "[core][SectionDetector]") {
  // The case the old mean + k*sd rule got wrong: in a steady section every
  // kick sits well above the average.
  BassTrack track;
  track.hits(60.0, kBeat, 1.0f);
  CHECK(track.drops().empty());
}

TEST_CASE("A breakdown and then the bass coming back is one drop", "[core][SectionDetector]") {
  BassTrack track;
  track.hits(16.0, kBeat, 1.0f).quiet(8.0, 0.001f);
  const auto dropHop = track.hop();
  track.hits(16.0, kBeat, 1.0f);

  const auto drops = track.drops();
  REQUIRE(drops.size() == 1);
  CHECK(drops[0] == dropHop);
}

TEST_CASE("The detector reports a breakdown before the drop", "[core][SectionDetector]") {
  SectionDetector detector(kSampleRate, kHopSize);
  SectionFrame last;
  float envelope = 0.0f;
  for (std::size_t i = 0; i < static_cast<std::size_t>(16.0 / kHopSeconds); ++i) {
    envelope = i % static_cast<std::size_t>(kBeat / kHopSeconds) == 0
                   ? 1.0f
                   : static_cast<float>(envelope * std::exp(-kHopSeconds / 0.2));
    last = detector.processHop(envelope * kKick, false);
  }
  CHECK_FALSE(last.breakdown);
  for (std::size_t i = 0; i < static_cast<std::size_t>(8.0 / kHopSeconds); ++i) {
    last = detector.processHop(0.001f * kKick, false);
  }
  CHECK(last.breakdown);
  CHECK(last.referenceDb > last.quietMaxDb + 20.0f);
}

TEST_CASE("Sparse hits with short gaps are not drops", "[core][SectionDetector]") {
  // Gaps shorter than the quiet stretch: the hit before is still in it.
  BassTrack track;
  track.hits(30.0, 1.7, 1.0f);
  CHECK(track.drops().empty());
}

TEST_CASE("A soft hit in a breakdown is not a drop", "[core][SectionDetector]") {
  BassTrack track;
  track.hits(16.0, kBeat, 1.0f)
      .quiet(6.0, 0.001f)
      .hits(4.0, kBeat, 0.05f); // 13 dB down: a filtered kick
  CHECK(track.drops().empty());
}

TEST_CASE("A drop needs a bass onset", "[core][SectionDetector]") {
  // The bass swelling back in (a long filter sweep) is a build, not a drop.
  SectionDetector detector(kSampleRate, kHopSize);
  for (int i = 0; i < 1500; ++i) {
    CHECK_FALSE(detector.processHop((i < 900 ? 1.0f : 0.001f) * kKick, false).drop);
  }
  for (int i = 0; i < 300; ++i) {
    CHECK_FALSE(detector.processHop(kKick, false).drop);
  }
}

TEST_CASE("Hits right after a drop are not more drops", "[core][SectionDetector]") {
  // The kicks after the drop still have the breakdown behind them.
  BassTrack track;
  track.hits(16.0, kBeat, 1.0f)
      .quiet(8.0, 0.001f)
      .hits(2.0, 0.1, 1.0f); // fast kicks: a roll into the drop
  CHECK(track.drops().size() == 1);
}

TEST_CASE("Music starting after silence is a drop", "[core][SectionDetector]") {
  BassTrack track;
  track.quiet(5.0);
  const auto startHop = track.hop();
  track.hits(10.0, kBeat, 1.0f);
  const auto drops = track.drops();
  REQUIRE(drops.size() == 1);
  CHECK(drops[0] == startHop);
}

TEST_CASE("Silence and near-silence never drop", "[core][SectionDetector]") {
  BassTrack track;
  track.quiet(5.0).hits(10.0, kBeat, 1.0e-6f); // below the floor: noise, not music
  CHECK(track.drops().empty());
}

TEST_CASE("A higher threshold ignores a smaller comeback", "[core][SectionDetector]") {
  // The breakdown keeps a bassline 8 dB down; the drop is an 8 dB jump.
  BassTrack track;
  track.hits(16.0, kBeat, 1.0f, 0.0f).hits(8.0, kBeat, 0.16f).hits(8.0, kBeat, 1.0f);
  SectionDetectorConfig sensitive;
  sensitive.jumpDb = 5.5f; // the Energy Threshold parameter's minimum
  SectionDetectorConfig strict;
  strict.jumpDb = 15.0f;
  CHECK(track.drops(sensitive).size() == 1);
  CHECK(track.drops(strict).empty());
}

TEST_CASE("Room noise on a quiet microphone is never a drop", "[core][SectionDetector]") {
  // What the app's mic input showed with nothing playing: bass around
  // -70..-120 dB with the odd blip the onset detector fires on.
  SectionDetector detector(kSampleRate, kHopSize);
  int drops = 0;
  for (int i = 0; i < 6000; ++i) {
    const bool blip = i % 400 == 399;
    drops += detector.processHop(blip ? 1.0e-3f : 1.0e-11f, blip).drop ? 1 : 0;
  }
  CHECK(drops == 0);
}
