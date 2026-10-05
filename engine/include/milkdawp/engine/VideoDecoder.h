// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <memory>
#include <string>

#include <juce_core/juce_core.h>

#include "milkdawp/engine/MediaSource.h"

namespace milkdawp::engine {

/// Phase 8.6c: a video file's frames, decoded in order on the CPU by the
/// platform's own decoder (exploration doc §4.5: no bundled FFmpeg, so no
/// codec licensing of our own). Windows: Media Foundation. macOS (AVFoundation)
/// and Linux (GStreamer at runtime) are 8.6d; until then `open()` says video is
/// unavailable there. "H.264 MP4 works everywhere" is the promise; anything
/// else the OS decodes is a bonus.
///
/// Not thread-safe: open it, and use it, on one thread (on Windows that thread
/// joins COM's multithreaded apartment for the decoder's lifetime).
class VideoDecoder {
public:
  virtual ~VideoDecoder() = default;

  /// Null, with `error` set, if the file can't be decoded here.
  [[nodiscard]] static std::unique_ptr<VideoDecoder> open(const juce::File& file, std::string& error);
  /// Whether this build has a video decoder at all.
  [[nodiscard]] static bool supported() noexcept;

  [[nodiscard]] virtual double durationSeconds() const = 0;
  /// The next frame in order and its presentation time in seconds. False at
  /// the end of the file, or if decoding failed.
  virtual bool next(MediaFrame& frame, double& timestampSeconds) = 0;
  /// Moves to the frame at or before `seconds` (the next `next()` returns it
  /// or the one after).
  virtual bool seek(double seconds) = 0;
};

} // namespace milkdawp::engine
