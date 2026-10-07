// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/core/HostTransport.h"
#include "milkdawp/core/LayerGate.h"
#include "milkdawp/core/MacroLock.h"
#include "milkdawp/core/Messages.h"
#include "milkdawp/core/Playlist.h"
#include "milkdawp/core/SeqlockSnapshot.h"
#include "milkdawp/core/TransitionScheduler.h"
#include "milkdawp/core/VisualControls.h"
#include "milkdawp/engine/PresetMetadataStore.h"
#include "milkdawp/engine/RenderEngine.h"

namespace milkdawp::engine {

/// What the shell wants the engine to do, published as one plain snapshot
/// (the plugin fills it from its parameters every audio block; the app and
/// mdw-view from their own controls). Trivially copyable, so it crosses
/// threads through a SeqlockSnapshot.
struct EngineControls {
  core::TransitionMode transitionMode = core::TransitionMode::BeatQuantized;
  std::uint32_t transitionBars = 4;
  /// Layers: cut on the beats where beat % (bars x 4) == `gridOffsetBeats`
  /// instead of every N bars from this instance's start (core::TransitionSchedulerConfig).
  bool gridAnchored = false;
  std::uint32_t gridOffsetBeats = 0;
  float timedDurationSeconds = 5.0f;
  bool jitterEnabled = false;
  float jitterMinSeconds = 3.0f;
  float jitterMaxSeconds = 15.0f;
  core::CutStyle cutStyle = core::CutStyle::Soft;
  float blendSeconds = 3.0f;
  /// Energy mode: standard deviations above the rolling mean that count as a drop.
  float energyThreshold = 2.0f;
  /// Plugin only: let the host's tempo/beat grid drive the beat modes while
  /// it plays. Off: the audio-detected tempo always does.
  bool useHostTempo = false;
  bool locked = false;
  core::PlaylistPolicy policy = core::PlaylistPolicy::Sequential;
  /// Jump to this playlist index whenever the value changes (host
  /// automation, the UI, or state restore).
  std::int32_t presetIndex = 0;
  float beatSensitivity = 1.0f;
  float qualityScale = 0.0f; // <= 0: adaptive (Auto, 5.3); else a fixed FBO scale
  /// Phase 8.1/8.2b: how this instance's layer looks, and when it hides. Not
  /// the director's: `Visualizer::setControls` hands them to the primary
  /// layer's channel, which the render thread (this engine's, or a hub's)
  /// reads every frame.
  core::VisualControls visual;
  core::LayerGateSettings gate;
  /// Phase 8.10: Macro 1-8 (0..1), which `.milkdawp` presets read as
  /// `mdw_m1`..`mdw_m8`. Like `visual`, handed to the primary layer's channel.
  std::array<float, core::kMacroCount> macros{};
};

enum class BeatSource : std::uint8_t { None, Detected, Host };

/// The director's view for the UI, published once per loop.
struct DirectorStatus {
  float bpm = 0.0f;
  float beatConfidence = 0.0f;
  BeatSource beatSource = BeatSource::None;
  bool transportPlaying = false;
  /// The beat clock's count (host or detected): changes once per beat while
  /// there is a beat (8.4's outbound OSC beat and bar signals).
  std::uint64_t beatIndex = 0;
  std::uint32_t barIndex = 0;
  std::int32_t currentIndex = -1;  // -1: no playlist loaded
  std::uint32_t playlistSize = 0;
  std::uint32_t transitionsIssued = 0;
  std::uint32_t presetsSkipped = 0; // failed pre-validation, read, or projectM load
  std::uint64_t playlistGeneration = 0; // bumps on every (re)scan
  // Energy mode's view (5.1, core::SectionDetector), for the diagnostics overlay.
  float bassLevelDb = -120.0f;     // the bass, averaged over half a second
  float bassReferenceDb = -120.0f; // the track's loud bass level
  bool inBreakdown = false;        // a drop can happen now
  std::uint32_t dropsDetected = 0;
  // 5.2: presets automatic picks can choose (not "never auto-select", in the
  // tag filter). Equal to playlistSize when nothing is filtered, and when
  // the filter matches nothing (it is then ignored).
  std::uint32_t autoSelectable = 0;
  bool tagFilterMatchesNothing = false;
};

/// The preset the director last handed over, and what it says about the
/// Macros (Phase 8.10). A plain `.milk` uses no Macros.
struct CurrentPreset {
  std::string path;
  bool milkdawp = false;
  core::MacroDefaults macroDefaults{}; // for Lock Macros off; nullopt where unused
  std::array<std::string, core::kMacroCount> macroNames{}; // empty where unused
};

/// The engine's analysis thread (§4.2, Phase 2.6). Consumes the audio ring in
/// 512-frame hops and runs the core pipeline (Analyzer, OnsetDetector,
/// TempoTracker, BeatClock, or HostTransport when the host is playing, then
/// TransitionScheduler); owns the Playlist and PresetLibrary; reads the
/// chosen preset through PresetLoader (pre-validation, blacklist) and hands
/// its text plus a TransitionRequestMessage to the RenderEngine, which loads
/// it on the render thread when due.
///
/// Preset files are read on this thread, when a transition fires. They are
/// small, and the scheduler decides on the hop a beat is crossed, so there is
/// no earlier moment to prefetch into; the render thread never touches the
/// filesystem either way (§4.2).
///
/// Threading: setControls() from one producer thread; publishHostTransport()
/// from the audio thread only (never blocks); requestNext()/
/// requestPrevious()/requestPreset() from any thread; the folder and name accessors from the
/// message thread (they take a mutex, never held by the audio thread).
class Director {
public:
  /// `ring` and `render` must outlive the director. `followHostTransport`:
  /// the plugin's host transport drives the beat clock while playing (§4.3).
  Director(core::AudioRing& ring, RenderEngine& render, bool followHostTransport);
  ~Director();
  Director(const Director&) = delete;
  Director& operator=(const Director&) = delete;

  void setSampleRate(double sampleRate) noexcept;
  void setControls(const EngineControls& controls) noexcept { controls_.publish(controls); }
  /// Audio thread. `info.samplePos` must be in AudioRing frame numbers.
  void publishHostTransport(const core::TransportInfo& info) noexcept { transport_.publish(info); }

  void requestNext() noexcept { nextRequests_.fetch_add(1); }
  void requestPrevious() noexcept { previousRequests_.fetch_add(1); }
  /// Jumps to playlist entry `index` (the UI's preset picker). Unlike the
  /// `presetIndex` control it fires every time, even for the index already
  /// asked for; out-of-range indices are ignored. Works while locked.
  void requestPreset(std::int32_t index) noexcept { presetRequest_.store(index); }
  /// Sends the preset that is currently playing to the render side again, as a
  /// hard cut, without moving the playlist. For when the thing drawing this
  /// director's picture has changed under it: a new projectM instance (Layers
  /// attaching this instance to another's canvas creates one, which would
  /// otherwise sit on projectM's idle preset until the next transition) or an
  /// old one that missed cuts while it stood down. No-op without a current preset.
  void requestReissueCurrent() noexcept { reissueRequests_.fetch_add(1); }

  /// Scans `folder` recursively for .milk and .milkdawp presets (on the
  /// director thread, `core::Playlist::scanFolder`) and starts playing:
  /// `preferredPresetPath` if it is in the folder, otherwise the
  /// `presetIndex` control.
  void setPresetFolder(const std::string& folder, const std::string& preferredPresetPath = {});
  void rescan();
  [[nodiscard]] std::string presetFolder() const;
  [[nodiscard]] std::string currentPresetPath() const;
  /// The path and its Macro defaults and names together (one lock), so a
  /// shell reacting to a preset change never pairs one preset's path with
  /// another's defaults.
  [[nodiscard]] CurrentPreset currentPreset() const;
  /// Display name (path relative to the folder, without ".milk" or
  /// ".milkdawp"), or empty.
  [[nodiscard]] std::string presetName(std::int32_t index) const;
  /// Every display name, in playlist order (one lock, for the preset picker).
  [[nodiscard]] std::vector<std::string> presetNames() const;
  /// Absolute path for playlist entry `index`, or empty. Parallel to
  /// `presetName`/`presetNames` (§4.5's browser keys favourites/recency by
  /// this, since a playlist index shifts on rescan but a path doesn't).
  [[nodiscard]] std::string presetPath(std::int32_t index) const;
  [[nodiscard]] std::vector<std::string> presetPaths() const;

  /// Marks `absolutePath` so playback skips it, the same way a preset
  /// projectM itself rejects is skipped (§2.5's PresetLoader) -- the
  /// browser's right-click "blacklist" (§4.5) uses this. Applied on the
  /// director thread; takes effect within one loop tick.
  void blacklistPreset(const std::string& absolutePath, std::string reason = "blacklisted by user");
  void unblacklistPreset(const std::string& absolutePath);
  /// Every currently blacklisted absolute path, user- and failure-added
  /// alike (one lock, for the browser).
  [[nodiscard]] std::vector<std::string> blacklistedPaths() const;

  /// 5.2: ratings (Weighted shuffle's weights) and "never auto-select" come
  /// from `store`; null (the default, and tests) means none. Any thread.
  void setPresetMetadata(std::shared_ptr<PresetMetadataStore> store);
  [[nodiscard]] std::shared_ptr<PresetMetadataStore> presetMetadata() const;
  /// 5.2: automatic picks (transitions, next/previous) only choose presets
  /// with one of these tags ("calm, dark"); empty: any. If no preset matches,
  /// the filter is ignored. Picking a preset by hand always works. Any thread.
  void setTagFilter(const std::string& commaSeparatedTags);
  [[nodiscard]] std::string tagFilter() const;

  [[nodiscard]] DirectorStatus status() const noexcept { return status_.read(); }

private:
  struct Pipeline;
  struct FolderRequest {
    std::string folder;
    std::string preferredPresetPath;
    std::uint64_t serial = 0;
  };

  void run();

  core::AudioRing& ring_;
  RenderEngine& render_;
  const bool followHostTransport_;

  std::atomic<double> sampleRate_{48000.0};
  std::atomic<bool> stopRequested_{false};
  std::atomic<std::uint32_t> nextRequests_{0};
  std::atomic<std::uint32_t> previousRequests_{0};
  std::atomic<std::int32_t> presetRequest_{-1}; // -1: none pending
  std::atomic<std::uint32_t> reissueRequests_{0};
  core::SeqlockSnapshot<EngineControls> controls_;
  core::SeqlockSnapshot<core::TransportInfo> transport_;
  core::SeqlockSnapshot<DirectorStatus> status_;

  mutable std::mutex mutex_; // guards everything below
  FolderRequest folderRequest_;
  std::string currentFolder_;
  CurrentPreset currentPreset_;
  std::vector<std::string> presetNames_;
  std::vector<std::string> presetPaths_; // parallel to presetNames_
  std::vector<std::pair<std::string, std::string>> pendingBlacklistOps_; // path, reason ("": unblacklist
  std::vector<std::string> blacklistedPaths_; // published snapshot of the loader's blacklist
  std::shared_ptr<PresetMetadataStore> metadataStore_;
  std::vector<std::string> tagFilter_; // normalised (PresetMetadata::parseTags)
  std::uint64_t selectionSerial_ = 1;   // bumps when either changes

  std::thread thread_;
};

} // namespace milkdawp::engine
