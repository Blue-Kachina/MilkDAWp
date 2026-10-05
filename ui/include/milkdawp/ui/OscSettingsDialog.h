// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/OscAddress.h"

namespace milkdawp::ui {

/// What the OSC dialog's fields say, read back into settings. Ports out of
/// 1..65535 or not numbers keep `current`'s; an empty host keeps it too.
[[nodiscard]] core::OscSettings oscSettingsFromFields(const core::OscSettings& current, bool enabled,
                                                      const juce::String& receivePort, const juce::String& sendHost,
                                                      const juce::String& sendPort);

/// Phase 8.4: Settings > OSC remote control. An asynchronous dialog (it never
/// blocks the host): on/off, the port to listen on, and where beat, bar, drop
/// and preset messages go. `status` says what the remote is doing now.
/// `onApply` gets the new settings when the user presses Apply.
void showOscSettingsDialog(const core::OscSettings& current, const juce::String& status,
                           std::function<void(const core::OscSettings&)> onApply,
                           juce::Component* centreAround = nullptr);

} // namespace milkdawp::ui
