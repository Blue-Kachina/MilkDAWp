// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/HostTransport.h"

using namespace milkdawp::core;

namespace {
constexpr double kSampleRate = 48000.0;
}

TEST_CASE("HostTransport reports zero confidence when stopped", "[core][HostTransport]") {
  HostTransport transport(kSampleRate);
  TransportInfo info;
  info.isPlaying = false;
  info.bpm = 120.0;

  auto state = transport.processTransport(info);
  CHECK(state.confidence == 0.0f);
}

TEST_CASE("HostTransport reports zero confidence for a nonsensical bpm", "[core][HostTransport]") {
  HostTransport transport(kSampleRate);
  TransportInfo info;
  info.isPlaying = true;
  info.bpm = 0.0;

  auto state = transport.processTransport(info);
  CHECK(state.confidence == 0.0f);
}

TEST_CASE("HostTransport derives beat position exactly on a beat boundary", "[core][HostTransport]") {
  HostTransport transport(kSampleRate);
  TransportInfo info;
  info.isPlaying = true;
  info.bpm = 120.0;
  info.ppqPosition = 0.0;
  info.samplePos = 1000;

  auto state = transport.processTransport(info);

  const std::uint64_t samplesPerBeat = 24000; // 60/120 * 48000
  CHECK(state.confidence == 1.0f);
  CHECK(state.beatIndex == 0);
  CHECK(state.barIndex == 0);
  CHECK(state.nextBeatSample == 1000 + samplesPerBeat);
}

TEST_CASE("HostTransport derives beat position mid-beat", "[core][HostTransport]") {
  HostTransport transport(kSampleRate);
  TransportInfo info;
  info.isPlaying = true;
  info.bpm = 120.0;
  info.ppqPosition = 2.5; // beat 2, halfway through
  info.samplePos = 5000;

  auto state = transport.processTransport(info);

  CHECK(state.beatIndex == 2);
  CHECK(state.nextBeatSample == 5000 + 12000); // half of a 24000-sample beat remains
}

TEST_CASE("HostTransport derives bar index from the time signature", "[core][HostTransport]") {
  HostTransport transport(kSampleRate);
  TransportInfo info;
  info.isPlaying = true;
  info.bpm = 120.0;
  info.ppqPosition = 9.0; // beat 9 -> bar 2 in 4/4 (beats 0-3 = bar 0, 4-7 = bar 1, 8-11 = bar 2)
  info.timeSigNumerator = 4;
  info.samplePos = 0;

  auto state = transport.processTransport(info);
  CHECK(state.beatIndex == 9);
  CHECK(state.barIndex == 2);
  CHECK(state.beatsPerBar == 4);
  CHECK(state.beatInBar == 1);

  info.timeSigNumerator = 3; // beat 9 -> bar 3, its first beat
  state = transport.processTransport(info);
  CHECK(state.barIndex == 3);
  CHECK(state.beatsPerBar == 3);
  CHECK(state.beatInBar == 0);
}

TEST_CASE("HostTransport reflects a loop (ppq jumping backward) with no stale state",
          "[core][HostTransport]") {
  HostTransport transport(kSampleRate);
  TransportInfo info;
  info.isPlaying = true;
  info.bpm = 120.0;
  info.timeSigNumerator = 4;

  info.ppqPosition = 10.0;
  info.samplePos = 100000;
  auto beforeLoop = transport.processTransport(info);
  CHECK(beforeLoop.beatIndex == 10);

  // The host looped back to beat 2.
  info.ppqPosition = 2.0;
  info.samplePos = 4000;
  auto afterLoop = transport.processTransport(info);

  CHECK(afterLoop.beatIndex == 2);
  CHECK(afterLoop.nextBeatSample == 4000 + 24000);
  CHECK(afterLoop.confidence == 1.0f);
}

TEST_CASE("HostTransport reflects a relocate (arbitrary ppq jump) with no stale state",
          "[core][HostTransport]") {
  HostTransport transport(kSampleRate);
  TransportInfo info;
  info.isPlaying = true;
  info.bpm = 140.0;
  info.timeSigNumerator = 4;

  info.ppqPosition = 1.0;
  info.samplePos = 0;
  transport.processTransport(info);

  // User dragged the playhead far ahead.
  info.ppqPosition = 500.0;
  info.samplePos = 9000000;
  auto afterRelocate = transport.processTransport(info);

  CHECK(afterRelocate.beatIndex == 500);
  CHECK(afterRelocate.confidence == 1.0f);
}
