// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/StateSchema.h"

#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <system_error>

#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::core {

StateSchemaV2 migrateFromV1(const V1StateRecord& v1) {
  StateSchemaV2 v2;
  v2.schemaVersion = StateSchemaV2::currentSchemaVersion;
  v2.presetAbsolutePath = v1.presetPath;
  v2.playlistFolderPath = v1.playlistFolderPath;
  v2.editorWidth = v1.editorWidth;
  v2.editorHeight = v1.editorHeight;

  for (const auto& spec : allParameters()) {
    if (!spec.v1Alias.empty()) {
      auto it = v1.paramValues.find(spec.v1Alias);
      v2.paramValues[spec.id] = (it != v1.paramValues.end()) ? it->second : spec.defaultValue;
    } else {
      v2.paramValues[spec.id] = spec.defaultValue;
    }
  }

  // Derived mapping (not a direct alias): v1's boolean `shuffle` implies a
  // v2 preset-selection policy, so a migrated session keeps behaving the
  // way it used to rather than silently reverting to Sequential.
  if (auto it = v1.paramValues.find("shuffle"); it != v1.paramValues.end()) {
    constexpr float kSequential = 0.0f;
    constexpr float kShuffleNoRepeat = 1.0f;
    v2.paramValues["presetSelectionPolicy"] = (it->second != 0.0f) ? kShuffleNoRepeat : kSequential;
  }

  return v2;
}

namespace {

std::string formatBounds(const WindowBounds& b) {
  return std::to_string(b.x) + "," + std::to_string(b.y) + "," + std::to_string(b.width) + "," +
         std::to_string(b.height);
}

// Parsers never throw: a malformed line (hand-edited, truncated, or a host
// fuzzing the state, as pluginval does) is skipped and the field keeps its
// default.
bool parseInt(const std::string& text, int& out) {
  const auto* end = text.data() + text.size();
  int value = 0;
  const auto [ptr, ec] = std::from_chars(text.data(), end, value);
  if (ec != std::errc{} || ptr != end) {
    return false; // leave `out` untouched: from_chars would have kept a numeric prefix
  }
  out = value;
  return true;
}

bool parseFloat(const std::string& text, float& out) {
  // Classic locale: a host may have set a decimal-comma C locale, which
  // would make strtof misread "3.5".
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  float value = 0.0f;
  in >> value;
  if (in.fail() || !in.eof() || !std::isfinite(value)) {
    return false;
  }
  out = value;
  return true;
}

bool parseBool(const std::string& text, bool& out) {
  int value = 0;
  if (!parseInt(text, value)) {
    return false;
  }
  out = value != 0;
  return true;
}

bool parseBounds(const std::string& text, WindowBounds& out) {
  WindowBounds b;
  int* fields[] = {&b.x, &b.y, &b.width, &b.height};
  std::size_t start = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto comma = i < 3 ? text.find(',', start) : text.size();
    if (comma == std::string::npos || !parseInt(text.substr(start, comma - start), *fields[i])) {
      return false;
    }
    start = comma + 1;
  }
  out = b;
  return true;
}

} // namespace

std::string serializeStateSchemaV2(const StateSchemaV2& state) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "schemaVersion=" << state.schemaVersion << "\n";
  out << "presetAbsolutePath=" << state.presetAbsolutePath << "\n";
  out << "playlistFolderPath=" << state.playlistFolderPath << "\n";
  out << "editorWidth=" << state.editorWidth << "\n";
  out << "editorHeight=" << state.editorHeight << "\n";
  out << "outputWindowOpen=" << (state.windows.outputWindowOpen ? 1 : 0) << "\n";
  out << "outputWindowFullscreen=" << (state.windows.outputWindowFullscreen ? 1 : 0) << "\n";
  out << "outputWindowBounds=" << formatBounds(state.windows.outputWindowBounds) << "\n";
  out << "controlsFloating=" << (state.windows.controlsFloating ? 1 : 0) << "\n";
  out << "controlsWindowBounds=" << formatBounds(state.windows.controlsWindowBounds) << "\n";
  for (const auto& [id, value] : state.paramValues) {
    out << "param." << id << "=" << value << "\n";
  }
  return out.str();
}

StateSchemaV2 deserializeStateSchemaV2(const std::string& text) {
  StateSchemaV2 state;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);

    if (key == "schemaVersion") {
      parseInt(value, state.schemaVersion);
    } else if (key == "presetAbsolutePath") {
      state.presetAbsolutePath = value;
    } else if (key == "playlistFolderPath") {
      state.playlistFolderPath = value;
    } else if (key == "editorWidth") {
      parseInt(value, state.editorWidth);
    } else if (key == "editorHeight") {
      parseInt(value, state.editorHeight);
    } else if (key == "outputWindowOpen") {
      parseBool(value, state.windows.outputWindowOpen);
    } else if (key == "outputWindowFullscreen") {
      parseBool(value, state.windows.outputWindowFullscreen);
    } else if (key == "outputWindowBounds") {
      parseBounds(value, state.windows.outputWindowBounds);
    } else if (key == "controlsFloating") {
      parseBool(value, state.windows.controlsFloating);
    } else if (key == "controlsWindowBounds") {
      parseBounds(value, state.windows.controlsWindowBounds);
    } else if (key.rfind("param.", 0) == 0) {
      float parsed = 0.0f;
      if (parseFloat(value, parsed)) {
        state.paramValues[key.substr(6)] = parsed;
      }
    }
  }
  return state;
}

} // namespace milkdawp::core
