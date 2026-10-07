// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/MilkConvert.h"
#include "milkdawp/core/Sha256.h"

using namespace milkdawp::core;

namespace {

std::vector<std::string> macroNameList(const MilkdawpPreset& preset) {
  std::vector<std::string> names;
  for (const auto& name : macroNames(preset)) {
    if (!name.empty()) {
      names.push_back(name);
    }
  }
  return names;
}

bool has(const std::vector<std::string>& names, const std::string& name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

std::ptrdiff_t indexOf(const std::vector<std::string>& names, const std::string& name) {
  return std::find(names.begin(), names.end(), name) - names.begin();
}

// A folder of presets in the temp directory, removed afterwards.
struct TempTree {
  std::filesystem::path root = std::filesystem::temp_directory_path() /
                               std::filesystem::path("milkdawp_convert_test_" + std::to_string(std::random_device{}()));
  TempTree() { std::filesystem::create_directories(root); }
  ~TempTree() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  TempTree(const TempTree&) = delete;
  TempTree& operator=(const TempTree&) = delete;

  void write(const std::filesystem::path& relative, const std::string& text) const {
    std::filesystem::create_directories((root / relative).parent_path());
    std::ofstream(root / relative, std::ios::binary) << text;
  }
  [[nodiscard]] std::string read(const std::filesystem::path& relative) const {
    std::ifstream in(root / relative, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }
  // Every file, by relative path, with its bytes.
  [[nodiscard]] std::map<std::string, std::string> snapshot() const {
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
      if (entry.is_regular_file()) {
        files[entry.path().lexically_relative(root).generic_string()] =
            read(entry.path().lexically_relative(root));
      }
    }
    return files;
  }
};

const std::string kPlain = "[preset00]\nfDecay=0.98\nzoom=1.01\n";

} // namespace

TEST_CASE("analyzeMilk reads the header, what per-frame code writes, and the q-variables read later (8.11)",
          "[core][MilkConvert]") {
  const auto analysis = analyzeMilk("[preset00]\r\n"
                                    "fDecay=0.950000\r\n"
                                    "ZOOM=1.01\r\n"
                                    "zoom=2\r\n" // the first one counts
                                    "per_frame_init_1=q9 = 1;\r\n"
                                    "per_frame_1=wave_r = 0.5; dx += 0.01; // ob_a = 1;\r\n"
                                    "per_frame_2=q1 = bass*0.5; x = (q2 == 3); /* cx = 1 */\r\n"
                                    "per_frame_4=never_read = 1;\r\n" // after a gap
                                    "per_pixel_1=zoom = zoom + q3*rad;\r\n"
                                    "comp_1=`shader_body { ret = _qb.x; }\r\n");
  CHECK(analysis.header.at("fdecay") == 0.95f);
  CHECK(analysis.header.at("zoom") == 1.01f);
  CHECK(analysis.hasPerFrameCode);
  CHECK(analysis.hasShaders);
  CHECK(analysis.frameWrites == std::set<std::string>{"q9", "wave_r", "dx", "q1", "x"});
  CHECK(analysis.frameAssignments.at("q1") == "bass*0.5");
  CHECK(analysis.qReads == std::set<std::string>{"q3", "q5", "q6", "q7", "q8"});
}

TEST_CASE("convertMilk keeps the preset byte for byte and names it from its path (8.11)", "[core][MilkConvert]") {
  const std::string milk = "MILKDROP_PRESET_VERSION=201\r\n[preset00]\r\nfDecay=0.98\r\nper_frame_1=dx = 0.01\r\n";
  const auto preset = convertMilk(milk, "! Transition/Fast, Bright/Someone - A preset.milk");
  CHECK(preset.milk == milk);
  CHECK(preset.title == "Someone - A preset");
  CHECK(preset.source == "! Transition/Fast, Bright/Someone - A preset.milk");
  CHECK(preset.sourceSha256 == sha256Hex(milk));
  CHECK(preset.tags == std::vector<std::string>{"transition", "fast bright"});
  CHECK(preset.renderer == "projectm");

  // It reads back as the same preset with nothing to complain about, and
  // without its mdw_ lines it is the original file.
  const auto text = serializeMilkdawp(preset);
  const auto back = parseMilkdawp(text);
  CHECK(back.problems.empty());
  CHECK(back.preset == preset);
  CHECK(back.preset.milk == milk);
}

TEST_CASE("convertMilk: every Macro starts centred and every control is neutral there (8.11)",
          "[core][MilkConvert]") {
  const auto preset = convertMilk("[preset00]\nper_frame_1=q1 = bass; ob_a = 0.5; sy = 1.01;\n"
                                  "per_pixel_1=rot = rot + q1;\n",
                                  "a.milk");
  REQUIRE_FALSE(preset.controls.empty());
  for (const auto& defaultValue : macroDefaults(preset)) {
    if (defaultValue) {
      CHECK(*defaultValue == 0.5f);
    }
  }
  for (const auto& control : preset.controls) {
    INFO(control.name << " -> " << control.target);
    CHECK(control.stage == ControlStage::PerFrame);
    CHECK(control.defaultValue == (control.mode == ControlMode::Scale ? 1.0f : 0.0f));
    CHECK((control.mode == ControlMode::Scale || control.mode == ControlMode::Offset));
  }
}

TEST_CASE("convertMilk proposes what is particular to the preset first, then fills with motion (8.11)",
          "[core][MilkConvert]") {
  SECTION("a preset with no code of its own gets the motion every preset has, and its wave") {
    const auto names = macroNameList(convertMilk(kPlain, "a.milk"));
    CHECK(names == std::vector<std::string>{"Wave", "Drift X", "Drift Y", "Centre X", "Centre Y", "Squash"});
  }

  SECTION("a hidden wave, border or echo gets no Macro unless the code animates it") {
    const auto names = macroNameList(convertMilk("[preset00]\nfWaveAlpha=0.001\nob_size=0.1\nob_a=0\n"
                                                 "fVideoEchoAlpha=0.5\n",
                                                 "a.milk"));
    CHECK_FALSE(has(names, "Wave"));
    CHECK_FALSE(has(names, "Outer Border"));
    CHECK(has(names, "Echo"));
  }

  SECTION("audio-driven q-variables read further on come first; phases and selectors are left alone") {
    const auto names = macroNameList(convertMilk("[preset00]\nfWaveAlpha=0\n"
                                                 "per_frame_1=q1 = time*0.3;\n"         // a phase
                                                 "per_frame_2=q2 = int(rand(4));\n"     // a selector
                                                 "per_frame_3=q3 = 0.5 + bass_att*0.2;\n"
                                                 "per_frame_4=q4 = 0.7;\n"
                                                 "per_frame_5=q5 = treb;\n"             // never read
                                                 "per_frame_6=ib_a = 0.3 * mid;\n"
                                                 "comp_1=`shader_body { ret = q1+q2+q3+q4; }\n",
                                                 "a.milk"));
    REQUIRE(names.size() >= 3);
    CHECK(names[0] == "q3");
    CHECK(names[1] == "Inner Border"); // animated by the code
    CHECK(names[2] == "q4");
    CHECK_FALSE(has(names, "q1"));
    CHECK_FALSE(has(names, "q2"));
    CHECK_FALSE(has(names, "q5"));
  }

  SECTION("what the code animates comes before motion it doesn't touch") {
    const auto names = macroNameList(convertMilk("[preset00]\nper_frame_1=cy = 0.5 + 0.1*sin(time);\n", "a.milk"));
    CHECK(indexOf(names, "Centre Y") < indexOf(names, "Drift X"));
  }

  SECTION("never more than 8 Macros, and the Squash Macro moves sx and sy opposite ways") {
    const auto preset = convertMilk("[preset00]\nob_a=1\nib_a=1\nmv_a=1\nfVideoEchoAlpha=0.5\n"
                                    "per_frame_1=q1 = bass; q2 = mid; q3 = treb; q4 = vol;\n"
                                    "per_pixel_1=x = q1+q2+q3+q4;\n",
                                    "a.milk");
    const auto names = macroNameList(preset);
    CHECK(names.size() == 8);
    CHECK(std::count_if(names.begin(), names.end(), [](const std::string& n) { return n[0] == 'q'; }) == 3);
    const auto squash = convertMilk(kPlain, "a.milk");
    std::vector<PresetControl> onSquash;
    std::copy_if(squash.controls.begin(), squash.controls.end(), std::back_inserter(onSquash),
                 [](const PresetControl& c) { return c.name == "Squash"; });
    REQUIRE(onSquash.size() == 2);
    CHECK(onSquash[0].macro == onSquash[1].macro);
    CHECK(onSquash[0].target == "sx");
    CHECK(onSquash[1].target == "sy");
    CHECK(onSquash[0].max == -onSquash[1].max);
  }
}

TEST_CASE("convertFolder writes .milkdawp files beside the presets and never changes a .milk (8.11)",
          "[core][MilkConvert]") {
  TempTree tree;
  tree.write("Fractal/one.milk", kPlain);
  tree.write("Fractal/Deep/two.MILK", "[preset00]\r\nper_frame_1=dx = 0.01;\r\n");
  tree.write("three.milk", kPlain);
  tree.write("three.milkdawp", "hand made"); // already there
  tree.write("excluded.milk", kPlain);
  tree.write("binary.milk", std::string("\x01\x00\x02", 3));
  tree.write("converted.milk", kPlain + "mdw_format=1\n");
  tree.write("notes.txt", "not a preset");
  const auto before = tree.snapshot();

  ConvertOptions options;
  options.exclude = {"excluded.milk"};
  std::size_t calls = 0;
  options.progress = [&calls](std::size_t done, std::size_t total) {
    ++calls;
    CHECK(done <= total);
  };
  const auto entries = convertFolder(tree.root, options);
  CHECK(calls == entries.size());

  std::map<std::string, ConvertOutcome> outcomes;
  for (const auto& entry : entries) {
    outcomes[entry.relativePath] = entry.outcome;
  }
  CHECK(outcomes == std::map<std::string, ConvertOutcome>{{"Fractal/one.milk", ConvertOutcome::Written},
                                                          {"Fractal/Deep/two.MILK", ConvertOutcome::Written},
                                                          {"three.milk", ConvertOutcome::Kept},
                                                          {"excluded.milk", ConvertOutcome::Excluded},
                                                          {"binary.milk", ConvertOutcome::Failed},
                                                          {"converted.milk", ConvertOutcome::Failed}});
  CHECK(std::is_sorted(entries.begin(), entries.end(),
                       [](const ConvertEntry& a, const ConvertEntry& b) { return a.relativePath < b.relativePath; }));

  // Every file that was there is byte for byte as it was; the only new files are .milkdawp.
  const auto after = tree.snapshot();
  for (const auto& [path, bytes] : before) {
    INFO(path);
    REQUIRE(after.count(path) == 1);
    CHECK(after.at(path) == bytes);
  }
  std::vector<std::string> added;
  for (const auto& [path, bytes] : after) {
    if (before.count(path) == 0) {
      added.push_back(path);
    }
  }
  CHECK(added == std::vector<std::string>{"Fractal/Deep/two.milkdawp", "Fractal/one.milkdawp"});

  const auto two = parseMilkdawp(tree.read("Fractal/Deep/two.milkdawp"));
  CHECK(two.problems.empty());
  CHECK(two.preset.milk == before.at("Fractal/Deep/two.MILK"));
  CHECK(two.preset.source == "Fractal/Deep/two.MILK");
  CHECK(two.preset.tags == std::vector<std::string>{"fractal", "deep"});

  SECTION("run again with overwrite: what's there is already right, and the hand-made one is replaced") {
    options.overwrite = true;
    for (const auto& entry : convertFolder(tree.root, options)) {
      if (entry.relativePath == "Fractal/one.milk" || entry.relativePath == "Fractal/Deep/two.MILK") {
        CHECK(entry.outcome == ConvertOutcome::Unchanged);
      } else if (entry.relativePath == "three.milk") {
        CHECK(entry.outcome == ConvertOutcome::Written);
      }
    }
    CHECK(parseMilkdawp(tree.read("three.milkdawp")).preset.milk == kPlain);
  }

  SECTION("excluding a preset later takes away its .milkdawp when asked, and nothing else") {
    options.exclude = {"Fractal/one.milk"};
    options.pruneExcluded = true;
    (void)convertFolder(tree.root, options);
    const auto pruned = tree.snapshot();
    CHECK(pruned.count("Fractal/one.milkdawp") == 0);
    CHECK(pruned.at("Fractal/one.milk") == kPlain);
    CHECK(pruned.count("Fractal/Deep/two.milkdawp") == 1);
    CHECK(pruned.at("three.milkdawp") == "hand made");
  }

  SECTION("a dry run and a rejecting check write nothing") {
    TempTree other;
    other.write("a.milk", kPlain);
    other.write("b.milk", kPlain);
    ConvertOptions dry;
    dry.dryRun = true;
    for (const auto& entry : convertFolder(other.root, dry)) {
      CHECK(entry.outcome == ConvertOutcome::Written);
      CHECK_FALSE(entry.controls.empty());
    }
    ConvertOptions checked;
    checked.verify = [](const std::filesystem::path& path, const MilkdawpPreset& preset) {
      CHECK(path.extension() == ".milk");
      CHECK_FALSE(preset.controls.empty());
      return path.filename() == "a.milk" ? std::string("looks different") : std::string{};
    };
    const auto results = convertFolder(other.root, checked);
    REQUIRE(results.size() == 2);
    CHECK(results[0].outcome == ConvertOutcome::Rejected);
    CHECK(results[0].detail == "looks different");
    CHECK(results[1].outcome == ConvertOutcome::Written);
    CHECK(other.snapshot().size() == 3); // a.milk, b.milk, b.milkdawp
  }
}

TEST_CASE("convertFolder can write into another folder, with the same layout (8.11)", "[core][MilkConvert]") {
  TempTree source;
  source.write("Fractal/Deep/one.milk", kPlain);
  source.write("two.milk", kPlain);
  source.write("skip.milk", kPlain);
  const auto before = source.snapshot();
  TempTree pack;

  ConvertOptions options;
  options.outputRoot = pack.root / "Converted";
  options.exclude = {"skip.milk"};
  const auto entries = convertFolder(source.root, options);
  REQUIRE(entries.size() == 3);
  CHECK(source.snapshot() == before); // nothing new beside the originals
  const auto written = pack.snapshot();
  CHECK(written.size() == 2);
  REQUIRE(written.count("Converted/Fractal/Deep/one.milkdawp") == 1);
  CHECK(written.count("Converted/two.milkdawp") == 1);
  const auto one = parseMilkdawp(written.at("Converted/Fractal/Deep/one.milkdawp"));
  CHECK(one.preset.milk == kPlain);
  CHECK(one.preset.source == "Fractal/Deep/one.milk");
}

TEST_CASE("convertFolder takes a single file too (8.11)", "[core][MilkConvert]") {
  TempTree tree;
  tree.write("Sub/a.milk", kPlain);
  tree.write("Sub/b.milk", kPlain);
  const auto entries = convertFolder(tree.root / "Sub" / "a.milk", {});
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].relativePath == "a.milk");
  CHECK(entries[0].outcome == ConvertOutcome::Written);
  CHECK(tree.snapshot().size() == 3);
}

TEST_CASE("withoutRandomness pins projectM's per-load randomness, for comparing renders (8.11)",
          "[core][MilkConvert]") {
  const std::vector<std::string> textures{"Clouds", "smalltiled_b", "smalltiled_a", "bad name", "zebra"};
  const std::string milk = "[preset00]\r\n"
                           "rand=1\r\n" // not code
                           "per_frame_1=x = int(rand (8)) + myrand(2) + rand;\r\n"
                           "shape_0_per_frame1=r = rand(1);\r\n"
                           "comp_1=`sampler sampler_rand00; sampler sampler_rand01_smalltiled;\r\n"
                           "comp_2=`ret = tex2D(sampler_rand00, uv).xyz * rand_preset.x * hue_shader;\r\n"
                           "comp_3=`ret += texsize_rand01_smalltiled.x + rand_frame.y + my_rand_preset;\n"
                           "warp_1=`ret = tex2D(sampler_RAND02_smalltiled, uv).xyz;\n"
                           "comp_4=`ret = tex2D(sampler_pw_rand00, uv) + tex2D(sampler_fc_noise_hq, uv) * texsize_noise_hq.x;\n"
                           "comp_5=`ret += tex3D(sampler_noisevol_hq, uvw);";
  CHECK(withoutRandomness(milk, textures) ==
        "[preset00]\r\n"
        "rand=1\r\n"
        "per_frame_1=x = int(0.5* (8)) + myrand(2) + rand;\r\n"
        "shape_0_per_frame1=r = 0.5*(1);\r\n"
        "comp_1=`sampler sampler_clouds; sampler sampler_smalltiled_a;\r\n"
        "comp_2=`ret = tex2D(sampler_clouds, uv).xyz * float4(0.37,0.61,0.23,0.83).x * float3(0.75,0.75,0.75);\r\n"
        "comp_3=`ret += texsize_smalltiled_a.x + float4(0.37,0.61,0.23,0.83).y + my_rand_preset;\n"
        "warp_1=`ret = tex2D(sampler_smalltiled_b, uv).xyz;\n"
        // The filter prefix stays; the 2D noise gets a fixed texture; 3D noise can't.
        "comp_4=`ret = tex2D(sampler_pw_clouds, uv) + tex2D(sampler_fc_zebra, uv) * texsize_zebra.x;\n"
        "comp_5=`ret += tex3D(sampler_noisevol_hq, uvw);");
  CHECK(withoutRandomness(kPlain, textures) == kPlain);
}

TEST_CASE("parseExclusions skips comments and blank lines (8.11)", "[core][MilkConvert]") {
  CHECK(parseExclusions("# why\r\nA/b.milk\r\n\r\n  C/d e.milk  \n#x\n") ==
        std::set<std::string>{"A/b.milk", "C/d e.milk"});
}
