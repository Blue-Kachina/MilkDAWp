// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "milkdawp/core/PresetInputs.h"

using namespace milkdawp::core;
using Catch::Matchers::WithinAbs;

namespace {

// 120 bpm at 48 kHz: a beat every 24000 samples. The next beat is at 100000,
// the third beat (from 0) of a 4/4 bar.
BeatSnapshot beatAt120() {
  BeatSnapshot beat;
  beat.sampleRate = 48000.0;
  beat.bpm = 120.0f;
  beat.confidence = 1.0f;
  beat.nextBeatSample = 100000;
  beat.beatsPerBar = 4;
  beat.beatInBar = 2;
  return beat;
}

} // namespace

TEST_CASE("Preset inputs: no beat, no onset, all zero (8.12)", "[core][PresetInputs]") {
  const auto inputs = presetAudioInputs(BeatSnapshot{}, 123456);
  CHECK(inputs.beatPhase == 0.0);
  CHECK(inputs.barPhase == 0.0);
  CHECK(inputs.bpm == 0.0);
  CHECK(inputs.onset == 0.0);

  auto unsure = beatAt120();
  unsure.confidence = 0.0f; // e.g. the host stopped
  CHECK(presetAudioInputs(unsure, 90000).bpm == 0.0);
  CHECK(presetAudioInputs(unsure, 90000).beatPhase == 0.0);
}

TEST_CASE("Preset inputs: beat and bar phase through a beat (8.12)", "[core][PresetInputs]") {
  const auto beat = beatAt120();
  // On the beat before the predicted one: the third beat starts.
  auto inputs = presetAudioInputs(beat, 76000);
  CHECK(inputs.bpm == 120.0);
  CHECK_THAT(inputs.beatPhase, WithinAbs(0.0, 1e-9));
  CHECK_THAT(inputs.barPhase, WithinAbs(2.0 / 4.0, 1e-9));
  // Halfway to the predicted beat.
  inputs = presetAudioInputs(beat, 88000);
  CHECK_THAT(inputs.beatPhase, WithinAbs(0.5, 1e-9));
  CHECK_THAT(inputs.barPhase, WithinAbs(2.5 / 4.0, 1e-9));
}

TEST_CASE("Preset inputs: the phases run on past the predicted beat, across the bar line (8.12)",
          "[core][PresetInputs]") {
  const auto beat = beatAt120();
  // A quarter into the beat after the predicted one: the bar's last beat.
  auto inputs = presetAudioInputs(beat, 106000);
  CHECK_THAT(inputs.beatPhase, WithinAbs(0.25, 1e-9));
  CHECK_THAT(inputs.barPhase, WithinAbs(3.25 / 4.0, 1e-9));
  // Two beats on: the next bar's first beat, a quarter in.
  inputs = presetAudioInputs(beat, 130000);
  CHECK_THAT(inputs.beatPhase, WithinAbs(0.25, 1e-9));
  CHECK_THAT(inputs.barPhase, WithinAbs(0.25 / 4.0, 1e-9));
  // Before the beat the snapshot counts from, it counts back the same way.
  inputs = presetAudioInputs(beat, 70000);
  CHECK_THAT(inputs.beatPhase, WithinAbs(0.75, 1e-9));
  CHECK_THAT(inputs.barPhase, WithinAbs(1.75 / 4.0, 1e-9));
}

TEST_CASE("Preset inputs: a 3/4 bar (8.12)", "[core][PresetInputs]") {
  auto beat = beatAt120();
  beat.beatsPerBar = 3;
  beat.beatInBar = 2;
  const auto inputs = presetAudioInputs(beat, 106000); // past the bar line
  CHECK_THAT(inputs.barPhase, WithinAbs(0.25 / 3.0, 1e-9));
}

TEST_CASE("Preset inputs: onset is 1 at the onset and decays (8.12)", "[core][PresetInputs]") {
  BeatSnapshot beat;
  beat.hasOnset = true;
  beat.lastOnsetSample = 48000;
  CHECK_THAT(presetAudioInputs(beat, 48000).onset, WithinAbs(1.0, 1e-9));
  CHECK_THAT(presetAudioInputs(beat, 40000).onset, WithinAbs(1.0, 1e-9)); // never above 1
  const double oneTimeConstant = presetAudioInputs(beat, 48000 + 4800).onset; // 0.1 s later
  CHECK_THAT(oneTimeConstant, WithinAbs(std::exp(-1.0), 1e-9));
  CHECK(presetAudioInputs(beat, 48000 * 2).onset < 1e-4);
}
