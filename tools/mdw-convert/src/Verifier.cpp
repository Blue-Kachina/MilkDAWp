// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Verifier.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <filesystem>
#include <optional>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/core/MilkConvert.h"
#include "milkdawp/engine/GlFrameTarget.h"
#include "milkdawp/engine/OffscreenGLContext.h"
#include "milkdawp/engine/ProjectMInstance.h"
#include "milkdawp/engine/ProjectMLibrary.h"

namespace mdw_convert {

using namespace milkdawp;

namespace {

constexpr double kFps = 60.0;
constexpr int kSampleRate = 48000;

using Frame = std::vector<std::uint8_t>;

// Mean absolute per-channel difference of two frames, 0..255 (alpha left out).
double difference(const Frame& a, const Frame& b) {
  double sum = 0.0;
  std::size_t count = 0;
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
    if (i % 4 != 3) {
      sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
      ++count;
    }
  }
  return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

double meanDifference(const std::vector<Frame>& a, const std::vector<Frame>& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
    sum += difference(a[i], b[i]);
  }
  return a.empty() ? 0.0 : sum / static_cast<double>(a.size());
}

bool identical(const std::vector<Frame>& a, const std::vector<Frame>& b) { return a == b; }

std::string formatted(double value) {
  char text[32];
  std::snprintf(text, sizeof text, "%.2f", value);
  return text;
}

} // namespace

struct Verifier::Impl {
  Settings settings;
  std::unique_ptr<engine::ProjectMLibrary> library;
  std::unique_ptr<engine::OffscreenGLContext> context;
  std::unique_ptr<engine::GlFrameTarget> target;
  std::vector<std::string> textures; // names (no extension) in the texture folders

  ~Impl() {
    // GL objects before the context, on this (the owning) thread.
    target.reset();
    context.reset();
  }

  struct Render {
    std::vector<Frame> frames; // the second half: the first frames are mostly the same anyway
    std::optional<std::string> failure;
  };

  // A fresh instance each time, so audio smoothing, time and the feedback
  // buffer all start the same way for every render.
  // `frames` 0 only loads the preset (which compiles everything).
  Render render(const std::string& text, const std::array<float, core::kMacroCount>& macros, int frames) {
    Render out;
    engine::ProjectMInstance::Settings instanceSettings;
    instanceSettings.width = settings.width;
    instanceSettings.height = settings.height;
    instanceSettings.textureSearchPaths = settings.textureSearchPaths;
    std::string error;
    auto instance = engine::ProjectMInstance::create(*library, instanceSettings, error);
    if (!instance) {
      out.failure = "projectM wouldn't start: " + error;
      return out;
    }
    instance->setPresetSwitchFailedCallback(
        [](const char*, const char* message, void* userData) {
          static_cast<Render*>(userData)->failure = message != nullptr ? message : "rejected";
        },
        &out);

    // The engine sets these in every preset every frame; neutral here.
    for (std::size_t k = 0; k < macros.size(); ++k) {
      instance->setPresetVariable(core::kMacroVariables[k], macros[k]);
    }
    instance->setPresetVariable(core::kZoomVariable, 0.0);
    instance->setPresetVariable(core::kRotationVariable, 0.0);
    instance->setPresetVariable(core::kWarpVariable, 1.0);
    instance->setPresetVariable(core::kTrailsVariable, 0.0);
    instance->setPresetVariable(core::kDtVariable, 1.0 / kFps);

    // Black to start with, so nothing of the last render can show through.
    juce::gl::glBindFramebuffer(juce::gl::GL_FRAMEBUFFER, target->framebuffer());
    juce::gl::glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    juce::gl::glClear(juce::gl::GL_COLOR_BUFFER_BIT);
    juce::gl::glBindFramebuffer(juce::gl::GL_FRAMEBUFFER, 0);

    instance->loadPresetData(text.c_str(), /*smoothTransition=*/false);
    if (out.failure) {
      return out;
    }

    // Music-like audio: a bass note that pulses twice a second over a higher
    // tone, so bass/mid/treb and the beat detector all move.
    const int samplesPerFrame = static_cast<int>(kSampleRate / kFps);
    std::vector<float> pcm(static_cast<std::size_t>(samplesPerFrame) * 2);
    for (int n = 0; n < frames; ++n) {
      for (int i = 0; i < samplesPerFrame; ++i) {
        const double t = (n * samplesPerFrame + i) / static_cast<double>(kSampleRate);
        const double pulse = std::exp(-8.0 * std::fmod(t, 0.5));
        const double sample = 0.6 * pulse * std::sin(2.0 * 3.14159265358979 * 55.0 * t) +
                              0.2 * std::sin(2.0 * 3.14159265358979 * 1760.0 * t);
        pcm[static_cast<std::size_t>(i) * 2] = static_cast<float>(sample);
        pcm[static_cast<std::size_t>(i) * 2 + 1] = static_cast<float>(sample);
      }
      instance->addPcm(pcm.data(), static_cast<std::size_t>(samplesPerFrame), 2);
      instance->setFrameTime(n / kFps);
      instance->renderTo(target->framebuffer());
      if (n >= frames / 2) {
        juce::gl::glFinish();
        Frame rgba;
        target->readPixels(rgba);
        out.frames.push_back(std::move(rgba));
      }
    }
    instance.reset();
    return out;
  }
};

std::unique_ptr<Verifier> Verifier::create(Settings settings, std::string& error) {
  auto impl = std::make_unique<Impl>();
  impl->settings = std::move(settings);
  auto loaded = engine::ProjectMLibrary::load();
  if (!loaded.library) {
    error = loaded.unavailableReason;
    return nullptr;
  }
  impl->library = std::move(loaded.library);
  auto created = engine::OffscreenGLContext::create();
  if (!created.context) {
    error = created.error;
    return nullptr;
  }
  impl->context = std::move(created.context);
  juce::gl::loadFunctions();
  impl->target = std::make_unique<engine::GlFrameTarget>(impl->settings.width, impl->settings.height);
  for (const auto& folder : impl->settings.textureSearchPaths) {
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ignored)) {
      auto extension = entry.path().extension().string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (extension == ".jpg" || extension == ".png" || extension == ".tga" || extension == ".bmp" || extension == ".dds") {
        impl->textures.push_back(entry.path().stem().string());
      }
    }
  }
  // The first render in a process comes out differently from every later one
  // (something projectM or the driver sets up once), so get it out of the way.
  impl->render("[preset00]\nper_frame_1=wave_r = 0.5;\n", {}, impl->settings.frames);
  return std::unique_ptr<Verifier>(new Verifier(std::move(impl)));
}

Verifier::Verifier(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Verifier::~Verifier() = default;

Verifier::Result Verifier::check(const std::string& milk, const core::MilkdawpPreset& preset) {
  Result result;
  std::array<float, core::kMacroCount> neutral{};
  const auto defaults = core::macroDefaults(preset);
  for (std::size_t k = 0; k < neutral.size(); ++k) {
    neutral[k] = defaults[k].value_or(0.0f);
  }
  const int frames = impl_->settings.frames;

  // What will actually be played has to load.
  if (const auto loaded = impl_->render(core::compileForProjectM(preset), neutral, 0); loaded.failure) {
    const bool originalLoads = !impl_->render(milk, neutral, 0).failure;
    result.reason = std::string(originalLoads ? "projectM can't load the conversion: " : "projectM can't load it: ") +
                    *loaded.failure;
    return result;
  }

  // Compare with projectM's per-load randomness pinned, the same way on both
  // sides, so most presets can match exactly. If pinning breaks the shader,
  // compare as they are.
  auto pinned = preset;
  pinned.milk = core::withoutRandomness(milk, impl_->textures);
  auto first = impl_->render(pinned.milk, neutral, frames);
  if (first.failure) {
    pinned.milk = milk;
    first = impl_->render(milk, neutral, frames);
    if (first.failure) {
      result.reason = "projectM can't load it: " + *first.failure;
      return result;
    }
  }
  const auto compiled = core::compileForProjectM(pinned);
  auto converted = impl_->render(compiled, neutral, frames);
  if (converted.failure) {
    result.reason = "projectM can't load the conversion: " + *converted.failure;
    return result;
  }
  if (identical(first.frames, converted.frames)) {
    result.passed = true;
    result.exact = true;
    return result;
  }
  // Not identical: either what randomness is left (noisevol_ textures are
  // seeded from the clock) or a real difference. Render the original again to
  // see how far it strays from itself.
  std::vector<std::vector<Frame>> originals{std::move(first.frames)};
  std::vector<std::vector<Frame>> conversions{std::move(converted.frames)};
  const auto sample = [&](const std::string& text, std::vector<std::vector<Frame>>& into) {
    auto again = impl_->render(text, neutral, frames);
    if (again.failure) {
      result.reason = "projectM loaded it once but not again: " + *again.failure;
      return false;
    }
    into.push_back(std::move(again.frames));
    return true;
  };
  if (!sample(pinned.milk, originals)) {
    return result;
  }
  // Renders of the original that match each other prove nothing on their
  // own: in the full pack, several "always the same" presets turned out to
  // vary when sampled more. So no special case; with a spread of 0 the rule
  // below allows only the small floor, and more samples settle it.
  // The conversion is one more sample of the preset's randomness: some render
  // of it should be about as close to some render of the original as those
  // are to each other. More samples of both when it isn't clearly so.
  const auto judge = [&]() {
    double spread = 0.0;
    for (std::size_t i = 0; i < originals.size(); ++i) {
      for (std::size_t j = i + 1; j < originals.size(); ++j) {
        spread = std::max(spread, meanDifference(originals[i], originals[j]));
      }
    }
    double nearest = std::numeric_limits<double>::max();
    for (const auto& conversion : conversions) {
      for (const auto& original : originals) {
        nearest = std::min(nearest, meanDifference(original, conversion));
      }
    }
    result.original = spread;
    result.converted = nearest;
    // The small floor is for frames that differ in a handful of pixels; a
    // 2% wave or 0.2% zoom change on a steady preset is well above it.
    return nearest <= 1.5 * spread + 0.05;
  };
  for (int round = 0; !judge(); ++round) {
    if (round == 2) {
      result.reason = "renders differently: " + formatted(result.converted) + " from the original at the nearest (" +
                      std::to_string(conversions.size()) + " renders against " + std::to_string(originals.size()) +
                      "), which differs by up to " + formatted(result.original) + " from itself";
      return result;
    }
    if (!sample(pinned.milk, originals) || !sample(compiled, conversions)) {
      return result;
    }
  }
  result.passed = true;
  return result;
}

std::vector<double> Verifier::compare(const std::string& a, const std::string& b) {
  std::array<float, core::kMacroCount> neutral{};
  neutral.fill(0.5f);
  const auto pinnedA = core::withoutRandomness(a, impl_->textures);
  const auto pinnedB = core::withoutRandomness(b, impl_->textures);
  // a, b, a: shows whether order matters, as well as how far apart they are.
  const auto a1 = impl_->render(pinnedA, neutral, impl_->settings.frames);
  const auto b1 = impl_->render(pinnedB, neutral, impl_->settings.frames);
  const auto a2 = impl_->render(pinnedA, neutral, impl_->settings.frames);
  if (a1.failure || b1.failure || a2.failure) {
    return {};
  }
  return {meanDifference(a1.frames, a2.frames), meanDifference(a1.frames, b1.frames),
          meanDifference(a2.frames, b1.frames)};
}

} // namespace mdw_convert
