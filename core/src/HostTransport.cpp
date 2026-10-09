// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/HostTransport.h"

#include <cmath>

namespace milkdawp::core {

HostTransport::HostTransport(double sampleRate) : sampleRate_(sampleRate) {}

BeatClockState HostTransport::processTransport(const TransportInfo& info) const {
  BeatClockState state;

  if (!info.isPlaying || info.bpm <= 0.0) {
    // Stopped (or a host reporting a nonsensical tempo): nothing to predict.
    // Report the current position with zero confidence rather than a stale
    // or fabricated beat -- there is no persistent state here to go stale in
    // the first place (see the class comment).
    state.bpm = static_cast<float>(info.bpm);
    state.nextBeatSample = info.samplePos;
    state.beatIndex = 0;
    state.barIndex = 0;
    state.confidence = 0.0f;
    return state;
  }

  // 1 ppq unit == 1 quarter-note beat, matching JUCE's AudioPlayHead
  // convention. A loop or relocate simply changes ppqPosition/samplePos;
  // everything below is recomputed fresh from them, so it's handled with no
  // special-casing.
  const double beatsElapsed = info.ppqPosition;
  const auto beatIndex = static_cast<std::uint64_t>(std::floor(beatsElapsed));
  const double fractionalBeat = beatsElapsed - static_cast<double>(beatIndex);

  const double samplesPerBeat = 60.0 / info.bpm * sampleRate_;
  const double samplesIntoBeat = fractionalBeat * samplesPerBeat;
  const double samplesUntilNextBeat = samplesPerBeat - samplesIntoBeat;

  const int timeSigNum = info.timeSigNumerator > 0 ? info.timeSigNumerator : 4;
  const auto barIndex = static_cast<std::uint32_t>(beatIndex / static_cast<std::uint64_t>(timeSigNum));

  state.bpm = static_cast<float>(info.bpm);
  state.nextBeatSample = info.samplePos + static_cast<std::uint64_t>(std::llround(samplesUntilNextBeat));
  state.beatIndex = beatIndex;
  state.barIndex = barIndex;
  state.confidence = 1.0f;
  state.beatsPerBar = static_cast<std::uint32_t>(timeSigNum);
  state.beatInBar = static_cast<std::uint32_t>(beatIndex % static_cast<std::uint64_t>(timeSigNum));
  return state;
}

} // namespace milkdawp::core
