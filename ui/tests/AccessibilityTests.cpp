// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 5.8: the accessibility and UX pass, as checks that keep holding.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "milkdawp/ui/ControlDrawer.h"
#include "milkdawp/ui/DiagnosticsPanel.h"
#include "milkdawp/ui/DrawerLookAndFeel.h"
#include "milkdawp/ui/DrawerScrim.h"
#include "milkdawp/ui/KeyboardNavigation.h"
#include "milkdawp/ui/TransitionSettings.h"

using namespace milkdawp::ui;

namespace {

// The drawer's controls, docked: the button row's top edge, as a proportion of the drawer.
constexpr float kRowTop =
    static_cast<float>(ControlDrawer::preferredHeight - 54) / static_cast<float>(ControlDrawer::preferredHeight);

juce::Colour overWhitePreset(float scrimAlpha) {
  return juce::Colours::white.overlaidWith(juce::Colours::black.withAlpha(scrimAlpha));
}

std::vector<juce::Component*> interactiveControls(ControlDrawer& drawer) {
  return {&drawer.prevButton,   &drawer.nextButton,          &drawer.lockButton,     &drawer.shuffleButton,
          &drawer.outputButton, &drawer.settingsButton,      &drawer.pinButton,      &drawer.moreButton,
          &drawer.transitionModeCombo, &drawer.presetTitleComponent()};
}

} // namespace

TEST_CASE("WCAG contrast ratios come out as the standard's examples", "[ui][Accessibility]") {
  CHECK(contrastRatio(juce::Colours::white, juce::Colours::black) > 20.9f);
  CHECK(contrastRatio(juce::Colours::black, juce::Colours::white) > 20.9f); // order doesn't matter
  CHECK(contrastRatio(juce::Colours::grey, juce::Colours::grey) == 1.0f);
  // #767676 on white is the classic "just passes AA" grey.
  CHECK(contrastRatio(juce::Colour(0xff767676), juce::Colours::white) > 4.5f);
  CHECK(contrastRatio(juce::Colour(0xff777777), juce::Colours::white) < 4.5f);
}

TEST_CASE("The drawer's text keeps AA contrast over a pure-white preset", "[ui][Accessibility]") {
  CHECK(DrawerScrim::alphaAt(0.0f) == 0.0f); // the top edge hides nothing
  for (float y = kRowTop; y <= 1.0f; y += 0.02f) {
    const auto ground = overWhitePreset(DrawerScrim::alphaAt(y));
    INFO("y = " << y << ", scrim alpha " << DrawerScrim::alphaAt(y));
    CHECK(contrastRatio(ground.overlaidWith(drawerTheme::text), ground) >= 4.5f);
    // The preset's detail line, the dimmest text in the drawer.
    CHECK(contrastRatio(ground.overlaidWith(drawerTheme::textSecondary), ground) >= 4.5f);
  }
}

TEST_CASE("Every drawer control is at least 32 px square, at every width (touch targets)", "[ui][Accessibility]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  ControlDrawer drawer;
  for (const bool floating : {false, true}) {
    drawer.setFloating(floating);
    for (const int width : {480, compactBelowWidth - 1, compactBelowWidth, 1280, 1920}) {
      drawer.setSize(width, floating ? ControlDrawer::floatingHeight : ControlDrawer::preferredHeight);
      for (auto* control : interactiveControls(drawer)) {
        if (!control->isVisible()) {
          continue;
        }
        INFO((floating ? "floating" : "docked") << " at " << width << " px: " << control->getName() << " is "
                                                << control->getWidth() << "x" << control->getHeight());
        CHECK(control->getWidth() >= 32);
        CHECK(control->getHeight() >= 32);
      }
    }
  }
}

TEST_CASE("Every drawer control has a tooltip, and the keyboard ones name their key", "[ui][Accessibility]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  ControlDrawer drawer;
  drawer.setPresetInfo("Spiral", "Geiss", {});
  for (auto* control : interactiveControls(drawer)) {
    auto* client = dynamic_cast<juce::TooltipClient*>(control);
    REQUIRE(client != nullptr);
    INFO(control->getName());
    CHECK(client->getTooltip().isNotEmpty());
  }
  CHECK(drawer.prevButton.getTooltip().contains("Left"));
  CHECK(drawer.nextButton.getTooltip().contains("Right"));
  CHECK(drawer.lockButton.getTooltip().contains("(L)"));
  CHECK(drawer.shuffleButton.getTooltip().contains("(S)"));
  CHECK(drawer.pinButton.getTooltip().contains("(P)"));
  CHECK(drawer.settingsButton.getTooltip().contains("(M)"));
  CHECK(drawer.presetTitleComponent().getTitle() == "Preset: Spiral"); // what a screen reader says
  CHECK(dynamic_cast<juce::TooltipClient&>(drawer.presetTitleComponent()).getTooltip().contains("(B)"));
}

TEST_CASE("A popover's controls become a Tab-navigable focus container", "[ui][Accessibility]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  TransitionSettingsPanel panel;
  CHECK_FALSE(panel.barsSlider.getWantsKeyboardFocus());
  makeKeyboardNavigable(panel);
  CHECK(panel.isKeyboardFocusContainer());
  for (juce::Component* control : std::initializer_list<juce::Component*>{
           &panel.modeCombo, &panel.barsSlider, &panel.jitterToggle, &panel.blendSlider, &panel.tagFilterEditor}) {
    INFO(control->getName());
    CHECK(control->getWantsKeyboardFocus());
  }
  // Not the label inside a slider: the slider itself is the stop.
  CHECK(isKeyboardOperable(panel.barsSlider));
  CHECK_FALSE(isKeyboardOperable(panel));

  // The diagnostics panel: Copy and Close.
  DiagnosticsPanel diagnostics;
  makeKeyboardNavigable(diagnostics);
  CHECK(diagnostics.copyButton.getWantsKeyboardFocus());
  CHECK(diagnostics.closeButton.getWantsKeyboardFocus());
  // Off screen, there is nothing to focus yet.
  CHECK_FALSE(focusFirstControl(diagnostics));
}
