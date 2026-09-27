// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "ParameterBinding.h"

using namespace milkdawp;

TEST_CASE("ParameterBinding takes ranges from the parameter model and writes through", "[app][binding]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  engine::ParameterValues values;
  int changes = 0;
  app::ParameterBinding binding(values, [&] { ++changes; });

  juce::Slider bars;
  binding.bind(bars, "transitionBars");
  CHECK(bars.getMinimum() == 1.0);
  CHECK(bars.getMaximum() == 16.0);
  CHECK(bars.getInterval() == 1.0);
  CHECK(bars.getValue() == 4.0);

  bars.setValue(9.0, juce::sendNotificationSync);
  CHECK(values.transitionBars == 9.0f);
  CHECK(changes == 1);
}

TEST_CASE("ParameterBinding keeps every widget bound to an id in step", "[app][binding]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  engine::ParameterValues values;
  app::ParameterBinding binding(values, {});

  juce::ComboBox drawerMode;
  juce::ComboBox panelMode;
  for (auto* combo : {&drawerMode, &panelMode}) {
    for (int i = 0; i < 5; ++i) {
      combo->addItem("mode " + juce::String(i), i + 1);
    }
  }
  binding.bind(drawerMode, "transitionMode");
  binding.bind(panelMode, "transitionMode");
  CHECK(panelMode.getSelectedId() == 3); // BeatQuantized, index 2

  drawerMode.setSelectedId(2, juce::sendNotificationSync); // Timed
  CHECK(values.transitionMode == 1.0f);
  CHECK(panelMode.getSelectedId() == 2);

  binding.set("transitionMode", 4.0f); // from a shortcut or the menu bar
  CHECK(drawerMode.getSelectedId() == 5);
  CHECK(panelMode.getSelectedId() == 5);
}

TEST_CASE("ParameterBinding toggles, clamps and ignores unknown ids", "[app][binding]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  engine::ParameterValues values;
  int changes = 0;
  app::ParameterBinding binding(values, [&] { ++changes; });

  juce::ToggleButton lock;
  binding.bind(lock, "lockCurrentPreset");
  binding.toggle("lockCurrentPreset");
  CHECK(values.lockCurrentPreset == 1.0f);
  CHECK(lock.getToggleState());

  binding.set("transitionBars", 100.0f);
  CHECK(values.transitionBars == 16.0f);
  binding.set("transitionBars", 16.0f); // unchanged: no notification
  const int before = changes;
  binding.set("transitionBars", 16.0f);
  CHECK(changes == before);

  binding.set("triggerNext", 1.0f); // not an engine value
  binding.set("noSuchParameter", 1.0f);
  CHECK(binding.get("noSuchParameter") == 0.0f);
}
