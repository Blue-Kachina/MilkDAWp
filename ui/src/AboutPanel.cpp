// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/AboutPanel.h"

#include "milkdawp/ui/DrawerLookAndFeel.h"

namespace milkdawp::ui {

juce::StringArray formatAboutVersions(const AboutInfo& info) {
  juce::StringArray lines;
  lines.add("MilkDAWp " + info.version + (info.shell.isNotEmpty() ? " (" + info.shell + ")" : juce::String()));
  lines.add("projectM " + (info.projectMVersion.isNotEmpty() ? info.projectMVersion : juce::String("not loaded")));
  // getJUCEVersion() says "JUCE v9.0.2"; show just the number.
  const auto juceNumber = info.juceVersion.replace("JUCE v", "").replace("JUCE ", "").trim();
  lines.add("JUCE " + juceNumber + (info.operatingSystem.isNotEmpty() ? " on " + info.operatingSystem : juce::String()));
  return lines;
}

juce::StringArray aboutCredits() {
  return {
      "MilkDAWp: copyright the MilkDAWp contributors, licensed under the GNU Affero General Public "
      "License, version 3 or later (AGPL-3.0-or-later). You may use, study, share and change it; "
      "if you share a changed version, share its source too. The source is on GitHub.",
      "projectM renders the presets: copyright the projectM team, GNU Lesser General Public License "
      "2.1 (LGPL-2.1). MilkDAWp loads it as a separate library (projectM-4.dll, libprojectM-4.so or "
      ".dylib) that you may replace with your own build.",
      "\"Cream of the CrAWp\" presets: the \"Cream of the Crop\" pack, about 9,700 MilkDrop presets "
      "curated and sorted by Jason Fletcher (ISOSCELES), packaged by the projectM project, with "
      "the MilkDrop texture pack. Each preset's author keeps its copyright; they were freely "
      "released, and each is shared as its author wrote it, with lines added that give it "
      "MilkDAWp's Macros. If you wrote one and want it removed, open an issue.",
      "Built with JUCE (juce.com), used under the AGPL-3.0. Icons from Lucide (lucide.dev), ISC "
      "licence. MilkDrop was created by Ryan Geiss.",
      "Full licence texts and every third-party notice ship with MilkDAWp (LICENSE, LICENSES/ and "
      "THIRD_PARTY_NOTICES.md).",
  };
}

AboutPanel::AboutPanel() {
  titleLabel.setText("About MilkDAWp", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(titleLabel);

  copyButton.setTooltip("Copy the versions, for a bug report");
  copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(versionLines_.joinIntoString("\n")); };
  closeButton.setTooltip("Close (Esc)");
  closeButton.onClick = [this] {
    if (onCloseRequested) {
      onCloseRequested();
    }
  };
  addAndMakeVisible(copyButton);
  addAndMakeVisible(closeButton);

  credits.setMultiLine(true, true);
  credits.setReadOnly(true);
  credits.setCaretVisible(false);
  credits.setScrollbarsShown(true);
  credits.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
  credits.setColour(juce::TextEditor::outlineColourId, drawerTheme::hairline);
  credits.setColour(juce::TextEditor::textColourId, drawerTheme::textSecondary);
  credits.setFont(juce::FontOptions(13.0f));
  credits.setText(aboutCredits().joinIntoString("\n\n"), false);
  credits.setTitle("Credits and licences");
  addAndMakeVisible(credits);

  for (auto* link : {&sourceLink, &guideLink, &releasesLink}) {
    link->setFont(juce::FontOptions(13.0f), false, juce::Justification::centredLeft);
    link->setColour(juce::HyperlinkButton::textColourId, drawerTheme::accent);
    addAndMakeVisible(*link);
  }

  autoCheckToggle.setTooltip("Asks GitHub once a day whether a newer MilkDAWp exists. Nothing else is sent.");
  autoCheckToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  autoCheckToggle.onClick = [this] {
    if (onAutoCheckChanged) {
      onAutoCheckChanged(autoCheckToggle.getToggleState());
    }
  };
  checkNowButton.onClick = [this] {
    if (onCheckNow) {
      onCheckNow();
    }
  };
  updateLabel.setColour(juce::Label::textColourId, drawerTheme::textSecondary);
  updateLabel.setFont(juce::FontOptions(13.0f));
  updateLink.setFont(juce::FontOptions(13.0f, juce::Font::bold), false, juce::Justification::centredLeft);
  updateLink.setColour(juce::HyperlinkButton::textColourId, drawerTheme::accent);
  addAndMakeVisible(autoCheckToggle);
  addAndMakeVisible(checkNowButton);
  addAndMakeVisible(updateLabel);
  addChildComponent(updateLink);

  setUpdateControls(false, false);
  setSize(preferredWidth, preferredHeight);
}

void AboutPanel::setInfo(const AboutInfo& info) {
  info_ = info;
  versionLines_ = formatAboutVersions(info);
  repaint();
}

void AboutPanel::setUpdateControls(bool canCheck, bool autoCheckEnabled) {
  canCheck_ = canCheck;
  autoCheckToggle.setVisible(canCheck);
  checkNowButton.setVisible(canCheck);
  autoCheckToggle.setToggleState(autoCheckEnabled, juce::dontSendNotification);
  resized();
}

void AboutPanel::setUpdateStatus(const juce::String& status, const juce::String& url, bool highlight) {
  updateLink.setVisible(url.isNotEmpty());
  updateLabel.setVisible(url.isEmpty());
  if (url.isNotEmpty()) {
    updateLink.setButtonText(status);
    updateLink.setURL(juce::URL(url));
    updateLink.setTooltip(url);
  } else {
    updateLabel.setText(status, juce::dontSendNotification);
    updateLabel.setColour(juce::Label::textColourId, highlight ? drawerTheme::accent : drawerTheme::textSecondary);
  }
}

void AboutPanel::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(juce::Colours::black.withAlpha(0.94f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  auto area = getLocalBounds().reduced(12, 0).withTrimmedTop(kHeaderHeight + 6);
  for (int i = 0; i < versionLines_.size(); ++i) {
    g.setColour(i == 0 ? juce::Colours::white : drawerTheme::textSecondary);
    g.setFont(juce::FontOptions(i == 0 ? 15.0f : 13.0f, i == 0 ? juce::Font::bold : juce::Font::plain));
    g.drawText(versionLines_[i], area.removeFromTop(i == 0 ? kLineHeight + 4 : kLineHeight),
               juce::Justification::centredLeft, true);
  }
}

void AboutPanel::resized() {
  auto area = getLocalBounds().reduced(10, 6);
  auto header = area.removeFromTop(kHeaderHeight - 4);
  closeButton.setBounds(header.removeFromRight(56));
  header.removeFromRight(6);
  copyButton.setBounds(header.removeFromRight(110));
  titleLabel.setBounds(header);

  area.removeFromTop(10 + kLineHeight * 3 + 4); // the version lines (painted)
  auto links = area.removeFromTop(22);
  sourceLink.setBounds(links.removeFromLeft(100));
  guideLink.setBounds(links.removeFromLeft(90));
  releasesLink.setBounds(links.removeFromLeft(80));
  area.removeFromTop(6);

  auto update = area.removeFromBottom(26);
  if (canCheck_) {
    checkNowButton.setBounds(update.removeFromRight(90));
    update.removeFromRight(8);
    autoCheckToggle.setBounds(update.removeFromLeft(std::min(240, update.getWidth() / 2)));
    update.removeFromLeft(8);
  }
  updateLabel.setBounds(update);
  updateLink.setBounds(update);
  area.removeFromBottom(8);
  credits.setBounds(area);
}

} // namespace milkdawp::ui
