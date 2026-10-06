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
  /// Whether video can be decoded here: on Linux, whether GStreamer is
  /// installed (it is loaded at run time, never bundled).
  [[nodiscard]] static bool supported() noexcept;

  /// For tests: writes a 2 s, 10 fps, 160 x 96 clip into `directory` (one
  /// second of red, then one of blue) in a format this platform encodes and
  /// decodes itself (H.264 MP4 on Windows and macOS, MJPEG AVI through
  /// GStreamer on Linux), so no video file lives in the repository. Returns
  /// the file, or a non-existent one where no encoder is available.
  [[nodiscard]] static juce::File writeTestClip(const juce::File& directory);

  [[nodiscard]] virtual double durationSeconds() const = 0;
  /// The next frame in order and its presentation time in seconds. False at
  /// the end of the file, or if decoding failed.
  virtual bool next(MediaFrame& frame, double& timestampSeconds) = 0;
  /// Moves to the frame at or before `seconds` (the next `next()` returns it
  /// or the one after).
  virtual bool seek(double seconds) = 0;
};

} // namespace milkdawp::engine
