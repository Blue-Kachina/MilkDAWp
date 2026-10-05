// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace milkdawp::engine {

/// One picture from a media source, ready to upload: tightly packed RGBA8,
/// not premultiplied, bottom row first (GL's order).
struct MediaFrame {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

/// Where a time-based source (a video) should be (8.6c). In the plugin the host
/// transport drives it, so a rendered DAW session is repeatable; in the app
/// (`hostDriven` false) it runs freely. Plain data: it crosses threads through
/// a SeqlockSnapshot.
struct MediaTimeline {
  bool hostDriven = false;
  bool playing = false;
  double seconds = 0.0; // the host's position (samples / rate)
};

/// The position in a looping video of `durationSeconds`: the host's position
/// (also while stopped, so scrubbing moves the picture), or `freeRunSeconds`
/// when nothing drives it, wrapped to the video's length.
[[nodiscard]] double videoPosition(const MediaTimeline& timeline, double freeRunSeconds,
                                   double durationSeconds) noexcept;

/// Phase 8.6: something that supplies pictures to mix into the visual (Media
/// Mix): an image now; a camera and video files later (8.6b-d). It hands over
/// CPU frames; the render thread uploads one to a GL texture only when it is
/// new. Thread-safe: frames may arrive on any thread and be read on another.
class MediaSource {
public:
  virtual ~MediaSource() = default;

  /// The newest frame (null before the first), and in `serial` a number that
  /// changes whenever a new frame arrives (0 with no frame).
  [[nodiscard]] virtual std::shared_ptr<const MediaFrame> latestFrame(std::uint64_t& serial) const = 0;
  /// What to call it in the UI ("logo.png").
  [[nodiscard]] virtual std::string description() const = 0;
  /// The render thread passes the layer's timeline every frame; sources that
  /// don't move in time ignore it. Must not block.
  virtual void setTimeline(const MediaTimeline&) noexcept {}
};

/// A video file (8.6c), decoded on a thread of its own by `VideoDecoder`, and
/// looping. Each frame is converted there, so the render thread only uploads.
/// Follows the timeline it is given (`setTimeline`): a jump in it (a host
/// relocate, a loop) seeks.
class VideoMediaSource final : public MediaSource {
public:
  /// Starts the decoding thread. An unreadable file shows nothing and reports
  /// its reason in `error()`.
  explicit VideoMediaSource(const juce::File& file);
  ~VideoMediaSource() override;
  VideoMediaSource(const VideoMediaSource&) = delete;
  VideoMediaSource& operator=(const VideoMediaSource&) = delete;

  [[nodiscard]] std::shared_ptr<const MediaFrame> latestFrame(std::uint64_t& serial) const override;
  [[nodiscard]] std::string description() const override { return description_; }
  void setTimeline(const MediaTimeline& timeline) noexcept override;

  /// Empty while fine; why nothing shows otherwise. Any thread.
  [[nodiscard]] std::string error() const;
  /// Blocks until the file has been opened (or failed), up to `timeoutMs`.
  /// True if it opened. For tests and diagnostics.
  bool waitUntilOpen(int timeoutMs) const;
  /// The video's position as of the last frame shown, in seconds.
  [[nodiscard]] double shownPosition() const;

  /// What the decoding thread and this object share (VideoMediaSource.cpp).
  struct Shared;

private:
  std::shared_ptr<Shared> shared_;
  std::string description_;
  std::thread thread_;
};

/// Whether `path` names a video file by its extension.
[[nodiscard]] bool isVideoPath(const std::string& path);

/// A still image (PNG, JPEG, GIF's first frame, or anything else JUCE reads),
/// scaled down to `kMaxDimension` on its longer side.
class ImageMediaSource final : public MediaSource {
public:
  static constexpr int kMaxDimension = 2048;

  /// Null, with `error` set, if the file can't be read as an image.
  [[nodiscard]] static std::shared_ptr<ImageMediaSource> load(const juce::File& file, std::string& error);
  /// From pixels already in memory (tests).
  explicit ImageMediaSource(MediaFrame frame, std::string description);

  [[nodiscard]] std::shared_ptr<const MediaFrame> latestFrame(std::uint64_t& serial) const override;
  [[nodiscard]] std::string description() const override { return description_; }

private:
  std::shared_ptr<const MediaFrame> frame_;
  std::string description_;
};

/// The source for a saved path: empty gives null with no error. A path is an
/// image file, or `camera:<device name>` (`cameraMediaPath`).
[[nodiscard]] std::shared_ptr<MediaSource> openMediaSource(const std::string& path, std::string& error);

/// The file-chooser pattern for the image files `openMediaSource` can open.
[[nodiscard]] juce::String mediaFileWildcard();

/// What the settings menu calls a saved path: an image's file name, or
/// "Camera: <device>". Empty for none.
[[nodiscard]] juce::String mediaSourceDisplayName(const std::string& path);

// ---- camera (8.6b) ----
/// Whether this build can use cameras (JUCE's CameraDevice: Windows and macOS;
/// Linux needs V4L2, 8.6d).
[[nodiscard]] bool camerasSupported() noexcept;
/// The cameras attached now, by name. Lists devices without opening any.
[[nodiscard]] juce::StringArray availableCameras();
/// The saved path for a camera, and back: `camera:<device name>`.
[[nodiscard]] std::string cameraMediaPath(const juce::String& deviceName);
[[nodiscard]] bool isCameraMediaPath(const std::string& path) noexcept;
/// The device name in a camera path; empty for any other path.
[[nodiscard]] juce::String cameraDeviceName(const std::string& path);

/// `image` (any JUCE pixel format) as a frame: RGBA8, un-premultiplied, bottom
/// row first, scaled down to `maxDimension` on its longer side when larger.
[[nodiscard]] MediaFrame frameFromImage(const juce::Image& image, int maxDimension);

/// Where a source sits in a frame: the part of the source to show so that it
/// fills a `targetWidth` x `targetHeight` frame without stretching (cropping
/// the overflow, centred). Returned as the scale to apply to texture
/// coordinates about the centre: (1, 1) when the shapes match.
struct UvScale {
  float x = 1.0f;
  float y = 1.0f;
};
[[nodiscard]] UvScale coverUvScale(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight) noexcept;

} // namespace milkdawp::engine
