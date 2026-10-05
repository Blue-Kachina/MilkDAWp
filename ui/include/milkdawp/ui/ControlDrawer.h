// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/ui/DrawerLookAndFeel.h"
#include "milkdawp/ui/DrawerScrim.h"
#include "milkdawp/ui/DrawerState.h"
#include "milkdawp/ui/IconButton.h"

namespace milkdawp::ui {

/// Which of the drawer's controls show at a given width. Below
/// `compactBelowWidth` the mode chip and BPM badge move into the preset's
/// detail line / the "more" menu, as do Output, Settings and Pin -- the
/// §4.9 480px minimum window cannot fit the full row.
struct DrawerLayout {
  bool compact = false;
  bool showModeChip = true;
  bool showBpm = true;
  bool showUtilities = true; // Output + Settings buttons
  bool showPin = true;
  bool showMore = false;
};
inline constexpr int compactBelowWidth = 700;
[[nodiscard]] DrawerLayout drawerLayoutFor(int width, bool floating) noexcept;

/// JUCE composition of Phase 2.11's `DrawerStateMachine` (§4.9): the control
/// row shown over the visualization, styled as a video-player overlay
/// (docs/design, "A: video overlay"): icon buttons over a bottom gradient, a
/// two-line preset title, a transition-mode chip and a progress track.
/// Deliberately owns only UI-layer widgets -- no
/// `juce::AudioProcessorValueTreeState` here, so `milkdawp_ui` stays
/// decoupled from the plugin layer (§4.1 layering) -- the owner (Phase 3.3's
/// `PluginEditor`) attaches these public widgets to real parameters and
/// wires the buttons' `onClick` and `onPresetTitleClicked`.
///
/// Keeps a fixed footprint at the bottom of its parent at all times (alpha 0
/// when hidden, so it stays hoverable) rather than being added/removed --
/// the classic video-player "move the mouse near the bottom edge reveals the
/// controls" gesture, matching `DrawerStateMachine`'s own class comment that
/// `ControlDrawer` "forwards Component::mouseEnter/mouseDown to
/// onPointerActivity()".
///
/// `transitionModeCombo` is pre-populated from `core::ParameterModel`'s
/// `transitionMode` spec (ui/ already depends on core/, §4.1) so its item
/// list can never drift from the parameter's actual choices; the owner still
/// supplies the `ComboBoxAttachment` since that needs `juce_audio_processors`.
class ControlDrawer final : public juce::Component, private juce::Timer {
public:
  /// Docked: the controls plus gradient headroom above them.
  static constexpr int preferredHeight = 96;
  /// The controls themselves (progress track + button row); docked, the
  /// shell places popovers this far up from the bottom.
  static constexpr int controlsHeight = 68;
  /// Floating in the detached window: just the controls, on a solid ground.
  static constexpr int floatingHeight = 72;

  explicit ControlDrawer(DrawerStateMachine::Config config = {});
  ~ControlDrawer() override;

  void resized() override;
  void paintOverChildren(juce::Graphics& g) override;
  void mouseEnter(const juce::MouseEvent&) override;
  void mouseDown(const juce::MouseEvent&) override;

  /// Esc: "if not fullscreen, reveal the drawer" (§4.9). Also usable by any
  /// other caller-driven reveal (counts as pointer activity).
  void reveal();
  /// 'H': reveal or hide, respecting pin.
  void toggleRevealHide();
  /// 'P': pin or unpin.
  void togglePin();
  [[nodiscard]] bool isPinned() const noexcept { return state_.isPinned(); }

  /// Hosted in its own window (Phase 3.13): always shown, a solid ground
  /// instead of the gradient and no pin button, since there is no video
  /// underneath to uncover.
  void setFloating(bool floating);
  [[nodiscard]] bool isFloating() const noexcept { return floating_; }

  /// The two-line title: the preset's name, and a detail line under it
  /// (folder, mode, position). `tooltip` shows on hover.
  void setPresetInfo(const juce::String& name, const juce::String& detail, const juce::String& tooltip);
  /// Fraction of the way to the next scheduled transition, or nullopt for an
  /// empty track (the engine does not publish this yet; see the roadmap).
  void setProgress(std::optional<float> fraction);

  /// Where the shell should anchor its settings menu: the Settings button,
  /// or the "more" button when Settings is folded into it.
  [[nodiscard]] juce::Component& settingsMenuAnchor() noexcept;
  [[nodiscard]] juce::Component& presetTitleComponent() noexcept { return title_; }
  [[nodiscard]] const DrawerLayout& layout() const noexcept { return layout_; }

  /// Clicking the preset title (the shell opens its preset picker).
  std::function<void()> onPresetTitleClicked;

  IconButton prevButton{"Previous preset", Icon::Prev};
  IconButton nextButton{"Next preset", Icon::Next};
  IconButton lockButton{"Lock preset", Icon::Unlock, Icon::Lock};
  IconButton shuffleButton{"Shuffle", Icon::Shuffle};
  juce::ComboBox transitionModeCombo;
  juce::Label bpmLabel;
  IconButton outputButton{"Output window", Icon::PopOut};
  IconButton settingsButton{"Settings", Icon::Settings};
  IconButton pinButton{"Pin controls", Icon::Pin};
  IconButton moreButton{"More controls", Icon::More};

private:
  /// The clickable two-line preset title.
  class PresetTitle final : public juce::Component, public juce::SettableTooltipClient {
  public:
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override;
    /// 5.8: a screen reader announces it as a button named after the preset,
    /// and can press it (opens the picker).
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::String name;
    juce::String detail;
    std::function<void()> onClick;
  };

  /// The thin bar above the buttons.
  class ProgressTrack final : public juce::Component {
  public:
    void paint(juce::Graphics& g) override;
    std::optional<float> fraction;
  };

  void timerCallback() override;
  void updateVisualState();
  void refreshTitle();
  void showMoreMenu();
  [[nodiscard]] static double nowSeconds();

  DrawerLookAndFeel lookAndFeel_;
  DrawerStateMachine state_;
  DrawerScrim scrim_;
  PresetTitle title_;
  ProgressTrack progress_;
  DrawerLayout layout_;
  juce::String detail_;
  juce::Array<juce::Rectangle<float>> dividers_;
  bool floating_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlDrawer)
};

} // namespace milkdawp::ui
