// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::core {

/// OSC remote control (Phase 8.4). Off until the user turns it on: listening
/// on a port makes the OS firewall ask, and in a plugin it asks in the host's
/// name. Shared by the app and every plugin instance in a process.
struct OscSettings {
  bool enabled = false;
  int receivePort = 9000;
  std::string sendHost = "127.0.0.1";
  int sendPort = 9001;

  friend bool operator==(const OscSettings&, const OscSettings&) = default;
};

/// One incoming OSC message, as MilkDAWp reads its address:
///
///   /milkdawp/<instance>/<parameterId>        f   value in the parameter's own units
///   /milkdawp/<instance>/<parameterId>/norm   f   value 0..1 across the range
///   /milkdawp/<instance>/next                     next preset (also /prev)
///
/// `<instance>` is an instance's name as `oscName()` spells it, its id, or `*`;
/// leaving it out (`/milkdawp/visualHue f`) addresses every instance.
struct OscCommand {
  enum class Kind { Parameter, Next, Previous };
  Kind kind = Kind::Parameter;
  std::string instance; // empty: every instance
  std::string parameterId;
  bool normalized = false;
};

/// Null for an address that isn't MilkDAWp's.
[[nodiscard]] std::optional<OscCommand> parseOscAddress(std::string_view address);

/// An instance name as an OSC address segment: lower case, with anything other
/// than a letter, digit, '-' or '_' made '_' ("Lead Guitar" is "lead_guitar").
/// Empty names stay empty.
[[nodiscard]] std::string oscName(std::string_view name);

/// Whether `command` addresses the instance called `name` with id `id`.
[[nodiscard]] bool oscAddresses(const OscCommand& command, std::string_view name, std::string_view id);

/// A value 0..1 across `spec`'s range, as a host's normalised value (with the
/// same skew), in the parameter's own units. Bool, Int and Choice round.
[[nodiscard]] float plainFromNormalized(const ParameterSpec& spec, float normalized);

} // namespace milkdawp::core
