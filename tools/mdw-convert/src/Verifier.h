// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "milkdawp/core/MilkdawpPreset.h"

namespace mdw_convert {

/// The before/after check (8.11): renders the original `.milk` and the
/// compiled `.milkdawp`, headless, with the same audio and frame times and
/// every control neutral, and compares them. projectM's per-load randomness
/// is pinned the same way in both (core::withoutRandomness), so most presets
/// match exactly. What can't be pinned (3D noise textures seeded from the
/// clock) means some presets don't match even themselves: then the original
/// is rendered several times, and the conversion passes when it is no
/// further from the original than the original is from itself.
class Verifier {
public:
  struct Settings {
    int frames = 60;
    int width = 256;
    int height = 144;
    std::vector<std::string> textureSearchPaths;
  };

  struct Result {
    bool passed = false;
    bool exact = false;  // every compared frame identical
    std::string reason;  // why it failed, when it did
    double original = 0; // mean difference of the original from itself, 0..255
    double converted = 0; // ... of the conversion from the original
  };

  /// Null, with `error`, when projectM or an offscreen GL context isn't available.
  static std::unique_ptr<Verifier> create(Settings settings, std::string& error);
  ~Verifier();
  Verifier(const Verifier&) = delete;
  Verifier& operator=(const Verifier&) = delete;

  Result check(const std::string& milk, const milkdawp::core::MilkdawpPreset& preset);

  /// Dev aid: renders two preset texts the way check() does (randomness
  /// pinned, every Macro at 0.5): a, b, a again, and the
  /// differences a-a, a-b, a2-b (0..255); empty when one doesn't load.
  std::vector<double> compare(const std::string& a, const std::string& b);

private:
  struct Impl;
  explicit Verifier(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace mdw_convert
