// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Platforms without a video decoder yet (8.6d: AVFoundation, GStreamer).

#include "milkdawp/engine/VideoDecoder.h"

namespace milkdawp::engine {

std::unique_ptr<VideoDecoder> VideoDecoder::open(const juce::File& file, std::string& error) {
  error = "video isn't supported on this system yet (\"" + file.getFileName().toStdString() + "\")";
  return nullptr;
}

bool VideoDecoder::supported() noexcept { return false; }

juce::File VideoDecoder::writeTestClip(const juce::File&) { return {}; }

} // namespace milkdawp::engine
