// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 8.4: OSC dispatch to instances, without opening a socket.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <juce_events/juce_events.h>

#include "milkdawp/engine/OscRemote.h"

using namespace milkdawp;

namespace {

struct FakeInstance {
  std::string name;
  std::string id;
  std::vector<std::pair<std::string, float>> moves;
  int nexts = 0;

  engine::OscRemote::Endpoint endpoint() {
    engine::OscRemote::Endpoint e;
    e.name = [this] { return name; };
    e.id = [this] { return id; };
    e.setParameter = [this](const std::string& parameterId, float value, bool normalized) {
      if (parameterId == "noSuchParameter") {
        return false;
      }
      moves.emplace_back(parameterId + (normalized ? "/norm" : ""), value);
      return true;
    };
    e.next = [this] { ++nexts; };
    return e;
  }
};

} // namespace

TEST_CASE("OscRemote hands each message to the instances it addresses", "[engine][osc]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  engine::OscRemote remote;
  remote.apply({}); // off: no socket, dispatch still works
  CHECK(remote.statusText() == "Off");

  FakeInstance guitar{"Lead Guitar", "a1b2", {}, 0};
  FakeInstance drums{"Drums", "c3d4", {}, 0};
  const int guitarHandle = remote.add(guitar.endpoint());
  remote.add(drums.endpoint());

  CHECK(remote.dispatch("/milkdawp/lead_guitar/visualHue", {}, 90.0f, true) == 1);
  REQUIRE(guitar.moves.size() == 1);
  CHECK(guitar.moves[0] == std::pair<std::string, float>{"visualHue", 90.0f});
  CHECK(drums.moves.empty());

  CHECK(remote.dispatch("/milkdawp/visualGlow/norm", {}, 0.5f, true) == 2); // everyone
  CHECK(drums.moves.back() == std::pair<std::string, float>{"visualGlow/norm", 0.5f});

  CHECK(remote.dispatch("/milkdawp/c3d4/next", {}, 1.0f, true) == 1); // by id
  CHECK(remote.dispatch("/milkdawp/c3d4/next", {}, 0.0f, true) == 0); // a button's release
  CHECK(drums.nexts == 1);

  CHECK(remote.dispatch("/milkdawp/drums/noSuchParameter", {}, 1.0f, true) == 0);
  CHECK(remote.dispatch("/milkdawp/drums/visualHue", {}, 0.0f, false) == 0); // no value
  CHECK(remote.dispatch("/somebody/else", {}, 1.0f, true) == 0);

  remote.remove(guitarHandle);
  CHECK(remote.dispatch("/milkdawp/visualHue", {}, 10.0f, true) == 1);
}

TEST_CASE("OSC settings round-trip through their file", "[engine][osc]") {
  const auto file = juce::File::createTempFile(".json");
  core::OscSettings settings;
  settings.enabled = true;
  settings.receivePort = 8000;
  settings.sendHost = "192.168.1.20";
  settings.sendPort = 8001;
  REQUIRE(engine::saveOscSettings(settings, file));
  CHECK(engine::loadOscSettings(file) == settings);
  file.deleteFile();
  CHECK(engine::loadOscSettings(file) == core::OscSettings{}); // missing: defaults, off
}
