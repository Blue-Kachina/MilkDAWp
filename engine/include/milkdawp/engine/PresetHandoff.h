// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "milkdawp/core/Messages.h"

namespace milkdawp::engine {

/// Hands preset text read off the render thread (by the director) to the
/// render thread, so projectM loads from memory and the render thread never
/// touches the filesystem (§4.2, Phase 2.6). A fixed pool of slots moves
/// between the two threads through two SPSC queues:
///
///   director: acquire a free slot, write text + presetId, offer()
///   render:   receive() offers into its table, find() by presetId when the
///             transition is due, release() the slot afterwards
///
/// Only the director writes a slot's std::string, and only while it owns
/// the slot; the queue hand-off orders that write before the render
/// thread's read. The render thread never allocates or frees here.
///
/// If transitions are superseded before they fire, the render side's table
/// fills up; receive() then releases its oldest entries so the director
/// always has at least one free slot to offer the next preset.
///
/// The render thread also reports presetIds projectM rejected back to the
/// director (for PresetLoader's blacklist) through reportFailure().
class PresetHandoff {
public:
  static constexpr std::size_t kSlots = 4;

  PresetHandoff() {
    for (std::size_t i = 0; i < kSlots; ++i) {
      free_.push(static_cast<std::uint8_t>(i));
    }
  }

  // ---- director thread ----

  /// Copies `text` into a free slot and offers it to the render thread.
  /// Returns false if no slot is free (the render thread has not caught up
  /// yet); the caller retries on its next tick. `milkdawp`: the text was
  /// compiled from a `.milkdawp` (Phase 8.10), so the preset takes the Visual
  /// globals it can (Zoom, Rotation) itself rather than as post effects.
  bool offer(std::uint32_t presetId, const std::string& text, bool milkdawp = false) {
    const auto slot = free_.pop();
    if (!slot) {
      return false;
    }
    slots_[*slot].text = text;
    slots_[*slot].presetId = presetId;
    slots_[*slot].milkdawp = milkdawp;
    ready_.push(*slot); // cannot fail: at most kSlots slots exist
    return true;
  }

  /// Preset ids the render thread saw projectM reject, oldest first.
  std::optional<std::uint32_t> popFailure() noexcept { return failed_.pop(); }

  // ---- render thread ----

  /// Moves newly offered slots into the render thread's table.
  void receive() noexcept {
    while (const auto slot = ready_.pop()) {
      if (heldCount() >= kSlots - 1) {
        releaseOldest();
      }
      held_[*slot] = true;
      heldOrder_[*slot] = ++orderCounter_;
    }
  }

  /// The slot holding `presetId`, if one was offered and not yet released.
  [[nodiscard]] std::optional<std::uint8_t> find(std::uint32_t presetId) const noexcept {
    std::optional<std::uint8_t> best;
    for (std::size_t i = 0; i < kSlots; ++i) {
      if (held_[i] && slots_[i].presetId == presetId &&
          (!best || heldOrder_[i] > heldOrder_[*best])) {
        best = static_cast<std::uint8_t>(i);
      }
    }
    return best;
  }

  [[nodiscard]] const char* text(std::uint8_t slot) const noexcept { return slots_[slot].text.c_str(); }
  [[nodiscard]] bool milkdawp(std::uint8_t slot) const noexcept { return slots_[slot].milkdawp; }

  void release(std::uint8_t slot) noexcept {
    if (held_[slot]) {
      held_[slot] = false;
      free_.push(slot);
    }
  }

  void reportFailure(std::uint32_t presetId) noexcept { failed_.push(presetId); }

private:
  struct Slot {
    std::string text;
    std::uint32_t presetId = 0;
    bool milkdawp = false;
  };

  [[nodiscard]] std::size_t heldCount() const noexcept {
    std::size_t count = 0;
    for (const bool held : held_) {
      count += held ? 1 : 0;
    }
    return count;
  }

  void releaseOldest() noexcept {
    std::optional<std::size_t> oldest;
    for (std::size_t i = 0; i < kSlots; ++i) {
      if (held_[i] && (!oldest || heldOrder_[i] < heldOrder_[*oldest])) {
        oldest = i;
      }
    }
    if (oldest) {
      release(static_cast<std::uint8_t>(*oldest));
    }
  }

  std::array<Slot, kSlots> slots_{};
  core::SpscQueue<std::uint8_t, 8> free_;  // render -> director
  core::SpscQueue<std::uint8_t, 8> ready_; // director -> render
  core::SpscQueue<std::uint32_t, 16> failed_;

  // Render-thread-only bookkeeping.
  std::array<bool, kSlots> held_{};
  std::array<std::uint64_t, kSlots> heldOrder_{};
  std::uint64_t orderCounter_ = 0;
};

} // namespace milkdawp::engine
