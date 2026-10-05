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
#include "milkdawp/engine/PresetCompileTimeCache.h"
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
  config.gridAnchored = controls.gridAnchored;
  config.gridOffsetBeats = controls.gridOffsetBeats;
  config.timedDurationSeconds = std::max(controls.timedDurationSeconds, 0.1f);
  config.jitterEnabled = controls.jitterEnabled;
  config.jitterMinSeconds = std::min(controls.jitterMinSeconds, controls.jitterMaxSeconds);
  config.jitterMaxSeconds = std::max(controls.jitterMinSeconds, controls.jitterMaxSeconds);
  config.cutStyle = controls.cutStyle;
  config.blendSeconds = std::max(controls.blendSeconds, 0.1f);
  // Energy Threshold keeps its 0.5-4 range (saved projects, automation) and
  // maps to 5.5-16 dB of bass jump; the default 2 is SectionDetector's 10 dB.
  // Below ~5 dB a steady kick (about 4.4 dB over its own average) would count.
  config.section.jumpDb = 4.0f + 3.0f * std::clamp(controls.energyThreshold, 0.5f, 4.0f);
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

std::string Director::presetPath(std::int32_t index) const {
  const std::lock_guard lock(mutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= presetPaths_.size()) {
    return {};
  }
  return presetPaths_[static_cast<std::size_t>(index)];
}

std::vector<std::string> Director::presetPaths() const {
  const std::lock_guard lock(mutex_);
  return presetPaths_;
}

void Director::blacklistPreset(const std::string& absolutePath, std::string reason) {
  const std::lock_guard lock(mutex_);
  pendingBlacklistOps_.emplace_back(absolutePath, std::move(reason));
}

void Director::unblacklistPreset(const std::string& absolutePath) {
  const std::lock_guard lock(mutex_);
  pendingBlacklistOps_.emplace_back(absolutePath, std::string{});
}

std::vector<std::string> Director::blacklistedPaths() const {
  const std::lock_guard lock(mutex_);
  return blacklistedPaths_;
}

void Director::setPresetMetadata(std::shared_ptr<PresetMetadataStore> store) {
  const std::lock_guard lock(mutex_);
  metadataStore_ = std::move(store);
  ++selectionSerial_;
}

std::shared_ptr<PresetMetadataStore> Director::presetMetadata() const {
  const std::lock_guard lock(mutex_);
  return metadataStore_;
}

void Director::setTagFilter(const std::string& commaSeparatedTags) {
  auto tags = core::PresetMetadata::parseTags(commaSeparatedTags);
  const std::lock_guard lock(mutex_);
  if (tags != tagFilter_) {
    tagFilter_ = std::move(tags);
    ++selectionSerial_;
  }
}

std::string Director::tagFilter() const {
  const std::lock_guard lock(mutex_);
  return core::PresetMetadata::joinTags(tagFilter_);
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
  PresetCompileTimeCache compileTimeCache; // path -> last render-thread load time (5.4)
  std::uint32_t lastMeasuredPresetId = 0;

  std::uint64_t appliedFolderSerial = 0;
  std::int32_t appliedPresetIndex = INT_MIN;
  std::uint32_t seenNext = nextRequests_.load();
  std::uint32_t seenPrevious = previousRequests_.load();
  std::uint32_t seenReissue = reissueRequests_.load();
  bool reissuePending = false; // kept until a handoff slot is free to send it through
  std::uint64_t appliedSelectionSerial = 0;
  std::uint64_t appliedMetadataGeneration = ~std::uint64_t{0};
  std::uint64_t appliedSelectionPlaylist = ~std::uint64_t{0};

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
      render_.errors().add("preset", juce::File(juce::String(entry.absolutePath)).getFileName().toStdString() +
                                         " skipped: " + fetched.reason);
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
  // unreadable ones), at most once around. Sequential order is a
  // user-visible sequence, so it always takes the very next entry -- cost
  // never overrides that guarantee. For the randomized policies -- where any
  // of several candidates is an equally valid pick -- prefer one this
  // session has already measured as cheap, when the scan turns one up (5.4).
  // This applies to every cut style, not just hard cuts: projectM compiles a
  // preset's shaders synchronously inside the load call regardless of
  // smoothTransition, so a soft/timed cut's blend has no rendered frames to
  // blend *during* that call either -- it stalls exactly like a hard cut,
  // the blend only smooths what happens once loading is done.
  auto step = [&](bool forward, core::CutStyle cutStyle, float blendSeconds, std::int64_t dueAtSample) {
    const std::size_t attempts = std::min<std::size_t>(playlist->size(), 16);

    if (playlist->policy() != core::PlaylistPolicy::Sequential) {
      constexpr float kCheapEnoughMs = 20.0f; // only switch to a candidate confirmed at least this cheap
      std::optional<std::size_t> firstValidIndex;
      std::optional<std::size_t> cheapIndex;
      float cheapCost = kCheapEnoughMs;
      for (std::size_t i = 0; i < attempts; ++i) {
        const auto index = forward ? playlist->advanceNext(rng) : playlist->advancePrevious();
        const auto& entry = playlist->at(index);
        if (loader.isBlacklisted(entry.absolutePath) ||
            !PresetLoader::validate(juce::File(juce::String(entry.absolutePath))).ok) {
          continue; // issue() would just skip it too; no point costing it
        }
        if (!firstValidIndex) {
          firstValidIndex = index;
        }
        if (const auto cost = compileTimeCache.estimateMs(entry.absolutePath); cost && *cost <= cheapCost) {
          cheapIndex = index;
          cheapCost = *cost;
        }
      }
      // An unmeasured candidate might be cheap or might not -- only a
      // *confirmed* cheap one is worth preferring over the first valid pick;
      // otherwise fall back to that first pick, same as the plain walk below.
      if (const auto chosen = cheapIndex ? cheapIndex : firstValidIndex; chosen) {
        playlist->setCurrentIndex(*chosen);
        if (issue(*chosen, cutStyle, blendSeconds, dueAtSample)) {
          return;
        }
      }
      // Fall through: nothing valid turned up (cache-cold candidates are
      // still fine -- firstValidIndex covers that), or the chosen one still
      // failed issue() (e.g. a file that vanished after validate()).
    }

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
      std::vector<std::string> paths;
      names.reserve(entries.size());
      paths.reserve(entries.size());
      std::optional<std::size_t> preferredIndex;
      for (std::size_t i = 0; i < entries.size(); ++i) {
        names.push_back(displayName(entries[i]));
        paths.push_back(entries[i].absolutePath);
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
        presetPaths_ = std::move(paths);
        if (entries.empty()) {
          currentPresetPath_.clear();
        }
      }
      playlist = entries.empty() ? nullptr : std::make_unique<core::Playlist>(std::move(entries));
      ++status.playlistGeneration;
      status.playlistSize = playlist ? static_cast<std::uint32_t>(playlist->size()) : 0;
      status.currentIndex = -1;
      status.autoSelectable = status.playlistSize; // until the selection pass below
      status.tagFilterMatchesNothing = false;
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
    bool blacklistChanged = false;
    while (const auto failedId = render_.presetHandoff().popFailure()) {
      if (const auto path = library.pathFor(*failedId)) {
        loader.blacklist(*path, "projectM could not load this preset");
        render_.errors().add("preset", juce::File(juce::String(*path)).getFileName().toStdString() +
                                           " skipped: projectM could not load it");
        blacklistChanged = true;
      }
      ++status.presetsSkipped;
    }

    // Learn how expensive each preset was to load, from the render thread's
    // own measurement, so a future hard cut can prefer a cheap one (5.4).
    if (const auto renderStats = render_.stats();
        renderStats.currentPresetId != 0 && renderStats.currentPresetId != lastMeasuredPresetId) {
      lastMeasuredPresetId = renderStats.currentPresetId;
      if (const auto path = library.pathFor(renderStats.currentPresetId)) {
        compileTimeCache.record(*path, renderStats.lastPresetLoadMs);
      }
    }

    // User blacklist/unblacklist requests from the browser (§4.5).
    std::vector<std::pair<std::string, std::string>> blacklistOps;
    {
      const std::lock_guard lock(mutex_);
      blacklistOps.swap(pendingBlacklistOps_);
    }
    for (auto& [path, reason] : blacklistOps) {
      if (reason.empty()) {
        loader.clearBlacklistEntry(path);
      } else {
        loader.blacklist(path, std::move(reason));
      }
      blacklistChanged = true;
    }
    if (blacklistChanged) {
      const std::lock_guard lock(mutex_);
      blacklistedPaths_ = loader.blacklistedPaths();
    }

    // Ratings, "never auto-select" and the tag filter (5.2), applied again
    // whenever any of them or the playlist changes.
    if (playlist) {
      std::shared_ptr<PresetMetadataStore> store;
      std::vector<std::string> filter;
      std::uint64_t serial = 0;
      {
        const std::lock_guard lock(mutex_);
        store = metadataStore_;
        filter = tagFilter_;
        serial = selectionSerial_;
      }
      const auto metadataGeneration = store ? store->generation() : 0;
      if (serial != appliedSelectionSerial || metadataGeneration != appliedMetadataGeneration ||
          status.playlistGeneration != appliedSelectionPlaylist) {
        appliedSelectionSerial = serial;
        appliedMetadataGeneration = metadataGeneration;
        appliedSelectionPlaylist = status.playlistGeneration;
        const auto metadata = store ? store->snapshot() : nullptr;
        std::uint32_t selectable = 0;
        for (std::size_t i = 0; i < playlist->size(); ++i) {
          const auto info = metadata ? metadata->get(playlist->at(i).absolutePath) : core::PresetInfo{};
          const bool autoSelect = !info.neverAutoSelect && core::PresetMetadata::matchesFilter(info, filter);
          playlist->setSelectionInfo(i, core::PresetMetadata::weightFor(info), autoSelect);
          selectable += autoSelect ? 1 : 0;
        }
        status.tagFilterMatchesNothing = selectable == 0 && !filter.empty();
        status.autoSelectable = selectable == 0 ? static_cast<std::uint32_t>(playlist->size()) : selectable;
      }
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
    if (const auto reissue = reissueRequests_.load(); reissue != seenReissue) {
      seenReissue = reissue;
      reissuePending = true;
    }
    if (reissuePending) {
      // The current preset again, as a hard cut now, without moving the playlist.
      const auto current = status.currentIndex;
      if (!playlist || current < 0 || static_cast<std::size_t>(current) >= playlist->size()) {
        reissuePending = false; // nothing is playing: there is nothing to repeat
      } else if (issue(static_cast<std::size_t>(current), core::CutStyle::Hard, 0.0f,
                       static_cast<std::int64_t>(ring_.samplePosition()))) {
        reissuePending = false;
      } // else: no free handoff slot this instant; try again next loop
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
      const bool hostDrives =
          controls.useHostTempo && followHostTransport_ && transport.isPlaying && transport.bpm > 0.0;
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

      const auto scheduled = pipeline->scheduler.tick(hopEnd, playing, discontinuity, beat, frame.bassEnergy,
                                                      bassOnset.has_value(), 0, 0);
      if (scheduled && playlist && !controls.locked) {
        step(true, scheduled->request.cutStyle, scheduled->request.blendSeconds, scheduled->request.dueAtSample);
      }

      status.bpm = beat.bpm;
      status.beatConfidence = beat.confidence;
      status.beatIndex = beat.beatIndex;
      status.barIndex = beat.barIndex;
      status.beatSource = hostDrives ? BeatSource::Host
                                     : (detected.confidence > 0.0f ? BeatSource::Detected : BeatSource::None);
      status.transportPlaying = playing;
      const auto& section = pipeline->scheduler.lastSection();
      status.bassLevelDb = section.levelDb;
      status.bassReferenceDb = section.referenceDb;
      status.inBreakdown = section.breakdown;
      status.dropsDetected += section.drop ? 1 : 0;
    }

    status_.publish(status);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

} // namespace milkdawp::engine
