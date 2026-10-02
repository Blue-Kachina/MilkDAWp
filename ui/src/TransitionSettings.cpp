// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/TransitionSettings.h"

#include <cmath>

#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::ui {

TransitionSettingsRelevance transitionSettingsRelevance(core::TransitionMode mode, bool jitterEnabled, bool hardCuts,
                                                        bool gridSync) noexcept {
  using core::TransitionMode;
  TransitionSettingsRelevance r;
  r.bars = mode == TransitionMode::BeatQuantized || mode == TransitionMode::Hybrid || mode == TransitionMode::Energy;
  r.timedDuration =
      mode == TransitionMode::Timed || mode == TransitionMode::Hybrid || mode == TransitionMode::BeatQuantized;
  r.jitter = r.timedDuration;
  r.jitterRange = r.jitter && jitterEnabled;
  r.timedDuration = r.timedDuration && !jitterEnabled; // jitter picks the duration instead
  r.energyThreshold = mode == TransitionMode::Energy;
  r.grid = mode == TransitionMode::BeatQuantized || mode == TransitionMode::Energy;
  r.gridOffset = r.grid && gridSync;
  // Manual prev/next steps use the cut style too, so these apply in every mode.
  r.blend = !hardCuts;
  return r;
}

namespace {
void styleSlider(juce::Slider& slider) {
  slider.setSliderStyle(juce::Slider::LinearHorizontal);
  slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
}
} // namespace

TransitionSettingsPanel::TransitionSettingsPanel() {
  titleLabel.setText("Transitions", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(titleLabel);

  closeButton.setTooltip("Close (Esc)");
  closeButton.onClick = [this] {
    if (onCloseRequested) {
      onCloseRequested();
    }
  };
  addAndMakeVisible(closeButton);

  if (const auto* spec = core::findParameter(core::allParameters(), "transitionMode")) {
    int itemId = 1;
    for (const auto& choice : spec->choices) {
      modeCombo.addItem(choice, itemId++);
    }
  }

  for (auto* slider : {&barsSlider, &timedDurationSlider, &jitterMinSlider, &jitterMaxSlider, &energyThresholdSlider,
                       &blendSlider, &gridOffsetSlider}) {
    styleSlider(*slider);
  }
  gridToggle.setTooltip("Cut on a fixed beat grid instead of counting bars from this instance's start. "
                        "With the host's tempo, instances using the same bars and offset cut together");
  gridOffsetSlider.setTooltip("Which beat of the bars-long cycle to cut on (0 = the first beat). "
                              "Different offsets on different instances stagger their cuts");
  addAndMakeVisible(gridToggle);
  addAndMakeVisible(gridOffsetSlider);
  gridToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  barsSlider.setTooltip("Bars between beat-quantized transitions");
  timedDurationSlider.setTooltip("Seconds between timed transitions");
  jitterToggle.setTooltip("Pick each timed interval at random from the jitter range");
  energyThresholdSlider.setTooltip("Energy mode: how far above the recent average (in standard deviations) "
                                   "the level must jump to count as a drop");
  hardCutToggle.setTooltip("Cut instantly instead of blending");
  blendSlider.setTooltip("Seconds a soft cut blends over");

  addRow(modeRow, "Mode", modeCombo);
  addRow(barsRow, "Bars", barsSlider);
  addRow(timedRow, "Every (s)", timedDurationSlider);
  addRow(energyRow, "Energy", energyThresholdSlider);
  addAndMakeVisible(jitterToggle);
  addRow(jitterMinRow, "Min (s)", jitterMinSlider);
  addRow(jitterMaxRow, "Max (s)", jitterMaxSlider);
  addAndMakeVisible(hardCutToggle);
  addRow(blendRow, "Blend (s)", blendSlider);

  for (auto* toggle : {&jitterToggle, &hardCutToggle}) {
    toggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  }

  // The shell's attachments set the values; these keep the dimming current
  // for changes made in the panel itself. Host automation changes are picked
  // up by the shell calling refreshRelevance().
  modeCombo.onChange = [this] { refreshRelevance(); };
  jitterToggle.onStateChange = [this] { refreshRelevance(); };
  hardCutToggle.onStateChange = [this] { refreshRelevance(); };
  gridToggle.onStateChange = [this] { refreshRelevance(); };

  setSize(preferredWidth, preferredHeight);
  refreshRelevance();
}

TransitionSettingsPanel::~TransitionSettingsPanel() = default;

void TransitionSettingsPanel::addRow(Row& row, const juce::String& text, juce::Component& control) {
  row.label.setText(text, juce::dontSendNotification);
  row.label.setColour(juce::Label::textColourId, juce::Colours::white);
  row.label.setJustificationType(juce::Justification::centredLeft);
  row.control = &control;
  addAndMakeVisible(row.label);
  addAndMakeVisible(control);
}

void TransitionSettingsPanel::refreshRelevance() {
  const auto modeIndex = std::max(modeCombo.getSelectedItemIndex(), 0);
  const auto r = transitionSettingsRelevance(static_cast<core::TransitionMode>(modeIndex),
                                             jitterToggle.getToggleState(), hardCutToggle.getToggleState(),
                                             gridToggle.getToggleState());
  // Dimmed, not disabled: a setting that does nothing in this mode can still
  // be prepared for the next one.
  const auto dim = [](juce::Component& c, bool relevant) { c.setAlpha(relevant ? 1.0f : 0.4f); };
  dim(barsRow.label, r.bars);
  dim(barsSlider, r.bars);
  dim(timedRow.label, r.timedDuration);
  dim(timedDurationSlider, r.timedDuration);
  dim(jitterToggle, r.jitter);
  dim(jitterMinRow.label, r.jitterRange);
  dim(jitterMinSlider, r.jitterRange);
  dim(jitterMaxRow.label, r.jitterRange);
  dim(jitterMaxSlider, r.jitterRange);
  dim(energyRow.label, r.energyThreshold);
  dim(energyThresholdSlider, r.energyThreshold);
  dim(gridToggle, r.grid);
  dim(gridOffsetSlider, r.gridOffset);
  dim(blendRow.label, r.blend);
  dim(blendSlider, r.blend);
}

void TransitionSettingsPanel::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(juce::Colours::black.withAlpha(0.85f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
}

void TransitionSettingsPanel::resized() {
  constexpr int rowHeight = 24;
  constexpr int gap = 4;
  constexpr int labelWidth = 64;

  auto area = getLocalBounds().reduced(8, 6);
  auto header = area.removeFromTop(rowHeight);
  closeButton.setBounds(header.removeFromRight(56));
  titleLabel.setBounds(header);
  area.removeFromTop(gap);

  auto left = area.removeFromLeft((area.getWidth() - 12) / 2);
  area.removeFromLeft(12);
  auto right = area;

  const auto place = [&](juce::Rectangle<int>& column, Row& row) {
    auto line = column.removeFromTop(rowHeight);
    column.removeFromTop(gap);
    row.label.setBounds(line.removeFromLeft(labelWidth));
    row.control->setBounds(line);
  };
  const auto placeToggle = [&](juce::Rectangle<int>& column, juce::Component& toggle) {
    toggle.setBounds(column.removeFromTop(rowHeight));
    column.removeFromTop(gap);
  };

  place(left, modeRow);
  place(left, barsRow);
  place(left, timedRow);
  place(left, energyRow);
  {
    auto line = left.removeFromTop(rowHeight);
    gridToggle.setBounds(line.removeFromLeft(line.getWidth() * 45 / 100));
    gridOffsetSlider.setBounds(line);
  }

  placeToggle(right, jitterToggle);
  place(right, jitterMinRow);
  place(right, jitterMaxRow);
  placeToggle(right, hardCutToggle);
  place(right, blendRow);
}

BeatBadge describeBeat(BeatBadgeSource source, float bpm, float confidence) {
  BeatBadge badge;
  badge.text = juce::String(juce::CharPointer_UTF8("\xE2\x99\xA9")); // quarter note
  if (source == BeatBadgeSource::None || !(bpm > 0.0f)) {
    badge.text << "--";
    badge.tooltip = "No beat yet: waiting for audio with a steady pulse";
    // Opaque grey, not translucent white: 50% alpha vanished over busy presets.
    badge.colour = juce::Colour(0xffa8a8a8);
    return badge;
  }

  const auto bpmText = juce::String(std::lround(bpm));
  if (source == BeatBadgeSource::Host) {
    badge.text << bpmText << " host";
    badge.tooltip = "Tempo and beat grid from the host transport (" + bpmText + " BPM)";
    badge.colour = juce::Colour(0xff8fe39a); // green: exact, synced to the host
    return badge;
  }

  const auto percent = static_cast<int>(std::lround(juce::jlimit(0.0f, 1.0f, confidence) * 100.0f));
  badge.text << bpmText << " " << juce::String(percent) << "%";
  badge.tooltip = "Tempo detected from the audio (" + bpmText + " BPM), confidence " + juce::String(percent) + "%";
  // Below BeatQuantized's fallback threshold the scheduler times transitions
  // instead of following the beat, so the badge says the beat is not trusted.
  const bool trusted = confidence >= core::TransitionSchedulerConfig{}.beatConfidenceFallbackThreshold;
  badge.colour = trusted ? juce::Colours::white : juce::Colour(0xffe3c28f); // amber: unsure
  if (!trusted) {
    badge.tooltip << " -- too unsure to follow; beat-quantized mode falls back to timed transitions";
  }
  return badge;
}

} // namespace milkdawp::ui
