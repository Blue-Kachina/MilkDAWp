// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/core/Diagnostics.h"
#include "milkdawp/core/HostTransport.h"
#include "milkdawp/engine/Director.h"
#include "milkdawp/engine/RenderEngine.h"

namespace milkdawp::engine {

/// The whole engine as one object a shell owns (§4.1 "one engine, two
/// shells"): the audio ring, the RenderEngine (render thread) and the
/// Director (analysis thread), created and destroyed in the right order.
/// The plugin processor, the app and mdw-view each hold one.
class Visualizer {
public:
  struct Config {
    RenderEngine::Config render;
    /// The plugin's host transport drives the beat clock while playing.
    bool followHostTransport = false;
    juce::File bundleDirectoryHint;
  };

  explicit Visualizer(const Config& config);
  ~Visualizer();
  Visualizer(const Visualizer&) = delete;
  Visualizer& operator=(const Visualizer&) = delete;

  /// Message thread, before audio runs (prepareToPlay). Sizes the audio
  /// thread's scratch buffer; the ring itself never changes.
  void prepare(double sampleRate, int maxBlockSize);

  /// Audio thread. Copies one block (any channel count: mono is
  /// duplicated, more than two uses the first two) into the ring, and, when
  /// given, publishes the host transport with its sample position rewritten
  /// into ring frame numbers so every beat time is on one clock. Never
  /// allocates, locks or blocks.
  void processAudio(const float* const* channels, int numChannels, int numSamples,
                    const core::TransportInfo* hostTransport) noexcept;

  /// One producer thread (the plugin's audio thread, or an app's message
  /// thread). Forwards render settings and publishes the rest to the director.
  void setControls(const EngineControls& controls) noexcept;

  [[nodiscard]] RenderEngine& renderEngine() noexcept { return *render_; }
  [[nodiscard]] Director& director() noexcept { return *director_; }
  [[nodiscard]] const core::AudioRing& audioRing() const noexcept { return ring_; }

  /// The engine's and director's part of the diagnostics panel (5.9); the shell
  /// adds `shell`, `surface` and `input`. Message thread (takes the director's
  /// and the error log's locks).
  [[nodiscard]] core::DiagnosticsInfo diagnostics();

  // ~1.4 s at 48 kHz: far more than any gap between audio callbacks and the
  // analysis or render threads reading it.
  static constexpr std::size_t kRingCapacityFrames = std::size_t{1} << 16;
  static constexpr int kRingChannels = 2;

private:
  core::AudioRing ring_{kRingCapacityFrames, kRingChannels};
  std::unique_ptr<RenderEngine> render_;
  std::unique_ptr<Director> director_;
  std::vector<float> interleaveScratch_;
};

} // namespace milkdawp::engine
