// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "SignalMonitor.h"

using milkdawp::app::SignalMonitor;
using State = SignalMonitor::State;

TEST_CASE("SignalMonitor reports a missing device at once", "[app][signal]") {
  SignalMonitor monitor;
  CHECK(monitor.update(false, 0.5f, 0.0) == State::NoDevice);
}

TEST_CASE("SignalMonitor waits before calling silence 'no signal'", "[app][signal]") {
  SignalMonitor monitor;
  CHECK(monitor.update(true, 0.0f, 10.0) == State::Signal); // just opened: benefit of the doubt
  CHECK(monitor.update(true, 0.0f, 12.0) == State::Signal);
  CHECK(monitor.update(true, 0.0f, 13.0) == State::NoSignal);
}

TEST_CASE("SignalMonitor clears the hint on the first real peak", "[app][signal]") {
  SignalMonitor monitor;
  monitor.update(true, 0.0f, 0.0);
  REQUIRE(monitor.update(true, 0.0f, 5.0) == State::NoSignal);
  CHECK(monitor.update(true, 0.2f, 5.1) == State::Signal);
  // A short pause afterwards is not "no signal" yet.
  CHECK(monitor.update(true, 0.0f, 7.0) == State::Signal);
  CHECK(monitor.update(true, 0.0f, 8.2) == State::NoSignal);
}

TEST_CASE("SignalMonitor treats noise below the threshold as silence", "[app][signal]") {
  SignalMonitor monitor;
  monitor.update(true, 0.0f, 0.0);
  CHECK(monitor.update(true, SignalMonitor::kThreshold * 0.5f, 4.0) == State::NoSignal);
}

TEST_CASE("SignalMonitor restarts the wait when a device reopens", "[app][signal]") {
  SignalMonitor monitor;
  monitor.update(true, 0.0f, 0.0);
  REQUIRE(monitor.update(true, 0.0f, 5.0) == State::NoSignal);
  CHECK(monitor.update(false, 0.0f, 6.0) == State::NoDevice);
  CHECK(monitor.update(true, 0.0f, 7.0) == State::Signal);
  CHECK(monitor.update(true, 0.0f, 10.5) == State::NoSignal);
}
