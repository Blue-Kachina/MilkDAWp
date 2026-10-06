// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>

#include "milkdawp/core/SeqlockSnapshot.h"
#include "milkdawp/engine/MediaSource.h"
#include "milkdawp/engine/VideoDecoder.h"

namespace milkdawp::engine {

double videoPosition(const MediaTimeline& timeline, double freeRunSeconds, double durationSeconds) noexcept {
  const double t = timeline.hostDriven ? timeline.seconds : freeRunSeconds;
  if (!(durationSeconds > 0.0)) {
    return std::max(t, 0.0);
  }
  const double wrapped = std::fmod(t, durationSeconds);
  return wrapped < 0.0 ? wrapped + durationSeconds : wrapped;
}

bool isVideoPath(const std::string& path) {
  const auto extension = juce::File(juce::String::fromUTF8(path.c_str())).getFileExtension().toLowerCase();
  return extension == ".mp4" || extension == ".m4v" || extension == ".mov" || extension == ".wmv" ||
         extension == ".avi" || extension == ".mkv" || extension == ".webm" || extension == ".ogv";
}

// What the decoding thread and the rest share. Held by shared_ptr so the
// thread can finish safely whichever side lets go last.
struct VideoMediaSource::Shared {
  std::atomic<bool> stop{false};
  core::SeqlockSnapshot<MediaTimeline> timeline;

  mutable std::mutex mutex; // frame, serial, error, opened, position
  mutable std::condition_variable openedChanged;
  std::shared_ptr<const MediaFrame> frame;
  std::uint64_t serial = 0;
  std::string error;
  bool settled = false; // opened or failed
  double position = 0.0;

  void publish(MediaFrame&& next, double at) {
    auto shared = std::make_shared<const MediaFrame>(std::move(next));
    const std::lock_guard lock(mutex);
    frame = std::move(shared);
    ++serial;
    position = at;
  }
  void settle(std::string reason) {
    const std::lock_guard lock(mutex);
    error = std::move(reason);
    settled = true;
    openedChanged.notify_all();
  }
};

namespace {

using Clock = std::chrono::steady_clock;

// The decoding loop: keeps the shown frame the latest one at or before where
// the timeline says the video is, decoding ahead by one frame.
void decodeLoop(const std::shared_ptr<VideoMediaSource::Shared>& shared, const juce::File& file) {
  std::string error;
  auto decoder = VideoDecoder::open(file, error);
  if (!decoder) {
    shared->settle(error);
    return;
  }
  shared->settle({});
  const double duration = decoder->durationSeconds();
  const auto started = Clock::now();

  MediaFrame pending;
  double pendingAt = 0.0;
  bool havePending = decoder->next(pending, pendingAt);
  // Where decoding stands: the last frame shown, or where it last sought to.
  double position = 0.0;
  constexpr double kSlack = 1.0e-3;

  while (!shared->stop.load()) {
    const double freeRun = std::chrono::duration<double>(Clock::now() - started).count();
    const double target = videoPosition(shared->timeline.read(), freeRun, duration);

    // Back in time (the loop wrapping, a host relocate) or more than a second
    // ahead: seek rather than decode through every frame in between.
    if (target + 0.05 < position || (havePending && target > pendingAt + 1.0)) {
      decoder->seek(target);
      havePending = decoder->next(pending, pendingAt);
      position = target;
    }
    // Show the newest frame that is due, skipping any that are already late.
    MediaFrame due;
    double dueAt = -1.0;
    while (havePending && pendingAt <= target + kSlack) {
      due = std::move(pending);
      dueAt = pendingAt;
      havePending = decoder->next(pending, pendingAt);
    }
    if (dueAt >= 0.0) {
      shared->publish(std::move(due), dueAt);
      position = dueAt;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
}

} // namespace

VideoMediaSource::VideoMediaSource(const juce::File& file)
    : shared_(std::make_shared<Shared>()), description_(file.getFileName().toStdString()) {
  thread_ = std::thread([shared = shared_, file] {
    juce::Thread::setCurrentThreadName("MilkDAWp video");
    decodeLoop(shared, file);
  });
}

VideoMediaSource::~VideoMediaSource() {
  shared_->stop.store(true);
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::shared_ptr<const MediaFrame> VideoMediaSource::latestFrame(std::uint64_t& serial) const {
  const std::lock_guard lock(shared_->mutex);
  serial = shared_->serial;
  return shared_->frame;
}

void VideoMediaSource::setTimeline(const MediaTimeline& timeline) noexcept { shared_->timeline.publish(timeline); }

std::string VideoMediaSource::error() const {
  const std::lock_guard lock(shared_->mutex);
  return shared_->error;
}

bool VideoMediaSource::waitUntilOpen(int timeoutMs) const {
  std::unique_lock lock(shared_->mutex);
  shared_->openedChanged.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return shared_->settled; });
  return shared_->settled && shared_->error.empty();
}

double VideoMediaSource::shownPosition() const {
  const std::lock_guard lock(shared_->mutex);
  return shared_->position;
}

} // namespace milkdawp::engine
