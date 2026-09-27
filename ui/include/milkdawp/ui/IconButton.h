// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/ui/Icons.h"

namespace milkdawp::ui {

/// A borderless icon button in the drawer's style: white icon, a soft
/// rounded fill on hover, and for toggles an accent-coloured icon plus an
/// underline while on (a colour change alone is too subtle over video).
/// A plain juce::Button, so ButtonAttachment works exactly as it did with
/// the TextButtons it replaces.
class IconButton final : public juce::Button {
public:
  static constexpr int preferredSize = 44;

  /// `onIcon`: shown instead of `icon` while toggled on (e.g. an open
  /// padlock when off, a closed one when on).
  IconButton(const juce::String& name, Icon icon, std::optional<Icon> onIcon = std::nullopt);

  void setIcons(Icon icon, std::optional<Icon> onIcon = std::nullopt);

protected:
  void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;

private:
  Icon icon_;
  std::optional<Icon> onIcon_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconButton)
};

} // namespace milkdawp::ui
