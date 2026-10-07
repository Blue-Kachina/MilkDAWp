// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/MilkdawpPreset.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace milkdawp::core {

namespace {

constexpr std::string_view kPrefix = "mdw_";
constexpr std::string_view kControlPrefix = "mdw_ctl_";
constexpr int kMaxControlNumber = 99;

// One line of a preset file: what it says, and the line break after it ("" on
// the last line of a file without a final line break).
struct Line {
  std::string_view content;
  std::string_view terminator;
};

std::vector<Line> splitLines(std::string_view text) {
  std::vector<Line> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find_first_of("\r\n", start);
    if (end == std::string_view::npos) {
      lines.push_back({text.substr(start), {}});
      break;
    }
    const std::size_t terminatorLength = text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n' ? 2 : 1;
    lines.push_back({text.substr(start, end - start), text.substr(end, terminatorLength)});
    start = end + terminatorLength;
  }
  return lines;
}

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
  const auto last = text.find_last_not_of(" \t");
  return text.substr(first, last - first + 1);
}

// A key as projectM (PresetFileParser::ParseLine) and MilkDrop
// (_GetLineByName) read it: everything before the first space or '='. Empty
// when the line has neither, or starts with one: both readers skip such lines.
std::string_view keyOf(std::string_view line) {
  const auto delimiter = line.find_first_of(" =");
  if (delimiter == std::string_view::npos || delimiter == 0) {
    return {};
  }
  return line.substr(0, delimiter);
}

std::string_view valueOf(std::string_view line) {
  const auto delimiter = line.find_first_of(" =");
  return delimiter == std::string_view::npos ? std::string_view{} : line.substr(delimiter + 1);
}

// A scalar value (a number, a slot, a mode) may carry a trailing "; comment".
std::string_view scalar(std::string_view value) { return trim(value.substr(0, value.find(';'))); }

std::optional<int> parseInt(std::string_view text) {
  int value = 0;
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<float> parseFloat(std::string_view text) {
  // Classic locale: a host may have set a decimal-comma C locale.
  std::istringstream in{std::string(text)};
  in.imbue(std::locale::classic());
  float value = 0.0f;
  in >> value;
  if (text.empty() || in.fail() || !in.eof() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

// Enough digits that reading the text back gives the same float.
std::string formatFloat(float value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(9) << value;
  return out.str();
}

// A literal for compiled code: exactly the double the engine passes for the
// same float (projectm_set_preset_variable takes a double), so a Macro sitting
// on its default makes `mdw_mK - default` exactly 0.
std::string literal(float value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17) << static_cast<double>(value);
  return "(" + out.str() + ")";
}

bool isIdentifier(std::string_view text) {
  if (text.empty() || std::isdigit(static_cast<unsigned char>(text.front())) != 0) {
    return false;
  }
  return std::all_of(text.begin(), text.end(),
                     [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; });
}

// True when `code` ends in a statement with no ';' after it, comments aside:
// code appended after it must start with one. (A leading ';' with nothing
// before it is a syntax error, so it can't simply always be added.)
bool endsOpen(std::string_view code) {
  std::string stripped;
  for (std::size_t i = 0; i < code.size(); ++i) {
    if (code.compare(i, 2, "//") == 0) {
      i = std::min(code.find('\n', i), code.size()) - 1;
    } else if (code.compare(i, 2, "/*") == 0) {
      const auto close = code.find("*/", i + 2);
      i = close == std::string_view::npos ? code.size() - 1 : close + 1;
    } else {
      stripped += code[i];
    }
  }
  const auto last = stripped.find_last_not_of(" \t\r\n");
  return last != std::string::npos && stripped[last] != ';';
}

std::string withSemicolon(std::string_view code) {
  auto trimmed = trim(code);
  std::string out(trimmed);
  if (!out.empty() && out.back() != ';') {
    out += ';';
  }
  return out;
}

const char* modeName(ControlMode mode) {
  switch (mode) {
  case ControlMode::Replace: return "replace";
  case ControlMode::Offset: return "offset";
  case ControlMode::Scale: return "scale";
  case ControlMode::Rate: return "rate";
  case ControlMode::Expression: return "expr";
  }
  return "offset";
}

std::optional<ControlMode> modeFrom(std::string_view text) {
  const auto name = lower(text);
  if (name == "replace") return ControlMode::Replace;
  if (name == "offset") return ControlMode::Offset;
  if (name == "scale") return ControlMode::Scale;
  if (name == "rate") return ControlMode::Rate;
  if (name == "expr" || name == "expression") return ControlMode::Expression;
  return std::nullopt;
}

// The value at which each mode leaves its target untouched, for a control
// that doesn't say what its default is.
float neutralFor(ControlMode mode) { return mode == ControlMode::Scale ? 1.0f : 0.0f; }

// The Macro value (0..1) that puts the control on `controlValue`.
float macroValueFor(const PresetControl& control, float controlValue) {
  return std::clamp((controlValue - control.min) / (control.max - control.min), 0.0f, 1.0f);
}

// Format versions: each step turns one version's keys into the next one's.
// Only version 1 exists so far, so there is nothing to step through yet.
void migrate(int /*fromFormat*/, std::vector<std::pair<std::string, std::string>>& /*keys*/) {}

// Turns one mdw_ctl_N_* block into a control, or explains why it can't.
std::optional<PresetControl> makeControl(int number, const std::map<std::string, std::string>& fields,
                                         std::vector<std::string>& problems) {
  const auto where = "Control " + std::to_string(number);
  const auto field = [&fields](const char* name) -> std::optional<std::string_view> {
    const auto found = fields.find(name);
    return found == fields.end() ? std::nullopt : std::optional<std::string_view>(found->second);
  };

  PresetControl control;
  control.number = number;
  control.name = std::string(trim(field("name").value_or("")));
  if (control.name.empty()) {
    control.name = "Macro " + std::to_string(number);
  }

  const auto slot = lower(scalar(field("slot").value_or("")));
  const auto slotNumber = slot.rfind("macro", 0) == 0 ? parseInt(std::string_view(slot).substr(5)) : std::nullopt;
  if (!slotNumber || *slotNumber < 1 || *slotNumber > kMacroCount) {
    problems.push_back(where + " left out: its slot must be macro1 to macro8, not \"" + slot + "\"");
    return std::nullopt;
  }
  control.macro = *slotNumber - 1;

  const auto mode = modeFrom(scalar(field("mode").value_or("")));
  if (!mode) {
    problems.push_back(where + " left out: its mode must be replace, offset, scale, rate or expr");
    return std::nullopt;
  }
  control.mode = *mode;

  if (const auto stage = field("stage")) {
    const auto name = lower(scalar(*stage));
    if (name == "pixel") {
      control.stage = ControlStage::PerPixel;
    } else if (name != "frame") {
      problems.push_back(where + ": stage must be frame or pixel; using frame");
    }
  }
  if (control.mode == ControlMode::Rate && control.stage == ControlStage::PerPixel) {
    problems.push_back(where + ": a rate runs once a frame, so it can't be per pixel; using frame");
    control.stage = ControlStage::PerFrame;
  }

  if (control.mode == ControlMode::Expression) {
    control.code = std::string(trim(field("code").value_or("")));
    if (control.code.empty()) {
      problems.push_back(where + " left out: an expr control needs code");
      return std::nullopt;
    }
  } else {
    control.target = std::string(scalar(field("target").value_or("")));
    if (!isIdentifier(control.target)) {
      problems.push_back(where + " left out: its target must be a variable name, not \"" + control.target + "\"");
      return std::nullopt;
    }
  }

  const auto numberField = [&](const char* name, float fallback) {
    const auto text = field(name);
    if (!text) {
      return fallback;
    }
    if (const auto value = parseFloat(scalar(*text))) {
      return *value;
    }
    problems.push_back(where + ": " + name + " is not a number; using " + formatFloat(fallback));
    return fallback;
  };
  // Unset ranges: an amount 0..1 for replace, 0..2 around 1 for scale, -1..1 otherwise.
  const bool fromZero = control.mode == ControlMode::Replace || control.mode == ControlMode::Scale;
  control.min = numberField("min", fromZero ? 0.0f : -1.0f);
  control.max = numberField("max", control.mode == ControlMode::Scale ? 2.0f : 1.0f);
  if (control.min == control.max) {
    problems.push_back(where + " left out: min and max are the same, so the Macro would do nothing");
    return std::nullopt;
  }
  const float low = std::min(control.min, control.max);
  const float high = std::max(control.min, control.max);
  control.defaultValue = numberField("default", std::clamp(neutralFor(control.mode), low, high));
  if (control.defaultValue < low || control.defaultValue > high) {
    problems.push_back(where + ": default is outside min..max; clamped");
    control.defaultValue = std::clamp(control.defaultValue, low, high);
  }
  if (control.mode == ControlMode::Replace) {
    if (!field("value")) {
      problems.push_back(where + " left out: a replace control needs the value it pulls towards");
      return std::nullopt;
    }
    control.value = numberField("value", 0.0f);
  }
  return control;
}

constexpr std::array<const char*, 10> kControlFields{"name", "slot", "mode", "target", "stage",
                                                     "min",  "max",  "default", "value", "code"};

} // namespace

bool isMilkdawpLine(std::string_view line) noexcept {
  const auto key = keyOf(line);
  if (key.size() < kPrefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < kPrefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(key[i])) != kPrefix[i]) {
      return false;
    }
  }
  return true;
}

bool hasMilkdawpExtension(std::string_view path) noexcept {
  constexpr std::string_view extension = ".milkdawp";
  if (path.size() < extension.size()) {
    return false;
  }
  const auto tail = path.substr(path.size() - extension.size());
  return std::equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == b;
  });
}

MilkdawpParseResult parseMilkdawp(std::string_view text) {
  MilkdawpParseResult result;
  auto& preset = result.preset;
  auto& problems = result.problems;

  // Split the file: mdw_ lines here, everything else back into `milk` as it was.
  std::vector<std::pair<std::string, std::string>> keys;
  std::set<std::string> seen;
  for (const auto& line : splitLines(text)) {
    if (!isMilkdawpLine(line.content)) {
      preset.milk.append(line.content);
      preset.milk.append(line.terminator);
      continue;
    }
    auto key = lower(keyOf(line.content));
    if (!seen.insert(key).second) {
      // projectM and MilkDrop both use a key's first occurrence.
      problems.push_back(key + " appears more than once; the first one is used");
      continue;
    }
    keys.emplace_back(std::move(key), std::string(trim(valueOf(line.content))));
  }
  if (keys.empty()) {
    return result; // a plain .milk
  }

  int format = 0;
  if (const auto found = std::find_if(keys.begin(), keys.end(), [](const auto& kv) { return kv.first == "mdw_format"; });
      found == keys.end()) {
    problems.push_back("mdw_format is missing; reading it as format " + std::to_string(kMilkdawpFormatVersion));
    format = kMilkdawpFormatVersion;
  } else if (const auto parsed = parseInt(scalar(found->second)); !parsed || *parsed < 1) {
    problems.push_back("mdw_format is not a version number; reading it as format " +
                       std::to_string(kMilkdawpFormatVersion));
    format = kMilkdawpFormatVersion;
  } else {
    format = *parsed;
  }
  const bool newer = format > kMilkdawpFormatVersion;
  if (newer) {
    problems.push_back("made for a newer MilkDAWp (format " + std::to_string(format) +
                       "); what this version doesn't know is ignored, and kept if the preset is saved");
  } else {
    migrate(format, keys);
  }
  preset.format = std::max(format, kMilkdawpFormatVersion);

  std::map<int, std::map<std::string, std::string>> controlFields;
  for (auto& [key, value] : keys) {
    if (key == "mdw_format") {
      continue;
    }
    if (key == "mdw_title") {
      preset.title = value;
    } else if (key == "mdw_source") {
      preset.source = value;
    } else if (key == "mdw_source_sha256") {
      preset.sourceSha256 = std::string(scalar(value));
    } else if (key == "mdw_renderer") {
      preset.renderer = lower(scalar(value));
    } else if (key == "mdw_tags") {
      std::size_t start = 0;
      while (start <= value.size()) {
        const auto end = std::min(value.find(',', start), value.size());
        if (const auto tag = trim(std::string_view(value).substr(start, end - start)); !tag.empty()) {
          preset.tags.emplace_back(tag);
        }
        start = end + 1;
      }
    } else if (key.rfind(kControlPrefix, 0) == 0) {
      // mdw_ctl_<N>_<field>
      const auto rest = std::string_view(key).substr(kControlPrefix.size());
      const auto underscore = rest.find('_');
      const auto number = underscore == std::string_view::npos ? std::nullopt : parseInt(rest.substr(0, underscore));
      const auto fieldName = underscore == std::string_view::npos ? std::string{} : std::string(rest.substr(underscore + 1));
      const bool knownField = std::find(kControlFields.begin(), kControlFields.end(), fieldName) != kControlFields.end();
      if (number && *number >= 1 && *number <= kMaxControlNumber && knownField) {
        controlFields[*number][fieldName] = value;
      } else {
        if (!newer) {
          problems.push_back(key + " is not a control setting this version knows; ignored");
        }
        preset.otherKeys.emplace_back(key, value);
      }
    } else {
      if (!newer) {
        problems.push_back(key + " is not used by this version; ignored");
      }
      preset.otherKeys.emplace_back(key, value);
    }
  }

  for (const auto& [number, fields] : controlFields) {
    if (auto control = makeControl(number, fields, problems)) {
      preset.controls.push_back(std::move(*control));
    }
  }
  if (preset.renderer != "projectm" && !newer) {
    problems.push_back("renderer \"" + preset.renderer + "\" isn't available; projectM draws it");
  }
  return result;
}

std::string serializeMilkdawp(const MilkdawpPreset& preset) {
  const auto lines = splitLines(preset.milk);
  std::string eol = "\n";
  for (const auto& line : lines) {
    if (!line.terminator.empty()) {
      eol = std::string(line.terminator);
      break;
    }
  }

  std::string block;
  const auto add = [&](const std::string& key, const std::string& value) { block += key + "=" + value + eol; };
  add("mdw_format", std::to_string(preset.format));
  if (!preset.title.empty()) add("mdw_title", preset.title);
  if (!preset.source.empty()) add("mdw_source", preset.source);
  if (!preset.sourceSha256.empty()) add("mdw_source_sha256", preset.sourceSha256);
  if (!preset.tags.empty()) {
    std::string tags;
    for (const auto& tag : preset.tags) {
      tags += (tags.empty() ? "" : ",") + tag;
    }
    add("mdw_tags", tags);
  }
  add("mdw_renderer", preset.renderer);
  for (const auto& control : preset.controls) {
    const auto key = [&control](const char* field) {
      return std::string(kControlPrefix) + std::to_string(control.number) + "_" + field;
    };
    add(key("name"), control.name);
    add(key("slot"), "macro" + std::to_string(control.macro + 1));
    add(key("mode"), modeName(control.mode));
    if (control.mode == ControlMode::Expression) {
      add(key("code"), control.code);
    } else {
      add(key("target"), control.target);
    }
    if (control.stage == ControlStage::PerPixel) {
      add(key("stage"), "pixel");
    }
    add(key("min"), formatFloat(control.min));
    add(key("max"), formatFloat(control.max));
    add(key("default"), formatFloat(control.defaultValue));
    if (control.mode == ControlMode::Replace) {
      add(key("value"), formatFloat(control.value));
    }
  }
  for (const auto& [key, value] : preset.otherKeys) {
    add(key, value);
  }

  // After "[preset00]", so MilkDrop 1 (which reads that INI section) sees the
  // lines inside it. A header with no line break after it would need one
  // added, which would change `milk`; the block goes at the top then.
  std::string out;
  out.reserve(preset.milk.size() + block.size());
  bool placed = false;
  for (const auto& line : lines) {
    out.append(line.content);
    out.append(line.terminator);
    if (!placed && !line.terminator.empty() && lower(trim(line.content)) == "[preset00]") {
      out += block;
      placed = true;
    }
  }
  return placed ? out : block + out;
}

MacroDefaults macroDefaults(const MilkdawpPreset& preset) {
  MacroDefaults defaults{};
  for (const auto& control : preset.controls) {
    auto& slot = defaults[static_cast<std::size_t>(control.macro)];
    if (!slot) {
      slot = macroValueFor(control, control.defaultValue);
    }
  }
  return defaults;
}

std::array<std::string, kMacroCount> macroNames(const MilkdawpPreset& preset) {
  std::array<std::string, kMacroCount> names{};
  for (const auto& control : preset.controls) {
    auto& name = names[static_cast<std::size_t>(control.macro)];
    if (name.empty()) {
      name = control.name;
    }
  }
  return names;
}

std::string compileForProjectM(const MilkdawpPreset& preset) {
  const auto lines = splitLines(preset.milk);
  std::string eol = "\n";
  for (const auto& line : lines) {
    if (!line.terminator.empty()) {
      eol = std::string(line.terminator);
      break;
    }
  }

  // projectM reads code lines prefix1, prefix2, ... and stops at the first
  // missing number (PresetFileParser::GetCode; MilkDrop's ReadCode does the
  // same). Our lines take the numbers from there on.
  std::map<std::string, std::string_view> values; // first occurrence wins, as in projectM
  for (const auto& line : lines) {
    if (const auto key = keyOf(line.content); !key.empty()) {
      values.emplace(lower(key), valueOf(line.content));
    }
  }
  // The first unused number, and whether the code so far ends open.
  const auto scan = [&values](const std::string& prefix) {
    int n = 1;
    std::string code;
    for (auto found = values.find(prefix + "1"); found != values.end();
         found = values.find(prefix + std::to_string(++n))) {
      auto line = found->second;
      if (!line.empty() && line.front() == '`') {
        line.remove_prefix(1);
      }
      code.append(line);
      code += '\n';
    }
    return std::pair{n, endsOpen(code)};
  };
  const auto [frameStart, frameOpen] = scan("per_frame_");
  const auto [pixelStart, pixelOpen] = scan("per_pixel_");

  // Lines past a gap in the numbering are never read. Filling the gap would
  // suddenly bring them in, so they are renamed out of the way: the preset
  // runs exactly the code it ran before.
  const auto orphaned = [](std::string_view key, std::string_view prefix, int start) {
    if (key.size() <= prefix.size() || lower(key.substr(0, prefix.size())) != prefix) {
      return false;
    }
    const auto digits = key.substr(prefix.size());
    if (digits.front() == '0') {
      return false; // "per_frame_07" is never read by either, and never collides with ours
    }
    const auto n = parseInt(digits);
    return n && *n >= start;
  };

  // The value of a control: its default, plus how far its Macro is from the
  // Macro's default. Written this way (not min + span x Macro) so that a Macro
  // on its default gives exactly the default, bit for bit.
  const auto defaults = macroDefaults(preset);
  const auto valueOfControl = [&defaults](const PresetControl& control) {
    const float macroDefault = defaults[static_cast<std::size_t>(control.macro)].value_or(0.0f);
    // A second control on a Macro starts where the Macro's default puts it.
    const float start = macroValueFor(control, control.defaultValue) == macroDefault
                            ? control.defaultValue
                            : control.min + (control.max - control.min) * macroDefault;
    return "(" + literal(start) + " + " + literal(control.max - control.min) + " * (" +
           kMacroVariables[static_cast<std::size_t>(control.macro)] + " - " + literal(macroDefault) + "))";
  };
  const auto codeFor = [&valueOfControl](const PresetControl& control) {
    const auto c = valueOfControl(control);
    const auto& t = control.target;
    switch (control.mode) {
    case ControlMode::Replace: return t + " = " + t + " + (" + literal(control.value) + " - " + t + ") * " + c + ";";
    case ControlMode::Offset: return t + " = " + t + " + " + c + ";";
    case ControlMode::Scale: return t + " = " + t + " * " + c + ";";
    case ControlMode::Rate: return t + " = " + t + " + " + std::string(kDtVariable) + " * " + c + ";";
    case ControlMode::Expression:
      return "mdw_c" + std::to_string(control.number) + " = " + c + "; " + withSemicolon(control.code);
    }
    return std::string{};
  };

  std::vector<std::string> frameCode;
  std::vector<std::string> pixelCode;
  for (const auto& control : preset.controls) {
    (control.stage == ControlStage::PerPixel ? pixelCode : frameCode).push_back(codeFor(control));
  }
  // The Visual globals a preset can take itself (exploration doc §6.2, Stage
  // B), after the controls: the user's knob goes on top of the preset's own.
  // Each is exact at neutral: x 1, + 0, x pow(_, 0) = x 1.
  frameCode.push_back(std::string("zoom = zoom * pow(1.04, ") + kZoomVariable + ");");
  frameCode.push_back(std::string("rot = rot + 0.04 * ") + kRotationVariable + ";");
  frameCode.push_back(std::string("warp = warp * ") + kWarpVariable + ";");
  frameCode.push_back(std::string("decay = decay + (1 - decay) * 0.5 * ") + kTrailsVariable + ";");

  std::string out;
  out.reserve(preset.milk.size() + 1024);
  for (const auto& line : lines) {
    const auto key = keyOf(line.content);
    if (orphaned(key, "per_frame_", frameStart) || orphaned(key, "per_pixel_", pixelStart)) {
      out += "mdw_unread_";
    }
    out.append(line.content);
    out.append(line.terminator);
  }
  if (!out.empty() && out.back() != '\n' && out.back() != '\r') {
    out += eol;
  }
  const auto append = [&out, &eol](const char* prefix, int start, bool open, const std::vector<std::string>& code) {
    for (std::size_t i = 0; i < code.size(); ++i) {
      // The preset's last statement may lack its ';' (fine at the very end of
      // its code, not once ours follows).
      const bool separate = i == 0 && open;
      out += prefix + std::to_string(start + static_cast<int>(i)) + "=" + (separate ? "; " : "") + code[i] + eol;
    }
  };
  append("per_frame_", frameStart, frameOpen, frameCode);
  append("per_pixel_", pixelStart, pixelOpen, pixelCode);
  return out;
}

} // namespace milkdawp::core
