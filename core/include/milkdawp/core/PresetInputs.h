// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>

namespace milkdawp::core {

/// Phase 8.12: what the director last knew about the beat and the onsets,
/// for the render thread to turn into preset variables at the moment it
/// draws. Sample positions are in the audio ring's frame numbering, the clock
/// the host transport is also rewritten into. Plain data: it crosses threads
/// through a SeqlockSnapshot.
struct BeatSnapshot {
  double sampleRate = 48000.0;
  /// The tempo, and how sure the beat clock is of it (the host's transport
  /// while it plays: 1). No beat while either is 0.
  float bpm = 0.0f;
  float confidence = 0.0f;
  std::uint64_t nextBeatSample = 0;
  std::uint32_t beatsPerBar = 4;
  std::uint32_t beatInBar = 0; // of the beat before `nextBeatSample`, from 0
  /// The latest broadband onset (a note, a hit), if there has been one.
  bool hasOnset = false;
  std::uint64_t lastOnsetSample = 0;
};

/// The values a preset reads as `mdw_beat_phase`, `mdw_bar_phase`, `mdw_bpm`
/// and `mdw_onset` (MilkdawpPreset.h has the names).
struct PresetAudioInputs {
  double beatPhase = 0.0; // 0 on each beat, rising to just below 1 before the next
  double barPhase = 0.0;  // 0 on each bar's first beat, rising to just below 1
  double bpm = 0.0;       // 0 while there is no beat
  double onset = 0.0;     // 1 at an onset, decaying (time constant kOnsetDecaySeconds)
};

inline constexpr double kOnsetDecaySeconds = 0.1;

/// The inputs at `nowSample`: the phases run on from the last snapshot at its
/// tempo, also past the predicted beat (across beats and bars), so they move
/// smoothly between the director's updates. With no beat, every value but
/// `onset` is 0.
[[nodiscard]] PresetAudioInputs presetAudioInputs(const BeatSnapshot& beat, std::uint64_t nowSample) noexcept;

} // namespace milkdawp::core
