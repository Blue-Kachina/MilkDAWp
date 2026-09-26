// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "milkdawp/engine/PresetHandoff.h"

using namespace milkdawp::engine;

TEST_CASE("PresetHandoff delivers offered text to the render side by presetId", "[engine][PresetHandoff]") {
  PresetHandoff handoff;
  REQUIRE(handoff.offer(7, "[preset00]\nzoom=1.0\n"));
  CHECK_FALSE(handoff.find(7).has_value()); // not received yet

  handoff.receive();
  const auto slot = handoff.find(7);
  REQUIRE(slot.has_value());
  CHECK(std::string(handoff.text(*slot)) == "[preset00]\nzoom=1.0\n");
  CHECK_FALSE(handoff.find(8).has_value());

  handoff.release(*slot);
  CHECK_FALSE(handoff.find(7).has_value());
}

TEST_CASE("PresetHandoff recycles released slots indefinitely", "[engine][PresetHandoff]") {
  PresetHandoff handoff;
  for (std::uint32_t id = 1; id <= 100; ++id) {
    REQUIRE(handoff.offer(id, "preset " + std::to_string(id)));
    handoff.receive();
    const auto slot = handoff.find(id);
    REQUIRE(slot.has_value());
    CHECK(std::string(handoff.text(*slot)) == "preset " + std::to_string(id));
    handoff.release(*slot);
  }
}

TEST_CASE("PresetHandoff frees the oldest held slot so offers never deadlock", "[engine][PresetHandoff]") {
  // Transitions that never fire (superseded) leave slots held on the render
  // side. The director must still be able to offer the next preset.
  PresetHandoff handoff;
  for (std::uint32_t id = 1; id <= 20; ++id) {
    INFO("offer " << id);
    CHECK(handoff.offer(id, "preset"));
    handoff.receive(); // render side receives but never releases
  }
  CHECK(handoff.find(20).has_value());
  CHECK_FALSE(handoff.find(1).has_value());
}

TEST_CASE("PresetHandoff reports failures back in order", "[engine][PresetHandoff]") {
  PresetHandoff handoff;
  CHECK_FALSE(handoff.popFailure().has_value());
  handoff.reportFailure(3);
  handoff.reportFailure(9);
  CHECK(handoff.popFailure() == 3U);
  CHECK(handoff.popFailure() == 9U);
  CHECK_FALSE(handoff.popFailure().has_value());
}
