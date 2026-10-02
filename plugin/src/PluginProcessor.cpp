// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "PluginEditor.h"
#include "milkdawp/core/DisplayLayout.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/StateSchema.h"
#include "milkdawp/engine/ControlMapping.h"
#include "milkdawp/ui/OutputSettings.h"

namespace milkdawp::plugin {

namespace {

// Size of a new Output window with nothing saved (matches engine::OutputWindow).
constexpr int kDefaultOutputWidth = 1280;
constexpr int kDefaultOutputHeight = 720;

// The parameters that say how this instance is mixed onto another's canvas.
constexpr const char* kLayerParameterIds[] = {"layerOpacity", "layerBlend", "layerMute", "layerOrder"};

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
  raw_.useHostTempo = apvts.getRawParameterValue("useHostTempo");
  raw_.layerOpacity = apvts.getRawParameterValue("layerOpacity");
  raw_.layerBlend = apvts.getRawParameterValue("layerBlend");
  raw_.layerMute = apvts.getRawParameterValue("layerMute");
  raw_.layerOrder = apvts.getRawParameterValue("layerOrder");
  raw_.transitionGridSync = apvts.getRawParameterValue("transitionGridSync");
  raw_.transitionGridOffset = apvts.getRawParameterValue("transitionGridOffset");

  // Momentary commands: react to the 0 -> 1 edge wherever it comes from
  // (editor pulse, host automation, MIDI learn later). A per-block poll
  // would miss a pulse that rises and falls between two blocks.
  apvts.addParameterListener("triggerNext", this);
  apvts.addParameterListener("triggerPrev", this);
  for (const auto* id : kLayerParameterIds) {
    apvts.addParameterListener(id, this);
  }

  visualizer_->setControls(readControls());

  // Layers: join the process-wide registry, so other instances can send their
  // picture here and this one can send its picture elsewhere. Another instance
  // appearing or going wakes handleAsyncUpdate(), which reconciles the link.
  {
    LayerRegistry::Entry::Hooks hooks;
    hooks.popOut = [this] { openOwnOutputWindow(); };
    hooks.toggleFullscreen = [this] { toggleOwnOutputFullscreen(); };
    // What a hub's Sources list reads and writes of this instance's layer.
    hooks.getParameter = [this](const std::string& id) {
      const auto* raw = apvts.getRawParameterValue(juce::String(id));
      return raw != nullptr ? raw->load() : 0.0f;
    };
    hooks.setParameter = [this](const std::string& id, float plainValue) {
      const juce::String jid(id);
      if (auto* parameter = apvts.getParameter(jid)) {
        // A gesture, so the host records it as one edit (undo, automation write).
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(apvts.getParameterRange(jid).convertTo0to1(plainValue));
        parameter->endChangeGesture();
      }
    };
    registryEntry_ = LayerRegistry::get().add({}, {}, visualizer_->renderEngine(), std::move(hooks));
  }
  refreshRegistryName();
  applyLayerParams();
  registryListener_ = LayerRegistry::get().addListener([this] { triggerAsyncUpdate(); });
}

MilkDAWpAudioProcessor::~MilkDAWpAudioProcessor() {
  // Stop being told about other instances first, then let go of any hub (or any
  // senders) while this instance's engine, which they use, still exists.
  LayerRegistry::get().removeListener(registryListener_);
  cancelPendingUpdate();
  detachFromHub();
  LayerRegistry::get().remove(registryEntry_);
  apvts.removeParameterListener("triggerNext", this);
  apvts.removeParameterListener("triggerPrev", this);
  for (const auto* id : kLayerParameterIds) {
    apvts.removeParameterListener(id, this);
  }
  outputWindow_.reset();
}

void MilkDAWpAudioProcessor::applyLayerParams() noexcept {
  auto& channel = visualizer_->renderEngine().primaryLayer();
  channel.setOpacity(load(raw_.layerOpacity, 1.0f));
  const auto blend = std::clamp(static_cast<int>(std::lround(load(raw_.layerBlend, 0.0f))), 0,
                                engine::kLayerBlendCount - 1);
  channel.setBlend(static_cast<engine::LayerBlend>(blend));
  channel.setVisible(load(raw_.layerMute, 0.0f) < 0.5f);
  channel.setOrder(static_cast<int>(std::lround(load(raw_.layerOrder, 0.0f))));
}

void MilkDAWpAudioProcessor::parameterChanged(const juce::String& parameterId, float newValue) {
  for (const auto* id : kLayerParameterIds) {
    if (parameterId == id) {
      applyLayerParams(); // a few atomic stores: fine on any thread
      return;
    }
  }
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
  copy(values.useHostTempo, raw_.useHostTempo);
  copy(values.transitionGridSync, raw_.transitionGridSync);
  copy(values.transitionGridOffset, raw_.transitionGridOffset);
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
  outputWindow_->show(outputOpenBounds(), fullscreen);
  updateOutputLayout();
}

juce::Rectangle<int> MilkDAWpAudioProcessor::outputOpenBounds() const {
  const auto layout = windowLayout();
  if (layout.outputTargetDisplay.isEmpty()) {
    return toRectangle(layout.outputWindowBounds); // automatic: where it was, or the primary display
  }
  std::vector<core::WindowBounds> displays;
  for (const auto& display : ui::currentDisplays()) {
    displays.push_back(display.id);
  }
  const auto index = core::findDisplay(displays, layout.outputTargetDisplay);
  if (index < 0) {
    // The chosen display is unplugged. Fall back to automatic, but keep the
    // choice in the state: it applies again when the display returns.
    return toRectangle(layout.outputWindowBounds);
  }
  const auto placed = core::placeOnDisplay(layout.outputWindowBounds, displays[static_cast<std::size_t>(index)],
                                           kDefaultOutputWidth, kDefaultOutputHeight);
  return toRectangle(placed);
}

void MilkDAWpAudioProcessor::popOutOutputWindow() {
  if (hub_ != nullptr && hub_->isAlive()) {
    hub_->popOut(); // this instance's picture lives in the hub's window
    return;
  }
  openOwnOutputWindow();
}

void MilkDAWpAudioProcessor::openOwnOutputWindow() { openOutputWindow(windowLayout().outputDefaultFullscreen); }

std::string MilkDAWpAudioProcessor::instanceId() const { return registryEntry_->id(); }

std::string MilkDAWpAudioProcessor::instanceLabel() const {
  const std::lock_guard lock(layoutMutex_);
  return instanceLabel_;
}

std::string MilkDAWpAudioProcessor::instanceDisplayName() const {
  {
    const std::lock_guard lock(layoutMutex_);
    if (!instanceLabel_.empty()) {
      return instanceLabel_;
    }
    if (!hostTrackName_.empty()) {
      return hostTrackName_;
    }
  }
  return "Instance " + instanceId().substr(0, 4);
}

void MilkDAWpAudioProcessor::refreshRegistryName() { registryEntry_->setName(instanceDisplayName()); }

void MilkDAWpAudioProcessor::setInstanceLabel(const std::string& label) {
  {
    const std::lock_guard lock(layoutMutex_);
    instanceLabel_ = label;
  }
  refreshRegistryName();
}

void MilkDAWpAudioProcessor::updateTrackProperties(const TrackProperties& properties) {
  {
    const std::lock_guard lock(layoutMutex_);
    hostTrackName_ = properties.name.has_value() ? properties.name->toStdString() : std::string{};
  }
  refreshRegistryName();
}

std::vector<InstanceInfo> MilkDAWpAudioProcessor::otherInstances() const {
  return LayerRegistry::get().instances(instanceId());
}

bool MilkDAWpAudioProcessor::canChooseOutputTarget() const { return registryEntry_->attachedCount() == 0; }

int MilkDAWpAudioProcessor::layerSenderCount() const { return registryEntry_->attachedCount(); }

void MilkDAWpAudioProcessor::setOutputTargetInstance(const std::string& instanceId) {
  {
    const std::lock_guard lock(layoutMutex_);
    layout_.outputTargetInstance = instanceId;
  }
  reconcileLayers();
}

void MilkDAWpAudioProcessor::reconcileLayers() {
  const auto desired = windowLayout().outputTargetInstance;
  if (hub_ != nullptr && (desired != hubId_ || !hub_->isAlive())) {
    detachFromHub();
  }
  if (hub_ == nullptr && !desired.empty() && desired != instanceId()) {
    // Lazy: the target may not exist yet (it loads after us, or the host has
    // not created it). The registry wakes us again when it appears.
    const auto target = LayerRegistry::get().find(desired);
    if (target != nullptr && target->isAlive() && !target->isSender() && registryEntry_->attachedCount() == 0) {
      attachToHub(target, desired);
    }
  }
}

void MilkDAWpAudioProcessor::attachToHub(const std::shared_ptr<LayerRegistry::Entry>& hub, const std::string& hubId) {
  auto& renderEngine = visualizer_->renderEngine();
  // This instance's engine lets go of its primary layer first: the layer's
  // channel has one consumer, and it is about to be the hub's.
  renderEngine.yieldPrimaryLayer(true);
  if (!hub->attach(renderEngine.primaryLayer(), registryEntry_)) {
    renderEngine.yieldPrimaryLayer(false);
    return;
  }
  hub_ = hub;
  hubId_ = hubId;
  registryEntry_->setSender(true);
  closeOutputWindow(); // this picture is shown in the hub's window now
  // The hub just made a fresh projectM instance for this layer, which knows
  // nothing about the preset this instance was showing and would sit on
  // projectM's idle preset until the next cut. Send the current one again.
  visualizer_->director().requestReissueCurrent();
}

void MilkDAWpAudioProcessor::detachFromHub() {
  if (hub_ == nullptr) {
    return;
  }
  auto& renderEngine = visualizer_->renderEngine();
  hub_->detach(renderEngine.primaryLayer()); // a no-op if the hub is already gone
  renderEngine.yieldPrimaryLayer(false);
  // This instance's own projectM stood down while the hub drew its picture, so
  // it missed every cut meanwhile: bring it up to the preset now playing.
  visualizer_->director().requestReissueCurrent();
  hub_.reset();
  hubId_.clear();
  registryEntry_->setSender(false);
}

void MilkDAWpAudioProcessor::setOutputDefaultFullscreen(bool fullscreen) {
  const std::lock_guard lock(layoutMutex_);
  layout_.outputDefaultFullscreen = fullscreen;
}

void MilkDAWpAudioProcessor::setOutputTargetDisplay(const core::WindowBounds& display) {
  {
    const std::lock_guard lock(layoutMutex_);
    if (layout_.outputTargetDisplay == display) {
      return;
    }
    layout_.outputTargetDisplay = display;
  }
  if (outputWindow_ != nullptr) {
    // A native window can't change display while keeping its GL context, so
    // reopen it where the user asked, in the same mode. Its windowed bounds are
    // dropped so it is centred on the new display, not left where it was.
    const bool wasFullscreen = outputWindow_->isFullscreen();
    {
      const std::lock_guard lock(layoutMutex_);
      layout_.outputWindowBounds = {};
    }
    outputWindow_.reset();
    openOutputWindow(wasFullscreen);
  }
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
  // Also what runs when another instance comes or goes: link to (or let go of)
  // the Output target this instance was told to use.
  reconcileLayers();
}

void MilkDAWpAudioProcessor::toggleOutputFullscreen() {
  if (hub_ != nullptr && hub_->isAlive()) {
    hub_->toggleFullscreen();
    return;
  }
  toggleOwnOutputFullscreen();
}

void MilkDAWpAudioProcessor::toggleOwnOutputFullscreen() {
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
  state.instanceId = instanceId();
  state.instanceLabel = instanceLabel();
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
    instanceLabel_ = state.instanceLabel;
  }
  // Take back the identity this instance had when it was saved, so instances
  // that point at it still find it. (If another live instance already has that
  // id, e.g. the state was copied to a duplicated track, a fresh one is used.)
  if (!state.instanceId.empty()) {
    LayerRegistry::get().claimId(registryEntry_, state.instanceId);
  }
  refreshRegistryName();
  triggerAsyncUpdate(); // opens/closes the Output window, and links to the Output target, on the message thread

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
