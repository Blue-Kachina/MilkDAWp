// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>

#include <juce_core/juce_core.h>

#include "AudioPeakReader.h"
#include "milkdawp/engine/Visualizer.h"

namespace milkdawp::app {

/// "Whatever plays through the speakers", captured and fed into the same
/// `Visualizer` a device input feeds (§4.7): Windows WASAPI loopback, macOS
/// Core Audio process taps (§4.8), Linux's PipeWire/Pulse monitor sources
/// need no special code here (§4.9, they already appear in the device
/// list), and Android's `AudioPlaybackCapture` (Phase 7, ADR-0010). One
/// implementation file per platform, chosen by CMake at build time, so a
/// platform's audio API never appears in this header or in another
/// platform's file. `AudioSourceRouter` is what actually switches between
/// this and `AudioInput`; nothing platform-specific goes here.
class SystemAudioCapture {
public:
  /// What the UI shows next to the "System audio" choice. Only macOS
  /// (14.2+) and Android ask the user for consent; Windows loopback needs
  /// none, so its implementation only ever reports Unsupported, Granted, or
  /// Failed-via-`describe()`.
  enum class PermissionState {
    Unsupported,    // no implementation on this OS/build, or the OS is too old
    NotDetermined,  // capture has never been requested yet
    Denied,         // the OS or the user refused
    Granted,        // capture is allowed (does not imply it is running)
  };

  virtual ~SystemAudioCapture() = default;

  /// Starts capturing system output into `visualizer`, the same
  /// `Visualizer::prepare`/`processAudio` path a device input uses. On a
  /// platform that needs consent, this may trigger an OS prompt and return
  /// before the user answers: watch `permissionState()`/`onPermissionChanged`
  /// rather than assuming capture is running just because this returned
  /// success. Returns an error string (empty on success or while consent is
  /// pending).
  virtual juce::String open(engine::Visualizer& visualizer) = 0;
  virtual void close() = 0;
  [[nodiscard]] virtual bool isOpen() const = 0;

  [[nodiscard]] virtual PermissionState permissionState() const = 0;
  /// One line for the UI: "System audio (loopback)", why it can't run right
  /// now, or "unsupported on this OS".
  [[nodiscard]] virtual juce::String describe() const = 0;

  /// Largest absolute captured sample since this reader's previous call.
  [[nodiscard]] virtual float takePeak(PeakReader reader) noexcept = 0;

  /// Message thread, when permissionState() changes asynchronously (an OS
  /// consent dialog answered, or the user revoked access while running).
  std::function<void()> onPermissionChanged;
};

/// The current platform's implementation, or a stub that always reports
/// `PermissionState::Unsupported`. Never null.
[[nodiscard]] std::unique_ptr<SystemAudioCapture> createSystemAudioCapture();

/// Shared by every platform implementation: the compare-and-swap-to-max peak
/// tracking `AudioInput` also uses, kept here so each `.cpp` doesn't retype
/// the same handful of lines.
class SystemAudioPeakTracker {
public:
  void report(float peak) noexcept {
    for (auto& slot : peaks_) {
      float previous = slot.load(std::memory_order_relaxed);
      while (peak > previous && !slot.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {
      }
    }
  }

  [[nodiscard]] float take(PeakReader reader) noexcept {
    return peaks_[static_cast<std::size_t>(reader)].exchange(0.0f, std::memory_order_relaxed);
  }

private:
  std::array<std::atomic<float>, 2> peaks_{};
};

} // namespace milkdawp::app
