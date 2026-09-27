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
/// (3.4). The preset label and BPM badge show the engine director's status:
/// current preset name, and the beat source with its confidence (3.5).
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
  void choosePresetFolder();
  void setTransitionSettingsVisible(bool visible);
  void layoutTransitionSettings();

  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
  using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

  MilkDAWpAudioProcessor& processorRef;
  engine::OutputSurface outputSurface;
  juce::Label diagnosticsLabel;
  milkdawp::ui::ControlDrawer controlDrawer;
  milkdawp::ui::TransitionSettingsPanel transitionSettings;
  // Shared across every open editor: one per editor would show each tooltip
  // once per plugin instance.
  juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow_;

  std::unique_ptr<ButtonAttachment> lockAttachment_;
  std::unique_ptr<ButtonAttachment> shuffleAttachment_;
  std::unique_ptr<ComboBoxAttachment> transitionModeAttachment_;
  std::vector<std::unique_ptr<SliderAttachment>> transitionSliderAttachments_;
  std::vector<std::unique_ptr<ButtonAttachment>> transitionButtonAttachments_;
  std::unique_ptr<ComboBoxAttachment> transitionSettingsModeAttachment_;
  std::unique_ptr<juce::FileChooser> folderChooser_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MilkDAWpAudioProcessorEditor)
};

} // namespace milkdawp::plugin
