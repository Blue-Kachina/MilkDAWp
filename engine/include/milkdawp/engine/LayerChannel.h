// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/core/LayerGate.h"
#include "milkdawp/core/Messages.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/SeqlockSnapshot.h"
#include "milkdawp/core/VisualControls.h"
#include "milkdawp/engine/MediaSource.h"
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
  /// 8.6e, media only: the layer's red and green push what is below sideways
  /// and up/down (mid-grey moves nothing), by up to a tenth of the frame at
  /// full opacity. Not a `layerBlend` choice: that parameter's list is fixed.
  Displace,
  /// 8.6f, media only: drawn into the preset's own picture before it renders
  /// (`projectm_opengl_burn_texture`), so the preset's warp and decay carry it
  /// on like anything it drew itself. Burned every frame, cropped to fit, with
  /// Media Mix as its transparency. Never drawn over the picture.
  BurnIn,
};
/// The `layerBlend` parameter's choices: Normal .. LumaKey.
inline constexpr int kLayerBlendCount = 5;
/// The media blend setting's choices (8.6e/f): every LayerBlend.
inline constexpr int kMediaBlendCount = 7;
/// The media blend setting's names, in LayerBlend order.
inline constexpr const char* kMediaBlendNames[kMediaBlendCount] = {"Normal",   "Add",      "Screen",  "Multiply",
                                                                   "Luma key", "Displace", "Burn in"};

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

  // ---- how this layer looks (Phase 8.1/8.2/8.2b) ----
  /// The instance's Visual globals: Speed drives this layer's preset clock, the
  /// rest its post effects (`EffectsChain`). For the engine's own primary layer
  /// they apply to the whole canvas once the engine composites several layers,
  /// since the instance that owns the window is the one mixing the picture.
  /// One writer thread (whoever calls `Visualizer::setControls`); the render
  /// thread reads.
  void setVisual(const core::VisualControls& visual) noexcept { visual_.publish(visual); }
  [[nodiscard]] core::VisualControls visual() const noexcept { return visual_.read(); }
  /// Phase 8.10: Macro 1-8 (0..1). Every frame the render thread sets them in
  /// this layer's preset as `mdw_m1`..`mdw_m8`, which `.milkdawp` code reads.
  /// Same writer as `setVisual`.
  void setMacros(const std::array<float, core::kMacroCount>& macros) noexcept { macros_.publish(macros); }
  [[nodiscard]] std::array<float, core::kMacroCount> macros() const noexcept { return macros_.read(); }
  /// The gate: while on, the layer fades out whenever its own input is below
  /// the threshold, and is back on the next loud frame. Unlike Mute or an
  /// Opacity of 0, a gated layer keeps being fed and rendered, so on reopening
  /// it is where it would have been. Any thread.
  void setGate(const core::LayerGateSettings& gate) noexcept {
    gateThresholdDb_.store(gate.thresholdDb);
    gateReleaseMs_.store(gate.releaseMs);
    gateEnabled_.store(gate.enabled);
  }
  [[nodiscard]] core::LayerGateSettings gate() const noexcept {
    return {gateEnabled_.load(), gateThresholdDb_.load(), gateReleaseMs_.load()};
  }
  /// The gate's view, for the drawer's meter: the layer's input level (dBFS,
  /// the loudest sample of the latest frame), whether the gate is open, and
  /// how visible the layer is because of it (0..1). Written by the render
  /// thread that draws this layer, while it draws it.
  [[nodiscard]] float gateLevelDb() const noexcept { return gateLevelDb_.load(); }
  [[nodiscard]] bool gateOpen() const noexcept { return gateOpen_.load(); }
  [[nodiscard]] float gateEnvelope() const noexcept { return gateEnvelope_.load(); }
  /// 8.6: the picture Media Mix mixes over this layer (null: none). Any thread;
  /// the render thread takes a reference each frame. Like the Visual globals,
  /// on the engine's own primary layer it goes over the whole canvas once
  /// several layers are mixed.
  void setMediaSource(std::shared_ptr<MediaSource> source) {
    const std::lock_guard lock(mediaMutex_);
    media_ = std::move(source);
  }
  [[nodiscard]] std::shared_ptr<MediaSource> mediaSource() const {
    const std::lock_guard lock(mediaMutex_);
    return media_;
  }
  /// 8.6c: where a video source should be (the host transport, in the plugin).
  /// One writer thread (`Visualizer::processAudio`); the render thread hands it
  /// to the source each frame.
  void setMediaTimeline(const MediaTimeline& timeline) noexcept { mediaTimeline_.publish(timeline); }
  /// 8.6e: how the media meets the picture (a setting, not a parameter). Any thread.
  void setMediaBlend(LayerBlend blend) noexcept { mediaBlend_.store(blend); }
  [[nodiscard]] LayerBlend mediaBlend() const noexcept { return mediaBlend_.load(); }
  [[nodiscard]] MediaTimeline mediaTimeline() const noexcept { return mediaTimeline_.read(); }

  /// Render thread only.
  void reportGate(float levelDb, bool open, float envelope) noexcept {
    gateLevelDb_.store(levelDb);
    gateOpen_.store(open);
    gateEnvelope_.store(envelope);
  }

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
  core::SeqlockSnapshot<core::VisualControls> visual_;
  core::SeqlockSnapshot<std::array<float, core::kMacroCount>> macros_;
  std::atomic<bool> gateEnabled_{false};
  std::atomic<float> gateThresholdDb_{-80.0f};
  std::atomic<float> gateReleaseMs_{80.0f};
  std::atomic<float> gateLevelDb_{-200.0f};
  std::atomic<bool> gateOpen_{true};
  std::atomic<float> gateEnvelope_{1.0f};
  mutable std::mutex mediaMutex_; // never taken on the audio thread
  std::shared_ptr<MediaSource> media_;
  core::SeqlockSnapshot<MediaTimeline> mediaTimeline_;
  std::atomic<LayerBlend> mediaBlend_{LayerBlend::Normal};
};

} // namespace milkdawp::engine
