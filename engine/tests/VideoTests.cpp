// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 8.6c/d: video media sources. The timeline maths runs everywhere; the
// decoding tests run wherever the platform decodes video, on a clip the
// platform writes itself (one second of red, then one of blue), so no video
// file lives in the repository. Without an encoder they say so and pass.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "milkdawp/engine/MediaSource.h"
#include "milkdawp/engine/VideoDecoder.h"

using namespace milkdawp::engine;
using Catch::Approx;

// GStreamer (Linux) decodes on threads of its own inside system libraries
// ThreadSanitizer can't see into, as with Mesa in the GL tests: under TSan the
// decoding tests stand down. They run in every other job.
#if defined(__SANITIZE_THREAD__)
#define MILKDAWP_UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MILKDAWP_UNDER_TSAN 1
#endif
#endif

TEST_CASE("videoPosition follows the host, or runs freely, and loops", "[engine][video]") {
  MediaTimeline host{true, true, 12.5};
  CHECK(videoPosition(host, 99.0, 10.0) == Approx(2.5)); // the host's time, wrapped
  host.playing = false;
  CHECK(videoPosition(host, 99.0, 10.0) == Approx(2.5)); // stopped: holds where the host is
  host.seconds = -1.0;
  CHECK(videoPosition(host, 0.0, 10.0) == Approx(9.0)); // pre-roll wraps from the end
  const MediaTimeline free;
  CHECK(videoPosition(free, 23.0, 10.0) == Approx(3.0));
  CHECK(videoPosition(free, 4.0, 0.0) == Approx(4.0)); // unknown length: no wrapping
}

TEST_CASE("isVideoPath goes by extension", "[engine][video]") {
  CHECK(isVideoPath("C:/clips/dance.mp4"));
  CHECK(isVideoPath("C:/clips/DANCE.MOV"));
  CHECK(isVideoPath("/home/me/clip.avi"));
  CHECK(isVideoPath("/home/me/clip.ogv"));
  CHECK_FALSE(isVideoPath("C:/clips/logo.png"));
  CHECK_FALSE(isVideoPath("camera:USB"));
}

namespace {

struct Rgb {
  int r;
  int g;
  int b;
};

Rgb centre(const MediaFrame& frame) {
  const auto i = (static_cast<std::size_t>(frame.height / 2) * static_cast<std::size_t>(frame.width) +
                  static_cast<std::size_t>(frame.width / 2)) * 4;
  return {frame.rgba[i], frame.rgba[i + 1], frame.rgba[i + 2]};
}

bool isRed(const Rgb& c) { return c.r > 150 && c.b < 70; }
bool isBlue(const Rgb& c) { return c.b > 150 && c.r < 70; }

struct Clip {
  juce::File file = VideoDecoder::writeTestClip(juce::File::getSpecialLocation(juce::File::tempDirectory));
  ~Clip() { file.deleteFile(); }
  [[nodiscard]] bool usable() const { return file.existsAsFile(); }
};

#ifdef MILKDAWP_UNDER_TSAN
constexpr bool kUnderTsan = true;
#else
constexpr bool kUnderTsan = false;
#endif

// Why a decoding test can't run here, or empty if it can.
std::string unavailable(const Clip& clip) {
  if (kUnderTsan) {
    return "ThreadSanitizer cannot analyse the system's video decoders";
  }
  if (!VideoDecoder::supported()) {
    return "no video decoder on this system";
  }
  if (!clip.usable()) {
    return "no encoder on this system to make the test clip";
  }
  return {};
}

} // namespace

TEST_CASE("VideoDecoder decodes and seeks", "[engine][video]") {
  if (kUnderTsan) {
    SUCCEED("skipped under ThreadSanitizer");
    return;
  }
  Clip clip;
  if (const auto reason = unavailable(clip); !reason.empty()) {
    SUCCEED(reason);
    return;
  }
  std::string error;
  auto decoder = VideoDecoder::open(clip.file, error);
  REQUIRE(decoder != nullptr);
  CHECK(error.empty());
  CHECK(decoder->durationSeconds() == Approx(2.0).margin(0.15));

  MediaFrame frame;
  double at = -1.0;
  REQUIRE(decoder->next(frame, at));
  CHECK(frame.width == 160);
  CHECK(frame.height == 96);
  CHECK(at == Approx(0.0).margin(0.05));
  CHECK(isRed(centre(frame)));

  // A seek lands on the keyframe at or before the target (with H.264, possibly
  // the clip's only one, at 0 s); decoding on from there reaches it.
  REQUIRE(decoder->seek(1.5));
  REQUIRE(decoder->next(frame, at));
  CHECK(at <= 1.5 + 1.0e-3);
  while (at < 1.45 && decoder->next(frame, at)) {
  }
  CHECK(at == Approx(1.5).margin(0.06));
  CHECK(isBlue(centre(frame)));

  int frames = 1;
  while (decoder->next(frame, at)) {
    ++frames;
  }
  CHECK(frames >= 2); // reaches the end, and says so
}

TEST_CASE("VideoMediaSource shows the frame the host's position asks for", "[engine][video]") {
  if (kUnderTsan) {
    SUCCEED("skipped under ThreadSanitizer");
    return;
  }
  Clip clip;
  if (const auto reason = unavailable(clip); !reason.empty()) {
    SUCCEED(reason);
    return;
  }
  VideoMediaSource source(clip.file);
  REQUIRE(source.waitUntilOpen(5000));
  CHECK(source.error().empty());

  const auto waitFor = [&](auto&& predicate) {
    for (int i = 0; i < 300; ++i) {
      std::uint64_t serial = 0;
      if (const auto frame = source.latestFrame(serial); frame != nullptr && predicate(*frame)) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  };

  source.setTimeline({true, false, 0.3}); // host stopped at 0.3 s: red
  CHECK(waitFor([](const MediaFrame& f) { return isRed(centre(f)); }));
  source.setTimeline({true, true, 1.6}); // relocated into the blue second
  CHECK(waitFor([](const MediaFrame& f) { return isBlue(centre(f)); }));
  CHECK(source.shownPosition() >= 1.0);
  source.setTimeline({true, true, 2.4}); // past the end loops back to 0.4 s: red again
  CHECK(waitFor([](const MediaFrame& f) { return isRed(centre(f)); }));
}

TEST_CASE("openMediaSource opens a video, and reports one it can't play", "[engine][video]") {
  if (kUnderTsan) {
    SUCCEED("skipped under ThreadSanitizer");
    return;
  }
  Clip clip;
  if (const auto reason = unavailable(clip); !reason.empty()) {
    SUCCEED(reason);
    return;
  }
  std::string error;
  CHECK(openMediaSource(clip.file.getFullPathName().toStdString(), error) != nullptr);
  CHECK(error.empty());

  const auto broken = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("milkdawp-broken", clip.file.getFileExtension());
  broken.replaceWithText("not a video");
  error.clear();
  CHECK(openMediaSource(broken.getFullPathName().toStdString(), error) == nullptr);
  CHECK_FALSE(error.empty());
  broken.deleteFile();
}
