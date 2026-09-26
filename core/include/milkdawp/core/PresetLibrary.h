// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace milkdawp::core {

/// Interns preset paths as small integer ids (§4.2: "strings cross threads
/// only as interned preset IDs"). `TransitionRequestMessage::presetId` and
/// friends carry these ids; the thread that owns the library (the engine's
/// director) turns them back into paths.
///
/// Ids are stable for the library's lifetime: the same path always gets the
/// same id, and ids are never reused, so an id that outlives a rescan still
/// means the same file (or nothing, if that file is gone). 0 is never
/// issued and means "no preset".
///
/// Not thread-safe; owned by one thread.
class PresetLibrary {
public:
  [[nodiscard]] std::uint32_t idFor(const std::string& absolutePath);
  [[nodiscard]] std::optional<std::string> pathFor(std::uint32_t id) const;
  [[nodiscard]] std::size_t size() const noexcept { return paths_.size(); }

private:
  std::unordered_map<std::string, std::uint32_t> ids_;
  std::vector<std::string> paths_; // index = id - 1
};

} // namespace milkdawp::core
