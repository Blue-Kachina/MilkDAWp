// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace milkdawp::ui {

/// The words of a search box query: lower case, split on whitespace.
[[nodiscard]] std::vector<std::string> presetSearchWords(const juce::String& query);

/// 6.1b: filter-as-you-type over a whole preset library (~10k presets in the
/// bundled pack). Each preset's display name ("Folder/Sub/Name") is lower-cased
/// once, when the playlist changes, so a keystroke is one substring pass
/// over plain strings: about a millisecond for 10k.
///
/// A preset matches when every word of the query appears somewhere in its
/// name or folder, or in its extra text (the shell passes its tags), in any
/// order: "geiss wave" finds "Waveform/Wire Tangle/Geiss - 3D - Shockwaves".
class PresetSearchIndex {
public:
  /// `names`: display names in playlist order (either slash).
  void rebuild(const std::vector<std::string>& names);
  /// Extra searchable text per preset, parallel to the names (tags);
  /// empty, or shorter than the names, means none for the rest.
  void setExtraText(std::vector<std::string> extra);

  [[nodiscard]] std::size_t size() const noexcept { return names_.size(); }

  /// Playlist indices whose name or extra text contains every word, in
  /// playlist order. No words matches everything.
  [[nodiscard]] std::vector<int> filter(const std::vector<std::string>& words) const;
  /// Whether preset `index` matches every word.
  [[nodiscard]] bool matches(int index, const std::vector<std::string>& words) const;

private:
  std::vector<std::string> names_; // lower case, forward slashes
  std::vector<std::string> extra_; // lower case
};

} // namespace milkdawp::ui
