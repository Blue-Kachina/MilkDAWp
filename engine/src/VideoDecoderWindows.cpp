// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 8.6c: video decoding through Media Foundation's source reader, which picks
// the system's decoders (H.264 always; HEVC with the Store extension) and,
// with video processing on, converts to 32-bit RGB for us.

#include "milkdawp/engine/VideoDecoder.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <mutex>

namespace milkdawp::engine {

namespace {

template <typename T> struct ComPtr {
  T* p = nullptr;
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ~ComPtr() { reset(); }
  void reset() {
    if (p != nullptr) {
      p->Release();
      p = nullptr;
    }
  }
  T** put() {
    reset();
    return &p;
  }
  T* operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }
};

// Media Foundation is started once per process and never shut down: MFShutdown
// while another thread still decodes would pull the platform from under it.
bool startMediaFoundation() {
  static const bool started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
  return started;
}

constexpr double kHundredNs = 1.0e-7;

class MediaFoundationDecoder final : public VideoDecoder {
public:
  ~MediaFoundationDecoder() override {
    reader_.reset();
    if (comInitialised_) {
      CoUninitialize();
    }
  }

  bool open(const juce::File& file, std::string& error) {
    comInitialised_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (!startMediaFoundation()) {
      error = "Media Foundation isn't available (a Windows \"N\" edition without the Media Feature Pack?)";
      return false;
    }
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(attributes.put(), 1)) ||
        FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE))) {
      error = "can't set up the video reader";
      return false;
    }
    const auto path = file.getFullPathName();
    if (FAILED(MFCreateSourceReaderFromURL(path.toWideCharPointer(), attributes.p, reader_.put()))) {
      error = "can't open \"" + file.getFileName().toStdString() + "\" as a video";
      return false;
    }
    reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

    ComPtr<IMFMediaType> wanted;
    MFCreateMediaType(wanted.put());
    wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr,
                                            wanted.p))) {
      error = "no decoder on this system can read \"" + file.getFileName().toStdString() +
              "\" (H.264 MP4 always works; HEVC needs Microsoft's HEVC extension)";
      return false;
    }
    ComPtr<IMFMediaType> actual;
    if (FAILED(reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), actual.put()))) {
      error = "can't read the video's format";
      return false;
    }
    UINT32 width = 0;
    UINT32 height = 0;
    MFGetAttributeSize(actual.p, MF_MT_FRAME_SIZE, &width, &height);
    width_ = static_cast<int>(width);
    height_ = static_cast<int>(height);
    if (width_ <= 0 || height_ <= 0) {
      error = "the video has no picture";
      return false;
    }

    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(reader_->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION,
                                                    &duration)) &&
        duration.vt == VT_UI8) {
      duration_ = static_cast<double>(duration.uhVal.QuadPart) * kHundredNs;
    }
    PropVariantClear(&duration);
    return true;
  }

  double durationSeconds() const override { return duration_; }

  bool next(MediaFrame& frame, double& timestampSeconds) override {
    for (int attempts = 0; attempts < 64; ++attempts) {
      DWORD stream = 0;
      DWORD flags = 0;
      LONGLONG timestamp = 0;
      ComPtr<IMFSample> sample;
      if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags,
                                     &timestamp, sample.put()))) {
        return false;
      }
      if ((flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) != 0) {
        return false;
      }
      if (!sample) {
        continue; // a gap in the stream: read on
      }
      timestampSeconds = static_cast<double>(timestamp) * kHundredNs;
      return copy(*sample.p, frame);
    }
    return false;
  }

  bool seek(double seconds) override {
    PROPVARIANT position;
    InitPropVariantFromInt64(static_cast<LONGLONG>(std::max(seconds, 0.0) / kHundredNs), &position);
    const bool ok = SUCCEEDED(reader_->SetCurrentPosition(GUID_NULL, position));
    PropVariantClear(&position);
    return ok;
  }

private:
  bool copy(IMFSample& sample, MediaFrame& frame) const {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample.ConvertToContiguousBuffer(buffer.put()))) {
      return false;
    }
    frame.width = width_;
    frame.height = height_;
    frame.rgba.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4);

    // Rows from the top, `pitch` bytes apart (negative for a bottom-up buffer).
    BYTE* top = nullptr;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer> buffer2d;
    BYTE* locked = nullptr;
    DWORD length = 0;
    const bool twoD = SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(buffer2d.put()))) &&
                      SUCCEEDED(buffer2d->Lock2D(&top, &pitch));
    if (!twoD) {
      if (FAILED(buffer->Lock(&locked, nullptr, &length))) {
        return false;
      }
      top = locked;
      pitch = width_ * 4;
      if (length < static_cast<DWORD>(pitch * height_)) {
        buffer->Unlock();
        return false;
      }
    }
    for (int y = 0; y < height_; ++y) {
      const BYTE* in = top + static_cast<std::ptrdiff_t>(y) * pitch;
      // MediaFrame rows go bottom up.
      auto* out = frame.rgba.data() + static_cast<std::size_t>(height_ - 1 - y) * static_cast<std::size_t>(width_) * 4;
      for (int x = 0; x < width_; ++x, in += 4, out += 4) {
        out[0] = in[2]; // RGB32 is B, G, R, X in memory
        out[1] = in[1];
        out[2] = in[0];
        out[3] = 255;
      }
    }
    if (twoD) {
      buffer2d->Unlock2D();
    } else {
      buffer->Unlock();
    }
    return true;
  }

  ComPtr<IMFSourceReader> reader_;
  bool comInitialised_ = false;
  int width_ = 0;
  int height_ = 0;
  double duration_ = 0.0;
};

} // namespace

std::unique_ptr<VideoDecoder> VideoDecoder::open(const juce::File& file, std::string& error) {
  auto decoder = std::make_unique<MediaFoundationDecoder>();
  if (!decoder->open(file, error)) {
    return nullptr;
  }
  return decoder;
}

bool VideoDecoder::supported() noexcept { return true; }

} // namespace milkdawp::engine
