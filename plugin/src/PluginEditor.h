// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "milkdawp/engine/OutputSurface.h"
#include "milkdawp/ui/ControlDrawer.h"

namespace milkdawp::plugin {

/// Phase 3.3: the whole editor is `engine::OutputSurface` (§4.5) with
/// `ui::ControlDrawer` (§4.9) composited as its child -- never a sibling,
/// the same GL-compositing rule the diagnostics label below already
/// exercises (§2.3's spike finding, §4.11) -- pinned by default per §4.9's
/// plugin-editor rule. Drawer widgets bind to real `apvts` parameters here
/// rather than inside `milkdawp_ui`, keeping that module decoupled from
/// `juce_audio_processors` (§4.1 layering).
///
/// `controlDrawer.outputButton`/`settingsButton` are wired but disabled:
/// they are Phase 2.4/3.12 (Output window) and Phase 3.4 (transition
/// settings popover) respectively, neither of which exists yet.
///
/// `presetIndex`/`triggerPrev`/`triggerNext`/`lockCurrentPreset`/`shuffle`/
/// `transitionMode` are wired to the editor here, but nothing on the engine
/// side consumes them yet to actually change presets -- that is Phase 2.6's
/// still-open `PresetLibrary` gap (see the roadmap's 2.2/2.5/2.6 notes).
/// This editor makes them real, automatable, host-visible parameters; a
/// following pass wires the engine side once `PresetLibrary` exists.
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

  MilkDAWpAudioProcessor& processorRef;
  engine::OutputSurface outputSurface;
  juce::Label diagnosticsLabel;
  milkdawp::ui::ControlDrawer controlDrawer;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> shuffleAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> transitionModeAttachment_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MilkDAWpAudioProcessorEditor)
};

} // namespace milkdawp::plugin
