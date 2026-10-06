// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 8.6d: cameras on Linux through V4L2, which JUCE's CameraDevice doesn't
// cover. Each open camera streams on a thread of its own (memory-mapped
// buffers); YUYV frames are converted there, MJPEG ones decoded with JUCE's
// JPEG reader, so the render thread only uploads.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <juce_graphics/juce_graphics.h>

#include "Cameras.h"

namespace milkdawp::engine::cameras {

namespace {

constexpr int kMaxDevices = 64;
constexpr int kBufferCount = 4;
constexpr int kMaxDimension = 1280;

int xioctl(int fd, unsigned long request, void* argument) {
  int result = 0;
  do {
    result = ioctl(fd, request, argument);
  } while (result == -1 && errno == EINTR);
  return result;
}

struct Device {
  juce::String name;
  juce::String path;
};

// Every /dev/video* that captures video by streaming. Names are the driver's
// card name; a second camera of the same model gets " (2)" added.
std::vector<Device> listDevices() {
  std::vector<Device> devices;
  for (int i = 0; i < kMaxDevices; ++i) {
    const auto path = "/dev/video" + juce::String(i);
    const int fd = ::open(path.toRawUTF8(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      continue;
    }
    v4l2_capability capability{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &capability) == 0) {
      const auto caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? capability.device_caps
                                                                              : capability.capabilities;
      if ((caps & V4L2_CAP_VIDEO_CAPTURE) != 0 && (caps & V4L2_CAP_STREAMING) != 0) {
        auto name = juce::String::fromUTF8(reinterpret_cast<const char*>(capability.card));
        juce::String unique = name;
        for (int n = 2; std::any_of(devices.begin(), devices.end(), [&](const Device& d) { return d.name == unique; });
             ++n) {
          unique = name + " (" + juce::String(n) + ")";
        }
        devices.push_back({unique, path});
      }
    }
    ::close(fd);
  }
  return devices;
}

/// One V4L2 camera, streaming on its own thread while it exists.
class V4l2Camera final : public MediaSource {
public:
  V4l2Camera(juce::String name, juce::String path) : name_(std::move(name)), path_(std::move(path)) {
    thread_ = std::thread([this] {
      juce::Thread::setCurrentThreadName("MilkDAWp camera");
      stream();
    });
  }

  ~V4l2Camera() override {
    stop_.store(true);
    if (thread_.joinable()) {
      thread_.join(); // at most one poll timeout (100 ms)
    }
  }

  std::shared_ptr<const MediaFrame> latestFrame(std::uint64_t& serial) const override {
    const std::lock_guard lock(mutex_);
    serial = serial_;
    return frame_;
  }

  std::string description() const override { return "Camera: " + name_.toStdString(); }

private:
  struct Mapped {
    void* start = MAP_FAILED;
    std::size_t length = 0;
  };

  void stream() {
    const int fd = ::open(path_.toRawUTF8(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      return;
    }
    // YUYV is the format every UVC webcam offers; MJPEG is the fallback.
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = 1280;
    format.fmt.pix.height = 720;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(fd, VIDIOC_S_FMT, &format) != 0 || format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
      format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
      if (xioctl(fd, VIDIOC_S_FMT, &format) != 0 || format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        ::close(fd);
        return;
      }
    }
    const int width = static_cast<int>(format.fmt.pix.width);
    const int height = static_cast<int>(format.fmt.pix.height);
    const int stride = std::max(static_cast<int>(format.fmt.pix.bytesperline), width * 2);
    const bool yuyv = format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV;

    v4l2_requestbuffers request{};
    request.count = kBufferCount;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    std::vector<Mapped> buffers;
    bool ok = xioctl(fd, VIDIOC_REQBUFS, &request) == 0 && request.count > 0;
    for (unsigned i = 0; ok && i < request.count; ++i) {
      v4l2_buffer buffer{};
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;
      buffer.index = i;
      ok = xioctl(fd, VIDIOC_QUERYBUF, &buffer) == 0;
      if (ok) {
        Mapped mapped;
        mapped.length = buffer.length;
        mapped.start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset);
        ok = mapped.start != MAP_FAILED;
        buffers.push_back(mapped);
        ok = ok && xioctl(fd, VIDIOC_QBUF, &buffer) == 0;
      }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ok = ok && xioctl(fd, VIDIOC_STREAMON, &type) == 0;

    while (ok && !stop_.load()) {
      pollfd waiting{fd, POLLIN, 0};
      const int ready = ::poll(&waiting, 1, 100);
      if (ready < 0 && errno != EINTR) {
        break; // unplugged
      }
      if (ready <= 0) {
        continue;
      }
      v4l2_buffer buffer{};
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;
      if (xioctl(fd, VIDIOC_DQBUF, &buffer) != 0) {
        if (errno == EAGAIN) {
          continue;
        }
        break;
      }
      const auto* data = static_cast<const std::uint8_t*>(buffers[buffer.index].start);
      MediaFrame frame;
      if (yuyv) {
        if (buffer.bytesused >= static_cast<unsigned>(stride * height)) {
          frame = frameFromYuyv(data, width, height, stride);
        }
      } else {
        // Some cameras leave the Huffman tables out of MJPEG frames; JUCE's
        // decoder can't read those, and they are skipped.
        frame = frameFromImage(juce::ImageFileFormat::loadFrom(data, buffer.bytesused), kMaxDimension);
      }
      if (frame.width > 0) {
        auto shared = std::make_shared<const MediaFrame>(std::move(frame));
        const std::lock_guard lock(mutex_);
        frame_ = std::move(shared);
        ++serial_;
      }
      if (xioctl(fd, VIDIOC_QBUF, &buffer) != 0) {
        break;
      }
    }

    xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (const auto& mapped : buffers) {
      if (mapped.start != MAP_FAILED) {
        munmap(mapped.start, mapped.length);
      }
    }
    ::close(fd);
  }

  juce::String name_;
  juce::String path_;
  std::atomic<bool> stop_{false};
  mutable std::mutex mutex_;
  std::shared_ptr<const MediaFrame> frame_;
  std::uint64_t serial_ = 0;
  std::thread thread_;
};

} // namespace

bool supported() noexcept { return true; }

juce::StringArray available() {
  juce::StringArray names;
  for (const auto& device : listDevices()) {
    names.add(device.name);
  }
  return names;
}

std::shared_ptr<MediaSource> open(const juce::String& name, std::string& error) {
  const auto devices = listDevices();
  const auto found =
      std::find_if(devices.begin(), devices.end(), [&](const Device& device) { return device.name == name; });
  if (found == devices.end()) {
    error = "camera \"" + name.toStdString() + "\" isn't connected";
    return nullptr;
  }
  static std::mutex mutex;
  static std::map<juce::String, std::weak_ptr<V4l2Camera>> cameras;
  const std::lock_guard lock(mutex);
  if (auto existing = cameras[name].lock()) {
    return existing;
  }
  auto camera = std::make_shared<V4l2Camera>(name, found->path);
  cameras[name] = camera;
  return camera;
}

} // namespace milkdawp::engine::cameras
