// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "PluginEditor.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/StateSchema.h"

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

// qualityOverride choices: Auto / Low / Medium / High. Auto is full
// resolution until adaptive quality (5.3) drives the scale itself.
float qualityScaleFor(int choice) noexcept {
  switch (choice) {
  case 1:
    return 0.5f;
  case 2:
    return 0.75f;
  default:
    return 1.0f;
  }
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
  engine::EngineControls controls;
  controls.transitionMode = static_cast<core::TransitionMode>(
      std::clamp(static_cast<int>(load(raw_.transitionMode, 2.0f)), 0, static_cast<int>(core::TransitionMode::Energy)));
  controls.transitionBars = static_cast<std::uint32_t>(std::max(1.0f, load(raw_.transitionBars, 4.0f)));
  controls.timedDurationSeconds = load(raw_.transitionDurationSeconds, 5.0f);
  controls.jitterEnabled = load(raw_.transitionJitterEnabled, 0.0f) > 0.5f;
  controls.jitterMinSeconds = load(raw_.transitionDurationMin, 3.0f);
  controls.jitterMaxSeconds = load(raw_.transitionDurationMax, 15.0f);
  controls.cutStyle = load(raw_.hardCutEnabled, 0.0f) > 0.5f ? core::CutStyle::Hard : core::CutStyle::Soft;
  controls.blendSeconds = load(raw_.softCutDuration, 3.0f);
  controls.energyThreshold = load(raw_.energyThreshold, 2.0f);
  controls.locked = load(raw_.lockCurrentPreset, 0.0f) > 0.5f;
  // v1's Shuffle toggle wins over the v2 policy choice when on.
  controls.policy = load(raw_.shuffle, 0.0f) > 0.5f
                        ? core::PlaylistPolicy::ShuffleNoRepeat
                        : static_cast<core::PlaylistPolicy>(
                              std::clamp(static_cast<int>(load(raw_.presetSelectionPolicy, 0.0f)), 0, 2));
  controls.presetIndex = static_cast<std::int32_t>(std::lround(load(raw_.presetIndex, 0.0f)));
  controls.beatSensitivity = load(raw_.beatSensitivity, 1.0f);
  controls.qualityScale = qualityScaleFor(static_cast<int>(load(raw_.qualityOverride, 0.0f)));
  return controls;
}

void MilkDAWpAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  visualizer_->prepare(sampleRate, samplesPerBlock);
  hostTransport_ = std::make_unique<core::HostTransport>(sampleRate);
}

void MilkDAWpAudioProcessor::releaseResources() {}

MILKDAWP_NONBLOCKING void MilkDAWpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
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
  outputWindow_->show(outputWindowBounds_, fullscreen);
}

void MilkDAWpAudioProcessor::closeOutputWindow() {
  if (outputWindow_ != nullptr) {
    outputWindowBounds_ = outputWindow_->windowedBounds();
    outputWindow_.reset();
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
