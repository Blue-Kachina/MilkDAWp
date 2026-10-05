// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/DiagnosticsPanel.h"

#include <algorithm>

namespace milkdawp::ui {

namespace {

const juce::String kDot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));

juce::String str(const std::string& text) { return juce::String(juce::CharPointer_UTF8(text.c_str())); }

juce::String errorLine(const core::DiagnosticsError& error) {
  juce::String line = juce::Time(error.lastTimeMs).formatted("%H:%M:%S") + "  " +
                      str(error.source).paddedRight(' ', 9) + str(error.message);
  if (error.count > 1) {
    line << " (x" << juce::String(error.count) << ")";
  }
  return line;
}

} // namespace

juce::StringArray formatDiagnosticsLines(const core::DiagnosticsInfo& info, int maxErrors) {
  juce::StringArray lines;
  if (info.engineAvailable) {
    juce::String renderer = "projectM " + str(info.projectMVersion);
    if (!info.glDescription.empty()) {
      renderer << kDot << str(info.glDescription);
    }
    lines.add(renderer);

    juce::String frame;
    if (info.paused) {
      frame << "paused (no visible picture)";
    } else {
      frame << juce::String(info.framesPerSecond, 1) << " fps";
    }
    frame << kDot << info.width << "x" << info.height << kDot << "render " << juce::String(info.cpuFrameMs, 1)
          << " ms (gpu " << (info.gpuFrameMs >= 0.0f ? juce::String(info.gpuFrameMs, 1) + " ms" : juce::String("n/a"))
          << ")" << kDot << "quality " << juce::roundToInt(info.qualityScale * 100.0f) << "%"
          << (info.qualityAuto ? " (auto)" : " (fixed)");
    if (info.layers > 1) {
      frame << kDot << info.layers << " layers";
    }
    if (info.gpuSharers > 1) {
      frame << kDot << "GPU shared by " << info.gpuSharers << " instances";
    }
    lines.add(frame);
  } else {
    lines.add("projectM: " + (info.engineUnavailableReason.empty()
                                  ? juce::String("starting...")
                                  : "unavailable (" + str(info.engineUnavailableReason) + ")"));
  }

  juce::String beat = "beat: " + str(info.beatSource);
  if (info.bpm > 0.0f) {
    beat << " " << juce::String(info.bpm, 1) << " BPM";
  }
  beat << ", confidence " << juce::String(info.beatConfidence, 2) << kDot << "bass "
       << juce::roundToInt(info.bassLevelDb) << " dB (loud " << juce::roundToInt(info.bassReferenceDb) << ")"
       << (info.inBreakdown ? ", breakdown" : "") << kDot << juce::String(info.dropsDetected) << " drops";
  lines.add(beat);

  juce::String presets = "presets: " + juce::String(info.playlistSize) + " in folder, " +
                         juce::String(info.presetsLoaded) + " loaded, " + juce::String(info.presetsSkipped) +
                         " skipped" + kDot + "last load " + juce::String(info.lastPresetLoadMs, 1) + " ms";
  if (!info.currentPreset.empty()) {
    presets << kDot << "now " << str(info.currentPreset);
  }
  lines.add(presets);

  juce::String shell;
  if (!info.input.empty()) {
    shell << "input: " << str(info.input);
  }
  if (!info.surface.empty()) {
    shell << (shell.isEmpty() ? "" : kDot) << "surface: " << str(info.surface);
  }
  if (shell.isNotEmpty()) {
    lines.add(shell);
  }

  if (info.recentErrors.empty()) {
    lines.add("errors: none");
  } else {
    const auto total = static_cast<int>(info.recentErrors.size());
    const auto shown = std::min(total, std::max(maxErrors, 0));
    lines.add("recent errors (" + juce::String(total) + (shown < total ? ", newest " + juce::String(shown) : "") +
              "):");
    for (int i = total - shown; i < total; ++i) {
      lines.add("  " + errorLine(info.recentErrors[static_cast<std::size_t>(i)]));
    }
  }
  return lines;
}

juce::String formatDiagnosticsReport(const core::DiagnosticsInfo& info) {
  juce::String report;
  if (!info.shell.empty()) {
    report << str(info.shell) << "\n";
  }
  report << "Collected: " << juce::Time::getCurrentTime().toISO8601(true) << "\n";
  report << "System: " << juce::SystemStats::getOperatingSystemName() << ", "
         << juce::SystemStats::getNumCpus() << " CPUs, " << juce::SystemStats::getMemorySizeInMegabytes()
         << " MB\n\n";
  report << formatDiagnosticsLines(info, static_cast<int>(info.recentErrors.size())).joinIntoString("\n") << "\n";
  return report;
}

DiagnosticsPanel::DiagnosticsPanel()
    : font_(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)) {
  titleLabel.setText("Diagnostics", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(titleLabel);

  copyButton.setTooltip("Copy everything shown here, plus every recent error, for a bug report");
  copyButton.onClick = [this] { copyToClipboard(); };
  addAndMakeVisible(copyButton);

  closeButton.setTooltip("Close (Esc)");
  closeButton.onClick = [this] {
    if (onCloseRequested) {
      onCloseRequested();
    }
  };
  addAndMakeVisible(closeButton);

  lines_ = formatDiagnosticsLines(info_, kVisibleErrors);
}

void DiagnosticsPanel::update(core::DiagnosticsInfo info) {
  info_ = std::move(info);
  auto lines = formatDiagnosticsLines(info_, kVisibleErrors);
  if (lines != lines_) {
    lines_ = std::move(lines);
    repaint();
  }
}

int DiagnosticsPanel::preferredHeight() const noexcept {
  return kHeaderHeight + 4 + lines_.size() * kLineHeight + 8;
}

void DiagnosticsPanel::copyToClipboard() {
  juce::SystemClipboard::copyTextToClipboard(formatDiagnosticsReport(info_));
  copyButton.setButtonText("Copied");
  juce::Timer::callAfterDelay(1500, [safe = juce::Component::SafePointer<DiagnosticsPanel>(this)] {
    if (safe != nullptr) {
      safe->copyButton.setButtonText("Copy diagnostics");
    }
  });
}

void DiagnosticsPanel::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(juce::Colours::black.withAlpha(0.8f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  g.setFont(font_);
  auto area = getLocalBounds().reduced(10, 0).withTrimmedTop(kHeaderHeight + 4);
  for (const auto& line : lines_) {
    const bool isError = line.startsWith("  ");
    g.setColour(isError ? juce::Colour(0xffffb347) : juce::Colours::white.withAlpha(0.9f));
    g.drawText(line, area.removeFromTop(kLineHeight), juce::Justification::centredLeft, true);
  }
}

void DiagnosticsPanel::resized() {
  auto header = getLocalBounds().reduced(8, 4).removeFromTop(kHeaderHeight - 4);
  closeButton.setBounds(header.removeFromRight(56));
  header.removeFromRight(6);
  copyButton.setBounds(header.removeFromRight(130));
  titleLabel.setBounds(header);
}

} // namespace milkdawp::ui
