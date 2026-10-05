// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 6.7: About's version lines and credits.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/ui/AboutPanel.h"

using namespace milkdawp::ui;

TEST_CASE("About: version lines name MilkDAWp, projectM and JUCE", "[ui][About]") {
  AboutInfo info;
  info.version = "1.0.0-beta.1";
  info.shell = "VST3 in REAPER";
  info.projectMVersion = "4.2.0";
  info.juceVersion = "JUCE v9.0.2";
  info.operatingSystem = "Windows 11";
  const auto lines = formatAboutVersions(info);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0] == "MilkDAWp 1.0.0-beta.1 (VST3 in REAPER)");
  CHECK(lines[1] == "projectM 4.2.0");
  CHECK(lines[2] == "JUCE 9.0.2 on Windows 11");

  info.projectMVersion = {};
  CHECK(formatAboutVersions(info)[1] == "projectM not loaded");
}

TEST_CASE("About: credits cover every licence and the preset curator", "[ui][About]") {
  const auto text = aboutCredits().joinIntoString("\n");
  for (const auto* needle : {"AGPL-3.0-or-later", "LGPL-2.1", "Jason Fletcher", "ISOSCELES", "JUCE", "Lucide",
                             "texture pack", "THIRD_PARTY_NOTICES.md"}) {
    INFO(needle);
    CHECK(text.contains(needle));
  }
}

TEST_CASE("About: the plugin shows the update status without the check controls", "[ui][About]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  AboutPanel panel;
  panel.setUpdateControls(false, false);
  CHECK_FALSE(panel.autoCheckToggle.isVisible());
  CHECK_FALSE(panel.checkNowButton.isVisible());
  panel.setUpdateControls(true, true);
  CHECK(panel.autoCheckToggle.isVisible());
  CHECK(panel.autoCheckToggle.getToggleState());

  panel.setUpdateStatus("MilkDAWp 1.0.1 is available", "https://example/1.0.1", true);
  CHECK(panel.updateLink.isVisible());
  CHECK_FALSE(panel.updateLabel.isVisible());
  panel.setUpdateStatus("Up to date.", {}, false);
  CHECK_FALSE(panel.updateLink.isVisible());
  CHECK(panel.updateLabel.getText() == "Up to date.");
}
