// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/ControlDrawer.h"

#include <algorithm>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/ui/Icons.h"

namespace milkdawp::ui {

namespace {
constexpr int kRowHeight = 48;
constexpr int kProgressHeight = 12;
constexpr int kBottomPadding = 6;
constexpr int kSidePadding = 10;
constexpr int kChipWidth = 132;
constexpr int kBpmWidth = 96;
constexpr int kDividerWidth = 13;

// "BeatQuantized" is the parameter's choice string (host-visible, part of
// saved state); the chip shows it spelled out.
juce::String displayNameForChoice(const juce::String& choice) {
  return choice == "BeatQuantized" ? juce::String("Beat quantized") : choice;
}
} // namespace

DrawerLayout drawerLayoutFor(int width, bool floating) noexcept {
  DrawerLayout layout;
  layout.compact = width < compactBelowWidth;
  layout.showModeChip = !layout.compact;
  layout.showBpm = !layout.compact;
  layout.showUtilities = !layout.compact;
  layout.showPin = !floating && !layout.compact;
  layout.showMore = layout.compact;
  return layout;
}

ControlDrawer::ControlDrawer(DrawerStateMachine::Config config) : state_(config) {
  setLookAndFeel(&lookAndFeel_);
  addAndMakeVisible(scrim_);
  addAndMakeVisible(progress_);

  prevButton.setTooltip("Previous preset (Left arrow)");
  nextButton.setTooltip("Next preset (Right arrow)");
  addAndMakeVisible(prevButton);
  addAndMakeVisible(nextButton);

  title_.onClick = [this] {
    if (onPresetTitleClicked) {
      onPresetTitleClicked();
    }
  };
  title_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  addAndMakeVisible(title_);

  lockButton.setClickingTogglesState(true);
  lockButton.setTooltip("Lock current preset (L)");
  addAndMakeVisible(lockButton);

  shuffleButton.setClickingTogglesState(true);
  shuffleButton.setTooltip("Shuffle (S)");
  addAndMakeVisible(shuffleButton);

  if (const auto* spec = core::findParameter(core::allParameters(), "transitionMode")) {
    int itemId = 1;
    for (const auto& choice : spec->choices) {
      transitionModeCombo.addItem(displayNameForChoice(choice), itemId++);
    }
  }
  // Each mode's icon in the dropdown, and in the chip itself.
  int modeIndex = 0;
  for (juce::PopupMenu::MenuItemIterator it(*transitionModeCombo.getRootMenu()); it.next();) {
    it.getItem().image = createIconDrawable(iconForTransitionMode(modeIndex++), drawerTheme::menuText);
  }
  transitionModeCombo.getProperties().set(DrawerLookAndFeel::modeIconProperty, true);
  transitionModeCombo.setTooltip("Transition mode: when presets change");
  transitionModeCombo.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  addAndMakeVisible(transitionModeCombo);

  bpmLabel.setJustificationType(juce::Justification::centred);
  bpmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  bpmLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
  bpmLabel.setMinimumHorizontalScale(0.7f);
  // Left mouse-sensitive (unlike the title's text) so its tooltip shows: the
  // beat source and confidence behind the number (Phase 3.5).
  addAndMakeVisible(bpmLabel);

  // The shell wires both: the Output window (§4.9) and a settings menu
  // (preset folder, transition settings popover, diagnostics).
  outputButton.setTooltip("Open or close the Output window (F11: fullscreen)");
  addAndMakeVisible(outputButton);

  settingsButton.setTooltip("Settings: preset folder, transitions");
  addAndMakeVisible(settingsButton);

  pinButton.setClickingTogglesState(true);
  pinButton.setTooltip("Pin controls (P)");
  pinButton.onClick = [this] { togglePin(); };
  addAndMakeVisible(pinButton);

  moreButton.setTooltip("More controls");
  moreButton.onClick = [this] { showMoreMenu(); };
  addChildComponent(moreButton);

  // Clicking a control must not take keyboard focus from the window: a
  // focused button would swallow Space/Return (in a DAW, the transport's
  // keys) and the window's shortcuts would depend on what was clicked last.
  for (auto* control : std::initializer_list<juce::Component*>{&prevButton, &nextButton, &title_, &lockButton,
                                                               &shuffleButton, &transitionModeCombo, &outputButton,
                                                               &settingsButton, &pinButton, &moreButton}) {
    control->setWantsKeyboardFocus(false);
    control->setMouseClickGrabsKeyboardFocus(false);
  }

  scrim_.toBack();
  addMouseListener(this, true);

  updateVisualState();
  startTimerHz(15);
}

ControlDrawer::~ControlDrawer() {
  stopTimer();
  setLookAndFeel(nullptr);
}

void ControlDrawer::resized() {
  scrim_.setBounds(getLocalBounds());
  layout_ = drawerLayoutFor(getWidth(), floating_);

  auto area = getLocalBounds().reduced(kSidePadding, 0);
  area.removeFromBottom(floating_ ? 8 : kBottomPadding);
  auto row = area.removeFromBottom(kRowHeight);
  progress_.setBounds(area.removeFromBottom(kProgressHeight).reduced(6, 0));

  transitionModeCombo.setVisible(layout_.showModeChip);
  bpmLabel.setVisible(layout_.showBpm);
  outputButton.setVisible(layout_.showUtilities);
  settingsButton.setVisible(layout_.showUtilities);
  pinButton.setVisible(layout_.showPin);
  moreButton.setVisible(layout_.showMore);

  const int button = IconButton::preferredSize;
  const auto place = [&row](juce::Component& c, int width, bool fromRight, int height = kRowHeight) {
    auto slot = fromRight ? row.removeFromRight(width) : row.removeFromLeft(width);
    c.setBounds(slot.withSizeKeepingCentre(width, std::min(height, slot.getHeight())));
  };
  dividers_.clearQuick();
  const auto divider = [this, &row] {
    const auto slot = row.removeFromRight(kDividerWidth).toFloat();
    dividers_.add(slot.withSizeKeepingCentre(1.0f, 22.0f));
  };

  place(prevButton, button, false);
  place(nextButton, button, false);

  // Right to left: the utilities, then the mode chip and badge, then the toggles.
  if (layout_.showMore) {
    place(moreButton, button, true);
  }
  if (layout_.showPin) {
    place(pinButton, button, true);
  }
  if (layout_.showUtilities) {
    place(settingsButton, button, true);
    place(outputButton, button, true);
    divider();
  }
  if (layout_.showBpm) {
    place(bpmLabel, kBpmWidth, true, 36);
  }
  if (layout_.showModeChip) {
    place(transitionModeCombo, kChipWidth, true, 36);
    divider();
  }
  place(shuffleButton, button, true);
  place(lockButton, button, true);

  title_.setBounds(row.reduced(6, 2));
  refreshTitle();
}

void ControlDrawer::paintOverChildren(juce::Graphics& g) {
  g.setColour(drawerTheme::hairline);
  for (const auto& d : dividers_) {
    g.fillRect(d);
  }
}

void ControlDrawer::mouseEnter(const juce::MouseEvent&) { reveal(); }
void ControlDrawer::mouseDown(const juce::MouseEvent&) { reveal(); }

void ControlDrawer::reveal() {
  state_.onPointerActivity(nowSeconds());
  updateVisualState();
}

void ControlDrawer::toggleRevealHide() {
  state_.toggleRevealHide(nowSeconds());
  updateVisualState();
}

void ControlDrawer::togglePin() {
  state_.togglePin(nowSeconds());
  updateVisualState();
}

void ControlDrawer::timerCallback() {
  state_.tick(nowSeconds());
  updateVisualState();
  if (layout_.compact) {
    refreshTitle(); // the BPM badge's text rides in the detail line
  }
}

void ControlDrawer::setFloating(bool floating) {
  floating_ = floating;
  scrim_.setFloating(floating);
  resized();
  updateVisualState();
}

void ControlDrawer::setPresetInfo(const juce::String& name, const juce::String& detail, const juce::String& tooltip) {
  detail_ = detail;
  if (name != title_.name) {
    title_.name = name;
    title_.repaint();
  }
  title_.setTooltip(tooltip.isEmpty() ? juce::String("Click to browse presets")
                                      : tooltip + "\nClick to browse presets");
  refreshTitle();
}

void ControlDrawer::setProgress(std::optional<float> fraction) {
  if (progress_.fraction != fraction) {
    progress_.fraction = fraction;
    progress_.repaint();
  }
}

juce::Component& ControlDrawer::settingsMenuAnchor() noexcept {
  return settingsButton.isVisible() ? static_cast<juce::Component&>(settingsButton) : moreButton;
}

void ControlDrawer::refreshTitle() {
  auto detail = detail_;
  if (layout_.compact) {
    if (const auto mode = transitionModeCombo.getText(); mode.isNotEmpty()) {
      detail = detail.isEmpty() ? mode : mode + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + detail;
    }
    if (const auto bpm = bpmLabel.getText(); bpm.isNotEmpty()) {
      detail << juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) << bpm;
    }
  }
  if (detail != title_.detail) {
    title_.detail = detail;
    title_.repaint();
  }
}

void ControlDrawer::showMoreMenu() {
  juce::PopupMenu menu;
  menu.setLookAndFeel(&lookAndFeel_);

  juce::PopupMenu modes;
  for (int i = 0; i < transitionModeCombo.getNumItems(); ++i) {
    juce::PopupMenu::Item item(transitionModeCombo.getItemText(i));
    item.setTicked(i == transitionModeCombo.getSelectedItemIndex());
    item.setImage(createIconDrawable(iconForTransitionMode(i), drawerTheme::menuText));
    item.setAction([this, i] { transitionModeCombo.setSelectedItemIndex(i, juce::sendNotificationAsync); });
    modes.addItem(std::move(item));
  }
  menu.addSubMenu("Transition mode", modes, true,
                  createIconDrawable(iconForTransitionMode(transitionModeCombo.getSelectedItemIndex()),
                                     drawerTheme::menuText));
  menu.addSeparator();

  juce::PopupMenu::Item output("Output window");
  output.setImage(createIconDrawable(Icon::PopOut, drawerTheme::menuText));
  output.setAction([this] { outputButton.triggerClick(); });
  menu.addItem(std::move(output));

  juce::PopupMenu::Item settings("Settings...");
  settings.setImage(createIconDrawable(Icon::Settings, drawerTheme::menuText));
  settings.setAction([this] { settingsButton.triggerClick(); });
  menu.addItem(std::move(settings));

  if (!floating_) {
    juce::PopupMenu::Item pin("Pin controls (P)");
    pin.setImage(createIconDrawable(Icon::Pin, drawerTheme::menuText));
    pin.setTicked(isPinned());
    pin.setAction([this] { togglePin(); });
    menu.addItem(std::move(pin));
  }

  menu.showMenuAsync(DrawerLookAndFeel::menuOptions(moreButton));
}

void ControlDrawer::updateVisualState() {
  setAlpha(floating_ || state_.isVisible() ? 1.0f : 0.0f);
  pinButton.setToggleState(state_.isPinned(), juce::dontSendNotification);
}

double ControlDrawer::nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

// --- PresetTitle ---------------------------------------------------------------

void ControlDrawer::PresetTitle::paint(juce::Graphics& g) {
  auto bounds = getLocalBounds().toFloat();
  if (isMouseOver(true) && onClick) {
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(bounds, 8.0f);
  }

  auto text = bounds.reduced(8.0f, 0.0f);
  const bool twoLines = detail.isNotEmpty() && text.getHeight() >= 34.0f;
  const auto nameFont = juce::Font(juce::FontOptions(15.0f, juce::Font::bold));
  const auto detailFont = juce::Font(juce::FontOptions(12.0f));
  const float block = nameFont.getHeight() + (twoLines ? 2.0f + detailFont.getHeight() : 0.0f);
  text = text.withSizeKeepingCentre(text.getWidth(), block);

  g.setColour(drawerTheme::text);
  g.setFont(nameFont);
  g.drawText(name, text.removeFromTop(nameFont.getHeight()), juce::Justification::centredLeft, true);
  if (twoLines) {
    text.removeFromTop(2.0f);
    g.setColour(drawerTheme::textSecondary);
    g.setFont(detailFont);
    g.drawText(detail, text, juce::Justification::centredLeft, true);
  }
}

void ControlDrawer::PresetTitle::mouseUp(const juce::MouseEvent& e) {
  if (onClick && getLocalBounds().contains(e.getPosition()) && !e.mouseWasDraggedSinceMouseDown()) {
    onClick();
  }
}

// --- ProgressTrack -------------------------------------------------------------

void ControlDrawer::ProgressTrack::paint(juce::Graphics& g) {
  const auto track = getLocalBounds().toFloat().withSizeKeepingCentre(static_cast<float>(getWidth()), 3.0f);
  g.setColour(drawerTheme::track);
  g.fillRoundedRectangle(track, 1.5f);
  if (fraction) {
    g.setColour(drawerTheme::accent);
    g.fillRoundedRectangle(track.withWidth(track.getWidth() * std::clamp(*fraction, 0.0f, 1.0f)), 1.5f);
  }
}

} // namespace milkdawp::ui
