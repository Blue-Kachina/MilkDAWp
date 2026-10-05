// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/PresetInfoMenu.h"

#include <memory>
#include <utility>

namespace milkdawp::ui {

juce::String ratingText(int rating) {
  if (rating <= 0) {
    return "Not rated";
  }
  const juce::String full(juce::CharPointer_UTF8("\xe2\x98\x85"));  // ★
  const juce::String empty(juce::CharPointer_UTF8("\xe2\x98\x86")); // ☆
  juce::String text;
  for (int i = 1; i <= 5; ++i) {
    text << (i <= rating ? full : empty);
  }
  return text;
}

void addPresetInfoItems(juce::PopupMenu& menu,
                        const core::PresetInfo& info,
                        const std::function<void(const core::PresetInfo&)>& onChange,
                        const std::function<void()>& onEditTags) {
  juce::PopupMenu ratings;
  for (int stars = 5; stars >= 0; --stars) {
    // The number too: the star glyphs come out small in some fonts.
    juce::PopupMenu::Item item(stars == 0 ? juce::String("No rating")
                                          : ratingText(stars) + "   " + juce::String(stars) +
                                                (stars == 1 ? " star" : " stars"));
    item.setTicked(info.rating == stars);
    item.setAction([info, stars, onChange] {
      auto changed = info;
      changed.rating = stars;
      onChange(changed);
    });
    ratings.addItem(std::move(item));
  }
  menu.addSubMenu(info.rating > 0 ? "Rating: " + ratingText(info.rating) + " " +
                                        juce::String(info.rating) + "/5"
                                  : juce::String("Rating: not rated"),
                  ratings);

  juce::PopupMenu::Item never("Never auto-select");
  never.setTicked(info.neverAutoSelect);
  never.setAction([info, onChange] {
    auto changed = info;
    changed.neverAutoSelect = !changed.neverAutoSelect;
    onChange(changed);
  });
  menu.addItem(std::move(never));

  const auto tags = juce::String(core::PresetMetadata::joinTags(info.tags)).replace(",", ", ");
  juce::PopupMenu::Item editTags(tags.isEmpty() ? juce::String("Tags...")
                                                : "Tags: " + tags + "...");
  editTags.setAction(onEditTags);
  menu.addItem(std::move(editTags));
}

void showTagEditor(const juce::String& presetName,
                   const std::vector<std::string>& current,
                   const std::vector<std::string>& known,
                   juce::Component* associated,
                   std::function<void(std::vector<std::string>)> onDone) {
  juce::String message = "Comma-separated, for example: calm, dark, abstract.";
  if (!known.empty()) {
    message << "\n\nIn use: "
            << juce::String(core::PresetMetadata::joinTags(known)).replace(",", ", ");
  }
  auto* window = new juce::AlertWindow(
      "Tags for " + presetName, message, juce::MessageBoxIconType::NoIcon, associated);
  window->addTextEditor("tags",
                        juce::String(core::PresetMetadata::joinTags(current)).replace(",", ", "));
  window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
  window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  window->enterModalState(
      true,
      juce::ModalCallbackFunction::create([window, done = std::move(onDone)](int result) {
        if (result == 1 && done) {
          done(
              core::PresetMetadata::parseTags(window->getTextEditorContents("tags").toStdString()));
        }
      }),
      true); // deletes the window when dismissed
}

void addPresetInfoSection(juce::PopupMenu& menu,
                          const juce::String& presetName,
                          const PresetInfoAccess& access,
                          juce::Component* associated) {
  if (!access.get || !access.set) {
    return;
  }
  menu.addSeparator();
  menu.addSectionHeader(presetName);
  // A SafePointer: the tag editor opens after the menu has closed, by which
  // time the component may be gone.
  juce::Component::SafePointer<juce::Component> safeAssociated(associated);
  addPresetInfoItems(menu, access.get(), access.set, [access, presetName, safeAssociated] {
    const auto known = access.knownTags ? access.knownTags() : std::vector<std::string>{};
    showTagEditor(presetName,
                  access.get().tags,
                  known,
                  safeAssociated.getComponent(),
                  [access](std::vector<std::string> tags) {
                    auto info = access.get();
                    info.tags = std::move(tags);
                    access.set(info);
                  });
  });
}

} // namespace milkdawp::ui
