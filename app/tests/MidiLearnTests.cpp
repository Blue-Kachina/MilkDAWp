// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "MidiLearn.h"
#include "ParameterBinding.h"

using namespace milkdawp;

// No real MIDI hardware in CI, so these exercise the pure state machine:
// learn-mode arming/cancelling and the mapping table's own round-trip,
// rather than handleIncomingMidiMessage (which only a real or virtual MIDI
// input would drive).
TEST_CASE("MidiLearn starts idle and unmapped", "[app][MidiLearn]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  juce::AudioDeviceManager devices;
  engine::ParameterValues values;
  app::ParameterBinding binding(values, nullptr);
  app::MidiLearn learn(devices, binding);

  CHECK_FALSE(learn.isLearning());
  CHECK_FALSE(learn.hasMapping("shuffle"));
  CHECK(learn.describeMapping("shuffle").isEmpty());
  CHECK(learn.stateString().isEmpty());
}

TEST_CASE("startLearning arms and cancelLearning disarms", "[app][MidiLearn]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  juce::AudioDeviceManager devices;
  engine::ParameterValues values;
  app::ParameterBinding binding(values, nullptr);
  app::MidiLearn learn(devices, binding);

  learn.startLearning("shuffle");
  CHECK(learn.isLearning());
  CHECK(learn.learningParameterId() == "shuffle");

  learn.cancelLearning();
  CHECK_FALSE(learn.isLearning());
  CHECK_FALSE(learn.hasMapping("shuffle")); // cancelling never commits a mapping
}

TEST_CASE("Mappings round-trip through stateString/restoreFromState", "[app][MidiLearn]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  juce::AudioDeviceManager devices;
  engine::ParameterValues values;
  app::ParameterBinding binding(values, nullptr);
  app::MidiLearn learn(devices, binding);

  learn.restoreFromState("shuffle=0,1,21\nlockCurrentPreset=1,3,60");
  CHECK(learn.hasMapping("shuffle"));
  CHECK(learn.describeMapping("shuffle") == "CC 21 ch 1");
  CHECK(learn.hasMapping("lockCurrentPreset"));
  CHECK(learn.describeMapping("lockCurrentPreset").startsWith("Note"));
  CHECK_FALSE(learn.hasMapping("unmappedParam"));

  app::MidiLearn restored(devices, binding);
  restored.restoreFromState(learn.stateString());
  CHECK(restored.describeMapping("shuffle") == learn.describeMapping("shuffle"));
  CHECK(restored.describeMapping("lockCurrentPreset") == learn.describeMapping("lockCurrentPreset"));

  learn.clearMapping("shuffle");
  CHECK_FALSE(learn.hasMapping("shuffle"));
  CHECK(learn.hasMapping("lockCurrentPreset"));
}

TEST_CASE("restoreFromState ignores malformed lines", "[app][MidiLearn]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  juce::AudioDeviceManager devices;
  engine::ParameterValues values;
  app::ParameterBinding binding(values, nullptr);
  app::MidiLearn learn(devices, binding);

  learn.restoreFromState("not a mapping at all\nshuffle=0,1\nlockCurrentPreset=1,3,60,extra\nvalid=0,2,10");
  CHECK_FALSE(learn.hasMapping("shuffle"));         // missing a field
  CHECK_FALSE(learn.hasMapping("lockCurrentPreset")); // too many fields
  CHECK(learn.hasMapping("valid"));
}
