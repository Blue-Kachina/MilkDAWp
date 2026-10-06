// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 8.6b: cameras through JUCE's CameraDevice (Windows, macOS). Builds that
// have no JUCE camera (JUCE_USE_CAMERA off) report none.

#include <map>
#include <mutex>

#include <juce_events/juce_events.h>
#include <juce_video/juce_video.h>

#include "Cameras.h"

namespace milkdawp::engine::cameras {

#if defined(JUCE_USE_CAMERA) && JUCE_USE_CAMERA

namespace {

/// A camera's frames as a media source. JUCE delivers each frame on the
/// camera's own thread; it is converted there and handed over under a lock,
/// so the render thread only ever uploads finished frames. The device is
/// opened, and closed, on the message thread. One per camera in the process
/// (`open`): two instances showing the same camera share it.
class CameraMediaSource final : public MediaSource,
                                private juce::CameraDevice::Listener,
                                public std::enable_shared_from_this<CameraMediaSource> {
public:
  static constexpr int kMaxDimension = 1280;

  explicit CameraMediaSource(juce::String deviceName) : deviceName_(std::move(deviceName)) {}

  ~CameraMediaSource() override {
    if (device_ != nullptr) {
      device_->removeListener(this); // no frame arrives after this returns
      // The last reference may go on any thread (the render thread, say);
      // the device is closed where it was opened.
      juce::MessageManager::callAsync([device = std::shared_ptr<juce::CameraDevice>(std::move(device_))] {});
    }
  }

  /// Opens the device on the message thread (now, if this is it).
  void start() {
    auto openIt = [weak = weak_from_this()] {
      if (auto self = weak.lock()) {
        self->openNow();
      }
    };
    if (juce::MessageManager::existsAndIsCurrentThread()) {
      openIt();
    } else {
      juce::MessageManager::callAsync(std::move(openIt));
    }
  }

  std::shared_ptr<const MediaFrame> latestFrame(std::uint64_t& serial) const override {
    const std::lock_guard lock(mutex_);
    serial = serial_;
    return frame_;
  }

  std::string description() const override { return "Camera: " + deviceName_.toStdString(); }

private:
  void openNow() {
    if (device_ != nullptr) {
      return;
    }
    const int index = juce::CameraDevice::getAvailableDevices().indexOf(deviceName_);
    if (index < 0) {
      return; // unplugged since: stays empty
    }
    device_.reset(juce::CameraDevice::openDevice(index, 128, 64, 1920, 1080, false));
    if (device_ != nullptr) {
      device_->addListener(this);
    }
  }

  void imageReceived(const juce::Image& image) override {
    auto frame = std::make_shared<const MediaFrame>(frameFromImage(image, kMaxDimension));
    const std::lock_guard lock(mutex_);
    frame_ = std::move(frame);
    ++serial_;
  }

  juce::String deviceName_;
  std::unique_ptr<juce::CameraDevice> device_; // message thread
  mutable std::mutex mutex_;
  std::shared_ptr<const MediaFrame> frame_;
  std::uint64_t serial_ = 0;
};

} // namespace

bool supported() noexcept { return true; }

juce::StringArray available() { return juce::CameraDevice::getAvailableDevices(); }

std::shared_ptr<MediaSource> open(const juce::String& name, std::string& error) {
  if (!available().contains(name)) {
    error = "camera \"" + name.toStdString() + "\" isn't connected";
    return nullptr;
  }
  static std::mutex mutex;
  static std::map<juce::String, std::weak_ptr<CameraMediaSource>> cameras;
  const std::lock_guard lock(mutex);
  if (auto existing = cameras[name].lock()) {
    return existing;
  }
  auto camera = std::make_shared<CameraMediaSource>(name);
  cameras[name] = camera;
  camera->start();
  return camera;
}

#else

bool supported() noexcept { return false; }

juce::StringArray available() { return {}; }

std::shared_ptr<MediaSource> open(const juce::String& name, std::string& error) {
  error = "cameras aren't supported on this system (\"" + name.toStdString() + "\")";
  return nullptr;
}

#endif

} // namespace milkdawp::engine::cameras
