// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "PluginEditor.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/StateSchema.h"
#include "milkdawp/engine/ControlMapping.h"

namespace milkdawp::plugin {

namespace {

core::TransportInfo extractTransportInfo(juce::AudioPlayHead* playHead) {
  core::TransportInfo info; // defaults: not playing, 0 bpm/ppq, 4/4, sample 0
  if (playHead == nullptr) {
    return info;
  }
  const auto position = playHead->getPosition();
  if (!position.hasValue()) {
    return info;
  }
  info.isPlaying = position->getIsPlaying();
  info.bpm = position->getBpm().orFallback(0.0);
  info.ppqPosition = position->getPpqPosition().orFallback(0.0);
  info.timeSigNumerator = position->getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature{}).numerator;
  info.samplePos = static_cast<std::uint64_t>(position->getTimeInSamples().orFallback(int64_t{0}));
  return info;
}

float load(const std::atomic<float>* value, float fallback) noexcept {
  return value != nullptr ? value->load(std::memory_order_relaxed) : fallback;
}

core::WindowBounds toWindowBounds(juce::Rectangle<int> r) noexcept {
  return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}

juce::Rectangle<int> toRectangle(const core::WindowBounds& b) noexcept {
  return b.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int>(b.x, b.y, b.width, b.height);
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MilkDAWpAudioProcessor::createParameterLayout() {
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

  for (const auto& spec : core::allParameters()) {
    const juce::ParameterID id{juce::String(spec.id), 1};
    switch (spec.type) {
    case core::ParameterType::Float:
      params.push_back(std::make_unique<juce::AudioParameterFloat>(id, spec.displayName, spec.minValue,
                                                                     spec.maxValue, spec.defaultValue));
      break;
    case core::ParameterType::Bool:
      params.push_back(
          std::make_unique<juce::AudioParameterBool>(id, spec.displayName, spec.defaultValue != 0.0f));
      break;
    case core::ParameterType::Int:
      params.push_back(std::make_unique<juce::AudioParameterInt>(id, spec.displayName,
                                                                   static_cast<int>(spec.minValue),
                                                                   static_cast<int>(spec.maxValue),
                                                                   static_cast<int>(spec.defaultValue)));
      break;
    case core::ParameterType::Choice: {
      juce::StringArray choices;
      for (const auto& choice : spec.choices) {
        choices.add(choice);
      }
      params.push_back(std::make_unique<juce::AudioParameterChoice>(id, spec.displayName, choices,
                                                                      static_cast<int>(spec.defaultValue)));
      break;
    }
    }
  }

  return {params.begin(), params.end()};
}

MilkDAWpAudioProcessor::MilkDAWpAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  engine::Visualizer::Config config;
  // The JUCE Standalone wrapper has no play head: there, the detected beat
  // drives everything and "transport stopped" never pauses the Timed clock.
  config.followHostTransport = wrapperType != wrapperType_Standalone;
  visualizer_ = std::make_unique<engine::Visualizer>(config);

  // CI guard (3.9/3.10): a plugin that can't find projectM stays inert and
  // still passes pluginval, which would hide a broken runtime layout (rpath,
  // install name, missing library). With MILKDAWP_REQUIRE_PROJECTM set, as
  // in the CI pluginval steps, that is fatal instead.
  if (juce::SystemStats::getEnvironmentVariable("MILKDAWP_REQUIRE_PROJECTM", {}).isNotEmpty()) {
    const auto version = visualizer_->renderEngine().projectMVersion();
    if (version.empty()) {
      std::fprintf(stderr, "MilkDAWp: projectM required (MILKDAWP_REQUIRE_PROJECTM) but not loaded: %s\n",
                   visualizer_->renderEngine().unavailableReason().c_str());
      std::fflush(stderr);
      std::abort();
    }
    std::fprintf(stderr, "MilkDAWp: projectM %s loaded\n", version.c_str());
  }

  raw_.beatSensitivity = apvts.getRawParameterValue("beatSensitivity");
  raw_.transitionDurationSeconds = apvts.getRawParameterValue("transitionDurationSeconds");
  raw_.shuffle = apvts.getRawParameterValue("shuffle");
  raw_.lockCurrentPreset = apvts.getRawParameterValue("lockCurrentPreset");
  raw_.presetIndex = apvts.getRawParameterValue("presetIndex");
  raw_.transitionJitterEnabled = apvts.getRawParameterValue("transitionJitterEnabled");
  raw_.transitionDurationMin = apvts.getRawParameterValue("transitionDurationMin");
  raw_.transitionDurationMax = apvts.getRawParameterValue("transitionDurationMax");
  raw_.hardCutEnabled = apvts.getRawParameterValue("hardCutEnabled");
  raw_.softCutDuration = apvts.getRawParameterValue("softCutDuration");
  raw_.qualityOverride = apvts.getRawParameterValue("qualityOverride");
  raw_.transitionMode = apvts.getRawParameterValue("transitionMode");
  raw_.transitionBars = apvts.getRawParameterValue("transitionBars");
  raw_.presetSelectionPolicy = apvts.getRawParameterValue("presetSelectionPolicy");
  raw_.energyThreshold = apvts.getRawParameterValue("energyThreshold");

  // Momentary commands: react to the 0 -> 1 edge wherever it comes from
  // (editor pulse, host automation, MIDI learn later). A per-block poll
  // would miss a pulse that rises and falls between two blocks.
  apvts.addParameterListener("triggerNext", this);
  apvts.addParameterListener("triggerPrev", this);

  visualizer_->setControls(readControls());
}

MilkDAWpAudioProcessor::~MilkDAWpAudioProcessor() {
  cancelPendingUpdate();
  apvts.removeParameterListener("triggerNext", this);
  apvts.removeParameterListener("triggerPrev", this);
  outputWindow_.reset();
}

void MilkDAWpAudioProcessor::parameterChanged(const juce::String& parameterId, float newValue) {
  if (newValue < 0.5f) {
    return;
  }
  // Any thread (including the audio thread under automation): both calls
  // are a single atomic increment.
  if (parameterId == "triggerNext") {
    visualizer_->director().requestNext();
  } else if (parameterId == "triggerPrev") {
    visualizer_->director().requestPrevious();
  }
}

engine::EngineControls MilkDAWpAudioProcessor::readControls() const noexcept {
  // The mapping itself is shared with the app (engine::toEngineControls);
  // this only copies the APVTS atomics into it, keeping the model's defaults
  // for any parameter that is missing.
  engine::ParameterValues values;
  const auto copy = [](float& field, const std::atomic<float>* value) { field = load(value, field); };
  copy(values.beatSensitivity, raw_.beatSensitivity);
  copy(values.transitionDurationSeconds, raw_.transitionDurationSeconds);
  copy(values.shuffle, raw_.shuffle);
  copy(values.lockCurrentPreset, raw_.lockCurrentPreset);
  copy(values.presetIndex, raw_.presetIndex);
  copy(values.transitionJitterEnabled, raw_.transitionJitterEnabled);
  copy(values.transitionDurationMin, raw_.transitionDurationMin);
  copy(values.transitionDurationMax, raw_.transitionDurationMax);
  copy(values.hardCutEnabled, raw_.hardCutEnabled);
  copy(values.softCutDuration, raw_.softCutDuration);
  copy(values.qualityOverride, raw_.qualityOverride);
  copy(values.transitionMode, raw_.transitionMode);
  copy(values.transitionBars, raw_.transitionBars);
  copy(values.presetSelectionPolicy, raw_.presetSelectionPolicy);
  copy(values.energyThreshold, raw_.energyThreshold);
  return engine::toEngineControls(values);
}

void MilkDAWpAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  visualizer_->prepare(sampleRate, samplesPerBlock);
  hostTransport_ = std::make_unique<core::HostTransport>(sampleRate);
}

void MilkDAWpAudioProcessor::releaseResources() {}

void MilkDAWpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) noexcept MILKDAWP_NONBLOCKING {
  // Bit-exact passthrough (§4.1 goal: zero audio impact) -- buffer is only
  // ever read below, never written to.
  const auto transportInfo = extractTransportInfo(getPlayHead());
  transportSnapshot_.publish(transportInfo);
  if (hostTransport_) {
    beatClockSnapshot_.publish(hostTransport_->processTransport(transportInfo));
  }

  visualizer_->processAudio(buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples(),
                            &transportInfo);
  visualizer_->setControls(readControls());
}

juce::AudioProcessorEditor* MilkDAWpAudioProcessor::createEditor() {
  return new MilkDAWpAudioProcessorEditor(*this);
}

void MilkDAWpAudioProcessor::setEditorSize(int width, int height) noexcept {
  editorWidth_ = width;
  editorHeight_ = height;
}

void MilkDAWpAudioProcessor::openOutputWindow(bool fullscreen) {
  if (outputWindow_ != nullptr) {
    if (fullscreen && !outputWindow_->isFullscreen()) {
      outputWindow_->setFullscreen(true);
    }
    outputWindow_->toFront(true);
    return;
  }
  outputWindow_ = std::make_unique<engine::OutputWindow>(visualizer_->renderEngine());
  // Deferred: the close button fires inside the window's own event handler.
  // The window is owned here, so while it is alive the processor is too.
  outputWindow_->onCloseRequested = [this] {
    juce::MessageManager::callAsync(
        [this, window = juce::Component::SafePointer<engine::OutputWindow>(outputWindow_.get())] {
          if (window != nullptr) {
            closeOutputWindow();
          }
        });
  };
  outputWindow_->onLayoutChanged = [this] { updateOutputLayout(); };
  outputWindow_->show(toRectangle(windowLayout().outputWindowBounds), fullscreen);
  updateOutputLayout();
}

void MilkDAWpAudioProcessor::closeOutputWindow() {
  if (outputWindow_ != nullptr) {
    updateOutputLayout();
    outputWindow_.reset();
    const std::lock_guard lock(layoutMutex_);
    layout_.outputWindowOpen = false;
  }
}

void MilkDAWpAudioProcessor::updateOutputLayout() {
  if (outputWindow_ == nullptr) {
    return;
  }
  const std::lock_guard lock(layoutMutex_);
  layout_.outputWindowOpen = true;
  layout_.outputWindowFullscreen = outputWindow_->isFullscreen();
  layout_.outputWindowBounds = toWindowBounds(outputWindow_->windowedBounds());
}

core::WindowLayout MilkDAWpAudioProcessor::windowLayout() const {
  const std::lock_guard lock(layoutMutex_);
  return layout_;
}

void MilkDAWpAudioProcessor::setControlsLayout(bool floating, juce::Rectangle<int> bounds) {
  const std::lock_guard lock(layoutMutex_);
  layout_.controlsFloating = floating;
  if (!bounds.isEmpty()) {
    layout_.controlsWindowBounds = toWindowBounds(bounds);
  }
}

void MilkDAWpAudioProcessor::handleAsyncUpdate() {
  // The layout was just restored; make the Output window match it. The
  // editor follows `controlsFloating` itself (its timer), and an editor
  // opened later reads it when constructed.
  const auto layout = windowLayout();
  if (!layout.outputWindowOpen) {
    closeOutputWindow();
  } else if (outputWindow_ == nullptr) {
    openOutputWindow(layout.outputWindowFullscreen);
  } else if (outputWindow_->isFullscreen() != layout.outputWindowFullscreen) {
    outputWindow_->setFullscreen(layout.outputWindowFullscreen);
  }
}

void MilkDAWpAudioProcessor::toggleOutputFullscreen() {
  if (outputWindow_ == nullptr) {
    openOutputWindow(true);
  } else {
    outputWindow_->toggleFullscreen();
  }
}

void MilkDAWpAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
  core::StateSchemaV2 state;
  state.editorWidth = editorWidth_;
  state.editorHeight = editorHeight_;
  state.playlistFolderPath = visualizer_->director().presetFolder();
  state.presetAbsolutePath = visualizer_->director().currentPresetPath();
  state.windows = windowLayout();
  for (const auto& spec : core::allParameters()) {
    if (auto* raw = apvts.getRawParameterValue(juce::String(spec.id))) {
      state.paramValues[spec.id] = raw->load(std::memory_order_relaxed);
    }
  }

  const auto text = core::serializeStateSchemaV2(state);
  destData.setSize(text.size());
  destData.copyFrom(text.data(), 0, text.size());
}

void MilkDAWpAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
  if (data == nullptr || sizeInBytes <= 0) {
    return;
  }
  const std::string text(static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes));
  const auto state = core::deserializeStateSchemaV2(text);

  for (const auto& [id, value] : state.paramValues) {
    const juce::String jid(id);
    if (auto* param = apvts.getParameter(jid)) {
      const auto range = apvts.getParameterRange(jid);
      param->setValueNotifyingHost(range.convertTo0to1(value));
    }
  }

  if (state.editorWidth > 0 && state.editorHeight > 0) {
    setEditorSize(state.editorWidth, state.editorHeight);
  }

  {
    const std::lock_guard lock(layoutMutex_);
    layout_ = state.windows;
  }
  triggerAsyncUpdate(); // opens/closes the Output window on the message thread

  // Not publishing controls here: some hosts restore state while audio runs,
  // and processBlock is the controls snapshot's one writer. The next block
  // publishes the restored parameters; the preferred preset path below
  // already selects the right preset on the scan.
  if (!state.playlistFolderPath.empty()) {
    visualizer_->director().setPresetFolder(state.playlistFolderPath, state.presetAbsolutePath);
  }
}

} // namespace milkdawp::plugin

// This is what the JUCE plugin wrappers call to create the processor instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new milkdawp::plugin::MilkDAWpAudioProcessor();
}
