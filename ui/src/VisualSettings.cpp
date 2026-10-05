// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/VisualSettings.h"

#include <algorithm>

#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::ui {

namespace {

constexpr int kRowHeight = 22;
constexpr int kGap = 3;
constexpr int kLabelWidth = 96;
constexpr int kHeaderHeight = 20;
constexpr int kColumnGap = 16;
constexpr int kSectionGap = 6;
// Below this the Visual globals take one column, and the panel scrolls.
constexpr int kThreeColumnWidth = 640;
// The title row and margins around the scrolling content.
constexpr int kChromeHeight = 6 + 24 + 4 + 6;

juce::String tooltipFor(const std::string& id) {
  if (id == "visualHue") return "Turns every colour around the colour wheel (degrees)";
  if (id == "visualSaturation") return "0 grey, 1 as the preset made it, 2 vivid";
  if (id == "visualBrightness") return "Exposure: 1 as the preset made it";
  if (id == "visualSpeed") return "How fast the preset's own motion runs. Turning it never jumps the picture. "
                                  "It can't slow the reaction to the music or the trails a preset leaves";
  if (id == "visualZoom") return "Zooms the picture in (right) or out (left)";
  if (id == "visualRotation") return "Spins the picture; the further from the middle, the faster. "
                                     "Back at 0 it settles upright";
  if (id == "visualTrails") return "Mixes the previous frames back in, fading";
  if (id == "visualPixelate") return "Big square pixels";
  if (id == "visualGlow") return "Bright parts bloom";
  if (id == "visualBlur") return "Softens the picture";
  if (id == "visualMirror") return "Folds the picture onto itself";
  if (id == "visualKaleidoscope") return "Folds the picture into a kaleidoscope of this many segments";
  if (id == "visualRgbSplit") return "Pulls the red and blue apart";
  if (id == "visualMediaMix") return "How much of the media source shows over the visual. Choose one in "
                                     "Settings > Media source";
  if (id == "lockMacros") return "On: Macros keep their values when the preset changes. "
                                 "Off: they move to the new preset's own starting values";
  if (id == "layerGateEnabled") return "Like a noise gate on a guitar: the picture disappears while this "
                                       "instance's input is below the threshold, and is back on the next note";
  if (id == "layerGateThreshold") return "Below this level (dBFS) the gate closes. The meter shows the input";
  if (id == "layerGateRelease") return "How long the picture takes to fade out once the gate closes (0: a hard cut)";
  if (id.rfind("macro", 0) == 0) return "Each preset decides what its Macros do";
  return {};
}

} // namespace

juce::String visualControlUnavailableReason(const std::string& parameterId) {
  if (parameterId == "visualWarp" || parameterId == "visualWaveSize") {
    return "Needs a .milkdawp preset (coming later): it changes the preset itself, not the picture";
  }
  if (parameterId.rfind("macro", 0) == 0) {
    return ".milk presets don't use Macros: .milkdawp presets (coming later) give them a meaning";
  }
  return {};
}

// ---- GateMeter -------------------------------------------------------------------

void GateMeter::setState(float levelDb, float thresholdDb, bool open, bool enabled) {
  if (levelDb != levelDb_ || thresholdDb != thresholdDb_ || open != open_ || enabled != enabled_) {
    levelDb_ = levelDb;
    thresholdDb_ = thresholdDb;
    open_ = open;
    enabled_ = enabled;
    repaint();
  }
}

void GateMeter::paint(juce::Graphics& g) {
  auto bounds = getLocalBounds().toFloat();
  const auto light = bounds.removeFromRight(bounds.getHeight()).reduced(3.0f);
  bounds.removeFromRight(6.0f);
  const auto track = bounds.withSizeKeepingCentre(bounds.getWidth(), std::min(bounds.getHeight(), 10.0f));
  const auto toX = [&](float db) {
    return track.getX() + track.getWidth() * std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
  };

  g.setColour(juce::Colours::white.withAlpha(0.15f));
  g.fillRoundedRectangle(track, 2.0f);
  if (levelDb_ > kFloorDb) {
    const bool above = levelDb_ >= thresholdDb_;
    g.setColour((above ? juce::Colour(0xff8fe39a) : juce::Colour(0xffa8a8a8)).withAlpha(enabled_ ? 1.0f : 0.5f));
    g.fillRoundedRectangle(track.withRight(toX(levelDb_)), 2.0f);
  }
  g.setColour(juce::Colours::white);
  const float x = toX(thresholdDb_);
  g.drawLine(x, track.getY() - 3.0f, x, track.getBottom() + 3.0f, 1.5f);

  // Lit while the picture shows; dark while the gate holds it back.
  const bool lit = !enabled_ || open_;
  g.setColour(lit ? juce::Colour(0xff8fe39a) : juce::Colour(0xff3a3a3a));
  g.fillEllipse(light);
  g.setColour(juce::Colours::white.withAlpha(0.4f));
  g.drawEllipse(light, 1.0f);
}

// ---- Content ---------------------------------------------------------------------

class VisualSettingsPanel::Content final : public juce::Component {
public:
  struct Row {
    std::string id;
    juce::Label label;
    std::unique_ptr<juce::Component> control;
  };
  struct Section {
    juce::Label header;
    std::vector<std::unique_ptr<Row>> rows;
  };

  Content() {
    makeSection(visual_, "Visual", "visual");
    makeSection(macros_, "Macros", "macros");
    makeSection(gate_, "Gate", "layerGate");
    addAndMakeVisible(meter);
  }

  void resized() override { layout(getWidth(), true); }

  /// The height every control needs at `width`, laid out as `resized()` would.
  [[nodiscard]] int preferredHeight(int width) { return layout(width, false); }

  template <typename Visit> void forEachRow(Visit&& visit) {
    for (auto* section : {&visual_, &macros_, &gate_}) {
      for (auto& row : section->rows) {
        visit(*row);
      }
    }
  }

  GateMeter meter;

private:
  void makeSection(Section& section, const juce::String& title, const std::string& group) {
    section.header.setText(title, juce::dontSendNotification);
    section.header.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    section.header.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addAndMakeVisible(section.header);
    for (const auto& spec : core::allParameters()) {
      if (spec.group != group) {
        continue;
      }
      auto row = std::make_unique<Row>();
      row->id = spec.id;
      // "Gate Threshold (dB)" reads "Threshold (dB)" under the Gate header.
      auto name = juce::String(spec.displayName);
      if (group == "layerGate" && name.startsWith("Gate ")) {
        name = name.fromFirstOccurrenceOf("Gate ", false, false);
      }
      row->label.setText(spec.type == core::ParameterType::Bool ? juce::String() : name, juce::dontSendNotification);
      row->label.setColour(juce::Label::textColourId, juce::Colours::white);
      row->label.setJustificationType(juce::Justification::centredLeft);
      addAndMakeVisible(row->label);

      switch (spec.type) {
      case core::ParameterType::Bool: {
        auto toggle = std::make_unique<juce::ToggleButton>(group == "layerGate" ? juce::String("Gate on")
                                                                                : juce::String(spec.displayName));
        toggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        row->control = std::move(toggle);
        break;
      }
      case core::ParameterType::Choice: {
        auto combo = std::make_unique<juce::ComboBox>();
        int itemId = 1;
        for (const auto& choice : spec.choices) {
          combo->addItem(choice, itemId++);
        }
        row->control = std::move(combo);
        break;
      }
      case core::ParameterType::Float:
      case core::ParameterType::Int: {
        auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        juce::NormalisableRange<double> range(spec.minValue, spec.maxValue,
                                              spec.type == core::ParameterType::Int ? 1.0 : 0.0);
        if (spec.skewCentre != 0.0f) {
          range.setSkewForCentre(spec.skewCentre);
        }
        slider->setNormalisableRange(range);
        slider->setDoubleClickReturnValue(true, spec.defaultValue);
        // The wheel scrolls the panel (in a small window it has to), rather
        // than nudging whichever slider happens to be under the pointer.
        slider->setScrollWheelEnabled(false);
        slider->setNumDecimalPlacesToDisplay(spec.maxValue - spec.minValue > 50.0f ? 0 : 2);
        row->control = std::move(slider);
        break;
      }
      }
      const auto unavailable = visualControlUnavailableReason(spec.id);
      const auto tooltip = unavailable.isNotEmpty() ? unavailable : tooltipFor(spec.id);
      if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(row->control.get())) {
        client->setTooltip(tooltip);
      }
      row->label.setTooltip(tooltip);
      if (unavailable.isNotEmpty()) {
        // Dimmed, not disabled: it can still be set (and automated) ahead of time.
        row->control->setAlpha(0.4f);
        row->label.setAlpha(0.4f);
      }
      addAndMakeVisible(*row->control);
      section.rows.push_back(std::move(row));
    }
  }

  // Wide enough: three columns (Visual 1-8; Visual 9-16 with the gate under
  // them; Macros), which fit without scrolling. Narrower: two (Visual; Macros
  // and the gate), and the panel scrolls. Returns the height used; sets bounds
  // only when `apply`.
  int layout(int width, bool apply) {
    const bool three = width >= kThreeColumnWidth;
    const int columns = three ? 3 : 2;
    const int columnWidth = (width - (columns - 1) * kColumnGap) / columns;
    int tallest = 0;
    const auto column = [&](int index) {
      return juce::Rectangle<int>(index * (columnWidth + kColumnGap), 0, columnWidth, 100000);
    };
    const auto finish = [&](const juce::Rectangle<int>& c) { tallest = std::max(tallest, c.getY()); };

    const auto visualRows = static_cast<int>(visual_.rows.size());
    const int split = three ? (visualRows + 1) / 2 : visualRows;
    auto first = column(0);
    placeHeader(first, visual_.header, apply);
    placeRows(first, visual_, 0, split, apply);
    finish(first);

    auto second = column(1);
    if (three) {
      second.removeFromTop(kHeaderHeight); // lines up with the first column's rows
      placeRows(second, visual_, split, visualRows, apply);
      second.removeFromTop(kSectionGap);
    } else {
      placeSection(second, macros_, apply);
      second.removeFromTop(kSectionGap);
    }
    placeSection(second, gate_, apply);
    const auto meterLine = second.removeFromTop(kRowHeight);
    if (apply) {
      meter.setBounds(meterLine.withTrimmedLeft(kLabelWidth));
    }
    finish(second);

    if (three) {
      auto third = column(2);
      placeSection(third, macros_, apply);
      finish(third);
    }
    return tallest + 4;
  }

  static void placeHeader(juce::Rectangle<int>& column, juce::Label& header, bool apply) {
    const auto line = column.removeFromTop(kHeaderHeight);
    if (apply) {
      header.setBounds(line);
    }
  }

  static void placeRows(juce::Rectangle<int>& column, Section& section, int from, int to, bool apply) {
    for (int i = from; i < to; ++i) {
      auto& row = *section.rows[static_cast<std::size_t>(i)];
      auto line = column.removeFromTop(kRowHeight);
      column.removeFromTop(kGap);
      if (!apply) {
        continue;
      }
      if (dynamic_cast<juce::ToggleButton*>(row.control.get()) != nullptr) {
        row.label.setBounds({});
        row.control->setBounds(line);
      } else {
        row.label.setBounds(line.removeFromLeft(kLabelWidth));
        row.control->setBounds(line);
      }
    }
  }

  static void placeSection(juce::Rectangle<int>& column, Section& section, bool apply) {
    placeHeader(column, section.header, apply);
    placeRows(column, section, 0, static_cast<int>(section.rows.size()), apply);
  }

  Section visual_;
  Section macros_;
  Section gate_;
};

// ---- VisualSettingsPanel -----------------------------------------------------------

VisualSettingsPanel::VisualSettingsPanel() : content_(std::make_unique<Content>()) {
  titleLabel_.setText("Visual", juce::dontSendNotification);
  titleLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(titleLabel_);

  closeButton_.setTooltip("Close (Esc)");
  closeButton_.onClick = [this] {
    if (onCloseRequested) {
      onCloseRequested();
    }
  };
  addAndMakeVisible(closeButton_);

  resetButton_.setTooltip("Puts every Visual control back to neutral (Macros and the gate stay)");
  resetButton_.onClick = [this] { resetVisual(); };
  addAndMakeVisible(resetButton_);

  content_->forEachRow([this](Content::Row& row) {
    if (auto* slider = dynamic_cast<juce::Slider*>(row.control.get())) {
      sliders_.emplace_back(row.id, slider);
    } else if (auto* toggle = dynamic_cast<juce::Button*>(row.control.get())) {
      toggles_.emplace_back(row.id, toggle);
    } else if (auto* combo = dynamic_cast<juce::ComboBox*>(row.control.get())) {
      combos_.emplace_back(row.id, combo);
    }
  });

  viewport_.setViewedComponent(content_.get(), false);
  viewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport_);
  setSize(preferredWidth, preferredHeight(preferredWidth));
}

VisualSettingsPanel::~VisualSettingsPanel() { viewport_.setViewedComponent(nullptr, false); }

void VisualSettingsPanel::resetVisual() {
  // Through the widgets, so the shell's attachments write the parameters.
  for (const auto& [id, slider] : sliders_) {
    if (id.rfind("visual", 0) == 0) {
      slider->setValue(slider->getDoubleClickReturnValue(), juce::sendNotificationSync);
    }
  }
  for (const auto& [id, combo] : combos_) {
    if (id.rfind("visual", 0) == 0) {
      combo->setSelectedItemIndex(0, juce::sendNotificationSync);
    }
  }
}

void VisualSettingsPanel::setGateMeter(float levelDb, bool open) {
  float threshold = -80.0f;
  bool enabled = false;
  for (const auto& [id, slider] : sliders_) {
    if (id == "layerGateThreshold") {
      threshold = static_cast<float>(slider->getValue());
    }
  }
  for (const auto& [id, toggle] : toggles_) {
    if (id == "layerGateEnabled") {
      enabled = toggle->getToggleState();
    }
  }
  content_->meter.setState(levelDb, threshold, open, enabled);
}

void VisualSettingsPanel::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(juce::Colours::black.withAlpha(0.85f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
}

void VisualSettingsPanel::resized() {
  auto area = getLocalBounds().reduced(8, 6);
  auto header = area.removeFromTop(24);
  closeButton_.setBounds(header.removeFromRight(56));
  header.removeFromRight(6);
  resetButton_.setBounds(header.removeFromRight(100));
  titleLabel_.setBounds(header);
  area.removeFromTop(4);
  viewport_.setBounds(area);
  int width = area.getWidth();
  int height = content_->preferredHeight(width);
  if (height > area.getHeight()) {
    // Scrolling: make room for the scroll bar (which may change the columns).
    width -= viewport_.getScrollBarThickness();
    height = content_->preferredHeight(width);
  }
  content_->setSize(width, height);
}

int VisualSettingsPanel::preferredHeight(int width) const {
  return kChromeHeight + content_->preferredHeight(width - 16);
}

} // namespace milkdawp::ui
