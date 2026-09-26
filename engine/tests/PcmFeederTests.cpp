// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/engine/PcmFeeder.h"

using namespace milkdawp;

namespace {
// Writes `frames` stereo frames whose left sample is their absolute frame
// number, so a test can see exactly which frames were fed.
void writeNumbered(core::AudioRing& ring, std::size_t frames) {
  std::vector<float> block(frames * 2);
  const auto start = ring.samplePosition();
  for (std::size_t i = 0; i < frames; ++i) {
    block[i * 2] = static_cast<float>(start + i);
    block[i * 2 + 1] = 0.0f;
  }
  ring.write(block.data(), frames);
}

struct Collected {
  std::vector<float> firstFrameValues;
  std::size_t totalFrames = 0;
  std::size_t calls = 0;
  float lastFeedFirst = -1.0f;
  float lastFeedLast = -1.0f;
};

std::size_t feedInto(engine::PcmFeeder& feeder, const core::AudioRing& ring, Collected& out) {
  return feeder.feed(ring, [&](const float* samples, std::size_t frames, int channels) {
    ++out.calls;
    out.totalFrames += frames;
    out.lastFeedFirst = samples[0];
    out.lastFeedLast = samples[(frames - 1) * static_cast<std::size_t>(channels)];
  });
}
} // namespace

TEST_CASE("PcmFeeder feeds every new frame exactly once at a low frame rate", "[engine][PcmFeeder]") {
  // 48 kHz audio, 30 fps video: 1600 new frames per video frame, which
  // the old fixed 512-frame feed dropped two-thirds of.
  core::AudioRing ring(1 << 14, 2);
  engine::PcmFeeder feeder(2048, 2);
  Collected got;

  feedInto(feeder, ring, got); // nothing written yet
  CHECK(got.calls == 0);

  for (int frame = 0; frame < 10; ++frame) {
    writeNumbered(ring, 1600);
    const auto fed = feedInto(feeder, ring, got);
    CHECK(fed == 1600);
    CHECK(got.lastFeedLast == static_cast<float>(ring.samplePosition() - 1));
  }
  CHECK(got.totalFrames == 16000);
}

TEST_CASE("PcmFeeder never repeats frames at a high frame rate", "[engine][PcmFeeder]") {
  // 48 kHz audio, 144 fps video, audio arriving in 512-frame blocks: most
  // video frames see no new audio at all, and must feed nothing.
  core::AudioRing ring(1 << 14, 2);
  engine::PcmFeeder feeder(2048, 2);
  Collected got;

  std::size_t written = 0;
  for (int videoFrame = 0; videoFrame < 144; ++videoFrame) {
    // One 512-frame audio block roughly every 1.5 video frames.
    while (written < static_cast<std::size_t>((videoFrame + 1) * 48000 / 144)) {
      writeNumbered(ring, 512);
      written += 512;
    }
    feedInto(feeder, ring, got);
  }
  CHECK(got.totalFrames == written);
}

TEST_CASE("PcmFeeder keeps only the newest frames when more than one call's worth is waiting",
          "[engine][PcmFeeder]") {
  core::AudioRing ring(1 << 14, 2);
  engine::PcmFeeder feeder(576, 2);
  Collected got;

  writeNumbered(ring, 100);
  feedInto(feeder, ring, got);
  writeNumbered(ring, 5000); // a long stall on the render thread
  const auto fed = feedInto(feeder, ring, got);
  CHECK(fed == 576);
  CHECK(got.lastFeedLast == static_cast<float>(5099));
  CHECK(got.lastFeedFirst == static_cast<float>(5100 - 576));
}

TEST_CASE("PcmFeeder starts from 'now' rather than replaying old audio, and after reset()",
          "[engine][PcmFeeder]") {
  core::AudioRing ring(1 << 14, 2);
  engine::PcmFeeder feeder(576, 2);
  Collected got;

  writeNumbered(ring, 10000);
  CHECK(feedInto(feeder, ring, got) == 576);
  CHECK(got.lastFeedLast == 9999.0f);

  feeder.reset();
  writeNumbered(ring, 100);
  CHECK(feedInto(feeder, ring, got) == 576);
  CHECK(got.lastFeedLast == 10099.0f);
}
