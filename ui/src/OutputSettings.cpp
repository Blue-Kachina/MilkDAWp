// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/OutputSettings.h"

#include <algorithm>
#include <initializer_list>
#include <span>

#include "milkdawp/core/DisplayLayout.h"
#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::ui {

namespace {
constexpr int kRowHeight = 24;
constexpr int kGap = 4;
constexpr int kMapHeight = 100;
constexpr int kNoteHeight = 16;
constexpr int kScreenLabelWidth = 56;
constexpr int kFrameThickness = 6;
const juce::Colour kAccent{0xff3fa9f5};

core::WindowBounds toWindowBounds(juce::Rectangle<int> r) noexcept {
  return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}

std::vector<core::WindowBounds> idsOf(const std::vector<DisplayEntry>& displays) {
  std::vector<core::WindowBounds> ids;
  ids.reserve(displays.size());
  for (const auto& d : displays) {
    ids.push_back(d.id);
  }
  return ids;
}
} // namespace

std::vector<DisplayEntry> currentDisplays() {
  std::vector<DisplayEntry> result;
  for (const auto& display : juce::Desktop::getInstance().getDisplays().displays) {
    result.push_back({toWindowBounds(display.userBounds.toNearestInt()), display.logicalBounds.toNearestInt(),
                      display.isMain});
  }
  return result;
}

DisplayHighlightFrame::DisplayHighlightFrame() {
  setOpaque(false);
  setInterceptsMouseClicks(false, false);
}

DisplayHighlightFrame::~DisplayHighlightFrame() { hide(); }

void DisplayHighlightFrame::showAround(juce::Rectangle<int> screenBounds) {
  if (!isOnDesktop()) {
    addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresMouseClicks |
                 juce::ComponentPeer::windowIgnoresKeyPresses);
  }
  setBounds(screenBounds);
  setVisible(true);
  toFront(false);
}

void DisplayHighlightFrame::hide() {
  setVisible(false);
  if (isOnDesktop()) {
    removeFromDesktop();
  }
}

void DisplayHighlightFrame::paint(juce::Graphics& g) {
  g.setColour(kAccent);
  g.drawRect(getLocalBounds(), kFrameThickness);
  g.setColour(juce::Colours::white.withAlpha(0.7f));
  g.drawRect(getLocalBounds().reduced(kFrameThickness), 1);
}

/// One row of the Sources list: a sender's name, opacity, blend and mute.
class OutputSettingsPanel::SourceRow final : public juce::Component {
public:
  SourceRow(OutputSettingsPanel& owner, std::string id) : owner_(owner), id_(std::move(id)) {
    name_.setColour(juce::Label::textColourId, juce::Colours::white);
    name_.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(name_);

    opacity_.setSliderStyle(juce::Slider::LinearHorizontal);
    opacity_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    opacity_.setRange(0.0, 1.0, 0.01);
    opacity_.setTooltip("Opacity");
    opacity_.onValueChange = [this] {
      if (!updating_) {
        send("layerOpacity", static_cast<float>(opacity_.getValue()));
      }
    };
    addAndMakeVisible(opacity_);

    if (const auto* spec = core::findParameter(core::allParameters(), "layerBlend")) {
      int itemId = 1;
      for (const auto& choice : spec->choices) {
        blend_.addItem(choice, itemId++);
      }
    }
    blend_.setTooltip("Blend");
    blend_.onChange = [this] {
      if (!updating_) {
        send("layerBlend", static_cast<float>(blend_.getSelectedId() - 1));
      }
    };
    addAndMakeVisible(blend_);

    mute_.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    mute_.setTooltip("Mute this layer");
    mute_.onClick = [this] {
      if (!updating_) {
        send("layerMute", mute_.getToggleState() ? 1.0f : 0.0f);
      }
    };
    addAndMakeVisible(mute_);
  }

  [[nodiscard]] const std::string& id() const noexcept { return id_; }

  void update(const SourceState& state) {
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    name_.setText(state.name, juce::dontSendNotification);
    // Cost at a glance: the name warms up as the layer gets expensive (a 60 fps
    // frame is 16.7 ms in all), with the number in the tooltip.
    name_.setTooltip(state.mute ? juce::String("Muted: not drawn")
                                : "GPU " + juce::String(state.gpuMs, 1) + " ms per frame");
    name_.setColour(juce::Label::textColourId, state.gpuMs >= 8.0f   ? juce::Colour(0xffff6b5e)
                                               : state.gpuMs >= 4.0f ? juce::Colour(0xffffc857)
                                                                     : juce::Colours::white);
    // Never fight the user's hand: a control being dragged or opened keeps its own value.
    if (!opacity_.isMouseButtonDown()) {
      opacity_.setValue(state.opacity, juce::dontSendNotification);
    }
    if (!blend_.isPopupActive()) {
      blend_.setSelectedId(state.blend + 1, juce::dontSendNotification);
    }
    mute_.setToggleState(state.mute, juce::dontSendNotification);
  }

  void resized() override {
    auto area = getLocalBounds();
    name_.setBounds(area.removeFromLeft(70));
    mute_.setBounds(area.removeFromRight(22));
    blend_.setBounds(area.removeFromRight(84).reduced(2, 0));
    opacity_.setBounds(area);
  }

private:
  void send(const char* parameterId, float value) {
    if (owner_.onSourceParameterChanged) {
      owner_.onSourceParameterChanged(id_, parameterId, value);
    }
  }

  OutputSettingsPanel& owner_;
  std::string id_;
  juce::Label name_;
  juce::Slider opacity_;
  juce::ComboBox blend_;
  juce::ToggleButton mute_;
  bool updating_ = false;
};

OutputSettingsPanel::OutputSettingsPanel() {
  titleLabel.setText("Output", juce::dontSendNotification);
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

  fullscreenToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  fullscreenToggle.setTooltip("The pop-out button opens the Output window fullscreen");
  fullscreenToggle.onClick = [this] {
    if (onDefaultFullscreenChanged) {
      onDefaultFullscreenChanged(fullscreenToggle.getToggleState());
    }
  };
  addAndMakeVisible(fullscreenToggle);

  screenLabel.setText("Screen", juce::dontSendNotification);
  screenLabel.setTooltip("Click a screen below, or press its number");
  screenLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  screenLabel.setJustificationType(juce::Justification::centredLeft);
  addChildComponent(screenLabel);

  automaticToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  automaticToggle.setTooltip("Open where the window was last, or on the main screen (A)");
  automaticToggle.setClickingTogglesState(false);
  automaticToggle.onClick = [this] {
    target_ = {};
    if (onTargetDisplayChanged) {
      onTargetDisplayChanged(target_);
    }
    automaticToggle.setToggleState(true, juce::dontSendNotification);
    noteLabel.setVisible(false);
    repaint();
  };
  addChildComponent(automaticToggle);

  noteLabel.setText("The saved screen isn't connected; using automatic.", juce::dontSendNotification);
  noteLabel.setFont(juce::FontOptions(12.0f));
  noteLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
  addChildComponent(noteLabel);

  // Layers: this instance's name, and where its picture is shown.
  nameLabel.setText("Name", juce::dontSendNotification);
  nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  nameLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(nameLabel);

  nameEditor.setTooltip("What this instance is called in other instances' \"Show on\" list");
  nameEditor.setMultiLine(false);
  nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white.withAlpha(0.1f));
  nameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
  nameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha(0.25f));
  nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
  const auto commitName = [this] {
    const auto text = nameEditor.getText().toStdString();
    if (text != instance_.label && onInstanceLabelChanged) {
      instance_.label = text; // the shell echoes it back through setInstanceState
      onInstanceLabelChanged(text);
    }
  };
  nameEditor.onReturnKey = commitName;
  nameEditor.onFocusLost = commitName;
  addAndMakeVisible(nameEditor);

  showOnLabel.setText("Show on", juce::dontSendNotification);
  showOnLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  showOnLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(showOnLabel);

  targetCombo.setTooltip("Send this instance's picture to another instance's Output window, "
                         "to be mixed with its own and other instances' pictures");
  targetCombo.onChange = [this] {
    if (rebuildingCombo_) {
      return;
    }
    const auto index = static_cast<std::size_t>(targetCombo.getSelectedId() - 1);
    if (index < comboIds_.size() && onTargetInstanceChanged) {
      instance_.target = comboIds_[index];
      onTargetInstanceChanged(instance_.target);
    }
  };
  addAndMakeVisible(targetCombo);

  infoLabel.setFont(juce::FontOptions(12.0f));
  infoLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
  addChildComponent(infoLabel);

  for (auto* label : {&opacityLabel, &blendLabel, &orderLabel}) {
    label->setColour(juce::Label::textColourId, juce::Colours::white);
    label->setJustificationType(juce::Justification::centredLeft);
    addChildComponent(*label);
  }
  opacityLabel.setText("Opacity", juce::dontSendNotification);
  blendLabel.setText("Blend", juce::dontSendNotification);
  orderLabel.setText("Order", juce::dontSendNotification);
  for (auto* slider : {&opacitySlider, &orderSlider}) {
    slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
    addChildComponent(*slider);
  }
  opacitySlider.setTooltip("How much of this picture shows in the canvas");
  orderSlider.setTooltip("Lower layers draw first (underneath); equal numbers keep the order they joined");
  if (const auto* spec = core::findParameter(core::allParameters(), "layerBlend")) {
    int itemId = 1;
    for (const auto& choice : spec->choices) {
      blendCombo.addItem(choice, itemId++);
    }
  }
  blendCombo.setTooltip("How this picture mixes with the layers below it");
  addChildComponent(blendCombo);
  muteToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  addChildComponent(muteToggle);

  sourcesLabel.setText("Sources (bottom to top)", juce::dontSendNotification);
  sourcesLabel.setFont(juce::FontOptions(12.0f));
  sourcesLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
  addChildComponent(sourcesLabel);

  rebuildTargetCombo();
  infoLabel.setText(infoText(), juce::dontSendNotification);
  infoLabel.setVisible(hasInfoLine());
  rebuildDisplays();
  setSize(preferredWidth, preferredHeight());
}

OutputSettingsPanel::~OutputSettingsPanel() { highlight_.hide(); }

int OutputSettingsPanel::preferredHeight() const {
  // header, [name, show on], [info], fullscreen toggle
  int height = 6 + kRowHeight + kGap + kRowHeight + 6;
  if (layersAvailable_) {
    height += 2 * (kRowHeight + kGap);
  }
  if (hasInfoLine()) {
    height += kNoteHeight + kGap;
  }
  if (hasLayerControls()) {
    height += 3 * (kRowHeight + kGap); // opacity, blend, order
  }
  if (!instance_.sources.empty()) {
    height += kNoteHeight + static_cast<int>(instance_.sources.size()) * (kRowHeight + kGap);
  }
  if (hasPicker()) {
    height += kGap + kRowHeight + kGap + kMapHeight + kNoteHeight;
  }
  return height;
}

bool OutputSettingsPanel::hasInfoLine() const noexcept {
  return layersAvailable_ && (instance_.sending || instance_.senderCount > 0 || instance_.choices.empty());
}

void OutputSettingsPanel::setLayersAvailable(bool available) {
  if (available == layersAvailable_) {
    return;
  }
  const int heightBefore = preferredHeight();
  layersAvailable_ = available;
  for (auto* component : std::initializer_list<juce::Component*>{&nameLabel, &nameEditor, &showOnLabel, &targetCombo}) {
    component->setVisible(available);
  }
  infoLabel.setVisible(hasInfoLine());
  for (auto* component : std::initializer_list<juce::Component*>{&opacityLabel, &opacitySlider, &blendLabel,
                                                                  &blendCombo, &orderLabel, &orderSlider,
                                                                  &muteToggle}) {
    component->setVisible(hasLayerControls());
  }
  updateSources();
  if (preferredHeight() != heightBefore && onPreferredSizeChanged) {
    onPreferredSizeChanged();
  }
  resized();
  repaint();
}

juce::String OutputSettingsPanel::infoText() const {
  if (instance_.sending) {
    return "Shown in the other instance's Output window.";
  }
  if (instance_.senderCount > 0) {
    return juce::String(instance_.senderCount) + (instance_.senderCount == 1 ? " instance sends" : " instances send") +
           " to this window.";
  }
  return "No other MilkDAWp instances found in this host.";
}

void OutputSettingsPanel::rebuildTargetCombo() {
  rebuildingCombo_ = true;
  targetCombo.clear(juce::dontSendNotification);
  comboIds_.clear();
  targetCombo.addItem("New window", 1);
  comboIds_.emplace_back();
  int selected = 1;
  int itemId = 2;
  bool found = instance_.target.empty();
  for (const auto& choice : instance_.choices) {
    targetCombo.addItem(choice.name, itemId);
    targetCombo.setItemEnabled(itemId, choice.selectable);
    comboIds_.push_back(choice.id);
    if (choice.id == instance_.target) {
      selected = itemId;
      found = true;
    }
    ++itemId;
  }
  if (!found) {
    // The saved target is not loaded (yet). Keep showing it: it is still the
    // choice, and the link is made as soon as that instance appears.
    targetCombo.addItem("Not connected (waiting)", itemId);
    comboIds_.push_back(instance_.target);
    selected = itemId;
  }
  targetCombo.setSelectedId(selected, juce::dontSendNotification);
  targetCombo.setEnabled(instance_.canChooseTarget);
  rebuildingCombo_ = false;
}

void OutputSettingsPanel::updateSources() {
  // Bottom to top, as the compositor draws them; equal orders keep the order they joined.
  auto sorted = instance_.sources;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const SourceState& a, const SourceState& b) { return a.order < b.order; });

  const bool sameRows = sorted.size() == sourceRows_.size() &&
                        std::equal(sorted.begin(), sorted.end(), sourceRows_.begin(),
                                   [](const SourceState& s, const std::unique_ptr<SourceRow>& row) {
                                     return s.id == row->id();
                                   });
  if (!sameRows) {
    sourceRows_.clear();
    for (const auto& source : sorted) {
      sourceRows_.push_back(std::make_unique<SourceRow>(*this, source.id));
      addAndMakeVisible(*sourceRows_.back());
    }
  }
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    sourceRows_[i]->update(sorted[i]);
  }
  sourcesLabel.setVisible(layersAvailable_ && !sourceRows_.empty());
}

void OutputSettingsPanel::setInstanceState(const InstanceState& state) {
  if (state == instance_) {
    return;
  }
  const int heightBefore = preferredHeight();
  instance_ = state;
  rebuildTargetCombo();
  nameEditor.setTextToShowWhenEmpty(instance_.defaultName, juce::Colours::white.withAlpha(0.4f));
  if (!nameEditor.hasKeyboardFocus(true)) { // never overwrite what is being typed
    nameEditor.setText(instance_.label, false);
  }
  infoLabel.setText(infoText(), juce::dontSendNotification);
  infoLabel.setVisible(hasInfoLine());
  // While this instance sends its picture elsewhere, its own window settings
  // are not in use (the other instance's are).
  fullscreenToggle.setAlpha(instance_.sending ? 0.4f : 1.0f);
  const bool layerControls = hasLayerControls();
  for (auto* component : std::initializer_list<juce::Component*>{&opacityLabel, &opacitySlider, &blendLabel,
                                                                  &blendCombo, &orderLabel, &orderSlider,
                                                                  &muteToggle}) {
    component->setVisible(layerControls);
  }
  muteToggle.setButtonText(instance_.senderCount > 0 ? "Hide my visual" : "Mute layer");
  updateSources();
  rebuildDisplays(); // the screen picker only shows for an instance with its own window
  if (preferredHeight() != heightBefore && onPreferredSizeChanged) {
    onPreferredSizeChanged();
  }
  resized();
  repaint();
}

int OutputSettingsPanel::selectedIndex() const { return core::findDisplay(idsOf(displays_), target_); }

void OutputSettingsPanel::rebuildDisplays() {
  displays_ = currentDisplays();
  const bool picker = hasPicker();
  screenLabel.setVisible(picker);
  automaticToggle.setVisible(picker);
  automaticToggle.setToggleState(selectedIndex() < 0, juce::dontSendNotification);
  noteLabel.setVisible(picker && !target_.isEmpty() && selectedIndex() < 0);
  if (hovered_ >= static_cast<int>(displays_.size())) {
    setHovered(-1);
  }
}

void OutputSettingsPanel::refresh(bool defaultFullscreen, const core::WindowBounds& targetDisplay) {
  fullscreenToggle.setToggleState(defaultFullscreen, juce::dontSendNotification);
  target_ = targetDisplay;
  rebuildDisplays();
  resized();
  repaint();
}

void OutputSettingsPanel::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(juce::Colours::black.withAlpha(0.85f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  const auto selected = selectedIndex();
  for (std::size_t i = 0; i < tiles_.size(); ++i) {
    const auto tile = tiles_[i].toFloat();
    const bool isSelected = static_cast<int>(i) == selected;
    const bool isHovered = static_cast<int>(i) == hovered_;
    g.setColour(isSelected ? kAccent.withAlpha(0.55f) : juce::Colours::white.withAlpha(isHovered ? 0.28f : 0.12f));
    g.fillRoundedRectangle(tile, 3.0f);
    g.setColour(isSelected || isHovered ? kAccent : juce::Colours::white.withAlpha(0.35f));
    g.drawRoundedRectangle(tile, 3.0f, isSelected || isHovered ? 2.0f : 1.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText(juce::String(i + 1) + (displays_[i].primary ? " (main)" : ""), tiles_[i], juce::Justification::centred);
  }
}

void OutputSettingsPanel::resized() {
  auto area = getLocalBounds().reduced(8, 6);
  auto header = area.removeFromTop(kRowHeight);
  closeButton.setBounds(header.removeFromRight(60));
  titleLabel.setBounds(header);
  area.removeFromTop(kGap);
  if (layersAvailable_) {
    auto nameRow = area.removeFromTop(kRowHeight);
    nameLabel.setBounds(nameRow.removeFromLeft(kScreenLabelWidth));
    nameEditor.setBounds(nameRow);
    area.removeFromTop(kGap);
    auto showOnRow = area.removeFromTop(kRowHeight);
    showOnLabel.setBounds(showOnRow.removeFromLeft(kScreenLabelWidth));
    targetCombo.setBounds(showOnRow);
    area.removeFromTop(kGap);
  }
  if (hasInfoLine()) {
    infoLabel.setBounds(area.removeFromTop(kNoteHeight));
    area.removeFromTop(kGap);
  }
  if (hasLayerControls()) {
    auto opacityRow = area.removeFromTop(kRowHeight);
    opacityLabel.setBounds(opacityRow.removeFromLeft(kScreenLabelWidth));
    opacitySlider.setBounds(opacityRow);
    area.removeFromTop(kGap);
    auto blendRow = area.removeFromTop(kRowHeight);
    blendLabel.setBounds(blendRow.removeFromLeft(kScreenLabelWidth));
    muteToggle.setBounds(blendRow.removeFromRight(blendRow.getWidth() / 2 + 8));
    blendCombo.setBounds(blendRow.withTrimmedRight(4));
    area.removeFromTop(kGap);
    auto orderRow = area.removeFromTop(kRowHeight);
    orderLabel.setBounds(orderRow.removeFromLeft(kScreenLabelWidth));
    orderSlider.setBounds(orderRow);
    area.removeFromTop(kGap);
  }
  if (!sourceRows_.empty()) {
    sourcesLabel.setBounds(area.removeFromTop(kNoteHeight));
    for (auto& row : sourceRows_) {
      row->setBounds(area.removeFromTop(kRowHeight));
      area.removeFromTop(kGap);
    }
  }
  fullscreenToggle.setBounds(area.removeFromTop(kRowHeight));

  tiles_.clear();
  mapArea_ = {};
  if (!hasPicker()) {
    return;
  }
  area.removeFromTop(kGap);
  auto row = area.removeFromTop(kRowHeight);
  screenLabel.setBounds(row.removeFromLeft(kScreenLabelWidth));
  automaticToggle.setBounds(row);
  area.removeFromTop(kGap);
  mapArea_ = area.removeFromTop(kMapHeight);
  noteLabel.setBounds(area.removeFromTop(kNoteHeight));

  const auto ids = idsOf(displays_);
  for (const auto& tile : core::layoutDisplayMap(ids, mapArea_.getWidth(), mapArea_.getHeight())) {
    tiles_.emplace_back(mapArea_.getX() + tile.x, mapArea_.getY() + tile.y, tile.width, tile.height);
  }
}

int OutputSettingsPanel::tileAt(juce::Point<int> position) const {
  for (std::size_t i = 0; i < tiles_.size(); ++i) {
    if (tiles_[i].contains(position)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void OutputSettingsPanel::setHovered(int tile) {
  if (tile == hovered_) {
    return;
  }
  hovered_ = tile;
  if (tile >= 0 && tile < static_cast<int>(displays_.size())) {
    highlight_.showAround(displays_[static_cast<std::size_t>(tile)].frame);
  } else {
    highlight_.hide();
  }
  repaint();
}

void OutputSettingsPanel::mouseMove(const juce::MouseEvent& event) { setHovered(tileAt(event.getPosition())); }

void OutputSettingsPanel::mouseExit(const juce::MouseEvent&) { setHovered(-1); }

void OutputSettingsPanel::mouseUp(const juce::MouseEvent& event) { chooseDisplay(tileAt(event.getPosition())); }

bool OutputSettingsPanel::keyPressed(const juce::KeyPress& key) {
  // Reached with focus on any of the panel's widgets that doesn't use the key
  // itself (a text field keeps its digits).
  if (!hasPicker() || key.getModifiers().isAnyModifierKeyDown()) {
    return false;
  }
  const auto character = juce::CharacterFunctions::toUpperCase(key.getTextCharacter());
  if (character >= '1' && character <= '9') {
    const int tile = static_cast<int>(character - '1');
    if (tile < static_cast<int>(displays_.size())) {
      chooseDisplay(tile);
      return true;
    }
    return false;
  }
  if (character == 'A' && !automaticToggle.getToggleState()) {
    automaticToggle.triggerClick();
    return true;
  }
  return false;
}

void OutputSettingsPanel::chooseDisplay(int tile) {
  if (tile < 0 || tile >= static_cast<int>(displays_.size())) {
    return;
  }
  target_ = displays_[static_cast<std::size_t>(tile)].id;
  automaticToggle.setToggleState(false, juce::dontSendNotification);
  noteLabel.setVisible(false);
  repaint();
  if (onTargetDisplayChanged) {
    onTargetDisplayChanged(target_);
  }
}

void OutputSettingsPanel::visibilityChanged() {
  if (isVisible()) {
    startTimer(1000); // notice a monitor being plugged in or out while open
  } else {
    stopTimer();
    setHovered(-1);
  }
}

void OutputSettingsPanel::timerCallback() {
  const auto before = idsOf(displays_);
  const auto current = idsOf(currentDisplays());
  if (before == current) {
    return;
  }
  const bool pickerChanged = (before.size() > 1) != (current.size() > 1);
  setHovered(-1);
  rebuildDisplays();
  if (pickerChanged && onPreferredSizeChanged) {
    onPreferredSizeChanged();
  }
  resized();
  repaint();
}

} // namespace milkdawp::ui
