// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "milkdawp/engine/OutputSurface.h"
#include "milkdawp/ui/ControlDrawer.h"
#include "milkdawp/ui/DetachedControlsWindow.h"
#include "milkdawp/ui/OutputSettings.h"
#include "milkdawp/ui/TransitionSettings.h"

namespace milkdawp::plugin {

/// Phase 3.3: the whole editor is `engine::OutputSurface` (§4.5) with
/// `ui::ControlDrawer` (§4.9) composited as its child -- never a sibling,
/// the same GL-compositing rule the diagnostics label below already
/// exercises (§2.3's spike finding, §4.11) -- pinned by default per §4.9's
/// plugin-editor rule. Drawer widgets bind to real `apvts` parameters here
/// rather than inside `milkdawp_ui`, keeping that module decoupled from
/// `juce_audio_processors` (§4.1 layering).
///
/// `controlDrawer.outputButton` opens/closes the processor-owned Output
/// window (2.4/3.12); F11 opens it fullscreen. `settingsButton` shows a
/// menu with the preset folder chooser and the transition settings popover
/// (3.4). The preset title and BPM badge show the engine director's status:
/// current preset name, and the beat source with its confidence (3.5);
/// clicking the title opens a preset picker.
/// "Float controls" moves the drawer into a `DetachedControlsWindow` (3.13)
/// owned by this editor; closing that window docks it again. Whether the
/// controls float is the processor's (saved) layout, which this editor
/// follows, so a reopened editor or a reloaded project floats them again.
class MilkDAWpAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor&);
  ~MilkDAWpAudioProcessorEditor() override;

  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  void visibilityChanged() override;

private:
  void timerCallback() override;
  void pulseTrigger(const juce::String& parameterId);
  void showSettingsMenu();
  /// Clicking the drawer's preset title: folder actions plus every preset,
  /// grouped by subfolder; picking one jumps straight to it.
  void showPresetPicker();
  void choosePresetFolder();
  void setTransitionSettingsVisible(bool visible);
  void layoutTransitionSettings();
  /// Settings -> Output: fullscreen default and target screen. Shares the
  /// popover slot with the transition settings, so only one shows at a time.
  void setOutputSettingsVisible(bool visible);
  void layoutOutputSettings();
  /// Hands the panel the Layers state (other instances, this one's target and name).
  void refreshOutputSettingsInstances();
  /// Phase 3.13: moves `controlDrawer` into its own window, or back.
  void setControlsFloating(bool floating);

  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
  using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

  MilkDAWpAudioProcessor& processorRef;
  engine::OutputSurface outputSurface;
  juce::Label diagnosticsLabel;
  /// Shown instead of a (frozen) picture while this instance's picture is part
  /// of another instance's canvas.
  juce::Label sendingLabel;
  milkdawp::ui::ControlDrawer controlDrawer;
  milkdawp::ui::TransitionSettingsPanel transitionSettings;
  milkdawp::ui::OutputSettingsPanel outputSettings;
  // Shared across every open editor: one per editor would show each tooltip
  // once per plugin instance.
  juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow_;

  std::unique_ptr<ButtonAttachment> lockAttachment_;
  std::unique_ptr<ButtonAttachment> shuffleAttachment_;
  std::unique_ptr<ComboBoxAttachment> transitionModeAttachment_;
  std::vector<std::unique_ptr<SliderAttachment>> transitionSliderAttachments_;
  std::vector<std::unique_ptr<ButtonAttachment>> transitionButtonAttachments_;
  std::unique_ptr<ComboBoxAttachment> transitionSettingsModeAttachment_;
  // The Output panel's layer controls (opacity, order, blend, mute).
  std::vector<std::unique_ptr<SliderAttachment>> layerSliderAttachments_;
  std::unique_ptr<ComboBoxAttachment> layerBlendAttachment_;
  std::unique_ptr<ButtonAttachment> layerMuteAttachment_;
  std::unique_ptr<juce::FileChooser> folderChooser_;
  // Declared after controlDrawer so it is destroyed first: it only borrows
  // the drawer, and hands it back in its destructor.
  std::unique_ptr<milkdawp::ui::DetachedControlsWindow> controlsWindow_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MilkDAWpAudioProcessorEditor)
};

} // namespace milkdawp::plugin
