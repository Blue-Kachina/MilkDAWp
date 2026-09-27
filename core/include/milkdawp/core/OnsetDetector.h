// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace milkdawp::core {

/// One detected onset (Phase 1.4, §4.3 step 4).
struct Onset {
  std::uint64_t samplePos = 0;
  float strength = 0.0f;
};

/// Adaptive-threshold peak picker over a single onset-detection-function
/// stream (§4.3 step 4). Feed it one ODF value per hop -- typically
/// AnalysisFrame::onsetStrength (broadband) or ::bassOnsetStrength; run two
/// instances if you want both streams (the "band" tag on the resulting
/// Onset is then just which instance produced it, assigned by the caller).
///
/// Peak detection has one hop of latency: a candidate local maximum at hop
/// N-1 is only confirmed once hop N's value is known to be lower (this is a
/// deliberately causal, real-time-friendly reading of §4.3's "sliding ±0.5s
/// window" -- a true centered window would add up to 0.25s of latency, which
/// a live analysis thread can't get back).
class OnsetDetector {
public:
  explicit OnsetDetector(double sampleRate, std::size_t hopSize = 512, float windowSeconds = 0.5f,
                          float thresholdMultiplier = 2.5f, float minIntervalSeconds = 0.06f);

  /// Feed the ODF value for the next hop, in hop order starting from
  /// absolute sample position 0. Returns a confirmed onset (at the previous
  /// hop) if this value confirms one, else std::nullopt.
  std::optional<Onset> processHop(float odfValue);

private:
  std::size_t hopSize_;
  std::size_t windowSizeHops_;
  float thresholdMultiplier_;
  std::uint64_t minIntervalSamples_;

  std::deque<float> window_;
  bool havePrevOdf_ = false;
  bool havePrevPrevOdf_ = false;
  float prevOdf_ = 0.0f;
  float prevPrevOdf_ = 0.0f;
  std::uint64_t hopIndex_ = 0; // index of the *next* value processHop() will receive

  bool haveLastOnset_ = false;
  std::uint64_t lastOnsetSamplePos_ = 0;
};

} // namespace milkdawp::core
