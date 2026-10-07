// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/MilkdawpPreset.h"

using namespace milkdawp::core;

namespace {

const std::string kMilk = "MILKDROP_PRESET_VERSION=201\n"
                          "[preset00]\n"
                          "fDecay=0.98\n"
                          "zoom=1.01\n"
                          "per_frame_1=wave_r = 0.5 + 0.5*sin(time);\n"
                          "per_frame_2=rot = rot + 0.01;\n"
                          "per_pixel_1=zoom = zoom + rad*0.01;\n";

const std::string kExtension = "mdw_format=1\n"
                               "mdw_title=Mindblob [mash-up]\n"
                               "mdw_source=Fractal/Flexi - mindblob.milk\n"
                               "mdw_source_sha256=4f1c\n"
                               "mdw_tags=fractal, warm,,slow\n"
                               "mdw_renderer=projectm          ; projectm | eyescream\n"
                               "mdw_ctl_1_name=Swirl\n"
                               "mdw_ctl_1_slot=macro1\n"
                               "mdw_ctl_1_target=rot\n"
                               "mdw_ctl_1_mode=rate\n"
                               "mdw_ctl_1_min=-0.2\n"
                               "mdw_ctl_1_max=0.2\n"
                               "mdw_ctl_1_default=0\n"
                               "mdw_ctl_2_name=Blob colour\n"
                               "mdw_ctl_2_slot=macro2\n"
                               "mdw_ctl_2_mode=expr\n"
                               "mdw_ctl_2_code=ob_r = ob_r * (1 - mdw_m2) + mdw_m2; ob_b = ob_b * (1 - mdw_m2);\n";

// The extension lines dropped into the plain preset right after [preset00].
std::string withExtension(const std::string& milk, const std::string& extension) {
  auto text = milk;
  const auto header = text.find("[preset00]\n") + 11;
  text.insert(header, extension);
  return text;
}

// The text after `key=` on the line that starts with `key`, or "".
std::string lineValue(const std::string& text, const std::string& key) {
  const auto at = text.find("\n" + key + "=");
  if (at == std::string::npos) {
    return {};
  }
  const auto start = at + key.size() + 2;
  return text.substr(start, text.find('\n', start) - start);
}

} // namespace

TEST_CASE("MilkdawpPreset: a plain .milk is a preset with no controls", "[core][milkdawp]") {
  const auto result = parseMilkdawp(kMilk);
  CHECK(result.problems.empty());
  CHECK(result.preset.milk == kMilk);
  CHECK(result.preset.controls.empty());
  CHECK(macroDefaults(result.preset) == MacroDefaults{});
}

TEST_CASE("MilkdawpPreset: taking the mdw_ lines out gives the original byte for byte", "[core][milkdawp]") {
  // Anywhere in the file, any case, CRLF, no final line break, and a key
  // separated by a space (as projectM and MilkDrop also accept).
  const std::string original = "MILKDROP_PRESET_VERSION=201\r\n[preset00]\r\nzoom=1.01\r\n\r\n"
                               "per_frame_1=x = 1;\r\nper_frame_2=y = 2;";
  const std::string text = "MDW_FORMAT=1\r\nMILKDROP_PRESET_VERSION=201\r\n[preset00]\r\nzoom=1.01\r\n"
                           "mdw_title Spaced\r\n\r\nper_frame_1=x = 1;\r\nMdw_Tags=a\r\nper_frame_2=y = 2;";
  const auto result = parseMilkdawp(text);
  CHECK(result.preset.milk == original);
  CHECK(result.preset.title == "Spaced");
  CHECK(result.preset.tags == std::vector<std::string>{"a"});
  CHECK(result.problems.empty());
}

TEST_CASE("MilkdawpPreset: reads the metadata and controls", "[core][milkdawp]") {
  const auto result = parseMilkdawp(withExtension(kMilk, kExtension));
  INFO(result.problems.size());
  CHECK(result.problems.empty());
  const auto& preset = result.preset;
  CHECK(preset.milk == kMilk);
  CHECK(preset.format == 1);
  CHECK(preset.title == "Mindblob [mash-up]");
  CHECK(preset.source == "Fractal/Flexi - mindblob.milk");
  CHECK(preset.sourceSha256 == "4f1c");
  CHECK(preset.tags == std::vector<std::string>{"fractal", "warm", "slow"});
  CHECK(preset.renderer == "projectm");
  REQUIRE(preset.controls.size() == 2);

  const auto& swirl = preset.controls[0];
  CHECK(swirl.number == 1);
  CHECK(swirl.name == "Swirl");
  CHECK(swirl.macro == 0);
  CHECK(swirl.mode == ControlMode::Rate);
  CHECK(swirl.stage == ControlStage::PerFrame);
  CHECK(swirl.target == "rot");
  CHECK(swirl.min == -0.2f);
  CHECK(swirl.max == 0.2f);
  CHECK(swirl.defaultValue == 0.0f);

  const auto& blob = preset.controls[1];
  CHECK(blob.macro == 1);
  CHECK(blob.mode == ControlMode::Expression);
  CHECK(blob.code == "ob_r = ob_r * (1 - mdw_m2) + mdw_m2; ob_b = ob_b * (1 - mdw_m2);");

  const auto defaults = macroDefaults(preset);
  REQUIRE(defaults[0].has_value());
  CHECK(*defaults[0] == 0.5f); // 0 in -0.2..0.2
  REQUIRE(defaults[1].has_value());
  CHECK(*defaults[1] == 0.5f); // expr: -1..1 unless set, default 0
  CHECK_FALSE(defaults[2].has_value());
  const auto names = macroNames(preset);
  CHECK(names[0] == "Swirl");
  CHECK(names[1] == "Blob colour");
  CHECK(names[2].empty());
}

TEST_CASE("MilkdawpPreset: saving and reading back gives the same preset", "[core][milkdawp]") {
  auto preset = parseMilkdawp(withExtension(kMilk, kExtension)).preset;
  PresetControl pixel;
  pixel.number = 7;
  pixel.name = "Bulge";
  pixel.macro = 5;
  pixel.mode = ControlMode::Replace;
  pixel.stage = ControlStage::PerPixel;
  pixel.target = "zoom";
  pixel.min = 0.0f;
  pixel.max = 1.0f;
  pixel.defaultValue = 0.0f;
  pixel.value = 1.3333334f;
  preset.controls.push_back(pixel);
  preset.otherKeys.emplace_back("mdw_fx_1", "glow,amount:0.35");

  const auto saved = serializeMilkdawp(preset);
  const auto reread = parseMilkdawp(saved);
  CHECK(reread.preset == preset);
  CHECK(serializeMilkdawp(reread.preset) == saved);
  // The block sits right after [preset00], so MilkDrop 1 (which reads that INI
  // section) sees it, and the rest is the original.
  CHECK(saved.rfind("MILKDROP_PRESET_VERSION=201\n[preset00]\nmdw_format=1\n", 0) == 0);
  CHECK(reread.preset.milk == kMilk);
}

TEST_CASE("MilkdawpPreset: without a [preset00] line the block goes first, in the file's line endings",
          "[core][milkdawp]") {
  MilkdawpPreset preset;
  preset.milk = "zoom=1\r\nper_frame_1=x=1;\r\n";
  const auto saved = serializeMilkdawp(preset);
  CHECK(saved.rfind("mdw_format=1\r\nmdw_renderer=projectm\r\nzoom=1\r\n", 0) == 0);
  CHECK(parseMilkdawp(saved).preset == preset);
}

TEST_CASE("MilkdawpPreset: broken controls are left out with a reason; the preset still loads", "[core][milkdawp]") {
  const std::string extension = "mdw_format=1\n"
                                "mdw_ctl_1_slot=macro9\n"
                                "mdw_ctl_1_mode=offset\n"
                                "mdw_ctl_1_target=zoom\n"
                                "mdw_ctl_2_slot=macro2\n"
                                "mdw_ctl_2_mode=wobble\n"
                                "mdw_ctl_2_target=zoom\n"
                                "mdw_ctl_3_slot=macro3\n"
                                "mdw_ctl_3_mode=scale\n"
                                "mdw_ctl_3_target=2bad\n"
                                "mdw_ctl_4_slot=macro4\n"
                                "mdw_ctl_4_mode=replace\n"
                                "mdw_ctl_4_target=ob_r\n"
                                "mdw_ctl_5_slot=macro5\n"
                                "mdw_ctl_5_mode=offset\n"
                                "mdw_ctl_5_target=rot\n"
                                "mdw_ctl_5_min=0.5\n"
                                "mdw_ctl_5_max=0.5\n"
                                "mdw_ctl_6_slot=macro6\n"
                                "mdw_ctl_6_mode=expr\n"
                                "mdw_ctl_7_slot=macro7\n"
                                "mdw_ctl_7_mode=offset\n"
                                "mdw_ctl_7_target=rot\n"
                                "mdw_ctl_7_max=lots\n"
                                "mdw_ctl_7_default=5\n"
                                "mdw_ctl_7_nmae=Typo\n"
                                "mdw_ctl_7_slot=macro1\n"
                                "mdw_mystery=1\n";
  const auto result = parseMilkdawp(withExtension(kMilk, extension));
  CHECK(result.preset.milk == kMilk);
  // Only control 7 survives: its bad max falls back to 1, its default is clamped.
  REQUIRE(result.preset.controls.size() == 1);
  const auto& survivor = result.preset.controls[0];
  CHECK(survivor.number == 7);
  CHECK(survivor.macro == 6); // the first slot line wins
  CHECK(survivor.min == -1.0f);
  CHECK(survivor.max == 1.0f);
  CHECK(survivor.defaultValue == 1.0f);
  CHECK(survivor.name == "Macro 7");
  // 6 left out, plus: bad max, clamped default, unknown field, duplicate slot, unknown key.
  CHECK(result.problems.size() == 11);
  const auto mentions = [&result](const std::string& text) {
    return std::any_of(result.problems.begin(), result.problems.end(),
                       [&text](const std::string& p) { return p.find(text) != std::string::npos; });
  };
  CHECK(mentions("Control 1 left out"));
  CHECK(mentions("Control 6 left out: an expr control needs code"));
  CHECK(mentions("mdw_ctl_7_slot appears more than once"));
  CHECK(mentions("mdw_mystery is not used"));
  // Unknown keys are kept for saving.
  CHECK(result.preset.otherKeys == std::vector<std::pair<std::string, std::string>>{{"mdw_ctl_7_nmae", "Typo"},
                                                                                    {"mdw_mystery", "1"}});
}

TEST_CASE("MilkdawpPreset: format versions", "[core][milkdawp]") {
  SECTION("missing: read as this version, with a note") {
    const auto result = parseMilkdawp(withExtension(kMilk, "mdw_title=x\n"));
    CHECK(result.preset.format == kMilkdawpFormatVersion);
    CHECK(result.problems.size() == 1);
  }
  SECTION("newer: one note, unknown keys kept without complaint") {
    const auto result = parseMilkdawp(withExtension(kMilk, "mdw_format=7\nmdw_hologram=on\nmdw_title=Future\n"));
    CHECK(result.preset.format == 7);
    CHECK(result.preset.title == "Future");
    CHECK(result.problems.size() == 1);
    CHECK(result.preset.otherKeys.size() == 1);
    CHECK(serializeMilkdawp(result.preset).find("mdw_format=7\n") != std::string::npos);
  }
}

TEST_CASE("MilkdawpPreset: controls compile to lines after the preset's own code", "[core][milkdawp]") {
  const auto preset = parseMilkdawp(withExtension(kMilk, kExtension)).preset;
  const auto compiled = compileForProjectM(preset);

  // The plain preset comes first, untouched, and no mdw_ line reaches projectM.
  CHECK(compiled.rfind(kMilk, 0) == 0);
  CHECK(compiled.find("\nmdw_") == std::string::npos);

  // Controls first, then the Visual globals, numbered on from the preset's own.
  const auto swirl = lineValue(compiled, "per_frame_3");
  CHECK(swirl.rfind("rot = rot + mdw_dt * ", 0) == 0);
  CHECK(swirl.find("mdw_m1") != std::string::npos);
  const auto blob = lineValue(compiled, "per_frame_4");
  CHECK(blob.rfind("mdw_c2 = ", 0) == 0);
  CHECK(blob.find("ob_r = ob_r * (1 - mdw_m2) + mdw_m2;") != std::string::npos);
  CHECK(lineValue(compiled, "per_frame_5").find("mdw_zoom") != std::string::npos);
  CHECK(lineValue(compiled, "per_frame_6").find("mdw_rot") != std::string::npos);
  CHECK(lineValue(compiled, "per_frame_7").find("mdw_warp") != std::string::npos);
  CHECK(lineValue(compiled, "per_frame_8").find("mdw_trails") != std::string::npos);
  CHECK(lineValue(compiled, "per_frame_9").empty());
  // No per-pixel control: the per-pixel code is left alone.
  CHECK(lineValue(compiled, "per_pixel_2").empty());
}

TEST_CASE("MilkdawpPreset: a Macro on its default leaves the target exactly as the preset set it",
          "[core][milkdawp]") {
  // c = default + span x (mdw_mK - macroDefault): the literal subtracted must
  // be exactly the double the engine sets for the Macro's default (a float),
  // so the difference is 0 and the line is `x + 0`, `x * 1` or `x + (v-x) * 0`.
  for (const char* range : {"mdw_ctl_1_min=-0.3\nmdw_ctl_1_max=0.7\nmdw_ctl_1_default=0.1\n",
                            "mdw_ctl_1_min=0\nmdw_ctl_1_max=3\nmdw_ctl_1_default=1\n"}) {
    const auto preset = parseMilkdawp(withExtension(
                                          kMilk, std::string("mdw_format=1\nmdw_ctl_1_slot=macro3\nmdw_ctl_1_mode=scale\n"
                                                             "mdw_ctl_1_target=zoom\n") +
                                                     range))
                            .preset;
    REQUIRE(preset.controls.size() == 1);
    const auto line = lineValue(compileForProjectM(preset), "per_frame_3");
    INFO(line);
    const auto at = line.find("mdw_m3 - (");
    REQUIRE(at != std::string::npos);
    const double subtracted = std::stod(line.substr(at + 10));
    CHECK(subtracted == static_cast<double>(*macroDefaults(preset)[2]));
    // ...and what's added back is the control's default, written exactly too.
    const auto start = line.find("* ((") + 4;
    CHECK(std::stod(line.substr(start)) == static_cast<double>(preset.controls[0].defaultValue));
  }
}

TEST_CASE("MilkdawpPreset: compiling keeps the preset's code as projectM reads it", "[core][milkdawp]") {
  SECTION("per-pixel controls go after the per-pixel code") {
    const auto preset = parseMilkdawp(withExtension(kMilk, "mdw_format=1\nmdw_ctl_1_slot=macro1\n"
                                                           "mdw_ctl_1_mode=offset\nmdw_ctl_1_target=rot\n"
                                                           "mdw_ctl_1_stage=pixel\n"))
                            .preset;
    const auto compiled = compileForProjectM(preset);
    CHECK(lineValue(compiled, "per_pixel_2").rfind("rot = rot + ", 0) == 0);
    CHECK(lineValue(compiled, "per_frame_3").find("mdw_zoom") != std::string::npos);
  }
  SECTION("lines after a gap in the numbering stay unread") {
    // projectM stops at per_frame_3; ours fill 3.., so the old 4 is renamed
    // rather than suddenly being read.
    MilkdawpPreset preset;
    preset.milk = "per_frame_1=a=1;\nper_frame_2=b=2;\nper_frame_4=c=3;\nper_frame_07=d=4;\n";
    const auto compiled = compileForProjectM(preset);
    CHECK(compiled.find("\nmdw_unread_per_frame_4=c=3;\n") != std::string::npos);
    CHECK(compiled.find("\nper_frame_07=d=4;\n") != std::string::npos); // never read, never collides
    CHECK(lineValue(compiled, "per_frame_3").find("mdw_zoom") != std::string::npos);
    CHECK(lineValue(compiled, "per_frame_4").find("mdw_rot") != std::string::npos);
  }
  SECTION("a last statement without ';' gets one before ours") {
    MilkdawpPreset open;
    open.milk = "per_frame_1=a = 1;\nper_frame_2=`b = 2 // no semicolon\n";
    CHECK(lineValue(compileForProjectM(open), "per_frame_3").rfind("; zoom", 0) == 0);
    MilkdawpPreset closed;
    closed.milk = "per_frame_1=a = 1; /* done */\n";
    CHECK(lineValue(compileForProjectM(closed), "per_frame_2").rfind("zoom", 0) == 0);
    MilkdawpPreset none;
    none.milk = "zoom=1"; // no line break at the end either
    const auto compiled = compileForProjectM(none);
    CHECK(compiled.rfind("zoom=1\nper_frame_1=zoom = ", 0) == 0);
  }
}

TEST_CASE("MilkdawpPreset: file names", "[core][milkdawp]") {
  CHECK(hasMilkdawpExtension("a/b/Flexi.milkdawp"));
  CHECK(hasMilkdawpExtension("FLEXI.MILKDAWP"));
  CHECK_FALSE(hasMilkdawpExtension("Flexi.milk"));
  CHECK_FALSE(hasMilkdawpExtension("milkdawp"));
  CHECK(isMilkdawpLine("mdw_title=x"));
  CHECK(isMilkdawpLine("MDW_x 1"));
  CHECK_FALSE(isMilkdawpLine(" mdw_title=x")); // projectM and MilkDrop don't read it as a key either
  CHECK_FALSE(isMilkdawpLine("per_frame_1=mdw_m1 = 2;"));
  CHECK_FALSE(isMilkdawpLine("mdw"));
}
