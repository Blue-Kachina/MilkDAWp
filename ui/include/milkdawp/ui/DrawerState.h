// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>

namespace milkdawp::ui {

/// Visual state of the control drawer (§4.9, Phase 2.11).
enum class DrawerVisualState : std::uint8_t {
  Hidden,
  Revealed,
  Pinned,
};

/// DrawerStateMachine::Config. At namespace scope because the constructor
/// defaults it with `= {}`: GCC and Clang reject that for a nested class with
/// default member initializers (CWG 1397), though MSVC accepts it.
struct DrawerStateMachineConfig {
  bool startPinned = false;     // e.g. true for the plugin editor's default (§4.9)
  bool firstRunRevealed = true; // "drawer starts open until the first interaction"
  double autoHideSeconds = 3.0;
};

/// The reveal/hide/pin state machine behind `ControlDrawer`, deliberately
/// kept free of JUCE (no `Component`, no `Timer`) so it is a plain,
/// deterministic unit under test -- the roadmap's own requirement for this
/// item. `ControlDrawer` (later in Phase 2.11/3.3/4.1) is a thin JUCE
/// wrapper: it forwards `Component::mouseEnter`/`mouseDown` to
/// `onPointerActivity()`, calls `tick()` from a `Timer` callback with
/// `Time::getMillisecondCounterHiRes() / 1000.0`, and reads `state()` to
/// decide what to paint.
///
/// Time is never read internally -- every method that needs "now" takes it
/// as a parameter -- so tests can drive the clock exactly instead of
/// sleeping.
///
/// Rules encoded here, from §4.9's table:
///  - Reveal on hover or tap; auto-hide after `autoHideSeconds` of no
///    pointer activity, but only once pinned.
///  - Pinned suppresses auto-hide entirely, and 'H' (toggleRevealHide) is a
///    no-op while pinned -- unpin first ('P' / setPinned(false)), *then*
///    hide. This is deliberate: the whole point of pinning is that the
///    drawer can't disappear out from under an in-progress knob edit.
///  - First-run reveal: the drawer starts open and stays open regardless of
///    elapsed time until the very first interaction of any kind (pointer
///    activity, a pin toggle, or a reveal/hide toggle) -- after that,
///    ordinary timer-based auto-hide applies.
class DrawerStateMachine {
public:
  using Config = DrawerStateMachineConfig;

  explicit DrawerStateMachine(Config config = {});

  [[nodiscard]] DrawerVisualState state() const noexcept;
  [[nodiscard]] bool isVisible() const noexcept { return state() != DrawerVisualState::Hidden; }
  [[nodiscard]] bool isPinned() const noexcept { return pinned_; }

  /// Hover or tap on the drawer/scrim. Always reveals and resets the
  /// auto-hide countdown; counts as the first interaction.
  void onPointerActivity(double nowSeconds);

  /// Call periodically (e.g. once per UI frame) with the current time.
  /// Hides the drawer once `autoHideSeconds` have elapsed with no pointer
  /// activity -- but only when not pinned and after the first interaction
  /// has happened (see class comment). A no-op otherwise.
  void tick(double nowSeconds);

  /// 'P': pin or unpin. Pinning always reveals; unpinning starts the
  /// auto-hide countdown fresh from `nowSeconds` (so it doesn't vanish the
  /// instant the pointer happens to already be elsewhere). Counts as the
  /// first interaction.
  void setPinned(bool pinned, double nowSeconds);
  void togglePin(double nowSeconds);

  /// 'H': reveal or hide, respecting pin (a no-op while pinned -- see class
  /// comment). Counts as the first interaction.
  void toggleRevealHide(double nowSeconds);

private:
  void markFirstInteraction(double nowSeconds);

  bool pinned_;
  bool revealed_;
  bool hasHadFirstInteraction_ = false;
  double lastActivityTime_ = 0.0;
  double autoHideSeconds_;
};

} // namespace milkdawp::ui
