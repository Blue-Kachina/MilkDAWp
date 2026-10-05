// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/OscAddress.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace milkdawp::core {

namespace {

constexpr std::string_view kPrefix = "/milkdawp/";

std::vector<std::string_view> splitSegments(std::string_view path) {
  std::vector<std::string_view> segments;
  while (!path.empty()) {
    const auto slash = path.find('/');
    const auto segment = path.substr(0, slash);
    if (!segment.empty()) {
      segments.push_back(segment);
    }
    if (slash == std::string_view::npos) {
      break;
    }
    path.remove_prefix(slash + 1);
  }
  return segments;
}

// The last part of an address: a command or a parameter id.
OscCommand commandFor(std::string_view what) {
  OscCommand command;
  if (what == "next") {
    command.kind = OscCommand::Kind::Next;
  } else if (what == "prev" || what == "previous") {
    command.kind = OscCommand::Kind::Previous;
  } else {
    command.parameterId = std::string(what);
  }
  return command;
}

} // namespace

std::optional<OscCommand> parseOscAddress(std::string_view address) {
  if (address.substr(0, kPrefix.size()) != kPrefix) {
    return std::nullopt;
  }
  auto segments = splitSegments(address.substr(kPrefix.size()));
  bool normalized = false;
  if (segments.size() >= 2 && segments.back() == "norm") {
    normalized = true;
    segments.pop_back();
  }
  OscCommand command;
  if (segments.size() == 1) {
    command = commandFor(segments[0]);
  } else if (segments.size() == 2) {
    command = commandFor(segments[1]);
    if (segments[0] != "*" && segments[0] != "all") {
      command.instance = std::string(segments[0]);
    }
  } else {
    return std::nullopt;
  }
  if (normalized && command.kind != OscCommand::Kind::Parameter) {
    return std::nullopt;
  }
  command.normalized = normalized;
  return command;
}

std::string oscName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    out.push_back(std::isalnum(u) != 0 || c == '-' || c == '_' ? static_cast<char>(std::tolower(u)) : '_');
  }
  return out;
}

bool oscAddresses(const OscCommand& command, std::string_view name, std::string_view id) {
  if (command.instance.empty()) {
    return true;
  }
  if (!id.empty() && command.instance == id) {
    return true;
  }
  const auto safe = oscName(name);
  return !safe.empty() && oscName(command.instance) == safe;
}

float plainFromNormalized(const ParameterSpec& spec, float normalized) {
  const float p = std::clamp(normalized, 0.0f, 1.0f);
  const float span = spec.maxValue - spec.minValue;
  float value = spec.minValue + span * p;
  if (spec.type == ParameterType::Float && spec.skewCentre != 0.0f && span > 0.0f) {
    // JUCE's NormalisableRange::setSkewForCentre, so OSC and the host agree.
    const float skew = std::log(0.5f) / std::log((spec.skewCentre - spec.minValue) / span);
    value = spec.minValue + span * (p > 0.0f ? std::exp(std::log(p) / skew) : 0.0f);
  }
  if (spec.type != ParameterType::Float) {
    value = std::round(value);
  }
  return std::clamp(value, spec.minValue, spec.maxValue);
}

} // namespace milkdawp::core
