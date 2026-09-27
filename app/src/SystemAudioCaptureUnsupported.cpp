// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// The fallback for every platform without a `SystemAudioCapture`
// implementation file yet (macOS process taps land in 4.8; Linux needs
// none of this -- PipeWire/Pulse monitor sources already show up in the
// ordinary device list, so 4.9 is verification and docs, not code here).

#include "SystemAudioCapture.h"

namespace milkdawp::app {

namespace {

class UnsupportedSystemAudioCapture final : public SystemAudioCapture {
public:
  juce::String open(engine::Visualizer&) override {
    return "System audio capture isn't implemented on this platform yet.";
  }
  void close() override {}
  [[nodiscard]] bool isOpen() const override { return false; }
  [[nodiscard]] PermissionState permissionState() const override { return PermissionState::Unsupported; }
  [[nodiscard]] juce::String describe() const override {
    return "System audio capture isn't available on this platform yet.";
  }
  [[nodiscard]] float takePeak(PeakReader) noexcept override { return 0.0f; }
};

} // namespace

std::unique_ptr<SystemAudioCapture> createSystemAudioCapture() {
  return std::make_unique<UnsupportedSystemAudioCapture>();
}

} // namespace milkdawp::app
