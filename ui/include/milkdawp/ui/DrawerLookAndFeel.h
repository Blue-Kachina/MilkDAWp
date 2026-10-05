// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// The drawer's palette (design "A: video overlay", docs/design): white
/// icons over a dark scrim, one accent for "on" states.
/// Built from literals only, never from juce::Colours: those are globals in
/// JUCE's own translation unit, and nothing orders their initialization before
/// ours (on Linux they were still zero, so these came out translucent black).
namespace drawerTheme {
inline const juce::Colour accent{0xff6cc4ff};
inline const juce::Colour text{0xffffffff};
inline const juce::Colour textSecondary = text.withAlpha(0.74f);
inline const juce::Colour hoverFill = text.withAlpha(0.14f);
inline const juce::Colour pressFill = text.withAlpha(0.22f);
inline const juce::Colour hairline = text.withAlpha(0.22f);
inline const juce::Colour track = text.withAlpha(0.28f);
/// Solid ground for the detached controls window (no video under it).
inline const juce::Colour panel{0xff101217};
inline const juce::Colour menuBackground{0xf014161c};
inline const juce::Colour menuText{0xffe8eaef};
inline const juce::Colour menuHeader{0xff9ba1ad};
} // namespace drawerTheme

/// Styles the drawer's ComboBox as a mode chip (icon, name, chevron) and the
/// popup menus opened from the drawer (dark, accent tick, item icons). Menus
/// the shell builds itself only pick this up via PopupMenu::setLookAndFeel().
class DrawerLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  DrawerLookAndFeel();

  /// Set on a ComboBox's properties to draw a mode icon in its chip.
  static constexpr const char* modeIconProperty = "milkdawpModeIcon";

  /// Options for a menu opened from a drawer control: anchored to it, one
  /// column, and opening upwards -- the drawer sits at the bottom of its
  /// window, and a menu dropping down from it lands off the window.
  [[nodiscard]] static juce::PopupMenu::Options menuOptions(juce::Component& anchor);

  juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox&, juce::Label&) override;

  void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                    int buttonW, int buttonH, juce::ComboBox&) override;
  juce::Font getComboBoxFont(juce::ComboBox&) override;
  void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

  void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
  void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                         bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                         const juce::String& shortcutKeyText, const juce::Drawable* icon,
                         const juce::Colour* textColour) override;
  juce::Font getPopupMenuFont() override;
  void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                 int& idealWidth, int& idealHeight) override;
};

} // namespace milkdawp::ui
