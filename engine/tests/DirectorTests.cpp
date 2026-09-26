// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 2.6 end to end: director thread -> preset hand-off -> render thread
// loads the preset when the transition is due. Needs projectM and a GL
// context (the render thread only loads presets while it renders); without
// them it reports why and passes, like the other engine tests.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

#include "milkdawp/engine/Visualizer.h"

using namespace milkdawp;
using namespace std::chrono_literals;

namespace {

bool waitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 3000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return condition();
}

// A folder with the three fixture presets plus one file that is not a
// preset at all (pre-validation must skip it).
struct PresetFolder {
  juce::TemporaryFile holder;
  juce::File folder;

  PresetFolder() : folder(holder.getFile()) {
    folder.createDirectory();
    const auto source = juce::File(MILKDAWP_FIXTURES_DIR).getChildFile("presets");
    for (const char* name : {"mdw-border.milk", "mdw-feedback.milk", "mdw-wave.milk"}) {
      source.getChildFile(name).copyFileTo(folder.getChildFile(name));
    }
    folder.getChildFile("zz-broken.milk").replaceWithData("\0\0\0\0", 4);
  }
  ~PresetFolder() { folder.deleteRecursively(); }
};

// Feeds a steady stream of audio from a helper thread, like an audio
// callback would, so the director has hops to analyse.
struct AudioPump {
  engine::Visualizer& visualizer;
  std::atomic<bool> stop{false};
  std::thread thread;

  explicit AudioPump(engine::Visualizer& v) : visualizer(v) {
    thread = std::thread([this] {
      std::vector<float> left(480, 0.0f);
      std::vector<float> right(480, 0.0f);
      const float* channels[] = {left.data(), right.data()};
      double phase = 0.0;
      while (!stop.load()) {
        for (std::size_t i = 0; i < left.size(); ++i) {
          phase += 2.0 * 3.14159265358979 * 110.0 / 48000.0;
          left[i] = right[i] = static_cast<float>(0.3 * std::sin(phase));
        }
        visualizer.processAudio(channels, 2, 480, nullptr);
        std::this_thread::sleep_for(10ms);
      }
    });
  }
  ~AudioPump() {
    stop.store(true);
    thread.join();
  }
};

} // namespace

TEST_CASE("Director loads a preset folder, steps through it, and skips files that are not presets (2.6)",
          "[engine][Director]") {
  engine::Visualizer::Config config;
  engine::Visualizer visualizer(config);
  auto& render = visualizer.renderEngine();
  auto& director = visualizer.director();
  waitFor([&] { return render.isAvailable() || !render.unavailableReason().empty(); });
  if (!render.isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + render.unavailableReason());
    return;
  }

  // The render thread only works (and loads presets) while a surface shows.
  const int surface = render.registerSurface();
  render.reportSurfaceSize(surface, 320, 180, true);

  engine::EngineControls controls;
  controls.transitionMode = core::TransitionMode::Manual;
  controls.cutStyle = core::CutStyle::Hard;
  visualizer.setControls(controls);

  PresetFolder presets;
  director.setPresetFolder(presets.folder.getFullPathName().toStdString());

  REQUIRE(waitFor([&] { return director.status().playlistSize == 4; }));
  REQUIRE(waitFor([&] { return render.stats().presetsLoaded >= 1; }));
  CHECK(director.status().currentIndex == 0);
  CHECK(director.presetName(0) == "mdw-border");
  CHECK(juce::File(juce::String(director.currentPresetPath())).getFileName() == "mdw-border.milk");

  director.requestNext();
  REQUIRE(waitFor([&] { return render.stats().presetsLoaded >= 2; }));
  CHECK(director.status().currentIndex == 1);

  director.requestNext(); // index 2 (mdw-wave)
  REQUIRE(waitFor([&] { return render.stats().presetsLoaded >= 3; }));
  // Index 3 is the broken file: stepping past it wraps to index 0.
  director.requestNext();
  REQUIRE(waitFor([&] { return render.stats().presetsLoaded >= 4; }));
  CHECK(director.status().currentIndex == 0);
  CHECK(director.status().presetsSkipped >= 1);

  director.requestPrevious();
  REQUIRE(waitFor([&] { return render.stats().presetsLoaded >= 5; }));
  CHECK(director.status().currentIndex == 2); // history: back to where we were

  // The presetIndex control jumps directly.
  controls.presetIndex = 1;
  visualizer.setControls(controls);
  REQUIRE(waitFor([&] { return director.status().currentIndex == 1; }));

  render.unregisterSurface(surface);
}

TEST_CASE("Director advances automatically in Timed mode and not while locked (2.6)", "[engine][Director]") {
  engine::Visualizer::Config config;
  engine::Visualizer visualizer(config);
  auto& render = visualizer.renderEngine();
  auto& director = visualizer.director();
  waitFor([&] { return render.isAvailable() || !render.unavailableReason().empty(); });
  if (!render.isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + render.unavailableReason());
    return;
  }
  visualizer.prepare(48000.0, 480);
  const int surface = render.registerSurface();
  render.reportSurfaceSize(surface, 320, 180, true);

  engine::EngineControls controls;
  controls.transitionMode = core::TransitionMode::Timed;
  controls.timedDurationSeconds = 0.3f;
  controls.cutStyle = core::CutStyle::Hard;
  controls.locked = true;
  visualizer.setControls(controls);

  PresetFolder presets;
  director.setPresetFolder(presets.folder.getFullPathName().toStdString());
  AudioPump pump(visualizer);

  REQUIRE(waitFor([&] { return render.stats().presetsLoaded >= 1; }));
  std::this_thread::sleep_for(1000ms);
  CHECK(render.stats().presetsLoaded == 1); // locked: no automatic transitions

  controls.locked = false;
  visualizer.setControls(controls);
  CHECK(waitFor([&] { return render.stats().presetsLoaded >= 3; }, 4000ms));
  CHECK(render.stats().lastLandingErrorSamples >= 0);

  render.unregisterSurface(surface);
}
