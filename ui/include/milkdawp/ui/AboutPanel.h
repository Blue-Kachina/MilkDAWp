// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/ui/KeyboardNavigation.h"

namespace milkdawp::ui {

/// What About shows about this build (the shell fills it in).
struct AboutInfo {
  juce::String version;         // core::versionString(), e.g. "1.0.0-beta.1"
  juce::String shell;           // "app", "VST3 in REAPER", ...
  juce::String projectMVersion; // empty: not loaded
  juce::String juceVersion;     // juce::SystemStats::getJUCEVersion()
  juce::String operatingSystem; // juce::SystemStats::getOperatingSystemName()
};

/// The version lines About shows (also what "Copy" puts on the clipboard).
[[nodiscard]] juce::StringArray formatAboutVersions(const AboutInfo& info);

/// The credits and licences, one paragraph per entry.
[[nodiscard]] juce::StringArray aboutCredits();

/// 6.7: About MilkDAWp, a popover over the picture like Diagnostics (Settings
/// > About in the plugin, Help > About in the app): versions, credits and
/// licences (AGPL, projectM's LGPL, JUCE, Lucide, the bundled presets and
/// their curator), links to the source, the user guide and the releases,
/// and the update check's state. The check itself belongs to the shell; in
/// the plugin it is read-only (it shows what the app last found).
class AboutPanel : public juce::Component {
public:
  static constexpr int preferredWidth = 560;
  static constexpr int preferredHeight = 470;

  AboutPanel();

  void setInfo(const AboutInfo& info);

  /// The update row. `canCheck`: this shell checks itself (the app); false
  /// shows `status` and a note that the app does the checking (the plugin).
  void setUpdateControls(bool canCheck, bool autoCheckEnabled);
  /// `url` non-empty makes the status a link (a newer release's page).
  void setUpdateStatus(const juce::String& status, const juce::String& url, bool highlight);

  std::function<void()> onCloseRequested;
  std::function<void(bool)> onAutoCheckChanged;
  std::function<void()> onCheckNow;

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override { focusRing_.paint(g); }
  void resized() override;

  juce::Label titleLabel;
  juce::TextButton copyButton{"Copy versions"};
  juce::TextButton closeButton{"Close"};
  juce::TextEditor credits; // read-only, scrolls
  juce::HyperlinkButton sourceLink{"Source code", juce::URL("https://github.com/Blue-Kachina/MilkDAWp2")};
  juce::HyperlinkButton guideLink{"User guide",
                                  juce::URL("https://github.com/Blue-Kachina/MilkDAWp2/blob/HEAD/docs/user-guide/README.md")};
  juce::HyperlinkButton releasesLink{"Releases", juce::URL("https://github.com/Blue-Kachina/MilkDAWp2/releases")};
  juce::ToggleButton autoCheckToggle{"Check for updates once a day"};
  juce::TextButton checkNowButton{"Check now"};
  juce::Label updateLabel;
  juce::HyperlinkButton updateLink;

private:
  static constexpr int kHeaderHeight = 30;
  static constexpr int kLineHeight = 17;

  AboutInfo info_;
  juce::StringArray versionLines_;
  bool canCheck_ = false;
  KeyboardFocusRing focusRing_{*this};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};

} // namespace milkdawp::ui
