// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/core/Messages.h"
#include "milkdawp/engine/PresetHandoff.h"
#include "milkdawp/engine/TransitionExecutor.h"

namespace milkdawp::engine {

/// How a layer's picture is mixed onto the layers beneath it.
enum class LayerBlend : std::uint8_t {
  Normal,   // the layer covers what is below in proportion to its opacity
  Add,      // light added on top
  Screen,   // brightens without clipping as hard as Add
  Multiply, // darkens: the layer tints what is below
  LumaKey,  // the layer's dark areas become transparent, so what is below shows through them
};
inline constexpr int kLayerBlendCount = 5;

/// Everything one visual layer shares between its director side and the render
/// thread (Layers, layers_like_shrek.md §3): the audio the layer reacts to, the
/// queue of timed transition requests, and the slots the director hands preset
/// text through. One per projectM instance the `RenderEngine` renders.
///
/// A solo `Visualizer` has exactly one, owned by its `RenderEngine`. Under
/// Layers, each sending instance keeps its own channel (its own ring and its
/// own `Director`) while the hub's render thread drains it, which is what lets
/// per-track analysis, playlists and transition modes keep working unchanged.
///
/// Threading is the same as it was when these lived in `RenderEngine`:
///   - director thread (single producer): `pushTransition()`, `presetHandoff()`;
///   - render thread: `executor().onTick()`, the handoff's consumer side, and
///     reading `audio()` through its own cursor.
class LayerChannel {
public:
  LayerChannel(const core::AudioRing& audio, double sampleRate) : audio_(audio), executor_(sampleRate) {}

  LayerChannel(const LayerChannel&) = delete;
  LayerChannel& operator=(const LayerChannel&) = delete;

  /// The ring this layer's visual reacts to. Must outlive the channel.
  [[nodiscard]] const core::AudioRing& audio() const noexcept { return audio_; }

  /// Director thread. Never blocks; false if the queue is momentarily full.
  bool pushTransition(const core::TransitionRequestMessage& request) noexcept {
    return executor_.pushRequest(request);
  }
  [[nodiscard]] PresetHandoff& presetHandoff() noexcept { return presetHandoff_; }

  /// Render thread only.
  [[nodiscard]] TransitionExecutor& executor() noexcept { return executor_; }

  // ---- how this layer is composited (any thread; read by the render thread) ----
  // Only used when the engine renders more than one layer; a lone layer is
  // always drawn as is.
  void setOpacity(float opacity) noexcept { opacity_.store(opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity)); }
  [[nodiscard]] float opacity() const noexcept { return opacity_.load(); }
  void setBlend(LayerBlend blend) noexcept { blend_.store(blend); }
  [[nodiscard]] LayerBlend blend() const noexcept { return blend_.load(); }
  /// A hidden layer is neither fed audio nor drawn (it costs no GPU time),
  /// though its transitions keep being serviced.
  void setVisible(bool visible) noexcept { visible_.store(visible); }
  [[nodiscard]] bool visible() const noexcept { return visible_.load(); }
  /// Lower draws first (bottom). Equal values keep the order layers were added.
  void setOrder(int order) noexcept { order_.store(order); }
  [[nodiscard]] int order() const noexcept { return order_.load(); }

  // ---- measured by the render thread, readable from anywhere ----
  /// GPU time the last frame spent drawing this layer, in milliseconds; 0 until
  /// it has been drawn, and while the layer is hidden. Measured only while the
  /// engine renders several layers (a lone layer is timed by `RenderStats`).
  [[nodiscard]] float gpuMs() const noexcept { return gpuMs_.load(); }
  [[nodiscard]] std::uint32_t presetsLoaded() const noexcept { return presetsLoaded_.load(); }
  [[nodiscard]] float lastPresetLoadMs() const noexcept { return lastPresetLoadMs_.load(); }
  /// Render thread only.
  void reportGpuMs(float milliseconds) noexcept { gpuMs_.store(milliseconds); }
  void reportPresetLoaded(float loadMilliseconds) noexcept {
    lastPresetLoadMs_.store(loadMilliseconds);
    presetsLoaded_.fetch_add(1);
  }

private:
  const core::AudioRing& audio_;
  TransitionExecutor executor_;
  PresetHandoff presetHandoff_;
  std::atomic<float> gpuMs_{0.0f};
  std::atomic<float> lastPresetLoadMs_{0.0f};
  std::atomic<std::uint32_t> presetsLoaded_{0};
  std::atomic<float> opacity_{1.0f};
  std::atomic<LayerBlend> blend_{LayerBlend::Normal};
  std::atomic<bool> visible_{true};
  std::atomic<int> order_{0};
};

} // namespace milkdawp::engine
