// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "milkdawp/core/MilkdawpPreset.h"

namespace milkdawp::core {

/// What a `.milk` says about itself, as far as proposing controls goes
/// (8.11). Keys and variable names are lower-cased, as projectM reads them.
struct MilkAnalysis {
  /// Header values that are numbers, by key ("fdecay", "zoom", "ob_a"), the
  /// first occurrence of each, as projectM uses.
  std::map<std::string, float> header;
  /// Variables the per-frame code (init or per frame) assigns to.
  std::set<std::string> frameWrites;
  /// Every right-hand side assigned to each of those, joined with "; ".
  std::map<std::string, std::string> frameAssignments;
  /// q1..q32 that something after the per-frame code reads: the per-pixel
  /// code, the warp or composite shader, or a custom wave or shape.
  std::set<std::string> qReads;
  bool hasPerFrameCode = false;
  bool hasShaders = false; // a warp_ or comp_ shader
};

[[nodiscard]] MilkAnalysis analyzeMilk(std::string_view milk);

/// Turns a `.milk` into a `.milkdawp` with proposed Macros (exploration doc
/// §7, 8.11): q-variables the preset feeds its shaders or mesh (audio-driven
/// ones first), then what its own code animates (wave, borders, motion
/// vectors, echo, drift, centre, stretch), then what it shows, then motion
/// any preset has, up to 8. Every control is neutral on its Macro's default,
/// so the result looks exactly like the original until a Macro moves.
/// `relativePath` (with '/') gives `mdw_source`, the title and the tags.
[[nodiscard]] MilkdawpPreset convertMilk(std::string_view milk, std::string_view relativePath);

enum class ConvertOutcome {
  Written,   // a new or changed .milkdawp
  Unchanged, // the .milkdawp already there is what would be written
  Kept,      // a .milkdawp is already there (and overwrite is off)
  Excluded,  // listed in ConvertOptions::exclude
  Rejected,  // ConvertOptions::verify turned it down
  Failed,    // couldn't be read, isn't a preset, or already has mdw_ lines
};

[[nodiscard]] const char* outcomeName(ConvertOutcome outcome);

struct ConvertEntry {
  std::string relativePath; // of the .milk, with '/'
  ConvertOutcome outcome = ConvertOutcome::Failed;
  std::string detail; // why, for Rejected and Failed
  std::vector<std::string> controls; // the proposed Macros' names
};

struct ConvertOptions {
  /// Replace an existing .milkdawp. Off by default: one may have been edited by hand.
  bool overwrite = false;
  /// Work everything out, write nothing.
  bool dryRun = false;
  /// Relative paths (with '/') of presets to leave alone.
  std::set<std::string> exclude;
  /// Also delete the .milkdawp beside an excluded preset (one an earlier run
  /// made, before the preset was excluded). For generated packs only.
  bool pruneExcluded = false;
  /// Optional check before writing: empty accepts, anything else is the
  /// reason to leave the preset out (Rejected). Called with the .milk's path.
  std::function<std::string(const std::filesystem::path&, const MilkdawpPreset&)> verify;
  /// Called after each preset, for progress (done, total).
  std::function<void(std::size_t, std::size_t)> progress;
};

/// Converts every `.milk` under `root` (or `root` itself, if it is a file)
/// into a `.milkdawp` beside it. Only ever creates or replaces `.milkdawp`
/// files: a `.milk` is opened read-only and never written, moved or deleted
/// (exploration doc §5.1). Entries are in path order.
[[nodiscard]] std::vector<ConvertEntry> convertFolder(const std::filesystem::path& root,
                                                      const ConvertOptions& options);

/// For comparing renders only (mdw-convert --verify), never for a file
/// anyone plays: the preset with projectM's per-load randomness pinned, so
/// two renders of it can match exactly. `rand(x)` in code becomes `0.5*(x)`;
/// in the shaders, `rand_preset`, `rand_frame` and `hue_shader` become
/// constants, and each random texture (`sampler_rand00`, `rand01_smalltiled`,
/// with or without a filter prefix such as `sampler_pw_`) and 2D noise
/// texture (`noise_lq`..`noise_hq`, which projectM fills from the clock) a
/// fixed one from `textures` (file names without extension; each name takes
/// the next unused match, so no two collide). The 3D `noisevol_` textures
/// can't be swapped for an image and stay random. Unchanged when the preset
/// has none of these.
[[nodiscard]] std::string withoutRandomness(std::string_view milk, const std::vector<std::string>& textures);

/// Reads an exclusions file: one relative path per line; blank lines and
/// lines starting with '#' are skipped.
[[nodiscard]] std::set<std::string> parseExclusions(std::string_view text);

} // namespace milkdawp::core
