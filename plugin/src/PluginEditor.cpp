// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"

#include "milkdawp/ui/Shortcuts.h"

namespace milkdawp::plugin {

namespace {
constexpr int kDrawerHeight = milkdawp::ui::ControlDrawer::preferredHeight;
} // namespace

MilkDAWpAudioProcessorEditor::MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processorRef(processor), outputSurface(processor.renderEngine()),
      controlDrawer(milkdawp::ui::DrawerStateMachine::Config{.startPinned = true}) {
  addAndMakeVisible(outputSurface);

  diagnosticsLabel.setJustificationType(juce::Justification::topLeft);
  diagnosticsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  diagnosticsLabel.setColour(juce::Label::backgroundColourId, juce::Colours::black.withAlpha(0.5f));
  // Must be a child of outputSurface, not a sibling: JUCE only composites a
  // GL-attached component's own paint() (and its children's) over the GL
  // content each frame. A sibling Component added at the editor level gets
  // drawn first and then overwritten by the GL surface's buffer swap --
  // exactly the "overlapping sibling peer" risk §4.11 flagged for the
  // drawer, hit here for real on Windows.
  outputSurface.addAndMakeVisible(diagnosticsLabel);
  outputSurface.addAndMakeVisible(controlDrawer);

  controlDrawer.prevButton.onClick = [this] { pulseTrigger("triggerPrev"); };
  controlDrawer.nextButton.onClick = [this] { pulseTrigger("triggerNext"); };
  controlDrawer.outputButton.onClick = [this] {
    if (processorRef.isOutputWindowOpen()) {
      processorRef.closeOutputWindow();
    } else {
      processorRef.openOutputWindow(/*fullscreen=*/false);
    }
  };
  controlDrawer.settingsButton.onClick = [this] { showSettingsMenu(); };

  lockAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef.apvts, "lockCurrentPreset", controlDrawer.lockButton);
  shuffleAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef.apvts, "shuffle", controlDrawer.shuffleButton);
  transitionModeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
      processorRef.apvts, "transitionMode", controlDrawer.transitionModeCombo);

  // Plugin build keeps EDITOR_WANTS_KEYBOARD_FOCUS FALSE (carried over from
  // v1, which received keys fine in practice) -- that only affects the
  // wrapper's initial focus request to the host. setWantsKeyboardFocus()
  // alone only makes this component ELIGIBLE for focus; it does not claim
  // it, so whatever the host gives initial focus to (confirmed in Reaper:
  // its own host-provided preset-selector combo in the FX chain UI) just
  // keeps it and our keyPressed() below never fires. grabKeyboardFocus()
  // actually claims it -- once here at construction, and again in
  // visibilityChanged() below for when the editor is hidden/reshown (e.g.
  // switching FX chain tabs and back) without being reconstructed.
  setWantsKeyboardFocus(true);
  grabKeyboardFocus();

  setResizable(true, true);
  if (auto* sizeConstrainer = getConstrainer()) {
    sizeConstrainer->setMinimumSize(480, 270); // §4.9: minimum primary-window size
  }
  // Reads whatever size was last persisted on the processor (defaults to
  // 480x270 if none was -- see MilkDAWpAudioProcessor::editorWidth_). This
  // is what makes editor-size persistence immune to host construction
  // order: the size lives on the processor, not applied imperatively to an
  // editor that might not exist yet when setStateInformation runs (§2.9's
  // Cubase lesson).
  setSize(processorRef.editorWidth(), processorRef.editorHeight());

  timerCallback(); // show correct text immediately, not just after the first tick
  startTimerHz(10);
}

MilkDAWpAudioProcessorEditor::~MilkDAWpAudioProcessorEditor() { stopTimer(); }

void MilkDAWpAudioProcessorEditor::resized() {
  outputSurface.setBounds(getLocalBounds());
  // Relative to outputSurface's own local bounds now that it's the parent.
  diagnosticsLabel.setBounds(outputSurface.getLocalBounds().removeFromTop(80).reduced(8));
  controlDrawer.setBounds(outputSurface.getLocalBounds().removeFromBottom(kDrawerHeight));
  processorRef.setEditorSize(getWidth(), getHeight());
}

void MilkDAWpAudioProcessorEditor::visibilityChanged() {
  // Re-claim focus whenever the editor becomes visible again (e.g. the host
  // switches FX chain tabs away and back) without reconstructing it -- the
  // constructor's grabKeyboardFocus() only covers the first time.
  if (isShowing()) {
    grabKeyboardFocus();
  }
}

bool MilkDAWpAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
  using milkdawp::ui::ShortcutAction;
  switch (milkdawp::ui::mapKeyPress(key, /*isAppShell=*/false)) {
  case ShortcutAction::ExitFullscreenOrRevealDrawer:
    controlDrawer.reveal();
    return true;
  case ShortcutAction::PreviousPreset:
    pulseTrigger("triggerPrev");
    return true;
  case ShortcutAction::NextPreset:
    pulseTrigger("triggerNext");
    return true;
  case ShortcutAction::ToggleLock:
    if (auto* param = processorRef.apvts.getParameter("lockCurrentPreset")) {
      param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    return true;
  case ShortcutAction::ToggleShuffle:
    if (auto* param = processorRef.apvts.getParameter("shuffle")) {
      param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    return true;
  case ShortcutAction::ToggleDrawer:
    controlDrawer.toggleRevealHide();
    return true;
  case ShortcutAction::TogglePin:
    controlDrawer.togglePin();
    return true;
  case ShortcutAction::ToggleFullscreen:
    // A host-framed editor cannot go fullscreen itself: F11 opens (or
    // toggles) the Output window fullscreen instead (§4.9).
    processorRef.toggleOutputFullscreen();
    return true;
  case ShortcutAction::None:
  default:
    return false; // never consume a key we can't act on (§4.9): the host still sees it
  }
}

void MilkDAWpAudioProcessorEditor::pulseTrigger(const juce::String& parameterId) {
  // triggerPrev/triggerNext are momentary commands, not persistent toggles:
  // pulse 0 -> 1 -> 0 in one gesture so state save/restore never captures a
  // "stuck on" trigger.
  auto* param = processorRef.apvts.getParameter(parameterId);
  if (param == nullptr) {
    return;
  }
  param->beginChangeGesture();
  param->setValueNotifyingHost(1.0f);
  param->setValueNotifyingHost(0.0f);
  param->endChangeGesture();
}

void MilkDAWpAudioProcessorEditor::showSettingsMenu() {
  auto& director = processorRef.visualizer().director();
  const auto folder = director.presetFolder();

  juce::PopupMenu menu;
  menu.addSectionHeader(folder.empty() ? juce::String("No preset folder") : juce::String(folder));
  menu.addItem("Choose preset folder...", [this] { choosePresetFolder(); });
  menu.addItem("Rescan preset folder", !folder.empty(), false,
               [this] { processorRef.visualizer().director().rescan(); });
  menu.addSeparator();
  menu.addItem("Output window fullscreen (F11)", [this] { processorRef.toggleOutputFullscreen(); });
  menu.addItem("Show diagnostics", true, diagnosticsLabel.isVisible(),
               [this] { diagnosticsLabel.setVisible(!diagnosticsLabel.isVisible()); });
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&controlDrawer.settingsButton));
}

void MilkDAWpAudioProcessorEditor::choosePresetFolder() {
  const auto current = processorRef.visualizer().director().presetFolder();
  folderChooser_ = std::make_unique<juce::FileChooser>(
      "Choose a folder of MilkDrop presets (.milk)",
      current.empty() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory) : juce::File(current));
  folderChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this](const juce::FileChooser& chooser) {
                                const auto result = chooser.getResult();
                                if (result.isDirectory()) {
                                  processorRef.visualizer().director().setPresetFolder(
                                      result.getFullPathName().toStdString());
                                }
                              });
}

void MilkDAWpAudioProcessorEditor::timerCallback() {
  auto& visualizer = processorRef.visualizer();
  auto& engine = visualizer.renderEngine();
  const auto stats = engine.stats();
  const auto status = visualizer.director().status();

  if (diagnosticsLabel.isVisible()) {
    juce::String text;
    if (engine.isAvailable()) {
      text << "projectM " << engine.projectMVersion() << ": " << juce::String(stats.framesPerSecond, 1) << " fps, "
           << stats.width << "x" << stats.height << ", render " << juce::String(stats.cpuFrameMs, 1) << " ms (gpu "
           << juce::String(stats.gpuFrameMs, 1) << " ms), last preset load "
           << juce::String(stats.lastPresetLoadMs, 1) << " ms\n";
    } else {
      const auto reason = engine.unavailableReason();
      text << "projectM: "
           << (reason.empty() ? juce::String("starting...") : juce::String("unavailable (" + reason + ")")) << "\n";
    }
    text << "presets: " << juce::String(status.playlistSize) << " in folder, " << juce::String(stats.presetsLoaded) << " loaded, "
         << juce::String(status.presetsSkipped) << " skipped; surface " << (outputSurface.isSharingWorking() ? "ok" : "NOT SHARING")
         << " (context x" << outputSurface.contextCreationCount() << ")\n";
    text << engine.glDescription();
    diagnosticsLabel.setText(text, juce::dontSendNotification);
  }

  juce::String presetText;
  if (status.playlistSize == 0) {
    presetText = "No presets: Set > Choose preset folder";
  } else if (status.currentIndex >= 0) {
    presetText = juce::String(status.currentIndex + 1) + "/" + juce::String(status.playlistSize) + "  " +
                 juce::String(visualizer.director().presetName(status.currentIndex));
  }
  controlDrawer.presetLabel.setText(presetText, juce::dontSendNotification);
  controlDrawer.presetLabel.setTooltip(presetText);

  juce::String bpmText(juce::CharPointer_UTF8("\xE2\x99\xA9")); // quarter note
  if (status.beatSource == engine::BeatSource::None || status.bpm <= 0.0f) {
    bpmText << "--";
  } else {
    bpmText << juce::String(status.bpm, 0) << (status.beatSource == engine::BeatSource::Host ? " host" : "");
  }
  controlDrawer.bpmLabel.setText(bpmText, juce::dontSendNotification);
}

} // namespace milkdawp::plugin
