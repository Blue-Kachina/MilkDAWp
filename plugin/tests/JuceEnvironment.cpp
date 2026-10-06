// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Every host runs a plugin with JUCE's message system up. These tests build
// processors directly, so this listener starts JUCE for the whole run and
// shuts it down at the end, as a host would. Without it, every timer, async
// update and message listener in a processor trips a JUCE assertion (the
// message manager doesn't exist), and singletons created on demand are never
// torn down, which showed up as a leak report at exit.

#include <memory>

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_events/juce_events.h>

namespace {

class JuceEnvironment final : public Catch::EventListenerBase {
public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(const Catch::TestRunInfo&) override {
    juce_ = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
  }
  void testRunEnded(const Catch::TestRunStats&) override { juce_.reset(); }

private:
  std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce_;
};

} // namespace

CATCH_REGISTER_LISTENER(JuceEnvironment)
