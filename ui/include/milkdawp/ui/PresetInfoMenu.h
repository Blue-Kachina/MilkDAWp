// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/PresetMetadata.h"

namespace milkdawp::ui {

/// "★★★☆☆" for 3; "Not rated" for 0.
[[nodiscard]] juce::String ratingText(int rating);

/// One preset's rating, "never auto-select" and tags as menu items (5.2):
/// a "Rating" submenu, a "Never auto-select" toggle and a "Tags..." item.
/// Shared by both shells' preset pickers and the app's browser.
/// `onChange` gets the changed info; `onEditTags` opens the tag editor.
void addPresetInfoItems(juce::PopupMenu& menu,
                        const core::PresetInfo& info,
                        const std::function<void(const core::PresetInfo&)>& onChange,
                        const std::function<void()>& onEditTags);

/// A small dialog for one preset's tags: comma-separated, with the tags in
/// use elsewhere listed as a reminder. `onDone` gets the new tags; cancel
/// calls nothing.
void showTagEditor(const juce::String& presetName,
                   const std::vector<std::string>& current,
                   const std::vector<std::string>& known,
                   juce::Component* associated,
                   std::function<void(std::vector<std::string>)> onDone);

/// How a shell reads and writes one preset's info (the engine's
/// `PresetMetadataStore`, which this library doesn't see).
struct PresetInfoAccess {
  std::function<core::PresetInfo()> get;
  std::function<void(const core::PresetInfo&)> set;
  std::function<std::vector<std::string>()> knownTags;
};

/// A separator, a "<name>" header and `addPresetInfoItems`, with "Tags..."
/// opening `showTagEditor` over `associated`.
void addPresetInfoSection(juce::PopupMenu& menu,
                          const juce::String& presetName,
                          const PresetInfoAccess& access,
                          juce::Component* associated);

} // namespace milkdawp::ui
