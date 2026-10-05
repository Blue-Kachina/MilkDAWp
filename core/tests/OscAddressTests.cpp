// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/OscAddress.h"

using namespace milkdawp::core;
using Catch::Approx;

TEST_CASE("parseOscAddress reads MilkDAWp's addresses", "[core][osc]") {
  SECTION("a parameter on one instance") {
    const auto c = parseOscAddress("/milkdawp/lead_guitar/visualHue");
    REQUIRE(c);
    CHECK(c->kind == OscCommand::Kind::Parameter);
    CHECK(c->instance == "lead_guitar");
    CHECK(c->parameterId == "visualHue");
    CHECK_FALSE(c->normalized);
  }
  SECTION("normalised") {
    const auto c = parseOscAddress("/milkdawp/drums/visualGlow/norm");
    REQUIRE(c);
    CHECK(c->instance == "drums");
    CHECK(c->parameterId == "visualGlow");
    CHECK(c->normalized);
  }
  SECTION("every instance") {
    for (const auto* address : {"/milkdawp/visualHue", "/milkdawp/*/visualHue", "/milkdawp/all/visualHue"}) {
      const auto c = parseOscAddress(address);
      REQUIRE(c);
      CHECK(c->instance.empty());
      CHECK(c->parameterId == "visualHue");
    }
    const auto norm = parseOscAddress("/milkdawp/visualHue/norm");
    REQUIRE(norm);
    CHECK(norm->instance.empty());
    CHECK(norm->normalized);
  }
  SECTION("next and previous") {
    CHECK(parseOscAddress("/milkdawp/next")->kind == OscCommand::Kind::Next);
    CHECK(parseOscAddress("/milkdawp/drums/prev")->kind == OscCommand::Kind::Previous);
    CHECK(parseOscAddress("/milkdawp/drums/previous")->kind == OscCommand::Kind::Previous);
    CHECK_FALSE(parseOscAddress("/milkdawp/drums/next/norm"));
  }
  SECTION("not ours") {
    CHECK_FALSE(parseOscAddress("/other/visualHue"));
    CHECK_FALSE(parseOscAddress("/milkdawp"));
    CHECK_FALSE(parseOscAddress("/milkdawp/"));
    CHECK_FALSE(parseOscAddress("/milkdawp/a/b/c"));
  }
}

TEST_CASE("OSC instance names and matching", "[core][osc]") {
  CHECK(oscName("Lead Guitar") == "lead_guitar");
  CHECK(oscName("Kick (sub)") == "kick__sub_");
  CHECK(oscName("") == "");

  OscCommand c;
  CHECK(oscAddresses(c, "Anything", "id1")); // no instance: everyone
  c.instance = "lead_guitar";
  CHECK(oscAddresses(c, "Lead Guitar", "id1"));
  CHECK_FALSE(oscAddresses(c, "Drums", "id1"));
  c.instance = "Lead Guitar"; // a client that sends the name as is
  CHECK(oscAddresses(c, "Lead Guitar", "id1"));
  c.instance = "id1";
  CHECK(oscAddresses(c, "Drums", "id1"));
}

TEST_CASE("plainFromNormalized matches the host's ranges", "[core][osc]") {
  const auto& params = allParameters();
  CHECK(plainFromNormalized(*findParameter(params, "visualHue"), 0.5f) == Approx(0.0f));
  CHECK(plainFromNormalized(*findParameter(params, "visualHue"), 1.0f) == Approx(180.0f));
  // Speed is skewed so the middle of the travel is 1.
  CHECK(plainFromNormalized(*findParameter(params, "visualSpeed"), 0.5f) == Approx(1.0f));
  CHECK(plainFromNormalized(*findParameter(params, "visualSpeed"), 1.0f) == Approx(4.0f));
  CHECK(plainFromNormalized(*findParameter(params, "layerGateRelease"), 0.5f) == Approx(250.0f));
  // Choices round to an index.
  CHECK(plainFromNormalized(*findParameter(params, "visualMirror"), 0.4f) == 1.0f);
  CHECK(plainFromNormalized(*findParameter(params, "visualMirror"), 2.0f) == 3.0f); // clamped
}
