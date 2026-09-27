// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Windows WASAPI loopback capture (4.7): captures whatever the system's
// default output device is playing, the same way OBS/Discord "capture
// desktop audio" features do, since JUCE's own device backends do not
// expose loopback as an input. Windows needs no user consent for this (§4.7
// vs. macOS's Core Audio process taps, §4.8), so permissionState() only
// ever reports Unsupported (never applicable here) or Granted; failures are
// runtime errors surfaced through describe().
//
// All COM objects are created and used on one thread running in a
// multi-threaded apartment (captureThreadMain): WASAPI's client interfaces
// are not safe to hand across an apartment boundary, so nothing obtained
// here is touched from open()/close()/describe() except through the atomics
// and mutex below.

#include "SystemAudioCapture.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Defines the actual GUID constants (CLSID_MMDeviceEnumerator, IID_IAudio...)
// in this translation unit instead of only declaring them, so this file
// links without pulling in a separate GUID library.
#define INITGUID
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

namespace milkdawp::app {

namespace {

using Microsoft::WRL::ComPtr;

juce::String hresultText(const char* what, HRESULT hr) {
  return juce::String(what) + " (hr=0x" + juce::String::toHexString(static_cast<int>(hr)) + ")";
}

bool isFloatFormat(const WAVEFORMATEX& format) {
  if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    return true;
  }
  if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.cbSize >= 22) {
    const auto& ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
    return ext.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  }
  return false;
}

} // namespace

class WindowsSystemAudioCapture final : public SystemAudioCapture {
public:
  ~WindowsSystemAudioCapture() override { close(); }

  juce::String open(engine::Visualizer& visualizer) override {
    close();

    // A quick, self-contained existence check on the calling thread so
    // open() can report an immediate, specific error for the common case
    // (no active playback device) without touching any COM object the
    // worker thread will also use -- WASAPI clients must stay on one
    // apartment.
    {
      const bool comInitializedHere = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
      ComPtr<IMMDeviceEnumerator> enumerator;
      HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
      ComPtr<IMMDevice> device;
      if (SUCCEEDED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf());
      }
      if (comInitializedHere) {
        CoUninitialize();
      }
      if (FAILED(hr)) {
        setStatus("System audio: no active default audio output device to capture.");
        return "No active default audio output device to capture.";
      }
    }

    setStatus("System audio: starting...");
    stopRequested_.store(false, std::memory_order_release);
    thread_ = std::thread([this, &visualizer] { captureThreadMain(visualizer); });
    return {};
  }

  void close() override {
    stopRequested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
    running_.store(false, std::memory_order_release);
  }

  [[nodiscard]] bool isOpen() const override { return running_.load(std::memory_order_acquire); }

  [[nodiscard]] PermissionState permissionState() const override {
    // Windows loopback capture needs no user consent; only Granted/
    // Unsupported are meaningful here, and this file only compiles when the
    // platform is supported.
    return PermissionState::Granted;
  }

  [[nodiscard]] juce::String describe() const override {
    const std::scoped_lock lock(statusMutex_);
    return statusText_;
  }

  [[nodiscard]] float takePeak(PeakReader reader) noexcept override { return peaks_.take(reader); }

private:
  void setStatus(juce::String text) {
    const std::scoped_lock lock(statusMutex_);
    statusText_ = std::move(text);
  }

  void deliverPacket(engine::Visualizer& visualizer, const BYTE* data, UINT32 numFrames, DWORD flags,
                     const WAVEFORMATEX& format) {
    const int channels = std::max<int>(1, format.nChannels);
    if (leftScratch_.size() < numFrames) {
      leftScratch_.resize(numFrames);
      rightScratch_.resize(numFrames);
    }

    float peak = 0.0f;
    if (data == nullptr || (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
      std::fill_n(leftScratch_.begin(), numFrames, 0.0f);
      std::fill_n(rightScratch_.begin(), numFrames, 0.0f);
    } else if (isFloatFormat(format)) {
      const auto* samples = reinterpret_cast<const float*>(data);
      for (UINT32 i = 0; i < numFrames; ++i) {
        const float l = samples[static_cast<std::size_t>(i) * channels];
        const float r = channels > 1 ? samples[static_cast<std::size_t>(i) * channels + 1] : l;
        leftScratch_[i] = l;
        rightScratch_[i] = r;
        peak = std::max({peak, std::abs(l), std::abs(r)});
      }
    } else if (format.wBitsPerSample == 16) {
      const auto* samples = reinterpret_cast<const std::int16_t*>(data);
      constexpr float kScale = 1.0f / 32768.0f;
      for (UINT32 i = 0; i < numFrames; ++i) {
        const float l = static_cast<float>(samples[static_cast<std::size_t>(i) * channels]) * kScale;
        const float r =
            channels > 1 ? static_cast<float>(samples[static_cast<std::size_t>(i) * channels + 1]) * kScale : l;
        leftScratch_[i] = l;
        rightScratch_[i] = r;
        peak = std::max({peak, std::abs(l), std::abs(r)});
      }
    } else {
      // An unrecognised mix format: deliver silence rather than noise.
      std::fill_n(leftScratch_.begin(), numFrames, 0.0f);
      std::fill_n(rightScratch_.begin(), numFrames, 0.0f);
    }

    peaks_.report(peak);
    const float* channelPtrs[2] = {leftScratch_.data(), rightScratch_.data()};
    visualizer.processAudio(channelPtrs, 2, static_cast<int>(numFrames), nullptr);
  }

  void captureThreadMain(engine::Visualizer& visualizer) {
    const bool comInitializedHere = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    const auto fail = [&](juce::String text) {
      setStatus(std::move(text));
      running_.store(false, std::memory_order_release);
      if (comInitializedHere) {
        CoUninitialize();
      }
    };

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
      fail(hresultText("Could not create the audio device enumerator", hr));
      return;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf());
    if (FAILED(hr)) {
      fail("No active default audio output device to capture.");
      return;
    }

    ComPtr<IAudioClient> audioClient;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (FAILED(hr)) {
      fail(hresultText("Could not activate the audio client", hr));
      return;
    }

    WAVEFORMATEX* mixFormat = nullptr;
    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr) || mixFormat == nullptr) {
      fail(hresultText("Could not read the output device's mix format", hr));
      return;
    }

    constexpr REFERENCE_TIME kBufferDuration = 100 * 10'000; // 100 ms, in 100-ns units
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kBufferDuration, 0,
                                 mixFormat, nullptr);
    if (FAILED(hr)) {
      CoTaskMemFree(mixFormat);
      fail(hresultText("Could not initialise loopback capture", hr));
      return;
    }

    ComPtr<IAudioCaptureClient> captureClient;
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                 reinterpret_cast<void**>(captureClient.GetAddressOf()));
    if (FAILED(hr)) {
      CoTaskMemFree(mixFormat);
      fail(hresultText("Could not get the capture client", hr));
      return;
    }

    // Same path a device input feeds (AudioInput::audioDeviceAboutToStart):
    // sizes the engine's scratch buffer for this sample rate.
    visualizer.prepare(static_cast<double>(mixFormat->nSamplesPerSec), 8192);

    hr = audioClient->Start();
    if (FAILED(hr)) {
      CoTaskMemFree(mixFormat);
      fail(hresultText("Could not start loopback capture", hr));
      return;
    }

    setStatus("System audio (loopback)");
    running_.store(true, std::memory_order_release);

    while (!stopRequested_.load(std::memory_order_acquire)) {
      UINT32 packetLength = 0;
      hr = captureClient->GetNextPacketSize(&packetLength);
      while (SUCCEEDED(hr) && packetLength != 0) {
        BYTE* data = nullptr;
        UINT32 numFrames = 0;
        DWORD flags = 0;
        hr = captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
        if (FAILED(hr)) {
          break;
        }
        deliverPacket(visualizer, data, numFrames, flags, *mixFormat);
        captureClient->ReleaseBuffer(numFrames);
        hr = captureClient->GetNextPacketSize(&packetLength);
      }
      Sleep(10);
    }

    audioClient->Stop();
    CoTaskMemFree(mixFormat);
    running_.store(false, std::memory_order_release);
    if (comInitializedHere) {
      CoUninitialize();
    }
  }

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  SystemAudioPeakTracker peaks_;
  std::vector<float> leftScratch_;
  std::vector<float> rightScratch_;

  mutable std::mutex statusMutex_;
  juce::String statusText_{"System audio: not started"};
};

std::unique_ptr<SystemAudioCapture> createSystemAudioCapture() {
  return std::make_unique<WindowsSystemAudioCapture>();
}

} // namespace milkdawp::app
