// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace milkdawp::engine {

/// The preset pack and textures that ship with MilkDAWp (6.1, D12): projectM's
/// "Cream of the Crop" pack, converted to .milkdawp as "Cream of the CrAWp"
/// (8.11, ADR-0014), and the MilkDrop texture pack, laid out under one content
/// folder (cmake/BundledContent.cmake builds the same layout in the build tree):
///
///   <root>/Presets/Cream of the CrAWp/  the library root a first run starts in
///   <root>/Textures/                    projectM's texture search path
///
/// Read-only: the user's own ratings and tags live in the user data folder
/// (PresetMetadataStore), never here.
struct BundledContent {
  /// The folder under Presets/ that ships.
  static constexpr const char* kPackFolderName = "Cream of the CrAWp";
  /// Where 1.0 shipped the pack (as .milk files), for sessions saved then.
  static constexpr const char* kOldPackFolderName = "Cream of the Crop";

  /// Relative to presetFolder(). Chosen 2026-10-04 for being cheap (no warp
  /// or composite shader, ~2 ms to load) and for looking good in silence: a
  /// first launch often has no audio yet, and most presets in the pack draw
  /// nothing without it.
  static constexpr const char* kDefaultPresetRelativePath = "Geometric/Wire Circles/Geiss - Many Colors 1.milkdawp";

  juce::File root;

  [[nodiscard]] juce::File presetFolder() const { return root.getChildFile("Presets").getChildFile(kPackFolderName); }
  [[nodiscard]] juce::File texturesFolder() const { return root.getChildFile("Textures"); }
  /// The default preset, or a null File if this pack no longer has it.
  [[nodiscard]] juce::File defaultPreset() const;
  /// What to hand projectm_set_texture_search_paths(): the textures folder.
  [[nodiscard]] std::vector<std::string> textureSearchPaths() const;

  /// A preset folder or file saved by 1.0, when the bundled pack was
  /// ".../Presets/Cream of the Crop/....milk": the same place in the pack that
  /// ships now (folder renamed, .milk as .milkdawp), when that exists.
  /// Anything else comes back unchanged.
  [[nodiscard]] static std::string fromOldPack(const std::string& path);

  /// True if `dir` holds the layout above (its preset folder exists).
  [[nodiscard]] static bool isContentRoot(const juce::File& dir);

  /// Where the content may be, in search order:
  ///   1. $MILKDAWP_CONTENT_DIR (tests, unusual installs);
  ///   2. beside the binary: `<binary dir>/Content` (the zipped app on
  ///      Windows/Linux) and `<binary dir>/../Resources/Content` (inside a
  ///      macOS .app or any .vst3 bundle);
  ///   3. the shared install location: %ProgramData%\MilkDAWp on Windows,
  ///      /Library/Application Support/MilkDAWp (then the per-user one) on
  ///      macOS; on Linux `<binary dir>/../share/milkdawp` (the AppImage),
  ///      then $XDG_DATA_HOME/milkdawp, /usr/local/share/milkdawp and
  ///      /usr/share/milkdawp (the .deb);
  ///   4. dev builds only: the build tree's content folder
  ///      (MILKDAWP_CONTENT_FROM_BUILD_TREE).
  [[nodiscard]] static std::vector<juce::File> candidateRoots();

  /// The first candidate that is a content root, or nothing (a build without
  /// MILKDAWP_BUNDLE_CONTENT, or nothing installed): shells then start with
  /// no library, as before 6.1.
  [[nodiscard]] static std::optional<BundledContent> find();
};

} // namespace milkdawp::engine
