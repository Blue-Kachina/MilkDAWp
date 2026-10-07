// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/MilkConvert.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <locale>
#include <optional>
#include <sstream>
#include <system_error>

#include "milkdawp/core/Sha256.h"

namespace milkdawp::core {

namespace {

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string_view trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t");
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// The preset's key/value lines as projectM reads them (PresetFileParser):
// the key is everything before the first space or '=', lower-cased, and the
// first occurrence of a key wins.
std::map<std::string, std::string> readValues(std::string_view text) {
  std::map<std::string, std::string> values;
  std::size_t start = 0;
  while (start < text.size()) {
    auto end = text.find_first_of("\r\n", start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    const auto line = text.substr(start, end - start);
    start = end + 1;
    const auto delimiter = line.find_first_of(" =");
    if (delimiter == std::string_view::npos || delimiter == 0) {
      continue;
    }
    values.emplace(lower(line.substr(0, delimiter)), std::string(line.substr(delimiter + 1)));
  }
  return values;
}

// prefix1, prefix2, ... up to the first missing number, as projectM reads
// code (backticks on shader lines dropped).
std::string codeFor(const std::map<std::string, std::string>& values, const std::string& prefix) {
  std::string code;
  for (int n = 1;; ++n) {
    const auto found = values.find(prefix + std::to_string(n));
    if (found == values.end()) {
      break;
    }
    std::string_view line = found->second;
    if (!line.empty() && line.front() == '`') {
      line.remove_prefix(1);
    }
    code.append(line);
    code += '\n';
  }
  return code;
}

std::optional<float> parseNumber(std::string_view text) {
  text = trim(text);
  if (text.empty()) {
    return std::nullopt;
  }
  // Classic locale: a host may have set a decimal-comma C locale.
  std::istringstream in{std::string(text)};
  in.imbue(std::locale::classic());
  float value = 0.0f;
  in >> value;
  if (in.fail() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

std::string withoutComments(std::string_view code) {
  std::string out;
  out.reserve(code.size());
  for (std::size_t i = 0; i < code.size(); ++i) {
    if (code.compare(i, 2, "//") == 0) {
      i = std::min(code.find('\n', i), code.size()) - 1;
    } else if (code.compare(i, 2, "/*") == 0) {
      const auto close = code.find("*/", i + 2);
      i = close == std::string_view::npos ? code.size() - 1 : close + 1;
      out += ' ';
    } else {
      out += code[i];
    }
  }
  return out;
}

bool isIdentifierStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Calls `visit(name, position after it)` for every identifier, lower-cased.
template <typename Visit> void forEachIdentifier(std::string_view code, Visit&& visit) {
  for (std::size_t i = 0; i < code.size();) {
    if (!isIdentifierStart(code[i]) || (i > 0 && (isIdentifierChar(code[i - 1]) || code[i - 1] == '.'))) {
      ++i;
      continue;
    }
    std::size_t end = i;
    while (end < code.size() && isIdentifierChar(code[end])) {
      ++end;
    }
    visit(lower(code.substr(i, end - i)), end);
    i = end;
  }
}

// Assignments in eel code: `x = ...` (not `==`) and `x += ...` style, with the
// right-hand side up to the next ';'.
void collectAssignments(std::string_view code, MilkAnalysis& analysis) {
  forEachIdentifier(code, [&](const std::string& name, std::size_t after) {
    auto i = code.find_first_not_of(" \t\r\n", after);
    if (i == std::string_view::npos) {
      return;
    }
    if (std::string_view("+-*/%&|^").find(code[i]) != std::string_view::npos && i + 1 < code.size() &&
        code[i + 1] == '=') {
      ++i;
    } else if (code[i] != '=' || (i + 1 < code.size() && code[i + 1] == '=')) {
      return;
    }
    const auto rhsStart = i + 1;
    const auto rhsEnd = std::min(code.find(';', rhsStart), code.size());
    analysis.frameWrites.insert(name);
    auto& joined = analysis.frameAssignments[name];
    joined += (joined.empty() ? "" : "; ") + std::string(trim(code.substr(rhsStart, rhsEnd - rhsStart)));
  });
}

// q-variables read in code: q1..q32, and HLSL's packed _qa.._qh (q1-4 ... q29-32).
void collectQReads(std::string_view code, std::set<std::string>& reads) {
  forEachIdentifier(code, [&](const std::string& name, std::size_t) {
    if (name.size() >= 2 && name[0] == 'q' &&
        std::all_of(name.begin() + 1, name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
      const int n = std::stoi(name.substr(1));
      if (n >= 1 && n <= 32) {
        reads.insert(name);
      }
    } else if (name.size() == 3 && name[0] == '_' && name[1] == 'q' && name[2] >= 'a' && name[2] <= 'h') {
      const int first = (name[2] - 'a') * 4 + 1;
      for (int n = first; n < first + 4; ++n) {
        reads.insert("q" + std::to_string(n));
      }
    }
  });
}

bool mentionsAny(std::string_view code, std::initializer_list<std::string_view> names) {
  bool found = false;
  forEachIdentifier(code, [&](const std::string& name, std::size_t) {
    found = found || std::find(names.begin(), names.end(), name) != names.end();
  });
  return found;
}

bool callsAny(std::string_view code, std::initializer_list<std::string_view> functions) {
  bool found = false;
  forEachIdentifier(code, [&](const std::string& name, std::size_t after) {
    const auto next = code.find_first_not_of(" \t", after);
    found = found || (next != std::string_view::npos && code[next] == '(' &&
                      std::find(functions.begin(), functions.end(), name) != functions.end());
  });
  return found;
}

// One Macro's worth of proposal: the controls on it share its name.
struct Proposal {
  std::string name;
  struct Target {
    std::string variable;
    ControlMode mode;
    float min;
    float max;
  };
  std::vector<Target> targets;
};

Proposal scaleOf(std::string name, std::string variable) {
  return {std::move(name), {{std::move(variable), ControlMode::Scale, 0.0f, 2.0f}}};
}
Proposal offsetOf(std::string name, std::string variable, float reach) {
  return {std::move(name), {{std::move(variable), ControlMode::Offset, -reach, reach}}};
}

// A "family": something a preset can show, the variables that animate it,
// how to tell it's visible from the header, and the Macro to propose.
struct Family {
  std::vector<std::string> animatedBy;
  std::function<bool(const MilkAnalysis&)> visible;
  Proposal proposal;
};

float headerValue(const MilkAnalysis& analysis, const std::string& key, float fallback) {
  const auto found = analysis.header.find(key);
  return found == analysis.header.end() ? fallback : found->second;
}

std::vector<Family> families() {
  // Defaults are projectM's (MilkdropPreset/PresetState.hpp) for a key the
  // preset leaves out.
  const auto shown = [](const char* alphaKey, float alphaDefault, const char* sizeKey, float sizeDefault) {
    return [=](const MilkAnalysis& a) {
      return headerValue(a, alphaKey, alphaDefault) > 0.02f &&
             (sizeKey == nullptr || headerValue(a, sizeKey, sizeDefault) > 0.0f);
    };
  };
  const auto always = [](const MilkAnalysis&) { return true; };
  return {
      {{"wave_a", "wave_r", "wave_g", "wave_b", "wave_x", "wave_y", "wave_mystery"},
       shown("fwavealpha", 0.8f, nullptr, 0.0f), scaleOf("Wave", "wave_a")},
      {{"ob_a", "ob_size", "ob_r", "ob_g", "ob_b"}, shown("ob_a", 0.0f, "ob_size", 0.01f),
       scaleOf("Outer Border", "ob_a")},
      {{"ib_a", "ib_size", "ib_r", "ib_g", "ib_b"}, shown("ib_a", 0.0f, "ib_size", 0.01f),
       scaleOf("Inner Border", "ib_a")},
      {{"mv_a", "mv_x", "mv_y", "mv_l", "mv_r", "mv_g", "mv_b"}, shown("mv_a", 0.0f, nullptr, 0.0f),
       scaleOf("Motion Vectors", "mv_a")},
      {{"echo_alpha", "echo_zoom", "echo_orient"}, shown("fvideoechoalpha", 0.0f, nullptr, 0.0f),
       scaleOf("Echo", "echo_alpha")},
      // Motion through the feedback: shown by every preset, so these are
      // also the fill when nothing more particular is left.
      {{"dx"}, always, offsetOf("Drift X", "dx", 0.02f)},
      {{"dy"}, always, offsetOf("Drift Y", "dy", 0.02f)},
      {{"cx"}, always, offsetOf("Centre X", "cx", 0.25f)},
      {{"cy"}, always, offsetOf("Centre Y", "cy", 0.25f)},
      // One knob that stretches one way and squashes the other.
      {{"sx", "sy"},
       always,
       {"Squash", {{"sx", ControlMode::Offset, -0.03f, 0.03f}, {"sy", ControlMode::Offset, 0.03f, -0.03f}}}},
  };
}

constexpr std::size_t kMaxQMacros = 3;

// q-variables worth a Macro: set every frame, read further down the
// pipeline, and safe to scale. A phase (time x speed) would jump when
// scaled, and a selector (int, rand, a comparison) would break.
std::vector<std::string> qCandidates(const MilkAnalysis& analysis, bool audio) {
  std::vector<std::string> out;
  for (int n = 1; n <= 32; ++n) {
    const auto name = "q" + std::to_string(n);
    const auto assigned = analysis.frameAssignments.find(name);
    if (assigned == analysis.frameAssignments.end() || analysis.qReads.count(name) == 0) {
      continue;
    }
    const auto& rhs = assigned->second;
    if (mentionsAny(rhs, {"time", "frame", "progress"}) ||
        callsAny(rhs, {"int", "rand", "floor", "ceil", "above", "below", "equal", "bnot", "band", "bor", "if",
                       "megabuf", "gmegabuf", "fmod"}) ||
        rhs.find('%') != std::string::npos) {
      continue;
    }
    if (mentionsAny(rhs, {"bass", "mid", "treb", "bass_att", "mid_att", "treb_att", "vol", "vol_att"}) == audio) {
      out.push_back(name);
    }
  }
  return out;
}

std::string titleOf(std::string_view relativePath) {
  auto name = relativePath.substr(relativePath.find_last_of('/') + 1);
  if (const auto dot = name.find_last_of('.'); dot != std::string_view::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  return std::string(trim(name));
}

std::vector<std::string> tagsOf(std::string_view relativePath, const MilkAnalysis& analysis) {
  std::vector<std::string> tags;
  std::size_t start = 0;
  for (auto slash = relativePath.find('/'); slash != std::string_view::npos;
       slash = relativePath.find('/', start)) {
    // Folder names, which in Cream of the Crop are category / subcategory.
    // A comma would split the tag in mdw_tags.
    auto tag = lower(trim(relativePath.substr(start, slash - start)));
    tag.erase(std::remove(tag.begin(), tag.end(), ','), tag.end());
    tag.erase(std::remove(tag.begin(), tag.end(), '!'), tag.end());
    if (const auto t = std::string(trim(tag)); !t.empty() && std::find(tags.begin(), tags.end(), t) == tags.end()) {
      tags.push_back(t);
    }
    start = slash + 1;
  }
  if (analysis.hasShaders) {
    tags.emplace_back("shader");
  }
  return tags;
}

std::string toUtf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {text.begin(), text.end()};
}

bool hasMilkExtension(const std::filesystem::path& path) { return lower(toUtf8(path.extension())) == ".milk"; }

std::optional<std::string> readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Writes beside, then renames over: an interrupted run never leaves half a
// file. The temporary name is no longer than the .milk's, for MAX_PATH.
std::string writeFile(const std::filesystem::path& path, const std::string& text) {
  auto partial = path;
  partial.replace_extension(".mdw~");
  {
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
      return "couldn't write " + toUtf8(partial);
    }
  }
  std::error_code error;
  std::filesystem::rename(partial, path, error);
  if (error) {
    std::filesystem::remove(partial, error);
    return "couldn't write " + toUtf8(path);
  }
  return {};
}

} // namespace

MilkAnalysis analyzeMilk(std::string_view milk) {
  MilkAnalysis analysis;
  const auto values = readValues(milk);
  for (const auto& [key, value] : values) {
    if (const auto number = parseNumber(value)) {
      analysis.header.emplace(key, *number);
    }
  }
  // Older presets write the motion-vector alpha only as a switch.
  if (analysis.header.count("mv_a") == 0 && headerValue(analysis, "bmotionvectorson", 0.0f) != 0.0f) {
    analysis.header["mv_a"] = 1.0f;
  }

  const auto perFrame = withoutComments(codeFor(values, "per_frame_"));
  analysis.hasPerFrameCode = !trim(perFrame).empty();
  collectAssignments(withoutComments(codeFor(values, "per_frame_init_")), analysis);
  collectAssignments(perFrame, analysis);

  const auto warp = withoutComments(codeFor(values, "warp_"));
  const auto comp = withoutComments(codeFor(values, "comp_"));
  analysis.hasShaders = !trim(warp).empty() || !trim(comp).empty();
  collectQReads(withoutComments(codeFor(values, "per_pixel_")), analysis.qReads);
  collectQReads(warp, analysis.qReads);
  collectQReads(comp, analysis.qReads);
  for (int i = 0; i < 4; ++i) {
    for (const auto* kind : {"wave_", "shape_"}) {
      const auto prefix = kind + std::to_string(i) + "_";
      for (const auto* part : {"init", "per_frame", "per_point"}) {
        collectQReads(withoutComments(codeFor(values, prefix + part)), analysis.qReads);
      }
    }
  }
  return analysis;
}

MilkdawpPreset convertMilk(std::string_view milk, std::string_view relativePath) {
  const auto analysis = analyzeMilk(milk);

  MilkdawpPreset preset;
  preset.milk = std::string(milk);
  preset.title = titleOf(relativePath);
  preset.source = std::string(relativePath);
  preset.sourceSha256 = sha256Hex(milk);
  preset.tags = tagsOf(relativePath, analysis);

  std::vector<Proposal> proposals;
  const auto taken = [&proposals](const std::string& name) {
    return std::any_of(proposals.begin(), proposals.end(), [&](const Proposal& p) { return p.name == name; });
  };
  const auto propose = [&](const Proposal& proposal) {
    if (proposals.size() < kMacroCount && !proposal.name.empty() && !taken(proposal.name)) {
      proposals.push_back(proposal);
    }
  };
  const auto qProposal = [](const std::string& q) { return scaleOf(q, q); };

  // 1. What the preset feeds its shaders or mesh from the music: the most
  //    particular to this preset. 2. What its own code animates. 3. Its
  //    other q-variables. 4. What it shows. 5. Motion any preset has.
  const auto audioQs = qCandidates(analysis, true);
  const auto otherQs = qCandidates(analysis, false);
  std::size_t qCount = 0;
  for (const auto& q : audioQs) {
    if (qCount < kMaxQMacros - 1) {
      propose(qProposal(q));
      ++qCount;
    }
  }
  const auto all = families();
  for (const auto& family : all) {
    const bool animated = std::any_of(family.animatedBy.begin(), family.animatedBy.end(),
                                      [&](const std::string& v) { return analysis.frameWrites.count(v) != 0; });
    if (animated) {
      propose(family.proposal);
    }
  }
  for (const auto* list : {&audioQs, &otherQs}) {
    for (const auto& q : *list) {
      if (qCount < kMaxQMacros && !taken(q)) {
        propose(qProposal(q));
        ++qCount;
      }
    }
  }
  for (const auto& family : all) {
    if (family.visible(analysis)) {
      propose(family.proposal);
    }
  }

  int number = 1;
  for (std::size_t macro = 0; macro < proposals.size(); ++macro) {
    for (const auto& target : proposals[macro].targets) {
      PresetControl control;
      control.number = number++;
      control.name = proposals[macro].name;
      control.macro = static_cast<int>(macro);
      control.mode = target.mode;
      control.target = target.variable;
      control.min = target.min;
      control.max = target.max;
      control.defaultValue = target.mode == ControlMode::Scale ? 1.0f : 0.0f;
      preset.controls.push_back(control);
    }
  }
  return preset;
}

const char* outcomeName(ConvertOutcome outcome) {
  switch (outcome) {
  case ConvertOutcome::Written: return "written";
  case ConvertOutcome::Unchanged: return "unchanged";
  case ConvertOutcome::Kept: return "kept";
  case ConvertOutcome::Excluded: return "excluded";
  case ConvertOutcome::Rejected: return "rejected";
  case ConvertOutcome::Failed: return "failed";
  }
  return "failed";
}

std::vector<ConvertEntry> convertFolder(const std::filesystem::path& root, const ConvertOptions& options) {
  std::error_code error;
  std::vector<std::filesystem::path> milkFiles;
  std::filesystem::path base = root;
  if (std::filesystem::is_regular_file(root, error)) {
    base = root.parent_path();
    if (hasMilkExtension(root)) {
      milkFiles.push_back(root);
    }
  } else if (std::filesystem::is_directory(root, error)) {
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
      if (it->is_regular_file(error) && hasMilkExtension(it->path())) {
        milkFiles.push_back(it->path());
      }
    }
  }

  std::vector<ConvertEntry> entries;
  entries.reserve(milkFiles.size());
  for (const auto& path : milkFiles) {
    ConvertEntry entry;
    entry.relativePath = toUtf8(path.lexically_relative(base));
    entries.push_back(std::move(entry));
  }
  std::vector<std::size_t> order(milkFiles.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(),
            [&entries](std::size_t a, std::size_t b) { return entries[a].relativePath < entries[b].relativePath; });

  std::vector<ConvertEntry> sorted;
  sorted.reserve(entries.size());
  std::size_t done = 0;
  for (const auto index : order) {
    auto entry = std::move(entries[index]);
    const auto& milkPath = milkFiles[index];
    auto outPath = options.outputRoot.empty() ? milkPath : options.outputRoot / milkPath.lexically_relative(base);
    outPath.replace_extension(".milkdawp");

    const auto finish = [&](ConvertOutcome outcome, std::string detail = {}) {
      entry.outcome = outcome;
      entry.detail = std::move(detail);
      sorted.push_back(std::move(entry));
      if (options.progress) {
        options.progress(++done, order.size());
      }
    };

    const bool exists = std::filesystem::exists(outPath, error);
    if (options.exclude.count(entry.relativePath) != 0) {
      if (exists && options.pruneExcluded && !options.dryRun) {
        std::filesystem::remove(outPath, error); // the .milkdawp: the .milk stays
      }
      finish(ConvertOutcome::Excluded);
      continue;
    }
    if (exists && !options.overwrite) {
      finish(ConvertOutcome::Kept);
      continue;
    }
    const auto text = readFile(milkPath);
    if (!text) {
      finish(ConvertOutcome::Failed, "couldn't read it");
      continue;
    }
    if (text->find('\0') != std::string::npos || trim(*text).empty()) {
      finish(ConvertOutcome::Failed, "not a preset (empty, or binary)");
      continue;
    }
    if (parseMilkdawp(*text).preset.milk != *text) {
      finish(ConvertOutcome::Failed, "already has mdw_ lines");
      continue;
    }

    const auto preset = convertMilk(*text, entry.relativePath);
    for (const auto& control : preset.controls) {
      if (entry.controls.empty() || entry.controls.back() != control.name) {
        entry.controls.push_back(control.name);
      }
    }
    const auto out = serializeMilkdawp(preset);
    // The file must read back as exactly this preset, with nothing to complain about.
    if (const auto back = parseMilkdawp(out); !back.problems.empty() || !(back.preset == preset)) {
      finish(ConvertOutcome::Failed,
             "doesn't read back the same" + (back.problems.empty() ? std::string{} : ": " + back.problems.front()));
      continue;
    }
    if (options.verify) {
      if (auto reason = options.verify(milkPath, preset); !reason.empty()) {
        finish(ConvertOutcome::Rejected, std::move(reason));
        continue;
      }
    }
    if (exists && readFile(outPath) == out) {
      finish(ConvertOutcome::Unchanged);
      continue;
    }
    if (!options.dryRun) {
      std::filesystem::create_directories(outPath.parent_path(), error);
      if (auto failure = writeFile(outPath, out); !failure.empty()) {
        finish(ConvertOutcome::Failed, std::move(failure));
        continue;
      }
    }
    finish(ConvertOutcome::Written);
  }
  return sorted;
}

std::string withoutRandomness(std::string_view milk, const std::vector<std::string>& textures) {
  // Random texture names in file order, each given its own fixed texture.
  std::map<std::string, std::string> textureFor;
  std::set<std::string> usedTextures;
  const auto fixedTexture = [&](const std::string& randomName) -> std::string {
    if (const auto found = textureFor.find(randomName); found != textureFor.end()) {
      return found->second;
    }
    // "rand00_smalltiled" picks among textures whose name starts "smalltiled".
    const auto prefix = randomName.rfind("rand", 0) == 0 && randomName.size() > 7 ? randomName.substr(7) : std::string{};
    std::vector<std::string> sorted(textures);
    std::sort(sorted.begin(), sorted.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    for (const auto& texture : sorted) {
      const auto name = lower(texture);
      if (name.rfind(prefix, 0) == 0 && usedTextures.count(name) == 0 &&
          std::all_of(name.begin(), name.end(), isIdentifierChar)) {
        usedTextures.insert(name);
        return textureFor[randomName] = name;
      }
    }
    return textureFor[randomName] = std::string{};
  };
  const auto isRandomTexture = [](std::string_view name) {
    return name.size() >= 6 && name.substr(0, 4) == "rand" && std::isdigit(static_cast<unsigned char>(name[4])) != 0 &&
           std::isdigit(static_cast<unsigned char>(name[5])) != 0 && (name.size() == 6 || name[6] == '_');
  };
  // projectM fills its 2D noise textures from the clock as well. (The 3D
  // noisevol_ ones can't take an image instead; they stay random.)
  const auto isPinned = [&isRandomTexture](std::string_view name) {
    return isRandomTexture(name) || name == "noise_lq" || name == "noise_lq_lite" || name == "noise_mq" ||
           name == "noise_hq";
  };

  std::string out;
  out.reserve(milk.size());
  std::size_t start = 0;
  while (start < milk.size()) {
    auto end = milk.find_first_of("\r\n", start);
    if (end == std::string_view::npos) {
      end = milk.size();
    }
    const std::size_t lineEnd =
        end == milk.size() ? end : end + (milk[end] == '\r' && end + 1 < milk.size() && milk[end + 1] == '\n' ? 2 : 1);
    const auto line = milk.substr(start, end - start);
    const auto terminator = milk.substr(end, lineEnd - end);
    start = lineEnd;

    const auto delimiter = line.find_first_of(" =");
    const auto key = delimiter == std::string_view::npos ? std::string{} : lower(line.substr(0, delimiter));
    const bool shader = key.rfind("warp_", 0) == 0 || key.rfind("comp_", 0) == 0;
    const bool code = !shader && (key.rfind("per_frame", 0) == 0 || key.rfind("per_pixel", 0) == 0 ||
                                  key.rfind("wave_", 0) == 0 || key.rfind("shape_", 0) == 0);
    if (!shader && !code) {
      out.append(line);
      out.append(terminator);
      continue;
    }

    std::string rewritten(line.substr(0, delimiter + 1));
    const auto value = line.substr(delimiter + 1);
    std::size_t copied = 0;
    forEachIdentifier(value, [&](const std::string& name, std::size_t after) {
      const auto begin = after - name.size();
      std::string replacement;
      if (code) {
        const auto next = value.find_first_not_of(" \t", after);
        if (name == "rand" && next != std::string_view::npos && value[next] == '(') {
          replacement = "0.5*";
        }
      } else if (name == "rand_preset" || name == "rand_frame") {
        replacement = "float4(0.37,0.61,0.23,0.83)";
      } else if (name == "hue_shader") {
        replacement = "float3(0.75,0.75,0.75)";
      } else {
        for (const char* kind : {"sampler_", "texsize_"}) {
          const std::string_view prefix = kind;
          if (name.rfind(prefix, 0) != 0) {
            continue;
          }
          // A sampler may name its filtering and wrap first: sampler_pw_rand00.
          auto texture = name.substr(prefix.size());
          std::string filter;
          if (prefix == "sampler_" && texture.size() > 3 && texture[2] == '_' &&
              (texture.rfind("fw", 0) == 0 || texture.rfind("fc", 0) == 0 || texture.rfind("pw", 0) == 0 ||
               texture.rfind("pc", 0) == 0)) {
            filter = texture.substr(0, 3);
            texture = texture.substr(3);
          }
          if (isPinned(texture)) {
            if (const auto fixed = fixedTexture(texture); !fixed.empty()) {
              replacement = std::string(prefix) + filter + fixed;
            }
          }
        }
      }
      if (!replacement.empty()) {
        rewritten.append(value.substr(copied, begin - copied));
        rewritten += replacement;
        copied = after;
      }
    });
    rewritten.append(value.substr(copied));
    out += rewritten;
    out.append(terminator);
  }
  return out;
}

std::set<std::string> parseExclusions(std::string_view text) {
  std::set<std::string> paths;
  std::size_t start = 0;
  while (start < text.size()) {
    auto end = text.find_first_of("\r\n", start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    const auto line = trim(text.substr(start, end - start));
    if (!line.empty() && line.front() != '#') {
      paths.emplace(line);
    }
    start = end + 1;
  }
  return paths;
}

} // namespace milkdawp::core
