// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/OscSettingsDialog.h"

#include <memory>

namespace milkdawp::ui {

namespace {

int portOr(const juce::String& text, int fallback) {
  const auto trimmed = text.trim();
  if (trimmed.isEmpty() || !trimmed.containsOnly("0123456789")) {
    return fallback;
  }
  const int port = trimmed.getIntValue();
  return port >= 1 && port <= 65535 ? port : fallback;
}

} // namespace

core::OscSettings oscSettingsFromFields(const core::OscSettings& current, bool enabled,
                                        const juce::String& receivePort, const juce::String& sendHost,
                                        const juce::String& sendPort) {
  core::OscSettings settings = current;
  settings.enabled = enabled;
  settings.receivePort = portOr(receivePort, current.receivePort);
  settings.sendPort = portOr(sendPort, current.sendPort);
  if (sendHost.trim().isNotEmpty()) {
    settings.sendHost = sendHost.trim().toStdString();
  }
  return settings;
}

void showOscSettingsDialog(const core::OscSettings& current, const juce::String& status,
                           std::function<void(const core::OscSettings&)> onApply, juce::Component* centreAround) {
  auto* window = new juce::AlertWindow(
      "OSC remote control",
      "Move any parameter from another app or device: /milkdawp/<instance>/<parameterId> f, or "
      "/milkdawp/<parameterId> f for every instance (add /norm for 0..1). /milkdawp/next and /prev step "
      "presets. Beats, bars, drops and preset names go out to the send address.\n\nNow: " +
          status,
      juce::MessageBoxIconType::NoIcon, centreAround);
  window->addComboBox("enabled", {"Off", "On"}, "OSC");
  if (auto* combo = window->getComboBoxComponent("enabled")) {
    combo->setSelectedItemIndex(current.enabled ? 1 : 0, juce::dontSendNotification);
  }
  window->addTextEditor("receivePort", juce::String(current.receivePort), "Listen on port");
  window->addTextEditor("sendHost", juce::String(current.sendHost), "Send to host");
  window->addTextEditor("sendPort", juce::String(current.sendPort), "Send to port");
  window->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
  window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

  window->enterModalState(true,
                          juce::ModalCallbackFunction::create([window, current, apply = std::move(onApply)](int result) {
                            if (result != 1 || !apply) {
                              return;
                            }
                            const auto* combo = window->getComboBoxComponent("enabled");
                            apply(oscSettingsFromFields(current, combo != nullptr && combo->getSelectedItemIndex() == 1,
                                                        window->getTextEditorContents("receivePort"),
                                                        window->getTextEditorContents("sendHost"),
                                                        window->getTextEditorContents("sendPort")));
                          }),
                          true);
}

} // namespace milkdawp::ui
