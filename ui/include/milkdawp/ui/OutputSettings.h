// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/StateSchema.h"

namespace milkdawp::ui {

/// One connected display, as the Output settings and the shell see it.
struct DisplayEntry {
  /// Identity saved in the state: the display's own bounds (JUCE has no
  /// stable monitor id). The same value `core::findDisplay` matches against.
  core::WindowBounds id;
  /// The whole monitor, in logical pixels: what fullscreen covers and what
  /// the highlight frame is drawn around.
  juce::Rectangle<int> frame;
  bool primary = false;
};

/// The displays JUCE reports right now, primary first-class but kept in
/// JUCE's order.
[[nodiscard]] std::vector<DisplayEntry> currentDisplays();

/// A thin, click-through frame drawn around a whole monitor, so the user can
/// tell which physical screen a picker tile stands for. A borderless
/// transparent desktop window of its own: it never takes focus or mouse input.
class DisplayHighlightFrame final : public juce::Component {
public:
  DisplayHighlightFrame();
  ~DisplayHighlightFrame() override;

  void showAround(juce::Rectangle<int> screenBounds);
  void hide();

  void paint(juce::Graphics& g) override;

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DisplayHighlightFrame)
};

/// Another plugin instance as the Output panel's "Show on" picker lists it.
struct InstanceChoice {
  std::string id;
  std::string name;
  /// False for an instance that already sends elsewhere: shown, but greyed out.
  bool selectable = true;

  bool operator==(const InstanceChoice&) const = default;
};

/// One instance that sends its picture to this one, as the Sources list shows it.
struct SourceState {
  std::string id;
  std::string name;
  float opacity = 1.0f;
  int blend = 0;
  bool mute = false;
  int order = 0;
  /// GPU time the last frame spent drawing this layer, in ms (0 when hidden or not measured).
  float gpuMs = 0.0f;

  bool operator==(const SourceState&) const = default;
};

/// What the Output panel needs to know about Layers, handed in by the shell.
struct InstanceState {
  /// The other instances in this process.
  std::vector<InstanceChoice> choices;
  /// The saved Output target: an instance id, or empty for this instance's own window.
  std::string target;
  /// This instance is, right now, part of the target's canvas (the saved target exists and linked).
  bool sending = false;
  /// False while other instances send to this one (no chains).
  bool canChooseTarget = true;
  /// The user's own name for this instance (may be empty), and what it is
  /// called when that is empty (the host's track name, say).
  std::string label;
  std::string defaultName;
  /// How many instances send to this one.
  int senderCount = 0;
  /// Those instances, with how each is mixed (the Sources list).
  std::vector<SourceState> sources;

  bool operator==(const InstanceState&) const = default;
};

/// Settings -> Output (M0 in layers_like_shrek.md): whether the Output window
/// opens fullscreen, and which screen it opens on. The screen picker only
/// appears when more than one monitor is connected; hovering a monitor tile
/// draws a frame around the real monitor, clicking it chooses it.
///
/// Like `TransitionSettingsPanel` it is a child of the editor's output
/// surface, never its own desktop window, and holds no processor state: the
/// shell calls `refresh()` with the saved layout and acts on the callbacks.
class OutputSettingsPanel : public juce::Component, private juce::Timer {
public:
  static constexpr int preferredWidth = 320;

  OutputSettingsPanel();
  ~OutputSettingsPanel() override;

  /// Height for the displays connected right now: taller when there is a
  /// screen picker to show.
  [[nodiscard]] int preferredHeight() const;
  /// How many rows the Sources list is showing.
  [[nodiscard]] int sourceRowCount() const noexcept { return static_cast<int>(sourceRows_.size()); }

  /// Shows the saved settings and re-reads the connected displays.
  void refresh(bool defaultFullscreen, const core::WindowBounds& targetDisplay);
  /// Whether the Layers rows (name, "Show on", layer controls, sources) exist at
  /// all. On by default; a shell with no other instances to link to (the
  /// standalone app) turns them off and the panel is just the window settings.
  void setLayersAvailable(bool available);
  /// Shows the Layers side: this instance's name, and where its picture goes.
  /// Cheap to call often; does nothing if `state` is what is already shown,
  /// and otherwise asks the shell to re-lay the panel out if its height changed.
  void setInstanceState(const InstanceState& state);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseMove(const juce::MouseEvent& event) override;
  void mouseExit(const juce::MouseEvent& event) override;
  void mouseUp(const juce::MouseEvent& event) override;
  void visibilityChanged() override;

  std::function<void()> onCloseRequested;
  /// A monitor was plugged in or out, so `preferredHeight()` changed: the
  /// shell re-lays the panel out.
  std::function<void()> onPreferredSizeChanged;
  std::function<void(bool)> onDefaultFullscreenChanged;
  /// The "Show on" choice: an instance id, or empty for "New window".
  std::function<void(const std::string&)> onTargetInstanceChanged;
  /// The user finished editing this instance's name.
  std::function<void(const std::string&)> onInstanceLabelChanged;
  /// A Sources row was edited: (sender id, `layerOpacity` | `layerBlend` | `layerMute`, plain value).
  std::function<void(const std::string&, const std::string&, float)> onSourceParameterChanged;

  // Public, like TransitionSettingsPanel's widgets, so the shell and tests can reach them.
  juce::TextEditor nameEditor;
  juce::ComboBox targetCombo; // item 1 is "New window", then one per other instance

  // How this instance's picture is mixed into a canvas. Shown only while it is
  // part of one (it sends to another instance, or others send to it); the shell
  // binds them to the layer parameters. Blend's items come from ParameterModel.
  juce::Slider opacitySlider;
  juce::ComboBox blendCombo;
  juce::Slider orderSlider;
  juce::ToggleButton muteToggle{"Mute layer"}; // worded "Hide my visual" on the instance that owns the canvas
  /// An empty `WindowBounds` means automatic.
  std::function<void(const core::WindowBounds&)> onTargetDisplayChanged;

private:
  void timerCallback() override;
  /// Index of the map tile under `position`, or -1.
  [[nodiscard]] int tileAt(juce::Point<int> position) const;
  void setHovered(int tile);
  void rebuildDisplays();
  /// The screen picker only makes sense for an instance that opens its own
  /// window, and only with more than one monitor to choose between.
  [[nodiscard]] bool hasPicker() const noexcept { return displays_.size() > 1 && !instance_.sending; }
  [[nodiscard]] bool hasLayerControls() const noexcept {
    return layersAvailable_ && (instance_.sending || instance_.senderCount > 0);
  }
  bool layersAvailable_ = true;
  [[nodiscard]] bool hasInfoLine() const noexcept;
  [[nodiscard]] juce::String infoText() const;
  void rebuildTargetCombo();
  /// The selected display's index, or -1 for automatic (or not connected).
  [[nodiscard]] int selectedIndex() const;

  juce::Label titleLabel;
  juce::TextButton closeButton{"Close"};
  juce::ToggleButton fullscreenToggle{"Open fullscreen"};
  juce::Label screenLabel;
  juce::ToggleButton automaticToggle{"Automatic"};
  juce::Label noteLabel;
  juce::Label nameLabel;
  juce::Label showOnLabel;
  juce::Label infoLabel;
  juce::Label opacityLabel;
  juce::Label blendLabel;
  juce::Label orderLabel;

  class SourceRow;
  void updateSources();
  juce::Label sourcesLabel;
  std::vector<std::unique_ptr<SourceRow>> sourceRows_;

  InstanceState instance_;
  std::vector<std::string> comboIds_; // instance id per combo item id (item 1 = "New window" = "")
  bool rebuildingCombo_ = false;

  std::vector<DisplayEntry> displays_;
  std::vector<juce::Rectangle<int>> tiles_; // map tiles, panel coordinates, same order as displays_
  juce::Rectangle<int> mapArea_;
  core::WindowBounds target_;
  int hovered_ = -1;
  DisplayHighlightFrame highlight_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputSettingsPanel)
};

} // namespace milkdawp::ui
