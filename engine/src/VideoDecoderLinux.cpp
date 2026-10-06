// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 8.6d: video on Linux through the system's GStreamer, loaded at run time
// (dlopen) so MilkDAWp neither links nor ships it: no GStreamer, no video,
// and a plain message saying so. Whatever formats the installed plugins
// decode play (H.264 needs gst-libav or the "ugly"/"bad" sets).
//
// Only a handful of GStreamer calls are used, declared here by hand; the few
// struct fields read (a buffer's timestamp, a message's type, a map's data)
// follow GStreamer 1.x's stable ABI.

#include "milkdawp/engine/VideoDecoder.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace milkdawp::engine {

namespace {

// ---- the slice of GStreamer's C API this uses ------------------------------------

using gboolean = int;
using gint64 = std::int64_t;
using guint64 = std::uint64_t;
struct GError {
  std::uint32_t domain;
  int code;
  char* message;
};
struct GstElement;
struct GstBus;
struct GstSample;
struct GstCaps;
struct GstStructure;

// GstMiniObject and the parts of GstBuffer / GstMessage after it (1.x ABI).
struct MiniObjectAbi {
  std::size_t type;
  int refcount;
  int lockstate;
  unsigned flags;
  void* copy;
  void* dispose;
  void* free;
  unsigned privUint;
  void* privPointer;
};
struct BufferAbi {
  MiniObjectAbi miniObject;
  void* pool;
  guint64 pts;
};
struct MessageAbi {
  MiniObjectAbi miniObject;
  int type;
};
struct MapInfoAbi {
  void* memory;
  int flags;
  std::uint8_t* data;
  std::size_t size;
  std::size_t maxsize;
  void* userData[4];
  void* reserved[4];
};

constexpr int kStateNull = 1;
constexpr int kStatePaused = 3;
constexpr int kStatePlaying = 4;
constexpr int kStateChangeFailure = 0;
constexpr int kFormatTime = 3;
constexpr int kSeekFlush = 1 << 0;
constexpr int kSeekKeyUnit = 1 << 2;
constexpr int kMapRead = 1;
constexpr int kMessageEos = 1 << 0;
constexpr int kMessageError = 1 << 1;
constexpr guint64 kClockTimeNone = ~guint64{0};
constexpr double kNanoseconds = 1.0e9;

struct Gst {
  gboolean (*initCheck)(int*, char***, GError**) = nullptr;
  GstElement* (*parseLaunch)(const char*, GError**) = nullptr;
  GstElement* (*binGetByName)(GstElement*, const char*) = nullptr;
  int (*elementSetState)(GstElement*, int) = nullptr;
  int (*elementGetState)(GstElement*, int*, int*, guint64) = nullptr;
  gboolean (*elementQueryDuration)(GstElement*, int, gint64*) = nullptr;
  gboolean (*elementSeekSimple)(GstElement*, int, int, gint64) = nullptr;
  GstBus* (*elementGetBus)(GstElement*) = nullptr;
  MessageAbi* (*busTimedPopFiltered)(GstBus*, guint64, int) = nullptr;
  void (*objectUnref)(void*) = nullptr;
  void (*miniObjectUnref)(void*) = nullptr;
  BufferAbi* (*sampleGetBuffer)(GstSample*) = nullptr;
  GstCaps* (*sampleGetCaps)(GstSample*) = nullptr;
  GstStructure* (*capsGetStructure)(const GstCaps*, unsigned) = nullptr;
  gboolean (*structureGetInt)(const GstStructure*, const char*, int*) = nullptr;
  gboolean (*bufferMap)(BufferAbi*, MapInfoAbi*, int) = nullptr;
  void (*bufferUnmap)(BufferAbi*, MapInfoAbi*) = nullptr;
  GstSample* (*appSinkTryPullSample)(GstElement*, guint64) = nullptr;
  void (*objectSet)(void*, const char*, ...) = nullptr;
  void (*errorFree)(GError*) = nullptr;
  bool ok = false;
};

template <typename F> bool load(void* library, const char* name, F& function) {
  function = reinterpret_cast<F>(dlsym(library, name));
  return function != nullptr;
}

// Loaded and initialised once per process; never unloaded (GStreamer can't be
// shut down and started again).
const Gst& gst() {
  static const Gst loaded = [] {
    Gst g;
    void* core = dlopen("libgstreamer-1.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    void* app = dlopen("libgstapp-1.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    void* gobject = dlopen("libgobject-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    void* glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (core == nullptr || app == nullptr || gobject == nullptr || glib == nullptr) {
      return g;
    }
    bool ok = load(core, "gst_init_check", g.initCheck) && load(core, "gst_parse_launch", g.parseLaunch) &&
              load(core, "gst_bin_get_by_name", g.binGetByName) &&
              load(core, "gst_element_set_state", g.elementSetState) &&
              load(core, "gst_element_get_state", g.elementGetState) &&
              load(core, "gst_element_query_duration", g.elementQueryDuration) &&
              load(core, "gst_element_seek_simple", g.elementSeekSimple) &&
              load(core, "gst_element_get_bus", g.elementGetBus) &&
              load(core, "gst_bus_timed_pop_filtered", g.busTimedPopFiltered) &&
              load(core, "gst_object_unref", g.objectUnref) &&
              load(core, "gst_mini_object_unref", g.miniObjectUnref) &&
              load(core, "gst_sample_get_buffer", g.sampleGetBuffer) &&
              load(core, "gst_sample_get_caps", g.sampleGetCaps) &&
              load(core, "gst_caps_get_structure", g.capsGetStructure) &&
              load(core, "gst_structure_get_int", g.structureGetInt) && load(core, "gst_buffer_map", g.bufferMap) &&
              load(core, "gst_buffer_unmap", g.bufferUnmap) &&
              load(app, "gst_app_sink_try_pull_sample", g.appSinkTryPullSample) &&
              load(gobject, "g_object_set", g.objectSet) && load(glib, "g_error_free", g.errorFree);
    GError* error = nullptr;
    ok = ok && g.initCheck(nullptr, nullptr, &error) != 0;
    if (error != nullptr) {
      g.errorFree(error);
    }
    g.ok = ok;
    return g;
  }();
  return loaded;
}

// Builds a pipeline from a description; the error text, if it fails.
GstElement* parse(const Gst& g, const char* description, std::string& error) {
  GError* parseError = nullptr;
  GstElement* pipeline = g.parseLaunch(description, &parseError);
  if (parseError != nullptr) {
    error = parseError->message != nullptr ? parseError->message : "GStreamer couldn't build the pipeline";
    g.errorFree(parseError);
  }
  return pipeline;
}

class GStreamerDecoder final : public VideoDecoder {
public:
  ~GStreamerDecoder() override {
    const auto& g = gst();
    if (pipeline_ != nullptr) {
      g.elementSetState(pipeline_, kStateNull);
    }
    if (sink_ != nullptr) {
      g.objectUnref(sink_);
    }
    if (pipeline_ != nullptr) {
      g.objectUnref(pipeline_);
    }
  }

  bool open(const juce::File& file, std::string& error) {
    const auto& g = gst();
    pipeline_ = parse(g,
                      "filesrc name=src ! decodebin ! videoconvert ! video/x-raw,format=RGBA ! "
                      "appsink name=sink sync=false max-buffers=4 enable-last-sample=false",
                      error);
    if (pipeline_ == nullptr) {
      return false;
    }
    GstElement* source = g.binGetByName(pipeline_, "src");
    sink_ = g.binGetByName(pipeline_, "sink");
    if (source == nullptr || sink_ == nullptr) {
      error = "GStreamer's pipeline is missing a part";
      if (source != nullptr) {
        g.objectUnref(source);
      }
      return false;
    }
    g.objectSet(source, "location", file.getFullPathName().toRawUTF8(), nullptr);
    g.objectUnref(source);

    const auto cannot = "GStreamer can't play \"" + file.getFileName().toStdString() +
                        "\" (a decoder for its format may not be installed)";
    if (g.elementSetState(pipeline_, kStatePaused) == kStateChangeFailure) {
      error = cannot;
      return false;
    }
    int state = 0;
    if (g.elementGetState(pipeline_, &state, nullptr, static_cast<guint64>(5 * kNanoseconds)) == kStateChangeFailure ||
        state != kStatePaused) {
      error = cannot;
      return false;
    }
    gint64 duration = 0;
    if (g.elementQueryDuration(pipeline_, kFormatTime, &duration) != 0 && duration > 0) {
      duration_ = static_cast<double>(duration) / kNanoseconds;
    }
    if (g.elementSetState(pipeline_, kStatePlaying) == kStateChangeFailure) {
      error = cannot;
      return false;
    }
    return true;
  }

  double durationSeconds() const override { return duration_; }

  bool next(MediaFrame& frame, double& timestampSeconds) override {
    const auto& g = gst();
    GstSample* sample = g.appSinkTryPullSample(sink_, static_cast<guint64>(2 * kNanoseconds));
    if (sample == nullptr) {
      return false; // the end, or stuck
    }
    bool ok = false;
    BufferAbi* buffer = g.sampleGetBuffer(sample);
    const GstCaps* caps = g.sampleGetCaps(sample);
    int width = 0;
    int height = 0;
    if (buffer != nullptr && caps != nullptr) {
      const GstStructure* structure = g.capsGetStructure(caps, 0);
      g.structureGetInt(structure, "width", &width);
      g.structureGetInt(structure, "height", &height);
    }
    MapInfoAbi map{};
    if (width > 0 && height > 0 && g.bufferMap(buffer, &map, kMapRead) != 0) {
      const auto stride = static_cast<std::size_t>(map.size) / static_cast<std::size_t>(height);
      if (stride >= static_cast<std::size_t>(width) * 4) {
        frame.width = width;
        frame.height = height;
        frame.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        for (int y = 0; y < height; ++y) {
          // GStreamer's rows go top down; MediaFrame's bottom up.
          std::memcpy(frame.rgba.data() + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4,
                      map.data + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(width) * 4);
        }
        timestampSeconds = buffer->pts != kClockTimeNone ? static_cast<double>(buffer->pts) / kNanoseconds : 0.0;
        ok = true;
      }
      g.bufferUnmap(buffer, &map);
    }
    g.miniObjectUnref(sample);
    return ok;
  }

  bool seek(double seconds) override {
    return gst().elementSeekSimple(pipeline_, kFormatTime, kSeekFlush | kSeekKeyUnit,
                                   static_cast<gint64>(std::max(seconds, 0.0) * kNanoseconds)) != 0;
  }

private:
  GstElement* pipeline_ = nullptr;
  GstElement* sink_ = nullptr;
  double duration_ = 0.0;
};

} // namespace

std::unique_ptr<VideoDecoder> VideoDecoder::open(const juce::File& file, std::string& error) {
  if (!gst().ok) {
    error = "video needs GStreamer, which isn't installed (\"" + file.getFileName().toStdString() + "\")";
    return nullptr;
  }
  auto decoder = std::make_unique<GStreamerDecoder>();
  if (!decoder->open(file, error)) {
    return nullptr;
  }
  return decoder;
}

bool VideoDecoder::supported() noexcept { return gst().ok; }

juce::File VideoDecoder::writeTestClip(const juce::File& directory) {
  const auto& g = gst();
  const auto file = directory.getNonexistentChildFile("milkdawp-test-clip", ".avi");
  if (!g.ok) {
    return file;
  }
  std::string error;
  GstElement* pipeline =
      parse(g,
            "concat name=c ! videoconvert ! jpegenc ! avimux ! filesink name=out "
            "videotestsrc pattern=red num-buffers=10 ! video/x-raw,format=I420,width=160,height=96,framerate=10/1 ! c. "
            "videotestsrc pattern=blue num-buffers=10 ! video/x-raw,format=I420,width=160,height=96,framerate=10/1 ! c.",
            error);
  if (pipeline == nullptr) {
    return file;
  }
  if (GstElement* out = g.binGetByName(pipeline, "out")) {
    g.objectSet(out, "location", file.getFullPathName().toRawUTF8(), nullptr);
    g.objectUnref(out);
    g.elementSetState(pipeline, kStatePlaying);
    GstBus* bus = g.elementGetBus(pipeline);
    MessageAbi* message = g.busTimedPopFiltered(bus, static_cast<guint64>(20 * kNanoseconds), kMessageEos | kMessageError);
    const bool finished = message != nullptr && message->type == kMessageEos;
    if (message != nullptr) {
      g.miniObjectUnref(message);
    }
    g.objectUnref(bus);
    g.elementSetState(pipeline, kStateNull);
    if (!finished) {
      file.deleteFile();
    }
  }
  g.objectUnref(pipeline);
  return file;
}

} // namespace milkdawp::engine
