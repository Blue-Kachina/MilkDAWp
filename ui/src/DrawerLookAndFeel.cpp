// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/DrawerLookAndFeel.h"

#include <algorithm>

#include "milkdawp/ui/Icons.h"

namespace milkdawp::ui {

namespace {
constexpr float kChipHeight = 36.0f;
constexpr int kChipIconSlot = 32; // left padding + mode icon + gap
constexpr int kChipChevronSlot = 26;
constexpr int kMenuItemHeight = 34;
constexpr int kMenuIconSlot = 36;
constexpr int kMenuTickSlot = 30;
} // namespace

DrawerLookAndFeel::DrawerLookAndFeel() {
  setColour(juce::ComboBox::textColourId, drawerTheme::text);
  setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
  setColour(juce::ComboBox::outlineColourId, drawerTheme::track);
  setColour(juce::ComboBox::arrowColourId, drawerTheme::text);
  setColour(juce::ComboBox::focusedOutlineColourId, drawerTheme::accent);

  setColour(juce::PopupMenu::backgroundColourId, drawerTheme::menuBackground);
  setColour(juce::PopupMenu::textColourId, drawerTheme::menuText);
  setColour(juce::PopupMenu::headerTextColourId, drawerTheme::menuHeader);
  setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::white.withAlpha(0.08f));
  setColour(juce::PopupMenu::highlightedTextColourId, drawerTheme::text);

  setColour(juce::TooltipWindow::backgroundColourId, drawerTheme::menuBackground);
  setColour(juce::TooltipWindow::textColourId, drawerTheme::menuText);
  setColour(juce::TooltipWindow::outlineColourId, drawerTheme::hairline);
}

void DrawerLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int,
                                     int, juce::ComboBox& box) {
  const auto chip = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
                        .withSizeKeepingCentre(static_cast<float>(width) - 1.0f,
                                               std::min(kChipHeight, static_cast<float>(height) - 1.0f));
  const bool open = box.isPopupActive();
  if (open || isButtonDown || box.isMouseOver(true)) {
    g.setColour(isButtonDown ? drawerTheme::pressFill : drawerTheme::hoverFill);
    g.fillRoundedRectangle(chip, 8.0f);
  }
  g.setColour(open ? drawerTheme::text.withAlpha(0.5f) : drawerTheme::track);
  g.drawRoundedRectangle(chip, 8.0f, 1.0f);

  const auto colour = box.isEnabled() ? drawerTheme::text : drawerTheme::text.withAlpha(0.4f);
  if (box.getProperties().getWithDefault(modeIconProperty, false)) {
    const auto iconArea = juce::Rectangle<float>(10.0f, chip.getCentreY() - 9.0f, 18.0f, 18.0f);
    drawIcon(g, iconForTransitionMode(box.getSelectedItemIndex()), iconArea, colour);
  }
  const auto chevronArea =
      juce::Rectangle<float>(static_cast<float>(width) - 24.0f, chip.getCentreY() - 8.0f, 16.0f, 16.0f);
  drawIcon(g, open ? Icon::ChevronUp : Icon::ChevronDown, chevronArea, colour);
}

juce::Font DrawerLookAndFeel::getComboBoxFont(juce::ComboBox&) {
  return juce::Font(juce::FontOptions(13.0f, juce::Font::bold));
}

void DrawerLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
  const bool hasIcon = box.getProperties().getWithDefault(modeIconProperty, false);
  const int left = hasIcon ? kChipIconSlot : 10;
  label.setBounds(left, 1, std::max(0, box.getWidth() - left - kChipChevronSlot), box.getHeight() - 2);
  label.setBorderSize(juce::BorderSize<int>(0));
  label.setFont(getComboBoxFont(box));
}

juce::PopupMenu::Options DrawerLookAndFeel::menuOptions(juce::Component& anchor) {
  return juce::PopupMenu::Options()
      .withTargetComponent(&anchor)
      .withMaximumNumColumns(1)
      .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
}

juce::PopupMenu::Options DrawerLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label&) {
  // Not the default's withItemThatMustBeVisible(): that centres the menu on
  // the selected item, so from a bottom-edge chip it hangs off the window.
  return menuOptions(box).withMinimumWidth(box.getWidth()).withStandardItemHeight(kMenuItemHeight);
}

void DrawerLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
  const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
  g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
  g.setColour(juce::Colours::white.withAlpha(0.12f));
  g.drawRect(bounds, 1.0f);
}

void DrawerLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                          bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                          const juce::String& text, const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon, const juce::Colour* textColour) {
  if (isSeparator) {
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRect(area.reduced(8, 0).withSizeKeepingCentre(area.getWidth() - 16, 1));
    return;
  }

  auto r = area.reduced(4, 1);
  if (isHighlighted && isActive) {
    g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
    g.fillRoundedRectangle(r.toFloat(), 6.0f);
  } else if (isTicked) {
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(r.toFloat(), 6.0f);
  }

  auto colour = textColour != nullptr ? *textColour : findColour(juce::PopupMenu::textColourId);
  if (isHighlighted && isActive) {
    colour = findColour(juce::PopupMenu::highlightedTextColourId);
  }
  if (!isActive) {
    colour = colour.withMultipliedAlpha(0.5f);
  }

  auto iconSlot = r.removeFromLeft(kMenuIconSlot).toFloat();
  if (icon != nullptr) {
    icon->drawWithin(g, iconSlot.withSizeKeepingCentre(18.0f, 18.0f), juce::RectanglePlacement::centred, 1.0f);
  }

  auto tickSlot = r.removeFromRight(kMenuTickSlot).toFloat();
  if (hasSubMenu) {
    juce::Path arrow;
    const auto c = tickSlot.getCentre();
    arrow.startNewSubPath(c.x - 2.0f, c.y - 5.0f);
    arrow.lineTo(c.x + 3.0f, c.y);
    arrow.lineTo(c.x - 2.0f, c.y + 5.0f);
    g.setColour(colour.withMultipliedAlpha(0.7f));
    g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  } else if (isTicked) {
    drawIcon(g, Icon::Check, tickSlot.withSizeKeepingCentre(16.0f, 16.0f), drawerTheme::accent);
  }

  g.setColour(colour);
  g.setFont(isTicked ? getPopupMenuFont().boldened() : getPopupMenuFont());
  if (shortcutKeyText.isNotEmpty()) {
    g.setColour(colour.withMultipliedAlpha(0.6f));
    g.drawText(shortcutKeyText, r, juce::Justification::centredRight, true);
    g.setColour(colour);
  }
  g.drawFittedText(text, r, juce::Justification::centredLeft, 1);
}

juce::Font DrawerLookAndFeel::getPopupMenuFont() { return juce::Font(juce::FontOptions(14.0f)); }

void DrawerLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int,
                                                  int& idealWidth, int& idealHeight) {
  if (isSeparator) {
    idealWidth = 50;
    idealHeight = 9;
    return;
  }
  idealHeight = kMenuItemHeight;
  idealWidth = juce::GlyphArrangement::getStringWidthInt(getPopupMenuFont().boldened(), text) + kMenuIconSlot +
               kMenuTickSlot + 16;
}

} // namespace milkdawp::ui
