// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// Settings > Media source > Camera (8.6b): one item per attached camera,
/// ticked for `currentDevice`. Without camera support, or with no camera
/// attached, a single disabled item says so.
[[nodiscard]] juce::PopupMenu cameraMenu(bool supported, const juce::StringArray& devices,
                                         const juce::String& currentDevice,
                                         std::function<void(const juce::String&)> onPick);

} // namespace milkdawp::ui
