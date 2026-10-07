// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/ui/MediaMenu.h"
#include "milkdawp/ui/OscSettingsDialog.h"
#include "milkdawp/ui/VisualSettings.h"

using namespace milkdawp;

TEST_CASE("VisualSettingsPanel has one widget per Visual, Macro and gate parameter", "[ui][VisualSettings]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  ui::VisualSettingsPanel panel;

  std::set<std::string> widgets;
  for (const auto& [id, slider] : panel.sliders()) {
    CHECK(widgets.insert(id).second);
    const auto* spec = core::findParameter(core::allParameters(), id);
    REQUIRE(spec != nullptr);
    CHECK(slider->getMinimum() == spec->minValue);
    CHECK(slider->getMaximum() == spec->maxValue);
  }
  for (const auto& [id, toggle] : panel.toggles()) {
    CHECK(widgets.insert(id).second);
  }
  for (const auto& [id, combo] : panel.combos()) {
    CHECK(widgets.insert(id).second);
    const auto* spec = core::findParameter(core::allParameters(), id);
    REQUIRE(spec != nullptr);
    CHECK(combo->getNumItems() == static_cast<int>(spec->choices.size()));
  }

  std::set<std::string> expected;
  for (const auto& spec : core::allParameters()) {
    if (spec.group == "visual" || spec.group == "macros" || spec.group == "layerGate") {
      expected.insert(spec.id);
    }
  }
  CHECK(widgets == expected);
}

namespace {

// Every widget the panel offers, with where it sits in the panel and in the
// scrolling content.
std::vector<std::pair<std::string, juce::Component*>> allWidgets(ui::VisualSettingsPanel& panel) {
  std::vector<std::pair<std::string, juce::Component*>> widgets;
  for (const auto& [id, w] : panel.sliders()) {
    widgets.emplace_back(id, w);
  }
  for (const auto& [id, w] : panel.toggles()) {
    widgets.emplace_back(id, w);
  }
  for (const auto& [id, w] : panel.combos()) {
    widgets.emplace_back(id, w);
  }
  return widgets;
}

juce::Viewport* viewportOf(ui::VisualSettingsPanel& panel) {
  for (auto* child : panel.getChildren()) {
    if (auto* viewport = dynamic_cast<juce::Viewport*>(child)) {
      return viewport;
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("VisualSettingsPanel shows every control at its own size", "[ui][VisualSettings]") {
  // Media Mix, the last Visual row, used to sit half below the panel's edge.
  const juce::ScopedJuceInitialiser_GUI gui;
  ui::VisualSettingsPanel panel;
  for (const int width : {ui::VisualSettingsPanel::preferredWidth, 560}) {
    INFO("panel width " << width);
    panel.setSize(width, panel.preferredHeight(width));
    auto* viewport = viewportOf(panel);
    REQUIRE(viewport != nullptr);
    const auto visible = viewport->getBounds();
    for (const auto& [id, widget] : allWidgets(panel)) {
      INFO("control " << id);
      const auto inPanel = panel.getLocalArea(widget, widget->getLocalBounds());
      CHECK(widget->getWidth() > 0);
      CHECK(visible.contains(inPanel)); // no scrolling needed to see all of it
    }
  }
}

TEST_CASE("VisualSettingsPanel keeps every control reachable in a small window", "[ui][VisualSettings]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  ui::VisualSettingsPanel panel;
  // The plugin's minimum editor (480 x 270) leaves about this much for a popover.
  for (const auto& size : {juce::Point<int>(468, 190), juce::Point<int>(700, 240)}) {
    INFO("panel " << size.x << " x " << size.y);
    panel.setSize(size.x, size.y);
    auto* viewport = viewportOf(panel);
    REQUIRE(viewport != nullptr);
    auto* content = viewport->getViewedComponent();
    REQUIRE(content != nullptr);
    CHECK(content->getWidth() <= viewport->getMaximumVisibleWidth()); // never sideways
    std::vector<juce::Rectangle<int>> placed;
    for (const auto& [id, widget] : allWidgets(panel)) {
      INFO("control " << id);
      const auto inContent = content->getLocalArea(widget, widget->getLocalBounds());
      CHECK(content->getLocalBounds().contains(inContent)); // reachable by scrolling
      if (dynamic_cast<juce::Slider*>(widget) != nullptr) {
        CHECK(widget->getWidth() >= 100); // room for the track and its value box
        CHECK_FALSE(static_cast<juce::Slider*>(widget)->isScrollWheelEnabled()); // the wheel scrolls the panel
      }
      for (const auto& other : placed) {
        CHECK_FALSE(other.intersects(inContent));
      }
      placed.push_back(inContent);
    }
  }
}

TEST_CASE("Visual controls that need later stages say why", "[ui][VisualSettings]") {
  CHECK(ui::visualControlUnavailableReason("visualWarp").isNotEmpty());
  CHECK(ui::visualControlUnavailableReason("visualWaveSize").isNotEmpty());
  CHECK(ui::visualControlUnavailableReason("visualMediaMix").isEmpty()); // 8.6a: images work
  CHECK(ui::visualControlUnavailableReason("macro3").isNotEmpty());
  CHECK(ui::visualControlUnavailableReason("visualGlow").isEmpty());
  CHECK(ui::visualControlUnavailableReason("lockMacros").isEmpty());
  CHECK(ui::visualControlUnavailableReason("layerGateEnabled").isEmpty());
}

TEST_CASE("A .milkdawp preset brings Warp and the Macros it uses to life (8.10)", "[ui][VisualSettings]") {
  ui::PresetControlsInfo preset;
  preset.milkdawp = true;
  preset.macroNames[2] = "Swirl";
  CHECK(ui::visualControlUnavailableReason("visualWarp", preset).isEmpty());
  CHECK(ui::visualControlUnavailableReason("macro3", preset).isEmpty());
  CHECK(ui::visualControlUnavailableReason("macro1", preset).contains("doesn't use Macro 1"));
  CHECK(ui::visualControlUnavailableReason("visualWaveSize", preset).isNotEmpty()); // projectM can't
  CHECK(ui::visualControlUnavailableReason("lockMacros", preset).isEmpty());

  // The panel shows the preset's name for the Macro.
  ui::VisualSettingsPanel panel;
  panel.setPresetControls(preset);
  const auto& sliders = panel.sliders();
  const auto macro3 = std::find_if(sliders.begin(), sliders.end(), [](const auto& s) { return s.first == "macro3"; });
  REQUIRE(macro3 != sliders.end());
  CHECK(macro3->second->getTooltip().startsWith("Macro 3: "));
  CHECK(macro3->second->getAlpha() == 1.0f);
  const auto macro1 = std::find_if(sliders.begin(), sliders.end(), [](const auto& s) { return s.first == "macro1"; });
  REQUIRE(macro1 != sliders.end());
  CHECK(macro1->second->getAlpha() < 1.0f);

  panel.setPresetControls({}); // back to a .milk
  CHECK(macro3->second->getAlpha() < 1.0f);
  CHECK_FALSE(macro3->second->getTooltip().startsWith("Macro 3: "));
}

TEST_CASE("The OSC dialog's fields become settings, keeping what doesn't parse", "[ui][osc]") {
  core::OscSettings current;
  current.receivePort = 9000;
  current.sendHost = "127.0.0.1";
  current.sendPort = 9001;

  const auto on = ui::oscSettingsFromFields(current, true, "8000", " 192.168.1.5 ", "8001");
  CHECK(on.enabled);
  CHECK(on.receivePort == 8000);
  CHECK(on.sendHost == "192.168.1.5");
  CHECK(on.sendPort == 8001);

  const auto junk = ui::oscSettingsFromFields(current, false, "port", "", "70000");
  CHECK_FALSE(junk.enabled);
  CHECK(junk.receivePort == 9000);
  CHECK(junk.sendHost == "127.0.0.1");
  CHECK(junk.sendPort == 9001);
}

TEST_CASE("The camera menu lists cameras, or says why there are none", "[ui][media]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  juce::String picked;
  const auto onPick = [&picked](const juce::String& device) { picked = device; };

  auto unsupported = ui::cameraMenu(false, {"Cam"}, {}, onPick);
  REQUIRE(unsupported.getNumItems() == 1);
  CHECK_FALSE(juce::PopupMenu::MenuItemIterator(unsupported).next() == false);

  auto none = ui::cameraMenu(true, {}, {}, onPick);
  juce::PopupMenu::MenuItemIterator noneIt(none);
  REQUIRE(noneIt.next());
  CHECK_FALSE(noneIt.getItem().isEnabled);
  CHECK(noneIt.getItem().text == "No camera found");

  auto two = ui::cameraMenu(true, {"Front", "USB"}, "USB", onPick);
  juce::PopupMenu::MenuItemIterator it(two);
  REQUIRE(it.next());
  CHECK(it.getItem().text == "Front");
  CHECK_FALSE(it.getItem().isTicked);
  REQUIRE(it.next());
  CHECK(it.getItem().isTicked);
  it.getItem().action();
  CHECK(picked == "USB");
}

TEST_CASE("Reset visual puts the Visual controls back and leaves Macros alone", "[ui][VisualSettings]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  ui::VisualSettingsPanel panel;
  juce::Slider* glow = nullptr;
  juce::Slider* macro = nullptr;
  for (const auto& [id, slider] : panel.sliders()) {
    if (id == "visualGlow") {
      glow = slider;
    } else if (id == "macro1") {
      macro = slider;
    }
  }
  REQUIRE(glow != nullptr);
  REQUIRE(macro != nullptr);
  glow->setValue(0.8);
  macro->setValue(0.5);

  panel.resetVisual();
  CHECK(glow->getValue() == 0.0);
  CHECK(macro->getValue() == 0.5);
}
