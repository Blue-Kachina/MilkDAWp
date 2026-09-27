// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/Director.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <optional>
#include <random>

#include <juce_core/juce_core.h>

#include "milkdawp/core/Analyzer.h"
#include "milkdawp/core/BeatClock.h"
#include "milkdawp/core/OnsetDetector.h"
#include "milkdawp/core/PresetLibrary.h"
#include "milkdawp/core/TempoTracker.h"
#include "milkdawp/engine/PresetLoader.h"

namespace milkdawp::engine {

struct Director::Pipeline {
  explicit Pipeline(double rate)
      : sampleRate(rate), analyzer(rate), bassOnsets(rate), tempo(rate), beatClock(rate), hostTransport(rate),
        scheduler(rate, core::Analyzer::hopSize) {}

  double sampleRate;
  core::Analyzer analyzer;
  core::OnsetDetector bassOnsets;
  core::TempoTracker tempo;
  core::BeatClock beatClock;
  core::HostTransport hostTransport;
  core::TransitionScheduler scheduler;
};

namespace {

std::string displayName(const core::PlaylistEntry& entry) {
  auto name = entry.relativePath;
  if (name.size() > 5 && name.compare(name.size() - 5, 5, ".milk") == 0) {
    name.resize(name.size() - 5);
  }
  return name;
}

core::TransitionSchedulerConfig schedulerConfigFor(const EngineControls& controls) {
  core::TransitionSchedulerConfig config;
  config.mode = controls.transitionMode;
  config.bars = std::max<std::uint32_t>(controls.transitionBars, 1);
  config.timedDurationSeconds = std::max(controls.timedDurationSeconds, 0.1f);
  config.jitterEnabled = controls.jitterEnabled;
  config.jitterMinSeconds = std::min(controls.jitterMinSeconds, controls.jitterMaxSeconds);
  config.jitterMaxSeconds = std::max(controls.jitterMinSeconds, controls.jitterMaxSeconds);
  config.cutStyle = controls.cutStyle;
  config.blendSeconds = std::max(controls.blendSeconds, 0.1f);
  config.energyThresholdMultiplier = std::max(controls.energyThreshold, 0.0f);
  return config;
}

} // namespace

Director::Director(core::AudioRing& ring, RenderEngine& render, bool followHostTransport)
    : ring_(ring), render_(render), followHostTransport_(followHostTransport) {
  thread_ = std::thread([this] { run(); });
}

Director::~Director() {
  stopRequested_.store(true);
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Director::setSampleRate(double sampleRate) noexcept {
  if (sampleRate > 0.0) {
    sampleRate_.store(sampleRate);
  }
}

void Director::setPresetFolder(const std::string& folder, const std::string& preferredPresetPath) {
  const std::lock_guard lock(mutex_);
  folderRequest_.folder = folder;
  folderRequest_.preferredPresetPath = preferredPresetPath;
  ++folderRequest_.serial;
}

void Director::rescan() {
  const std::lock_guard lock(mutex_);
  if (!folderRequest_.folder.empty()) {
    folderRequest_.preferredPresetPath = currentPresetPath_;
    ++folderRequest_.serial;
  }
}

std::string Director::presetFolder() const {
  const std::lock_guard lock(mutex_);
  return folderRequest_.folder;
}

std::string Director::currentPresetPath() const {
  const std::lock_guard lock(mutex_);
  return currentPresetPath_;
}

std::string Director::presetName(std::int32_t index) const {
  const std::lock_guard lock(mutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= presetNames_.size()) {
    return {};
  }
  return presetNames_[static_cast<std::size_t>(index)];
}

std::vector<std::string> Director::presetNames() const {
  const std::lock_guard lock(mutex_);
  return presetNames_;
}

void Director::run() {
  juce::Thread::setCurrentThreadName("MilkDAWp director");

  constexpr std::size_t hop = core::Analyzer::hopSize;
  const auto channels = static_cast<std::size_t>(ring_.numChannels());
  std::vector<float> interleavedHop(hop * channels);
  std::vector<float> monoHop(hop);

  std::unique_ptr<Pipeline> pipeline;
  std::unique_ptr<core::Playlist> playlist;
  core::PresetLibrary library;
  PresetLoader loader;
  std::mt19937 rng{std::random_device{}()};

  std::uint64_t appliedFolderSerial = 0;
  std::int32_t appliedPresetIndex = INT_MIN;
  std::uint32_t seenNext = nextRequests_.load();
  std::uint32_t seenPrevious = previousRequests_.load();

  bool haveLastTransport = false;
  core::TransportInfo lastTransport;

  DirectorStatus status;

  // Reads and hands over one playlist entry. False if it can't be used.
  auto issue = [&](std::size_t index, core::CutStyle cutStyle, float blendSeconds, std::int64_t dueAtSample) {
    const auto& entry = playlist->at(index);
    if (loader.isBlacklisted(entry.absolutePath)) {
      return false;
    }
    const auto fetched = loader.prefetch(juce::File(juce::String(entry.absolutePath)));
    if (!fetched.success) {
      ++status.presetsSkipped;
      return false;
    }
    const auto presetId = library.idFor(entry.absolutePath);
    if (!render_.presetHandoff().offer(presetId, fetched.contents)) {
      return false; // no free slot this instant; the next trigger tries again
    }
    render_.pushTransition({presetId, cutStyle, blendSeconds, dueAtSample});
    ++status.transitionsIssued;
    status.currentIndex = static_cast<std::int32_t>(index);
    const std::lock_guard lock(mutex_);
    currentPresetPath_ = entry.absolutePath;
    return true;
  };

  // Steps the playlist until an entry loads (skipping blacklisted or
  // unreadable ones), at most once around.
  auto step = [&](bool forward, core::CutStyle cutStyle, float blendSeconds, std::int64_t dueAtSample) {
    const std::size_t attempts = std::min<std::size_t>(playlist->size(), 16);
    for (std::size_t i = 0; i < attempts; ++i) {
      const auto index = forward ? playlist->advanceNext(rng) : playlist->advancePrevious();
      if (issue(index, cutStyle, blendSeconds, dueAtSample)) {
        return;
      }
    }
  };

  // Manual steps work while locked: lock only stops automatic transitions.
  auto manualStep = [&](bool forward, const EngineControls& controls) {
    if (!playlist || playlist->empty()) {
      return;
    }
    const bool wasLocked = playlist->isLocked();
    playlist->setLocked(false);
    step(forward, controls.cutStyle, controls.blendSeconds, static_cast<std::int64_t>(ring_.samplePosition()));
    playlist->setLocked(wasLocked);
  };

  while (!stopRequested_.load()) {
    const auto controls = controls_.read();

    // Folder (re)scan requested from the message thread.
    FolderRequest request;
    {
      const std::lock_guard lock(mutex_);
      request = folderRequest_;
    }
    if (request.serial != appliedFolderSerial) {
      appliedFolderSerial = request.serial;
      auto entries = request.folder.empty() ? std::vector<core::PlaylistEntry>{}
                                            : core::Playlist::scanFolder(request.folder);
      std::vector<std::string> names;
      names.reserve(entries.size());
      std::optional<std::size_t> preferredIndex;
      for (std::size_t i = 0; i < entries.size(); ++i) {
        names.push_back(displayName(entries[i]));
        if (!request.preferredPresetPath.empty() &&
            juce::File(juce::String(entries[i].absolutePath)) ==
                juce::File(juce::String(request.preferredPresetPath))) {
          preferredIndex = i;
        }
      }
      {
        const std::lock_guard lock(mutex_);
        currentFolder_ = request.folder;
        presetNames_ = std::move(names);
        if (entries.empty()) {
          currentPresetPath_.clear();
        }
      }
      playlist = entries.empty() ? nullptr : std::make_unique<core::Playlist>(std::move(entries));
      ++status.playlistGeneration;
      status.playlistSize = playlist ? static_cast<std::uint32_t>(playlist->size()) : 0;
      status.currentIndex = -1;
      appliedPresetIndex = controls.presetIndex;
      if (playlist) {
        playlist->setPolicy(controls.policy);
        playlist->setLocked(controls.locked);
        const auto start = preferredIndex.value_or(static_cast<std::size_t>(
            std::clamp<std::int32_t>(controls.presetIndex, 0, static_cast<std::int32_t>(playlist->size()) - 1)));
        playlist->setCurrentIndex(start);
        const auto now = static_cast<std::int64_t>(ring_.samplePosition());
        if (!issue(start, core::CutStyle::Hard, 0.0f, now)) {
          step(true, core::CutStyle::Hard, 0.0f, now);
        }
      }
    }

    // Presets projectM rejected on the render thread: never pick them again.
    while (const auto failedId = render_.presetHandoff().popFailure()) {
      if (const auto path = library.pathFor(*failedId)) {
        loader.blacklist(*path, "projectM could not load this preset");
      }
      ++status.presetsSkipped;
    }

    if (playlist) {
      if (playlist->policy() != controls.policy) {
        playlist->setPolicy(controls.policy);
      }
      playlist->setLocked(controls.locked);

      if (controls.presetIndex != appliedPresetIndex) {
        appliedPresetIndex = controls.presetIndex;
        const auto index = static_cast<std::size_t>(
            std::clamp<std::int32_t>(controls.presetIndex, 0, static_cast<std::int32_t>(playlist->size()) - 1));
        playlist->setCurrentIndex(index);
        issue(index, controls.cutStyle, controls.blendSeconds, static_cast<std::int64_t>(ring_.samplePosition()));
      }
    }

    if (const auto next = nextRequests_.load(); next != seenNext) {
      seenNext = next;
      manualStep(true, controls);
    }
    if (const auto previous = previousRequests_.load(); previous != seenPrevious) {
      seenPrevious = previous;
      manualStep(false, controls);
    }
    if (const auto picked = presetRequest_.exchange(-1);
        picked >= 0 && playlist && static_cast<std::size_t>(picked) < playlist->size()) {
      const auto index = static_cast<std::size_t>(picked);
      playlist->setCurrentIndex(index);
      if (!issue(index, controls.cutStyle, controls.blendSeconds, static_cast<std::int64_t>(ring_.samplePosition()))) {
        manualStep(true, controls); // unreadable or blacklisted: the next one that loads
      }
    }

    const double rate = sampleRate_.load();
    if (!pipeline || pipeline->sampleRate != rate) {
      pipeline = std::make_unique<Pipeline>(rate);
    }
    pipeline->scheduler.setConfig(schedulerConfigFor(controls));

    while (ring_.consumeHop(interleavedHop.data(), hop)) {
      const auto hopEnd = ring_.readPosition();
      const auto hopStart = hopEnd - hop;
      for (std::size_t i = 0; i < hop; ++i) {
        float sum = 0.0f;
        for (std::size_t c = 0; c < channels; ++c) {
          sum += interleavedHop[i * channels + c];
        }
        monoHop[i] = sum / static_cast<float>(channels);
      }

      const auto frame = pipeline->analyzer.processHop(monoHop.data());
      const auto bassOnset = pipeline->bassOnsets.processHop(frame.bassOnsetStrength);
      const auto tempo = pipeline->tempo.processHop(frame.onsetStrength);
      const auto detected = pipeline->beatClock.processHop(hopStart, tempo, bassOnset);

      // Host transport (plugin): the host's beat grid wins while it plays
      // (§4.3); the detector keeps running for energy and for when it stops.
      const auto transport = transport_.read();
      const bool hostDrives = followHostTransport_ && transport.isPlaying && transport.bpm > 0.0;
      const auto beat = hostDrives ? pipeline->hostTransport.processTransport(transport) : detected;
      const bool playing = followHostTransport_ ? transport.isPlaying : true;

      // Stop/start, loop and relocate: the Timed clock pauses or resets
      // (§4.4). A relocate shows as ppq not advancing the way the elapsed
      // samples say it should.
      bool discontinuity = false;
      if (followHostTransport_ && haveLastTransport && transport.samplePos != lastTransport.samplePos) {
        if (transport.isPlaying != lastTransport.isPlaying) {
          discontinuity = true;
        } else if (transport.isPlaying && transport.bpm > 0.0) {
          const double elapsedBeats = static_cast<double>(transport.samplePos - lastTransport.samplePos) / rate *
                                      transport.bpm / 60.0;
          discontinuity = std::abs(transport.ppqPosition - (lastTransport.ppqPosition + elapsedBeats)) > 0.25;
        }
      }
      if (!haveLastTransport || transport.samplePos != lastTransport.samplePos) {
        lastTransport = transport;
        haveLastTransport = true;
      }

      const auto scheduled = pipeline->scheduler.tick(hopEnd, playing, discontinuity, beat, frame.broadbandRms,
                                                      bassOnset.has_value(), 0, 0);
      if (scheduled && playlist && !controls.locked) {
        step(true, scheduled->request.cutStyle, scheduled->request.blendSeconds, scheduled->request.dueAtSample);
      }

      status.bpm = beat.bpm;
      status.beatConfidence = beat.confidence;
      status.beatSource = hostDrives ? BeatSource::Host
                                     : (detected.confidence > 0.0f ? BeatSource::Detected : BeatSource::None);
      status.transportPlaying = playing;
    }

    status_.publish(status);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

} // namespace milkdawp::engine
