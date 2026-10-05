// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 8.6c: video media sources. The timeline maths runs everywhere; the
// decoding tests run where the platform has a decoder, on a clip they encode
// themselves (one second of red, then one of blue), so no video file lives in
// the repository.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "milkdawp/engine/MediaSource.h"
#include "milkdawp/engine/VideoDecoder.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

using namespace milkdawp::engine;
using Catch::Approx;

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
  CHECK_FALSE(isVideoPath("C:/clips/logo.png"));
  CHECK_FALSE(isVideoPath("camera:USB"));
}

#if JUCE_WINDOWS

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 96;
constexpr int kFps = 10;
constexpr LONGLONG kFrameDuration = 10'000'000 / kFps; // 100 ns units

// Encodes 2 s of H.264: frames 0-9 red, 10-19 blue. False if this system has no
// H.264 encoder (a Windows "N" edition), in which case the test says so.
bool writeClip(const juce::File& file) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) && false) {
    return false;
  }
  MFStartup(MF_VERSION, MFSTARTUP_LITE);
  IMFSinkWriter* writer = nullptr;
  if (FAILED(MFCreateSinkWriterFromURL(file.getFullPathName().toWideCharPointer(), nullptr, nullptr, &writer))) {
    return false;
  }
  bool ok = true;
  IMFMediaType* out = nullptr;
  IMFMediaType* in = nullptr;
  DWORD stream = 0;
  MFCreateMediaType(&out);
  out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  out->SetUINT32(MF_MT_AVG_BITRATE, 400000);
  out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(out, MF_MT_FRAME_SIZE, kWidth, kHeight);
  MFSetAttributeRatio(out, MF_MT_FRAME_RATE, kFps, 1);
  MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  ok = ok && SUCCEEDED(writer->AddStream(out, &stream));
  MFCreateMediaType(&in);
  in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(in, MF_MT_FRAME_SIZE, kWidth, kHeight);
  MFSetAttributeRatio(in, MF_MT_FRAME_RATE, kFps, 1);
  MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  ok = ok && SUCCEEDED(writer->SetInputMediaType(stream, in, nullptr));
  ok = ok && SUCCEEDED(writer->BeginWriting());
  for (int i = 0; ok && i < 2 * kFps; ++i) {
    IMFMediaBuffer* buffer = nullptr;
    const DWORD bytes = kWidth * kHeight * 4;
    MFCreateMemoryBuffer(bytes, &buffer);
    BYTE* data = nullptr;
    buffer->Lock(&data, nullptr, nullptr);
    for (DWORD p = 0; p < bytes; p += 4) {
      const bool red = i < kFps;
      data[p] = red ? 0 : 220;     // B
      data[p + 1] = 0;             // G
      data[p + 2] = red ? 220 : 0; // R
      data[p + 3] = 255;
    }
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);
    IMFSample* sample = nullptr;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer);
    sample->SetSampleTime(i * kFrameDuration);
    sample->SetSampleDuration(kFrameDuration);
    ok = SUCCEEDED(writer->WriteSample(stream, sample));
    sample->Release();
    buffer->Release();
  }
  ok = ok && SUCCEEDED(writer->Finalize());
  in->Release();
  out->Release();
  writer->Release();
  return ok;
}

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
  juce::File file = juce::File::createTempFile(".mp4");
  bool written = writeClip(file);
  ~Clip() { file.deleteFile(); }
};

} // namespace

TEST_CASE("VideoDecoder (Media Foundation) decodes and seeks", "[engine][video]") {
  Clip clip;
  if (!clip.written) {
    SUCCEED("no H.264 encoder on this system to make the test clip");
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
  CHECK(frame.width == kWidth);
  CHECK(frame.height == kHeight);
  CHECK(at == Approx(0.0).margin(0.05));
  CHECK(isRed(centre(frame)));

  // A seek lands on the keyframe at or before the target (here possibly the
  // clip's only one, at 0 s); decoding on from there reaches it.
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
  Clip clip;
  if (!clip.written) {
    SUCCEED("no H.264 encoder on this system to make the test clip");
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
  Clip clip;
  if (!clip.written) {
    SUCCEED("no H.264 encoder on this system to make the test clip");
    return;
  }
  std::string error;
  CHECK(openMediaSource(clip.file.getFullPathName().toStdString(), error) != nullptr);
  CHECK(error.empty());

  const auto broken = juce::File::createTempFile(".mp4");
  broken.replaceWithText("not a video");
  error.clear();
  CHECK(openMediaSource(broken.getFullPathName().toStdString(), error) == nullptr);
  CHECK_FALSE(error.empty());
  broken.deleteFile();
}

#endif
