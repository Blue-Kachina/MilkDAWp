// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"

#include <algorithm>
#include <utility>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/Version.h"
#include "milkdawp/ui/Icons.h"
#include "milkdawp/ui/PresetInfoMenu.h"
#include "milkdawp/ui/PresetMenu.h"
#include "milkdawp/ui/Shortcuts.h"

namespace milkdawp::plugin {

namespace {
constexpr int kDrawerHeight = milkdawp::ui::ControlDrawer::preferredHeight;
} // namespace

MilkDAWpAudioProcessorEditor::MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processorRef(processor), outputSurface(processor.renderEngine()),
      controlDrawer(milkdawp::ui::DrawerStateMachine::Config{.startPinned = true}) {
  addAndMakeVisible(outputSurface);

  // Must be a child of outputSurface, not a sibling: JUCE only composites a
  // GL-attached component's own paint() (and its children's) over the GL
  // content each frame. A sibling Component added at the editor level gets
  // drawn first and then overwritten by the GL surface's buffer swap --
  // exactly the "overlapping sibling peer" risk §4.11 flagged for the
  // drawer, hit here for real on Windows.
  outputSurface.addChildComponent(diagnosticsPanel);
  diagnosticsPanel.onCloseRequested = [this] { setDiagnosticsVisible(false); };
  // Same rule as above: a child of outputSurface, or the GL buffer swap paints over it.
  sendingLabel.setJustificationType(juce::Justification::centred);
  sendingLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  sendingLabel.setColour(juce::Label::backgroundColourId, juce::Colours::black.withAlpha(0.6f));
  sendingLabel.setInterceptsMouseClicks(false, false);
  outputSurface.addChildComponent(sendingLabel);
  outputSurface.addAndMakeVisible(controlDrawer);

  controlDrawer.prevButton.onClick = [this] { pulseTrigger("triggerPrev"); };
  controlDrawer.nextButton.onClick = [this] { pulseTrigger("triggerNext"); };
  controlDrawer.outputButton.onClick = [this] {
    if (processorRef.isOutputWindowOpen()) {
      processorRef.closeOutputWindow();
    } else {
      processorRef.popOutOutputWindow();
    }
  };
  controlDrawer.settingsButton.onClick = [this] { showSettingsMenu(); };
  controlDrawer.onPresetTitleClicked = [this] { showPresetPicker(); };

  lockAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef.apvts, "lockCurrentPreset", controlDrawer.lockButton);
  shuffleAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processorRef.apvts, "shuffle", controlDrawer.shuffleButton);
  transitionModeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
      processorRef.apvts, "transitionMode", controlDrawer.transitionModeCombo);

  // Phase 3.4: the transition settings popover. A child of outputSurface for
  // the same compositing reason as the drawer, added after it so it sits on
  // top; hidden until chosen from the settings menu.
  outputSurface.addChildComponent(transitionSettings);
  transitionSettings.onCloseRequested = [this] { setTransitionSettingsVisible(false); };
  auto& apvts = processorRef.apvts;
  transitionSettingsModeAttachment_ =
      std::make_unique<ComboBoxAttachment>(apvts, "transitionMode", transitionSettings.modeCombo);
  for (auto [id, slider] : {std::pair<const char*, juce::Slider*>{"transitionBars", &transitionSettings.barsSlider},
                            {"transitionDurationSeconds", &transitionSettings.timedDurationSlider},
                            {"transitionDurationMin", &transitionSettings.jitterMinSlider},
                            {"transitionDurationMax", &transitionSettings.jitterMaxSlider},
                            {"energyThreshold", &transitionSettings.energyThresholdSlider},
                            {"softCutDuration", &transitionSettings.blendSlider},
                            {"beatSensitivity", &transitionSettings.reactivitySlider}}) {
    transitionSliderAttachments_.push_back(std::make_unique<SliderAttachment>(apvts, id, *slider));
  }
  transitionButtonAttachments_.push_back(
      std::make_unique<ButtonAttachment>(apvts, "transitionJitterEnabled", transitionSettings.jitterToggle));
  transitionButtonAttachments_.push_back(
      std::make_unique<ButtonAttachment>(apvts, "hardCutEnabled", transitionSettings.hardCutToggle));
  transitionButtonAttachments_.push_back(
      std::make_unique<ButtonAttachment>(apvts, "transitionGridSync", transitionSettings.gridToggle));
  transitionSliderAttachments_.push_back(
      std::make_unique<SliderAttachment>(apvts, "transitionGridOffset", transitionSettings.gridOffsetSlider));
  transitionSettings.refreshRelevance();
  // 5.2: the tag filter lives in the director (saved with the plugin state).
  transitionSettings.tagFilterEditor.setText(juce::String(processor.visualizer().director().tagFilter()),
                                             juce::dontSendNotification);
  transitionSettings.onTagFilterChanged = [this](const juce::String& text) {
    processorRef.visualizer().director().setTagFilter(text.toStdString());
  };

  // Settings -> Output, same compositing rules as the transition popover.
  outputSurface.addChildComponent(outputSettings);
  outputSettings.onCloseRequested = [this] { setOutputSettingsVisible(false); };
  outputSettings.onPreferredSizeChanged = [this] { layoutOutputSettings(); };
  outputSettings.onDefaultFullscreenChanged = [this](bool fullscreen) {
    processorRef.setOutputDefaultFullscreen(fullscreen);
  };
  outputSettings.onTargetDisplayChanged = [this](const milkdawp::core::WindowBounds& display) {
    processorRef.setOutputTargetDisplay(display);
  };
  outputSettings.onTargetInstanceChanged = [this](const std::string& instanceId) {
    processorRef.setOutputTargetInstance(instanceId);
    refreshOutputSettingsInstances();
  };
  outputSettings.onInstanceLabelChanged = [this](const std::string& label) {
    processorRef.setInstanceLabel(label);
    refreshOutputSettingsInstances();
  };
  outputSettings.onSourceParameterChanged = [this](const std::string& senderId, const std::string& parameterId,
                                                   float value) {
    processorRef.setLayerSenderParameter(senderId, parameterId, value);
  };
  layerSliderAttachments_.push_back(std::make_unique<SliderAttachment>(apvts, "layerOpacity", outputSettings.opacitySlider));
  layerSliderAttachments_.push_back(std::make_unique<SliderAttachment>(apvts, "layerOrder", outputSettings.orderSlider));
  layerBlendAttachment_ = std::make_unique<ComboBoxAttachment>(apvts, "layerBlend", outputSettings.blendCombo);
  layerMuteAttachment_ = std::make_unique<ButtonAttachment>(apvts, "layerMute", outputSettings.muteToggle);

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

MilkDAWpAudioProcessorEditor::~MilkDAWpAudioProcessorEditor() {
  stopTimer();
  // Hand the drawer back before it is destroyed, without touching the
  // processor's layout: a floating session reopens floating with the editor.
  controlsWindow_.reset();
}

void MilkDAWpAudioProcessorEditor::resized() {
  outputSurface.setBounds(getLocalBounds());
  layoutDiagnostics();
  sendingLabel.setBounds(outputSurface.getLocalBounds()
                             .withTrimmedBottom(kDrawerHeight)
                             .withSizeKeepingCentre(std::min(getWidth() - 16, 360), 56));
  if (controlsWindow_ == nullptr) {
    controlDrawer.setBounds(outputSurface.getLocalBounds().removeFromBottom(kDrawerHeight));
  }
  layoutTransitionSettings();
  layoutOutputSettings();
  processorRef.setEditorSize(getWidth(), getHeight());
}

void MilkDAWpAudioProcessorEditor::setControlsFloating(bool floating) {
  if (floating == (controlsWindow_ != nullptr)) {
    return;
  }
  if (floating) {
    outputSurface.removeChildComponent(&controlDrawer);
    controlDrawer.setFloating(true);
    controlDrawer.setSize(controlDrawer.getWidth(), milkdawp::ui::ControlDrawer::floatingHeight);
    controlsWindow_ = std::make_unique<milkdawp::ui::DetachedControlsWindow>(controlDrawer);
    // Deferred: the close button fires inside the window's own handler.
    controlsWindow_->onDockRequested = [this] {
      juce::MessageManager::callAsync([editor = juce::Component::SafePointer(this)] {
        if (editor != nullptr) {
          editor->setControlsFloating(false);
        }
      });
    };
    controlsWindow_->onKeyPressed = [this](const juce::KeyPress& key) { return keyPressed(key); };
    controlsWindow_->onLayoutChanged = [this] {
      if (controlsWindow_ != nullptr) {
        processorRef.setControlsLayout(true, controlsWindow_->getBounds());
      }
    };
    const auto saved = processorRef.windowLayout().controlsWindowBounds;
    controlsWindow_->show(saved.isEmpty() ? juce::Rectangle<int>()
                                          : juce::Rectangle<int>(saved.x, saved.y, saved.width, saved.height));
    processorRef.setControlsLayout(true, controlsWindow_->getBounds());
  } else {
    processorRef.setControlsLayout(false, controlsWindow_->getBounds());
    controlsWindow_.reset(); // releases the drawer
    controlDrawer.setFloating(false);
    outputSurface.addAndMakeVisible(controlDrawer);
    transitionSettings.toFront(false); // the popover stays above the drawer
    outputSettings.toFront(false);
    milkdawp::ui::focusFirstControl(outputSettings); // 5.8: Tab through it, Esc closes
    grabKeyboardFocus();
  }
  resized();
}

void MilkDAWpAudioProcessorEditor::layoutTransitionSettings() {
  // Just above the drawer, right-aligned under the settings button that
  // opened it; shrinks to fit at the 480x270 minimum size.
  // The controls, not the gradient headroom above them.
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : milkdawp::ui::ControlDrawer::controlsHeight;
  auto area = outputSurface.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(6);
  const auto width = std::min(milkdawp::ui::TransitionSettingsPanel::preferredWidth, area.getWidth());
  const auto height = std::min(milkdawp::ui::TransitionSettingsPanel::preferredHeight, area.getHeight());
  transitionSettings.setBounds(area.removeFromBottom(height).removeFromRight(width));
}

void MilkDAWpAudioProcessorEditor::layoutOutputSettings() {
  // Same slot as the transition popover: above the drawer, right-aligned.
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : milkdawp::ui::ControlDrawer::controlsHeight;
  auto area = outputSurface.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(6);
  const auto width = std::min(milkdawp::ui::OutputSettingsPanel::preferredWidth, area.getWidth());
  const auto height = std::min(outputSettings.preferredHeight(), area.getHeight());
  outputSettings.setBounds(area.removeFromBottom(height).removeFromRight(width));
}

void MilkDAWpAudioProcessorEditor::setDiagnosticsVisible(bool visible) {
  if (visible) {
    diagnosticsPanel.update(diagnosticsInfo());
    layoutDiagnostics();
  }
  diagnosticsPanel.setVisible(visible);
  if (visible) {
    milkdawp::ui::focusFirstControl(diagnosticsPanel); // 5.8: Copy is one Space away
  } else {
    grabKeyboardFocus();
  }
}

void MilkDAWpAudioProcessorEditor::layoutDiagnostics() {
  // Top left, over the picture; the popovers sit bottom right, above the drawer.
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : milkdawp::ui::ControlDrawer::controlsHeight;
  auto area = outputSurface.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(8);
  diagnosticsPanel.setBounds(area.removeFromTop(std::min(diagnosticsPanel.preferredHeight(), area.getHeight()))
                                 .removeFromLeft(std::min(760, area.getWidth())));
}

core::DiagnosticsInfo MilkDAWpAudioProcessorEditor::diagnosticsInfo() {
  auto info = processorRef.visualizer().diagnostics();
  info.shell = juce::String(JucePlugin_Name " " + juce::String(core::versionString()) + " (" +
                            juce::AudioProcessor::getWrapperTypeDescription(processorRef.wrapperType) + " in " +
                            juce::PluginHostType().getHostDescription() + ")")
                   .toStdString();
  info.surface = ((outputSurface.isSharingWorking() ? juce::String("shared context")
                                                     : juce::String("readback (no shared context)")) +
                  ", context created x" + juce::String(outputSurface.contextCreationCount()))
                     .toStdString();
  return info;
}

void MilkDAWpAudioProcessorEditor::refreshOutputSettingsInstances() {
  milkdawp::ui::InstanceState state;
  for (const auto& info : processorRef.otherInstances()) {
    state.choices.push_back({info.id, info.name, info.canBeTarget});
  }
  state.target = processorRef.windowLayout().outputTargetInstance;
  state.sending = processorRef.isSendingToOtherInstance();
  state.canChooseTarget = processorRef.canChooseOutputTarget();
  state.label = processorRef.instanceLabel();
  state.defaultName = processorRef.instanceDisplayName();
  state.senderCount = processorRef.layerSenderCount();
  for (const auto& sender : processorRef.layerSenders()) {
    state.sources.push_back(
        {sender.id, sender.name, sender.opacity, sender.blend, sender.mute, sender.order, sender.gpuMs});
  }
  outputSettings.setInstanceState(state);
}

void MilkDAWpAudioProcessorEditor::setOutputSettingsVisible(bool visible) {
  if (visible) {
    setTransitionSettingsVisible(false);
    const auto layout = processorRef.windowLayout();
    outputSettings.refresh(layout.outputDefaultFullscreen, layout.outputTargetDisplay);
    refreshOutputSettingsInstances();
    layoutOutputSettings();
  }
  outputSettings.setVisible(visible);
  if (visible) {
    outputSettings.toFront(false);
    milkdawp::ui::focusFirstControl(outputSettings); // 5.8: Tab through it, Esc closes
    controlDrawer.reveal();
  } else {
    grabKeyboardFocus(); // the panel's widgets may have taken it; shortcuts need it back
  }
}

void MilkDAWpAudioProcessorEditor::setTransitionSettingsVisible(bool visible) {
  if (visible && outputSettings.isVisible()) {
    setOutputSettingsVisible(false);
  }
  transitionSettings.setVisible(visible);
  if (visible) {
    transitionSettings.refreshRelevance();
    controlDrawer.reveal();
    milkdawp::ui::focusFirstControl(transitionSettings); // 5.8: Tab through it, Esc closes
  } else {
    grabKeyboardFocus(); // the panel's widgets may have taken it; shortcuts need it back
  }
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
    if (outputSettings.isVisible()) {
      setOutputSettingsVisible(false); // Esc closes the popover first
    } else if (transitionSettings.isVisible()) {
      setTransitionSettingsVisible(false);
    } else if (diagnosticsPanel.isVisible()) {
      setDiagnosticsVisible(false);
    } else {
      controlDrawer.reveal();
    }
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
  case ShortcutAction::BrowsePresets:
    controlDrawer.reveal();
    showPresetPicker();
    return true;
  case ShortcutAction::OpenSettingsMenu:
    controlDrawer.reveal();
    showSettingsMenu();
    return true;
  case ShortcutAction::ToggleDiagnostics:
    setDiagnosticsVisible(!diagnosticsPanel.isVisible());
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
  menu.addItem("Transition settings...", true, transitionSettings.isVisible(),
               [this] { setTransitionSettingsVisible(!transitionSettings.isVisible()); });
  menu.addItem("Output settings...", true, outputSettings.isVisible(),
               [this] { setOutputSettingsVisible(!outputSettings.isVisible()); });
  const bool floating = controlsWindow_ != nullptr;
  menu.addItem(floating ? "Dock controls" : "Float controls in a window",
               [this, floating] { setControlsFloating(!floating); });
  menu.addItem("Output window fullscreen (F11)", [this] { processorRef.toggleOutputFullscreen(); });
  menu.addItem("BPM from DAW", true, processorRef.apvts.getRawParameterValue("useHostTempo")->load() > 0.5f, [this] {
    if (auto* param = processorRef.apvts.getParameter("useHostTempo")) {
      param->beginChangeGesture();
      param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
      param->endChangeGesture();
    }
  });
  // 5.3: Auto adapts the render scale to the GPU; the others fix it.
  if (const auto* spec = milkdawp::core::findParameter(milkdawp::core::allParameters(), "qualityOverride")) {
    juce::PopupMenu quality;
    const auto current = static_cast<int>(processorRef.apvts.getRawParameterValue("qualityOverride")->load());
    for (int i = 0; i < static_cast<int>(spec->choices.size()); ++i) {
      quality.addItem(juce::String(spec->choices[static_cast<std::size_t>(i)]), true, i == current, [this, i] {
        if (auto* param = processorRef.apvts.getParameter("qualityOverride")) {
          param->beginChangeGesture();
          param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(i)));
          param->endChangeGesture();
        }
      });
    }
    menu.addSubMenu("Quality", quality);
  }
  juce::PopupMenu::Item diagnostics("Show diagnostics");
  diagnostics.setTicked(diagnosticsPanel.isVisible());
  diagnostics.shortcutKeyDescription = "D";
  diagnostics.setAction([this] { setDiagnosticsVisible(!diagnosticsPanel.isVisible()); });
  menu.addItem(std::move(diagnostics));
  menu.setLookAndFeel(&controlDrawer.getLookAndFeel());
  menu.showMenuAsync(milkdawp::ui::DrawerLookAndFeel::menuOptions(controlDrawer.settingsMenuAnchor()));
}

void MilkDAWpAudioProcessorEditor::showPresetPicker() {
  auto& director = processorRef.visualizer().director();
  const auto folder = director.presetFolder();

  juce::PopupMenu menu;
  menu.setLookAndFeel(&controlDrawer.getLookAndFeel());
  menu.addSectionHeader(folder.empty() ? juce::String("No preset folder") : juce::String(folder));
  const auto menuIcon = [](milkdawp::ui::Icon icon) {
    return milkdawp::ui::createIconDrawable(icon, milkdawp::ui::drawerTheme::menuText);
  };
  juce::PopupMenu::Item choose("Choose preset folder...");
  choose.setImage(menuIcon(milkdawp::ui::Icon::Folder));
  choose.setAction([this] { choosePresetFolder(); });
  menu.addItem(std::move(choose));
  juce::PopupMenu::Item rescan("Rescan preset folder");
  rescan.setImage(menuIcon(milkdawp::ui::Icon::Rescan));
  rescan.setEnabled(!folder.empty());
  rescan.setAction([this] { processorRef.visualizer().director().rescan(); });
  menu.addItem(std::move(rescan));

  // 5.2: rate / tag / exclude the preset that is playing.
  if (const auto index = director.status().currentIndex; index >= 0) {
    if (auto store = director.presetMetadata()) {
      const auto path = director.presetPath(index);
      const milkdawp::ui::PresetInfoAccess access{
          [store, path] { return store->get(path); },
          [store, path](const milkdawp::core::PresetInfo& info) { store->set(path, info); },
          [store] { return store->allTags(); }};
      milkdawp::ui::addPresetInfoSection(
          menu, juce::String(milkdawp::ui::splitPresetName(director.presetName(index)).leaf), access, this);
    }
  }

  if (const auto names = director.presetNames(); !names.empty()) {
    menu.addSeparator();
    milkdawp::ui::addPresetTree(menu, milkdawp::ui::buildPresetTree(names), director.status().currentIndex,
                                [this](int index) { processorRef.visualizer().director().requestPreset(index); });
  }
  menu.showMenuAsync(milkdawp::ui::DrawerLookAndFeel::menuOptions(controlDrawer.presetTitleComponent()));
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

  if (diagnosticsPanel.isVisible()) {
    const int before = diagnosticsPanel.preferredHeight();
    diagnosticsPanel.update(diagnosticsInfo());
    if (diagnosticsPanel.preferredHeight() != before) {
      layoutDiagnostics();
    }
  }

  if (transitionSettings.isVisible()) {
    const auto tagStatus = milkdawp::ui::describeAutoSelection(status.autoSelectable, status.playlistSize,
                                                               status.tagFilterMatchesNothing);
    transitionSettings.setTagFilterStatus(tagStatus.text, tagStatus.warning);
  }

  if (status.playlistSize == 0) {
    controlDrawer.setPresetInfo("No presets loaded", "Click to choose a preset folder", {});
  } else if (status.currentIndex >= 0) {
    const auto fullName = visualizer.director().presetName(status.currentIndex);
    const auto parts = milkdawp::ui::splitPresetName(fullName);
    juce::String detail;
    if (!parts.folder.empty()) {
      detail << juce::String(parts.folder) << juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
    }
    detail << juce::String(status.currentIndex + 1) << " / " << juce::String(status.playlistSize);
    if (const auto quality = milkdawp::ui::describeRenderQuality(stats.qualityScale, stats.qualityAuto);
        quality.isNotEmpty()) {
      detail << juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) << quality; // 5.3
    }
    controlDrawer.setPresetInfo(juce::String(parts.leaf), detail, juce::String(fullName));
  }

  using milkdawp::ui::BeatBadgeSource;
  const auto source = status.beatSource == engine::BeatSource::Host       ? BeatBadgeSource::Host
                      : status.beatSource == engine::BeatSource::Detected ? BeatBadgeSource::Detected
                                                                          : BeatBadgeSource::None;
  const auto badge = milkdawp::ui::describeBeat(source, status.bpm, status.beatConfidence);
  controlDrawer.bpmLabel.setText(badge.text, juce::dontSendNotification);
  controlDrawer.bpmLabel.setColour(juce::Label::textColourId, badge.colour);
  controlDrawer.bpmLabel.setTooltip(badge.tooltip);

  // Follow the processor's layout: a restored project may float or dock the
  // controls while this editor is open (and this also applies it on open).
  if (const bool wantFloating = processorRef.windowLayout().controlsFloating;
      wantFloating != (controlsWindow_ != nullptr)) {
    setControlsFloating(wantFloating);
  }

  // Host automation moves the attached widgets but not their dimming.
  if (transitionSettings.isVisible()) {
    transitionSettings.refreshRelevance();
  }
  // Other instances come and go (and link up) while the panel is open.
  if (outputSettings.isVisible()) {
    refreshOutputSettingsInstances();
  }

  // While this instance's picture lives in another instance's Output window,
  // its own surface has nothing new to show: say where the picture went.
  const bool sending = processorRef.isSendingToOtherInstance();
  if (sending) {
    sendingLabel.setText("Shown in \"" + juce::String(processorRef.outputTargetName()) +
                             "\"'s Output window.\nUse the pop-out button to open it.",
                         juce::dontSendNotification);
  }
  if (sendingLabel.isVisible() != sending) {
    sendingLabel.setVisible(sending);
  }
}

} // namespace milkdawp::plugin
