// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace milkdawp::core {

/// What the user has said about one preset (5.2).
struct PresetInfo {
  int rating = 0;                // 0: unrated, else 1-5 stars
  bool neverAutoSelect = false;  // only ever played when picked by hand
  std::vector<std::string> tags; // normalised: lower case, trimmed, sorted, unique
  bool favourite = false;        // 6.1b: the browser's Favourites tab (last, so {rating, never, tags} still works)

  [[nodiscard]] bool isDefault() const noexcept {
    return rating == 0 && !neverAutoSelect && !favourite && tags.empty();
  }
  bool operator==(const PresetInfo&) const = default;
};

/// Ratings, tags and "never auto-select" for a whole preset library: one
/// file shared by the app and every plugin instance, since they describe
/// the user's presets, not one project.
///
/// Keyed by the preset's file name, not its path, so moving or re-rooting a
/// preset folder keeps them, and the same preset in two packs (a common
/// thing with MilkDrop packs) shares one rating. Case-insensitive.
///
/// Pure value type: the engine's `PresetMetadataStore` loads, saves and
/// shares it.
class PresetMetadata {
public:
  /// The file name of `pathOrName` (either slash), lower-cased (ASCII).
  [[nodiscard]] static std::string keyFor(std::string_view pathOrName);

  /// The preset's info; default (unrated, no tags) when there is none.
  [[nodiscard]] PresetInfo get(std::string_view path) const;
  /// Sets the preset's info (normalising the tags); a default info removes it.
  void set(std::string_view path, PresetInfo info);
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  /// Every tag in use, sorted.
  [[nodiscard]] std::vector<std::string> allTags() const;

  /// Text, one preset per line: `rating<TAB>flags<TAB>tag,tag<TAB>file name`.
  [[nodiscard]] std::string serialize() const;
  /// The reverse; malformed lines are skipped, never fatal.
  [[nodiscard]] static PresetMetadata parse(std::string_view text);

  /// Weighted shuffle's weight: unrated 1, then each star doubles it
  /// (1 star 0.25 ... 3 stars 1 ... 5 stars 4).
  [[nodiscard]] static float weightFor(const PresetInfo& info) noexcept;
  /// "Calm, Dark ,calm" -> {"calm", "dark"}: comma-separated, normalised.
  [[nodiscard]] static std::vector<std::string> parseTags(std::string_view text);
  [[nodiscard]] static std::string joinTags(const std::vector<std::string>& tags);
  /// True when `filter` is empty or `info` has any of its tags.
  [[nodiscard]] static bool matchesFilter(const PresetInfo& info,
                                          const std::vector<std::string>& filter);

  bool operator==(const PresetMetadata&) const = default;

private:
  std::map<std::string, PresetInfo> entries_; // by keyFor()
};

} // namespace milkdawp::core
