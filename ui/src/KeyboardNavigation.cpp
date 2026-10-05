// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/KeyboardNavigation.h"

#include "milkdawp/ui/DrawerLookAndFeel.h"

namespace milkdawp::ui {

namespace {

void enableOperable(juce::Component& parent) {
  for (auto* child : parent.getChildren()) {
    if (isKeyboardOperable(*child)) {
      // Not into a slider's text box or a combo box's label: the widget
      // itself is the focus stop.
      child->setWantsKeyboardFocus(true);
    } else {
      enableOperable(*child);
    }
  }
}

} // namespace

bool isKeyboardOperable(const juce::Component& component) noexcept {
  return dynamic_cast<const juce::Slider*>(&component) != nullptr ||
         dynamic_cast<const juce::ComboBox*>(&component) != nullptr ||
         dynamic_cast<const juce::Button*>(&component) != nullptr ||
         dynamic_cast<const juce::TextEditor*>(&component) != nullptr;
}

void makeKeyboardNavigable(juce::Component& panel) {
  panel.setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
  enableOperable(panel);
}

bool focusFirstControl(juce::Component& panel) {
  makeKeyboardNavigable(panel);
  if (!panel.isShowing()) {
    return false;
  }
  if (auto traverser = panel.createKeyboardFocusTraverser()) {
    if (auto* first = traverser->getDefaultComponent(&panel)) {
      first->grabKeyboardFocus();
      return first->hasKeyboardFocus(false);
    }
  }
  return false;
}

KeyboardFocusRing::KeyboardFocusRing(juce::Component& panel) : panel_(panel) {
  juce::Desktop::getInstance().addFocusChangeListener(this);
}

KeyboardFocusRing::~KeyboardFocusRing() { juce::Desktop::getInstance().removeFocusChangeListener(this); }

void KeyboardFocusRing::paint(juce::Graphics& g) const {
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused == nullptr || !panel_.isParentOf(focused)) {
    return;
  }
  // A slider's or combo box's inner text field: ring the whole widget.
  for (auto* parent = focused->getParentComponent(); parent != nullptr && parent != &panel_;
       parent = parent->getParentComponent()) {
    if (isKeyboardOperable(*parent)) {
      focused = parent;
    }
  }
  const auto area = panel_.getLocalArea(focused, focused->getLocalBounds()).toFloat().expanded(2.0f);
  g.setColour(drawerTheme::accent);
  g.drawRoundedRectangle(area, 4.0f, 2.0f);
}

void KeyboardFocusRing::globalFocusChanged(juce::Component* focused) {
  const bool inside = focused != nullptr && panel_.isParentOf(focused);
  // Repaint when the ring appears, moves or goes away.
  if (inside || painted_) {
    panel_.repaint();
  }
  painted_ = inside;
}

} // namespace milkdawp::ui
