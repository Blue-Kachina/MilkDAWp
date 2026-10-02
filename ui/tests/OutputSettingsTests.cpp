// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "milkdawp/ui/OutputSettings.h"

using namespace milkdawp::ui;

namespace {

InstanceState withOthers() {
  InstanceState state;
  state.choices = {{"id-kick", "Kick", true}, {"id-bass", "Bass", true}, {"id-lead", "Lead", false}};
  state.defaultName = "Instance 1a2b";
  return state;
}

std::vector<std::string> itemTexts(const juce::ComboBox& combo) {
  std::vector<std::string> texts;
  for (int i = 0; i < combo.getNumItems(); ++i) {
    texts.push_back(combo.getItemText(i).toStdString());
  }
  return texts;
}

} // namespace

TEST_CASE("OutputSettingsPanel lists New window first, then the other instances", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  CHECK(itemTexts(panel.targetCombo) == std::vector<std::string>{"New window", "Kick", "Bass", "Lead"});
  CHECK(panel.targetCombo.getSelectedId() == 1); // nothing chosen: its own window
}

TEST_CASE("OutputSettingsPanel greys out an instance that already sends elsewhere", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  CHECK(panel.targetCombo.isItemEnabled(2)); // Kick
  CHECK(panel.targetCombo.isItemEnabled(3)); // Bass
  CHECK_FALSE(panel.targetCombo.isItemEnabled(4)); // Lead: a sender, so no chains
}

TEST_CASE("OutputSettingsPanel reports the chosen instance, and empty for New window", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  std::vector<std::string> chosen;
  panel.onTargetInstanceChanged = [&](const std::string& id) { chosen.push_back(id); };

  panel.targetCombo.setSelectedId(3, juce::sendNotificationSync); // Bass
  panel.targetCombo.setSelectedId(1, juce::sendNotificationSync); // back to New window
  REQUIRE(chosen.size() == 2);
  CHECK(chosen[0] == "id-bass");
  CHECK(chosen[1].empty());
}

TEST_CASE("OutputSettingsPanel shows the saved target as the choice", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  auto state = withOthers();
  state.target = "id-bass";
  state.sending = true;
  panel.setInstanceState(state);
  CHECK(panel.targetCombo.getSelectedId() == 3);
  CHECK(panel.targetCombo.getText() == "Bass");
}

TEST_CASE("OutputSettingsPanel keeps showing a saved target that is not loaded", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  auto state = withOthers();
  state.target = "id-not-loaded-yet";
  panel.setInstanceState(state);
  // Still the choice, so the link is made when that instance appears.
  CHECK(panel.targetCombo.getText().startsWith("Not connected"));
  std::string reported = "untouched";
  panel.onTargetInstanceChanged = [&](const std::string& id) { reported = id; };
  panel.targetCombo.setSelectedId(2, juce::sendNotificationSync);
  CHECK(reported == "id-kick");
}

TEST_CASE("OutputSettingsPanel cannot pick a target while others send to it (no chains)",
          "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  auto state = withOthers();
  state.canChooseTarget = false;
  state.senderCount = 2;
  panel.setInstanceState(state);
  CHECK_FALSE(panel.targetCombo.isEnabled());

  state.canChooseTarget = true;
  state.senderCount = 0;
  panel.setInstanceState(state);
  CHECK(panel.targetCombo.isEnabled());
}

TEST_CASE("OutputSettingsPanel only grows for the hint line when there is something to say",
          "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  const int plain = panel.preferredHeight();

  InstanceState alone; // no other instances: a hint explains why the list is empty
  panel.setInstanceState(alone);
  CHECK(panel.preferredHeight() > plain);

  auto hub = withOthers();
  hub.senderCount = 1; // someone sends here: the panel says so
  panel.setInstanceState(hub);
  CHECK(panel.preferredHeight() > plain);
}

TEST_CASE("OutputSettingsPanel asks the shell to relayout when its height changes, and only then",
          "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  int relayouts = 0;
  panel.onPreferredSizeChanged = [&] { ++relayouts; };

  panel.setInstanceState(withOthers()); // identical: nothing happens
  CHECK(relayouts == 0);

  auto renamed = withOthers();
  renamed.label = "My kick"; // changes the name field, not the height
  panel.setInstanceState(renamed);
  CHECK(relayouts == 0);

  panel.setInstanceState(InstanceState{}); // the hint line appears: taller
  CHECK(relayouts == 1);
}

TEST_CASE("OutputSettingsPanel offers the layer controls only to an instance that is part of a canvas",
          "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  const int alone = panel.preferredHeight();
  CHECK_FALSE(panel.opacitySlider.isVisible());
  CHECK_FALSE(panel.blendCombo.isVisible());
  CHECK_FALSE(panel.muteToggle.isVisible());

  auto sender = withOthers();
  sender.target = "id-kick";
  sender.sending = true;
  panel.setInstanceState(sender);
  CHECK(panel.opacitySlider.isVisible());
  CHECK(panel.blendCombo.isVisible());
  CHECK(panel.orderSlider.isVisible());
  CHECK(panel.muteToggle.isVisible());
  CHECK(panel.muteToggle.getButtonText() == "Mute layer");

  // (A sender also loses the screen picker, so its height is not comparable to
  // `alone` on a multi-monitor machine. The hub keeps the picker, so it is.)
  auto hub = withOthers();
  hub.senderCount = 2;
  panel.setInstanceState(hub);
  CHECK(panel.muteToggle.isVisible());
  CHECK(panel.muteToggle.getButtonText() == "Hide my visual"); // the owner's own layer: a pure mixer
  CHECK(panel.preferredHeight() > alone);
}

TEST_CASE("OutputSettingsPanel lists the senders as Sources rows, and grows with them",
          "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  auto hub = withOthers();
  hub.senderCount = 2;
  panel.setInstanceState(hub);
  CHECK(panel.sourceRowCount() == 0);
  const int withoutSources = panel.preferredHeight();

  hub.sources = {{"s1", "Kick", 1.0f, 0, false, 0}, {"s2", "Vocal", 0.5f, 4, true, 1}};
  panel.setInstanceState(hub);
  CHECK(panel.sourceRowCount() == 2);
  CHECK(panel.preferredHeight() > withoutSources);
  const int twoRows = panel.preferredHeight();

  hub.sources.push_back({"s3", "Pad", 1.0f, 1, false, 2});
  hub.senderCount = 3;
  panel.setInstanceState(hub);
  CHECK(panel.sourceRowCount() == 3);
  CHECK(panel.preferredHeight() > twoRows);

  hub.sources.clear(); // everyone left
  hub.senderCount = 0;
  panel.setInstanceState(hub);
  CHECK(panel.sourceRowCount() == 0);
}

TEST_CASE("OutputSettingsPanel keeps its Sources rows when only their values change", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  auto hub = withOthers();
  hub.senderCount = 1;
  hub.sources = {{"s1", "Kick", 1.0f, 0, false, 0}};
  panel.setInstanceState(hub);
  int relayouts = 0;
  panel.onPreferredSizeChanged = [&] { ++relayouts; };

  hub.sources[0].opacity = 0.2f; // an edit made from somewhere else: same row, new value
  hub.sources[0].mute = true;
  panel.setInstanceState(hub);
  CHECK(panel.sourceRowCount() == 1);
  CHECK(relayouts == 0); // no size change, so no relayout
}

TEST_CASE("OutputSettingsPanel without Layers is just the window settings", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  const int withLayers = panel.preferredHeight();
  CHECK(panel.targetCombo.isVisible());
  CHECK(panel.nameEditor.isVisible());

  int relayouts = 0;
  panel.onPreferredSizeChanged = [&] { ++relayouts; };
  panel.setLayersAvailable(false); // the standalone app: nothing to link to
  CHECK_FALSE(panel.targetCombo.isVisible());
  CHECK_FALSE(panel.nameEditor.isVisible());
  CHECK(panel.preferredHeight() < withLayers);
  CHECK(relayouts == 1);

  // Even a state that would show the layer controls and sources shows none of them.
  auto hub = withOthers();
  hub.senderCount = 1;
  hub.sources = {{"s1", "Kick", 1.0f, 0, false, 0, 0.0f}};
  panel.setInstanceState(hub);
  CHECK_FALSE(panel.opacitySlider.isVisible());
  CHECK_FALSE(panel.muteToggle.isVisible());
  CHECK(panel.preferredHeight() < withLayers);

  panel.setLayersAvailable(true);
  CHECK(panel.targetCombo.isVisible());
  CHECK(panel.opacitySlider.isVisible());
}

TEST_CASE("OutputSettingsPanel's blend choices come from the parameter model", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  CHECK(panel.blendCombo.getNumItems() == 5);
  CHECK(panel.blendCombo.getItemText(0) == "Normal");
  CHECK(panel.blendCombo.getItemText(3) == "Multiply");
  CHECK(panel.blendCombo.getItemText(4) == "Luma key");
}

TEST_CASE("OutputSettingsPanel reports a finished name edit", "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  panel.setInstanceState(withOthers());
  std::vector<std::string> labels;
  panel.onInstanceLabelChanged = [&](const std::string& label) { labels.push_back(label); };

  panel.nameEditor.setText("Sub bass", false);
  panel.nameEditor.onReturnKey();
  REQUIRE(labels.size() == 1);
  CHECK(labels[0] == "Sub bass");

  panel.nameEditor.onReturnKey(); // unchanged: not reported again
  CHECK(labels.size() == 1);
}

TEST_CASE("OutputSettingsPanel shows the user's label, and the default name as a hint when empty",
          "[ui][OutputSettings][layers]") {
  const juce::ScopedJuceInitialiser_GUI juceInit; // frees JUCE's font singletons at exit (else LeakSanitizer fails the test)
  OutputSettingsPanel panel;
  auto state = withOthers();
  state.label = "Kick";
  panel.setInstanceState(state);
  CHECK(panel.nameEditor.getText() == "Kick");

  state.label.clear();
  panel.setInstanceState(state);
  CHECK(panel.nameEditor.getText().isEmpty());
  CHECK(panel.nameEditor.getTextToShowWhenEmpty() == "Instance 1a2b");
}
