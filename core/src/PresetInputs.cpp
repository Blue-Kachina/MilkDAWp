// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/PresetInputs.h"

#include <cmath>

namespace milkdawp::core {

PresetAudioInputs presetAudioInputs(const BeatSnapshot& beat, std::uint64_t nowSample) noexcept {
  PresetAudioInputs inputs;
  const double rate = beat.sampleRate > 0.0 ? beat.sampleRate : 48000.0;
  if (beat.hasOnset) {
    const double age =
        nowSample > beat.lastOnsetSample ? static_cast<double>(nowSample - beat.lastOnsetSample) / rate : 0.0;
    inputs.onset = std::exp(-age / kOnsetDecaySeconds);
  }
  if (beat.bpm <= 0.0f || beat.confidence <= 0.0f) {
    return inputs;
  }
  inputs.bpm = beat.bpm;
  const double period = 60.0 / static_cast<double>(beat.bpm) * rate;
  // Beats since the one before `nextBeatSample`: in [0, 1) up to the
  // predicted beat, more after it, negative if `now` is before that beat.
  const double sinceBeat =
      1.0 - (static_cast<double>(beat.nextBeatSample) - static_cast<double>(nowSample)) / period;
  const double whole = std::floor(sinceBeat);
  inputs.beatPhase = sinceBeat - whole;
  const auto perBar = static_cast<long long>(beat.beatsPerBar > 0 ? beat.beatsPerBar : 4);
  auto beatInBar = (static_cast<long long>(beat.beatInBar) + static_cast<long long>(whole)) % perBar;
  if (beatInBar < 0) {
    beatInBar += perBar;
  }
  inputs.barPhase = (static_cast<double>(beatInBar) + inputs.beatPhase) / static_cast<double>(perBar);
  return inputs;
}

} // namespace milkdawp::core
