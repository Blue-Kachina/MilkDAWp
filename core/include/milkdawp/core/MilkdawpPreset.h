// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "milkdawp/core/MacroLock.h"
#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::core {

/// The `.milkdawp` format version this build writes (`mdw_format`).
inline constexpr int kMilkdawpFormatVersion = 1;

/// How a control changes its target, after the preset's own code has set it
/// (exploration doc §4.3). `c` is the control's value, in min..max.
enum class ControlMode : std::uint8_t {
  Replace,    // target = target + (value - target) x c   (c is an amount: 0 = untouched)
  Offset,     // target = target + c
  Scale,      // target = target x c
  Rate,       // target = target + dt x c, per frame: a phase that never jumps (Speed scales dt)
  Expression, // `code`, as written, reading mdw_cN (this control's c) or mdw_mK (Macro K, 0..1)
};

/// Which code the control's line is appended to.
enum class ControlStage : std::uint8_t {
  PerFrame, // once a frame, after the preset's per-frame code (the default)
  PerPixel, // for every mesh vertex, after the preset's per-pixel code
};

/// One `mdw_ctl_N_*` block: a Macro mapped onto something inside the preset.
struct PresetControl {
  int number = 0;     // the N in mdw_ctl_N_* (1..99)
  std::string name;   // shown beside the Macro ("Swirl")
  int macro = 0;      // 0..7: macro1..macro8
  ControlMode mode = ControlMode::Offset;
  ControlStage stage = ControlStage::PerFrame;
  std::string target; // the preset variable it changes (not for Expression)
  float min = 0.0f;
  float max = 1.0f;
  float defaultValue = 0.0f; // in min..max
  float value = 0.0f;        // Replace only: what the target is pulled towards
  std::string code;          // Expression only

  friend bool operator==(const PresetControl&, const PresetControl&) = default;
};

/// A `.milkdawp` preset (Phase 8.9, exploration doc §5): a complete `.milk`
/// file plus `mdw_` lines. Taking the `mdw_` lines out gives the original
/// preset back byte for byte, which is what `milk` holds.
struct MilkdawpPreset {
  int format = kMilkdawpFormatVersion;
  std::string title;
  std::string source;       // the original .milk, relative to its pack
  std::string sourceSha256; // of that original
  std::vector<std::string> tags;
  std::string renderer = "projectm";
  std::vector<PresetControl> controls;
  /// `mdw_` keys this build doesn't use (a newer format's, or ones sketched
  /// for later such as `mdw_fx_*`), in file order. Kept on save, never dropped.
  std::vector<std::pair<std::string, std::string>> otherKeys;
  /// The plain preset: the file without its `mdw_` lines.
  std::string milk;

  friend bool operator==(const MilkdawpPreset&, const MilkdawpPreset&) = default;
};

struct MilkdawpParseResult {
  MilkdawpPreset preset;
  /// Everything that was wrong, one sentence each. A broken control is left
  /// out (the preset still plays); nothing here stops the preset loading.
  std::vector<std::string> problems;
};

/// The variables the engine sets in every preset each frame (through the
/// patched `projectm_set_preset_variable`, ADR-0012), which compiled code reads.
inline constexpr std::array<const char*, kMacroCount> kMacroVariables{"mdw_m1", "mdw_m2", "mdw_m3", "mdw_m4",
                                                                      "mdw_m5", "mdw_m6", "mdw_m7", "mdw_m8"};
inline constexpr const char* kZoomVariable = "mdw_zoom";     // Visual Zoom, -1..1 (neutral 0)
inline constexpr const char* kRotationVariable = "mdw_rot";  // Visual Rotation, -1..1 (neutral 0)
inline constexpr const char* kWarpVariable = "mdw_warp";     // Visual Warp, 0..3 (neutral 1)
inline constexpr const char* kTrailsVariable = "mdw_trails"; // Visual Trails, 0..1 (neutral 0)
inline constexpr const char* kDtVariable = "mdw_dt";         // this frame's step of the preset clock, in seconds
// 8.12: the music, any preset can read them (PresetInputs.h says how they move).
inline constexpr const char* kBeatPhaseVariable = "mdw_beat_phase"; // 0..1 through each beat
inline constexpr const char* kBarPhaseVariable = "mdw_bar_phase";   // 0..1 through each bar
inline constexpr const char* kBpmVariable = "mdw_bpm";              // the tempo; 0 with no beat
inline constexpr const char* kOnsetVariable = "mdw_onset";          // 1 at a note or hit, decaying to 0
/// 8.12: the textures the engine hands presets, as `sampler_camera` and
/// `sampler_video` in warp and composite shaders (any wrap/filter prefix,
/// `texsize_` too). Both are the layer's media source (camera, video or
/// image), whatever it is; black while there is none.
inline constexpr const char* kCameraTexture = "camera";
inline constexpr const char* kVideoTexture = "video";

/// True for a line `.milkdawp` owns: its key (up to the first space or '=',
/// as projectM and MilkDrop read keys) starts with `mdw_`, in any case.
[[nodiscard]] bool isMilkdawpLine(std::string_view line) noexcept;

/// True for a path ending in ".milkdawp" (any case).
[[nodiscard]] bool hasMilkdawpExtension(std::string_view path) noexcept;

/// Reads a `.milkdawp` (or a plain `.milk`, which is one with no controls).
/// Never fails: problems are reported, and what can't be used is left out.
/// Format versions are migrated forward to this build's (there is only 1 so
/// far); a newer file loads what this build understands.
[[nodiscard]] MilkdawpParseResult parseMilkdawp(std::string_view text);

/// Writes the preset: the `mdw_` lines right after the `[preset00]` line (at
/// the top if there is none), then the rest of `milk` unchanged. Uses the
/// line ending `milk` uses. parse(serialize(p)) gives p back.
[[nodiscard]] std::string serializeMilkdawp(const MilkdawpPreset& preset);

/// Each Macro's starting value (0..1) for Lock Macros off: from the first
/// control on that Macro; nullopt for a Macro no control uses.
[[nodiscard]] MacroDefaults macroDefaults(const MilkdawpPreset& preset);

/// Each Macro's name (the first control's on it), empty where unused.
[[nodiscard]] std::array<std::string, kMacroCount> macroNames(const MilkdawpPreset& preset);

/// The text projectM loads: `milk` with the controls and the Visual globals
/// (Zoom, Rotation, Warp, Trails) compiled into lines appended after the
/// preset's own per-frame code (and per-pixel code, for per-pixel controls).
/// The `mdw_` lines themselves are left out. With every Macro at its default
/// and the globals neutral, each appended line leaves its variable exactly
/// as the preset set it, so the preset looks exactly as before.
[[nodiscard]] std::string compileForProjectM(const MilkdawpPreset& preset);

} // namespace milkdawp::core
