// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/MediaMenu.h"

namespace milkdawp::ui {

juce::PopupMenu cameraMenu(bool supported, const juce::StringArray& devices, const juce::String& currentDevice,
                           std::function<void(const juce::String&)> onPick) {
  juce::PopupMenu menu;
  if (!supported) {
    menu.addItem("Cameras aren't supported on this system yet", false, false, [] {});
    return menu;
  }
  if (devices.isEmpty()) {
    menu.addItem("No camera found", false, false, [] {});
    return menu;
  }
  for (const auto& device : devices) {
    menu.addItem(device, true, device == currentDevice, [device, onPick] {
      if (onPick) {
        onPick(device);
      }
    });
  }
  return menu;
}

} // namespace milkdawp::ui
