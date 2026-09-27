// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "milkdawp/core/OnsetDetector.h"
#include "milkdawp/core/TempoTracker.h"

namespace milkdawp::core {

/// A beat-phase snapshot (Phase 1.6, §4.3 step 6).
struct BeatClockState {
  float bpm = 0.0f;
  std::uint64_t nextBeatSample = 0;
  std::uint64_t beatIndex = 0;
  std::uint32_t barIndex = 0; // assumes 4/4; see the class comment
  float confidence = 0.0f;
};

/// Predictive beat-phase tracker: turns a TempoTracker's bpm into a
/// concrete predicted next-beat sample position, corrects that prediction
/// on strong onsets (bass-band onsets are the intended input -- kick drums
/// are the most reliable beat marker), and derives a best-effort downbeat
/// under a 4/4 assumption (§4.3 step 6: "assume 4/4, pick the phase that
/// maximizes bass onset energy"). This is deliberately simple: a real
/// dynamic-programming beat tracker is a larger project than Phase 1 needs,
/// and the host-transport path (Phase 1.7) is authoritative whenever a DAW
/// is present anyway.
class BeatClock {
public:
  explicit BeatClock(double sampleRate, std::size_t hopSize = 512, float phaseCorrectionGain = 0.25f,
                      float phaseCorrectionWindowFraction = 0.5f);

  /// Call once per hop, in hop order. `bassOnset` is this hop's bass-band
  /// onset, if any (from an OnsetDetector fed AnalysisFrame::bassOnsetStrength).
  BeatClockState processHop(std::uint64_t currentSamplePos, const TempoEstimate& tempo,
                             const std::optional<Onset>& bassOnset);

  [[nodiscard]] std::uint64_t samplesUntilNextBeat(std::uint64_t currentSamplePos) const noexcept;

private:
  double sampleRate_;
  float phaseCorrectionGain_;
  float phaseCorrectionWindowFraction_;

  bool established_ = false;
  double periodSamples_ = 0.0;
  std::uint64_t nextBeatSample_ = 0;
  std::uint64_t beatIndex_ = 0;

  // Best-effort downbeat: average bass-onset strength observed at each of
  // the 4 possible beat-mod-4 phases, and which phase currently looks like
  // beat 1 of the bar.
  std::array<float, 4> phaseEnergyAverage_{};
  std::array<std::uint32_t, 4> phaseObservationCount_{};
  std::uint32_t downbeatPhase_ = 0;
  std::uint64_t barCount_ = 0;
};

} // namespace milkdawp::core
