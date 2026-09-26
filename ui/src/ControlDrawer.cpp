// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/ControlDrawer.h"

#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::ui {

ControlDrawer::ControlDrawer(DrawerStateMachine::Config config) : state_(config) {
  addAndMakeVisible(scrim_);

  prevButton.setTooltip("Previous preset (Left arrow)");
  nextButton.setTooltip("Next preset (Right arrow)");
  addAndMakeVisible(prevButton);
  addAndMakeVisible(nextButton);

  presetLabel.setJustificationType(juce::Justification::centred);
  presetLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  presetLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(presetLabel);

  // Plain text, not an emoji glyph: JUCE's font fallback does not reliably
  // resolve supplementary-plane codepoints (U+1F000+, e.g. the padlock/
  // shuffle/pin emoji originally used here) the way it resolves BMP symbols
  // -- confirmed missing in a real host (Reaper) and the Standalone build on
  // this Windows box. Text is guaranteed to render with the default font.
  lockButton.setButtonText("Lock");
  lockButton.setClickingTogglesState(true);
  lockButton.setTooltip("Lock current preset (L)");
  addAndMakeVisible(lockButton);

  shuffleButton.setButtonText("Shuf");
  shuffleButton.setClickingTogglesState(true);
  shuffleButton.setTooltip("Shuffle (S)");
  addAndMakeVisible(shuffleButton);

  if (const auto* spec = core::findParameter(core::allParameters(), "transitionMode")) {
    int itemId = 1;
    for (const auto& choice : spec->choices) {
      transitionModeCombo.addItem(choice, itemId++);
    }
  }
  transitionModeCombo.setTooltip("Transition mode");
  addAndMakeVisible(transitionModeCombo);

  bpmLabel.setJustificationType(juce::Justification::centred);
  bpmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  bpmLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(bpmLabel);

  // Phase 2.4/3.12 (Output window) does not exist yet -- disabled with a
  // tooltip is the honest state, not a button that silently does nothing.
  outputButton.setButtonText("Out");
  outputButton.setTooltip("Output window (Phase 2.4/3.12 -- not implemented yet)");
  outputButton.setEnabled(false);
  addAndMakeVisible(outputButton);

  // Phase 3.4 (transition settings popover) does not exist yet -- same
  // honesty as outputButton above.
  settingsButton.setButtonText("Set");
  settingsButton.setTooltip("Transition settings (Phase 3.4 -- not implemented yet)");
  settingsButton.setEnabled(false);
  addAndMakeVisible(settingsButton);

  pinButton.setButtonText("Pin");
  pinButton.setClickingTogglesState(true);
  pinButton.setTooltip("Pin drawer (P)");
  pinButton.onClick = [this] { togglePin(); };
  addAndMakeVisible(pinButton);

  scrim_.toBack();
  addMouseListener(this, true);

  updateVisualState();
  startTimerHz(15);
}

ControlDrawer::~ControlDrawer() { stopTimer(); }

void ControlDrawer::resized() {
  scrim_.setBounds(getLocalBounds());

  auto bounds = getLocalBounds().reduced(4);
  juce::FlexBox fb;
  fb.flexDirection = juce::FlexBox::Direction::row;
  fb.alignItems = juce::FlexBox::AlignItems::center;
  fb.justifyContent = juce::FlexBox::JustifyContent::flexStart;

  // FlexItem's height defaults to "unassigned", which computePreferredSize()
  // falls back to minHeight (0.0f) for anything but AlignItems::stretch --
  // every item below must set an explicit height or it lays out at zero
  // size and paints nothing, despite a perfectly valid width.
  const auto item = [rowHeight = static_cast<float>(bounds.getHeight())](juce::Component& c, float width) {
    return juce::FlexItem(c).withWidth(width).withHeight(rowHeight).withMargin(juce::FlexItem::Margin(0, 4, 0, 0));
  };

  fb.items.add(item(prevButton, 24));
  fb.items.add(item(presetLabel, 70));
  fb.items.add(item(nextButton, 24));
  fb.items.add(item(lockButton, 42));
  fb.items.add(item(shuffleButton, 42));
  fb.items.add(item(transitionModeCombo, 118));
  fb.items.add(item(bpmLabel, 50));
  fb.items.add(juce::FlexItem().withFlex(1.0f)); // spacer: pushes output/settings/pin to the right edge
  fb.items.add(item(outputButton, 38));
  fb.items.add(item(settingsButton, 38));
  fb.items.add(item(pinButton, 38));

  fb.performLayout(bounds);
}

void ControlDrawer::mouseEnter(const juce::MouseEvent&) { reveal(); }
void ControlDrawer::mouseDown(const juce::MouseEvent&) { reveal(); }

void ControlDrawer::reveal() {
  state_.onPointerActivity(nowSeconds());
  updateVisualState();
}

void ControlDrawer::toggleRevealHide() {
  state_.toggleRevealHide(nowSeconds());
  updateVisualState();
}

void ControlDrawer::togglePin() {
  state_.togglePin(nowSeconds());
  updateVisualState();
}

void ControlDrawer::timerCallback() {
  state_.tick(nowSeconds());
  updateVisualState();
}

void ControlDrawer::updateVisualState() {
  setAlpha(state_.isVisible() ? 1.0f : 0.0f);
  pinButton.setToggleState(state_.isPinned(), juce::dontSendNotification);
}

double ControlDrawer::nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

} // namespace milkdawp::ui
