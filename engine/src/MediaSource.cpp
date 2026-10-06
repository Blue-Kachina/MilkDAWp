// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/MediaSource.h"

#include <algorithm>

#include <juce_graphics/juce_graphics.h>

#include "Cameras.h"
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

MediaFrame frameFromYuyv(const std::uint8_t* yuyv, int width, int height, int stride) {
  MediaFrame frame;
  if (yuyv == nullptr || width <= 0 || height <= 0 || stride < width * 2) {
    return frame;
  }
  frame.width = width;
  frame.height = height;
  frame.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
  const auto clamp = [](int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); };
  for (int y = 0; y < height; ++y) {
    const auto* in = yuyv + static_cast<std::ptrdiff_t>(y) * stride;
    auto* out = frame.rgba.data() + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4;
    for (int x = 0; x < width; x += 2, in += 4) { // two pixels share one U and V
      const int d = in[1] - 128; // U
      const int e = in[3] - 128; // V
      for (int k = 0; k < 2 && x + k < width; ++k) {
        const int c = in[k * 2] - 16; // Y0, Y1
        *out++ = clamp((298 * c + 409 * e + 128) >> 8);
        *out++ = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
        *out++ = clamp((298 * c + 516 * d + 128) >> 8);
        *out++ = 255;
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

bool camerasSupported() noexcept { return cameras::supported(); }

juce::StringArray availableCameras() { return cameras::available(); }

std::string cameraMediaPath(const juce::String& deviceName) {
  return std::string(kCameraPrefix) + deviceName.toStdString();
}

bool isCameraMediaPath(const std::string& path) noexcept { return path.rfind(kCameraPrefix, 0) == 0; }

juce::String cameraDeviceName(const std::string& path) {
  return isCameraMediaPath(path) ? juce::String::fromUTF8(path.c_str() + kCameraPrefix.size()) : juce::String();
}

// ---- paths --------------------------------------------------------------------

std::shared_ptr<MediaSource> openMediaSource(const std::string& path, std::string& error) {
  if (path.empty()) {
    return nullptr;
  }
  if (isCameraMediaPath(path)) {
    return cameras::open(cameraDeviceName(path), error);
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
  return "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm;*.ogv";
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
