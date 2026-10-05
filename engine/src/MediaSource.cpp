// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/MediaSource.h"

#include <algorithm>
#include <map>

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_video/juce_video.h>

#include "milkdawp/engine/VideoDecoder.h"

namespace milkdawp::engine {

namespace {

constexpr std::string_view kCameraPrefix = "camera:";

} // namespace

MediaFrame frameFromImage(const juce::Image& source, int maxDimension) {
  MediaFrame frame;
  if (!source.isValid()) {
    return frame;
  }
  juce::Image image = source;
  const int longer = std::max(image.getWidth(), image.getHeight());
  if (longer > maxDimension) {
    const double scale = static_cast<double>(maxDimension) / longer;
    image = image.rescaled(std::max(1, static_cast<int>(image.getWidth() * scale)),
                           std::max(1, static_cast<int>(image.getHeight() * scale)),
                           juce::Graphics::mediumResamplingQuality);
  }
  frame.width = image.getWidth();
  frame.height = image.getHeight();
  frame.rgba.resize(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4);
  const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
  for (int y = 0; y < frame.height; ++y) {
    // GL's rows go bottom up.
    auto* out = frame.rgba.data() + static_cast<std::size_t>(frame.height - 1 - y) * static_cast<std::size_t>(frame.width) * 4;
    const auto* in = pixels.getLinePointer(y);
    for (int x = 0; x < frame.width; ++x, out += 4, in += pixels.pixelStride) {
      switch (pixels.pixelFormat) {
      case juce::Image::RGB: {
        const auto* p = reinterpret_cast<const juce::PixelRGB*>(in);
        out[0] = p->getRed();
        out[1] = p->getGreen();
        out[2] = p->getBlue();
        out[3] = 255;
        break;
      }
      case juce::Image::ARGB: {
        auto p = *reinterpret_cast<const juce::PixelARGB*>(in);
        p.unpremultiply();
        out[0] = p.getRed();
        out[1] = p.getGreen();
        out[2] = p.getBlue();
        out[3] = p.getAlpha();
        break;
      }
      case juce::Image::SingleChannel:
      case juce::Image::UnknownFormat:
      default:
        out[0] = out[1] = out[2] = *in;
        out[3] = 255;
        break;
      }
    }
  }
  return frame;
}

// ---- images -------------------------------------------------------------------

ImageMediaSource::ImageMediaSource(MediaFrame frame, std::string description)
    : frame_(std::make_shared<const MediaFrame>(std::move(frame))), description_(std::move(description)) {}

std::shared_ptr<const MediaFrame> ImageMediaSource::latestFrame(std::uint64_t& serial) const {
  serial = frame_ != nullptr && frame_->width > 0 ? 1 : 0; // a still image never changes
  return frame_;
}

std::shared_ptr<ImageMediaSource> ImageMediaSource::load(const juce::File& file, std::string& error) {
  const auto image = juce::ImageFileFormat::loadFrom(file);
  if (!image.isValid()) {
    error = "can't read \"" + file.getFileName().toStdString() + "\" as an image";
    return nullptr;
  }
  return std::make_shared<ImageMediaSource>(frameFromImage(image, kMaxDimension), file.getFileName().toStdString());
}

// ---- cameras ------------------------------------------------------------------

bool camerasSupported() noexcept { return JUCE_USE_CAMERA != 0; }

std::string cameraMediaPath(const juce::String& deviceName) {
  return std::string(kCameraPrefix) + deviceName.toStdString();
}

bool isCameraMediaPath(const std::string& path) noexcept { return path.rfind(kCameraPrefix, 0) == 0; }

juce::String cameraDeviceName(const std::string& path) {
  return isCameraMediaPath(path) ? juce::String::fromUTF8(path.c_str() + kCameraPrefix.size()) : juce::String();
}

#if JUCE_USE_CAMERA

juce::StringArray availableCameras() { return juce::CameraDevice::getAvailableDevices(); }

namespace {

/// A camera's frames as a media source. JUCE delivers each frame on the
/// camera's own thread; it is converted there and handed over under a lock,
/// so the render thread only ever uploads finished frames. The device is
/// opened, and closed, on the message thread. One per camera in the process
/// (`sharedCamera`): two instances showing the same camera share it.
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
    auto open = [weak = weak_from_this()] {
      if (auto self = weak.lock()) {
        self->openNow();
      }
    };
    if (juce::MessageManager::existsAndIsCurrentThread()) {
      open();
    } else {
      juce::MessageManager::callAsync(std::move(open));
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

std::shared_ptr<MediaSource> sharedCamera(const juce::String& deviceName) {
  static std::mutex mutex;
  static std::map<juce::String, std::weak_ptr<CameraMediaSource>> cameras;
  const std::lock_guard lock(mutex);
  if (auto existing = cameras[deviceName].lock()) {
    return existing;
  }
  auto camera = std::make_shared<CameraMediaSource>(deviceName);
  cameras[deviceName] = camera;
  camera->start();
  return camera;
}

} // namespace

#else

juce::StringArray availableCameras() { return {}; }

#endif

// ---- paths --------------------------------------------------------------------

std::shared_ptr<MediaSource> openMediaSource(const std::string& path, std::string& error) {
  if (path.empty()) {
    return nullptr;
  }
  if (isCameraMediaPath(path)) {
    const auto name = juce::String::fromUTF8(path.c_str() + kCameraPrefix.size());
#if JUCE_USE_CAMERA
    if (!availableCameras().contains(name)) {
      error = "camera \"" + name.toStdString() + "\" isn't connected";
      return nullptr;
    }
    return sharedCamera(name);
#else
    error = "cameras aren't supported on this platform yet (\"" + name.toStdString() + "\")";
    return nullptr;
#endif
  }
  const juce::File file(juce::String::fromUTF8(path.c_str()));
  if (!file.existsAsFile()) {
    error = "\"" + path + "\" is missing";
    return nullptr;
  }
  if (isVideoPath(path)) {
    if (!VideoDecoder::supported()) {
      error = "video isn't supported on this system yet (\"" + file.getFileName().toStdString() + "\")";
      return nullptr;
    }
    auto video = std::make_shared<VideoMediaSource>(file);
    // Opening takes a moment; waiting for it lets a file that can't play say so now.
    if (!video->waitUntilOpen(3000)) {
      error = video->error().empty() ? "\"" + file.getFileName().toStdString() + "\" took too long to open"
                                     : video->error();
      return nullptr;
    }
    return video;
  }
  return ImageMediaSource::load(file, error);
}

juce::String mediaFileWildcard() {
  return "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm";
}

juce::String mediaSourceDisplayName(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  if (isCameraMediaPath(path)) {
    return "Camera: " + juce::String::fromUTF8(path.c_str() + kCameraPrefix.size());
  }
  return juce::File(juce::String::fromUTF8(path.c_str())).getFileName();
}

UvScale coverUvScale(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight) noexcept {
  if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
    return {};
  }
  const float source = static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight);
  const float target = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
  if (source > target) {
    return {target / source, 1.0f}; // wider than the frame: show its middle, full height
  }
  return {1.0f, source / target};
}

} // namespace milkdawp::engine
