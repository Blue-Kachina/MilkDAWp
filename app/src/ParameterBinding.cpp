// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ParameterBinding.h"

#include <algorithm>
#include <cmath>

#include "MidiLearn.h"
#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::app {

namespace {
const core::ParameterSpec* specFor(std::string_view id) {
  return core::findParameter(core::allParameters(), std::string(id));
}
} // namespace

ParameterBinding::ParameterBinding(engine::ParameterValues& values, std::function<void()> onChanged)
    : values_(values), onChanged_(std::move(onChanged)) {}

ParameterBinding::~ParameterBinding() {
  for (const auto& entry : entries_) {
    entry.widget->removeMouseListener(this);
  }
}

void ParameterBinding::bind(juce::Slider& slider, std::string id) {
  if (const auto* spec = specFor(id)) {
    const double step = spec->type == core::ParameterType::Int ? 1.0 : 0.0;
    slider.setRange(spec->minValue, spec->maxValue, step);
    slider.setDoubleClickReturnValue(true, spec->defaultValue);
    if (spec->type == core::ParameterType::Float) {
      slider.setNumDecimalPlacesToDisplay(1);
    }
  }
  slider.onValueChange = [this, id, &slider] { write(id, static_cast<float>(slider.getValue())); };
  entries_.push_back({Kind::Slider, &slider, std::move(id)});
  slider.addMouseListener(this, false);
  refresh(entries_.back());
}

void ParameterBinding::bind(juce::Button& button, std::string id) {
  button.setClickingTogglesState(true);
  button.onClick = [this, id, &button] { write(id, button.getToggleState() ? 1.0f : 0.0f); };
  entries_.push_back({Kind::Button, &button, std::move(id)});
  button.addMouseListener(this, false);
  refresh(entries_.back());
}

void ParameterBinding::bind(juce::ComboBox& combo, std::string id) {
  combo.onChange = [this, id, &combo] {
    if (combo.getSelectedId() > 0) {
      write(id, static_cast<float>(combo.getSelectedId() - 1));
    }
  };
  entries_.push_back({Kind::ComboBox, &combo, std::move(id)});
  combo.addMouseListener(this, false);
  refresh(entries_.back());
}

float ParameterBinding::get(std::string_view id) const noexcept {
  const float* field = engine::parameterField(values_, id);
  return field != nullptr ? *field : 0.0f;
}

void ParameterBinding::set(std::string_view id, float value) {
  write(id, value);
  for (const auto& entry : entries_) {
    if (entry.id == id) {
      refresh(entry);
    }
  }
}

void ParameterBinding::refreshAll() {
  for (const auto& entry : entries_) {
    refresh(entry);
  }
}

void ParameterBinding::refresh(const Entry& entry) {
  const float value = get(entry.id);
  switch (entry.kind) {
  case Kind::Slider:
    static_cast<juce::Slider*>(entry.widget)->setValue(value, juce::dontSendNotification);
    break;
  case Kind::Button:
    static_cast<juce::Button*>(entry.widget)->setToggleState(value > 0.5f, juce::dontSendNotification);
    break;
  case Kind::ComboBox:
    static_cast<juce::ComboBox*>(entry.widget)->setSelectedId(static_cast<int>(std::lround(value)) + 1,
                                                              juce::dontSendNotification);
    break;
  }
}

void ParameterBinding::write(std::string_view id, float value) {
  float* field = engine::parameterField(values_, id);
  if (field == nullptr) {
    return;
  }
  if (const auto* spec = specFor(id)) {
    value = spec->type == core::ParameterType::Bool ? (value > 0.5f ? 1.0f : 0.0f)
                                                    : juce::jlimit(spec->minValue, spec->maxValue, value);
    if (spec->type != core::ParameterType::Float) {
      value = std::round(value);
    }
  }
  if (*field == value) {
    return;
  }
  *field = value;
  // Other widgets bound to the same id (the drawer's mode chip and the
  // panel's mode combo) follow.
  for (const auto& entry : entries_) {
    if (entry.id == id) {
      refresh(entry);
    }
  }
  if (onChanged_) {
    onChanged_();
  }
}

void ParameterBinding::attachMidiLearn(MidiLearn& midiLearn) {
  midiLearn_ = &midiLearn;
  refreshMidiLearnTooltips();
}

void ParameterBinding::refreshMidiLearnTooltips() {
  if (midiLearn_ == nullptr) {
    return;
  }
  for (const auto& entry : entries_) {
    auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*>(entry.widget);
    if (tooltipClient == nullptr) {
      continue;
    }
    if (midiLearn_->isLearning() && midiLearn_->learningParameterId() == entry.id) {
      tooltipClient->setTooltip("Listening for a MIDI control or note...");
      continue;
    }
    const auto mapped = midiLearn_->describeMapping(entry.id);
    tooltipClient->setTooltip(mapped.isNotEmpty() ? "MIDI: " + mapped + " (right-click to change)"
                                                  : "Right-click to map a MIDI control");
  }
}

void ParameterBinding::mouseDown(const juce::MouseEvent& event) {
  if (midiLearn_ == nullptr || !event.mods.isPopupMenu()) {
    return;
  }
  auto* comp = event.eventComponent;
  const auto it =
      std::find_if(entries_.begin(), entries_.end(), [comp](const Entry& entry) { return entry.widget == comp; });
  if (it == entries_.end()) {
    return;
  }
  const auto id = it->id;
  const auto mapped = midiLearn_->describeMapping(id);
  juce::PopupMenu menu;
  if (mapped.isNotEmpty()) {
    menu.addSectionHeader("Mapped: " + mapped);
  }
  menu.addItem("MIDI Learn...", [this, id] { midiLearn_->startLearning(id); });
  menu.addItem("Clear MIDI mapping", mapped.isNotEmpty(), false, [this, id] { midiLearn_->clearMapping(id); });
  menu.showMenuAsync(juce::PopupMenu::Options());
}

} // namespace milkdawp::app
