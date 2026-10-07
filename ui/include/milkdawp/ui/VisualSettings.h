// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/ui/KeyboardNavigation.h"

namespace milkdawp::ui {

/// What the preset playing now offers the Visual panel (Phase 8.10): a
/// `.milkdawp` takes Warp and gives the Macros it uses a name.
struct PresetControlsInfo {
  bool milkdawp = false;
  std::array<std::string, core::kMacroCount> macroNames{}; // empty: the preset doesn't use that Macro

  friend bool operator==(const PresetControlsInfo&, const PresetControlsInfo&) = default;
};

/// Why a Visual control does nothing with `preset`, or empty if it works.
/// Shown as the control's tooltip, and the control is dimmed: a control faked
/// as a post effect would mean something else once its real version arrives
/// (exploration doc §6.2).
[[nodiscard]] juce::String visualControlUnavailableReason(const std::string& parameterId,
                                                          const PresetControlsInfo& preset = {});

/// The gate's level meter (Phase 8.2b), like the one on a hardware gate: the
/// layer's input level, the threshold as a line, and a light that is lit while
/// the gate is open, so the threshold can be set by eye.
class GateMeter final : public juce::Component {
public:
  static constexpr float kFloorDb = -100.0f;

  void paint(juce::Graphics& g) override;
  /// `levelDb` -200 or below draws an empty meter.
  void setState(float levelDb, float thresholdDb, bool open, bool enabled);

private:
  float levelDb_ = -200.0f;
  float thresholdDb_ = -80.0f;
  bool open_ = true;
  bool enabled_ = false;
};

/// Phase 8.1's "Visual" popover: the 16 Visual globals, Macro 1-8 with Lock
/// Macros, and the layer gate with its meter. Like `TransitionSettingsPanel`
/// it owns only UI widgets, built from `core::ParameterModel`'s groups (so they
/// cannot drift from the parameters), and the shell attaches each one to its
/// parameter by id: the plugin with APVTS attachments, the app with its
/// `ParameterBinding`. Scrolls when the window is too small to show it all.
class VisualSettingsPanel final : public juce::Component {
public:
  /// Wide enough for three columns, where every control fits without scrolling.
  static constexpr int preferredWidth = 720;
  /// The height that shows every control at `width` without scrolling. The
  /// shell may give it less (a small plugin window); the controls then scroll.
  [[nodiscard]] int preferredHeight(int width) const;

  VisualSettingsPanel();
  ~VisualSettingsPanel() override;

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override { focusRing_.paint(g); }
  void resized() override;

  /// Every widget, by parameter id, for the shell to attach.
  [[nodiscard]] const std::vector<std::pair<std::string, juce::Slider*>>& sliders() const noexcept { return sliders_; }
  [[nodiscard]] const std::vector<std::pair<std::string, juce::Button*>>& toggles() const noexcept { return toggles_; }
  [[nodiscard]] const std::vector<std::pair<std::string, juce::ComboBox*>>& combos() const noexcept { return combos_; }

  /// The shell's timer, while the panel shows: this instance's layer input and
  /// gate state (`engine::LayerChannel::gateLevelDb()` and `gateOpen()`).
  void setGateMeter(float levelDb, bool open);
  /// The shell's timer, while the panel shows: what the preset playing now
  /// offers (`engine::Director::currentPreset()`). Names the Macros it uses
  /// and dims what it can't use. Cheap when nothing changed.
  void setPresetControls(const PresetControlsInfo& preset);
  /// "Reset visual": every Visual global back to neutral, through the widgets
  /// (so the shell's attachments write the parameters). Macros and the gate stay.
  void resetVisual();

  std::function<void()> onCloseRequested;

private:
  class Content;

  juce::Label titleLabel_;
  juce::TextButton closeButton_{"Close"};
  juce::TextButton resetButton_{"Reset visual"};
  juce::Viewport viewport_;
  std::unique_ptr<Content> content_;
  std::vector<std::pair<std::string, juce::Slider*>> sliders_;
  std::vector<std::pair<std::string, juce::Button*>> toggles_;
  std::vector<std::pair<std::string, juce::ComboBox*>> combos_;

  KeyboardFocusRing focusRing_{*this};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualSettingsPanel)
};

} // namespace milkdawp::ui
