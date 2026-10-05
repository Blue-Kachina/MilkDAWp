// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"
#include "milkdawp/core/ParameterModel.h"

using namespace milkdawp::plugin;

namespace {

class FakePlayHead : public juce::AudioPlayHead {
public:
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setIsPlaying(true);
    info.setBpm(128.0);
    info.setPpqPosition(2.5);
    info.setTimeSignature(TimeSignature{3, 4});
    info.setTimeInSamples(static_cast<int64_t>(12345));
    return info;
  }
};

} // namespace

TEST_CASE("MilkDAWpAudioProcessor exposes every ParameterModel parameter through the APVTS",
          "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor processor;
  for (const auto& spec : milkdawp::core::allParameters()) {
    INFO("parameter id: " << spec.id);
    CHECK(processor.apvts.getParameter(juce::String(spec.id)) != nullptr);
    CHECK(processor.apvts.getRawParameterValue(juce::String(spec.id)) != nullptr);
  }
}

TEST_CASE("MilkDAWpAudioProcessor gives hosts the parameters in ParameterModel order, grouped",
          "[plugin][MilkDAWpAudioProcessor]") {
  // Hosts such as REAPER store automation by index: grouping must not reorder (ADR-0011).
  MilkDAWpAudioProcessor processor;
  const auto& hostParams = processor.getParameters();
  const auto& specs = milkdawp::core::allParameters();
  REQUIRE(static_cast<std::size_t>(hostParams.size()) == specs.size());
  for (std::size_t i = 0; i < specs.size(); ++i) {
    INFO("index " << i);
    const auto* withId = dynamic_cast<const juce::HostedAudioProcessorParameter*>(hostParams[static_cast<int>(i)]);
    REQUIRE(withId != nullptr);
    CHECK(withId->getParameterID() == juce::String(specs[i].id));
  }
  const auto* visual = processor.getParameterTree().getGroupsForParameter(processor.apvts.getParameter("visualHue"))
                           .getFirst();
  REQUIRE(visual != nullptr);
  CHECK(visual->getName() == "Visual");
  CHECK(processor.getParameterTree().getGroupsForParameter(processor.apvts.getParameter("shuffle")).isEmpty());
}

TEST_CASE("MilkDAWpAudioProcessor parameters start at ParameterModel's defaults",
          "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor processor;
  for (const auto& spec : milkdawp::core::allParameters()) {
    INFO("parameter id: " << spec.id);
    const auto* raw = processor.apvts.getRawParameterValue(juce::String(spec.id));
    REQUIRE(raw != nullptr);
    CHECK(raw->load() == Catch::Approx(spec.defaultValue).margin(0.001));
  }
}

TEST_CASE("MilkDAWpAudioProcessor::processBlock passes audio through bit-exact",
          "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor processor;
  processor.setPlayHead(nullptr);
  processor.prepareToPlay(48000.0, 512);

  juce::AudioBuffer<float> buffer(2, 512);
  for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
      buffer.setSample(ch, i, static_cast<float>(ch + 1) * 0.01f * static_cast<float>(i));
    }
  }
  juce::AudioBuffer<float> expected(buffer);

  juce::MidiBuffer midi;
  processor.processBlock(buffer, midi);

  for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
      CHECK(buffer.getSample(ch, i) == expected.getSample(ch, i));
    }
  }
}

TEST_CASE("MilkDAWpAudioProcessor captures a transport snapshot from processBlock",
          "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor processor;
  FakePlayHead fakePlayHead;
  processor.setPlayHead(&fakePlayHead);
  processor.prepareToPlay(48000.0, 512);

  CHECK_FALSE(processor.currentTransport().isPlaying); // nothing captured yet

  juce::AudioBuffer<float> buffer(2, 512);
  buffer.clear();
  juce::MidiBuffer midi;
  processor.processBlock(buffer, midi);

  const auto transport = processor.currentTransport();
  CHECK(transport.isPlaying);
  CHECK(transport.bpm == Catch::Approx(128.0));
  CHECK(transport.ppqPosition == Catch::Approx(2.5));
  CHECK(transport.timeSigNumerator == 3);
  CHECK(transport.samplePos == 12345);

  const auto beatClock = processor.currentBeatClock();
  CHECK(beatClock.confidence == Catch::Approx(1.0f));
  CHECK(beatClock.bpm == Catch::Approx(128.0f));
  CHECK(beatClock.beatIndex == 2); // ppq 2.5 -> beat 2, 0.5 into it
  CHECK(beatClock.barIndex == 0);  // 3/4 time, beat 2 is still bar 0

  processor.setPlayHead(nullptr);
}

TEST_CASE("MilkDAWpAudioProcessor editor size defaults and can be changed",
          "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor processor;
  CHECK(processor.editorWidth() == 480);
  CHECK(processor.editorHeight() == 270);

  processor.setEditorSize(900, 500);
  CHECK(processor.editorWidth() == 900);
  CHECK(processor.editorHeight() == 500);
}

TEST_CASE("MilkDAWpAudioProcessor state round-trips a changed parameter and the editor size",
          "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor source;
  auto* beatSensitivity = source.apvts.getParameter("beatSensitivity");
  REQUIRE(beatSensitivity != nullptr);
  beatSensitivity->setValueNotifyingHost(0.25f); // normalized; range is 0..2 -> raw 0.5
  source.setEditorSize(777, 333);

  juce::MemoryBlock block;
  source.getStateInformation(block);

  MilkDAWpAudioProcessor destination;
  destination.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

  const auto* restoredRaw = destination.apvts.getRawParameterValue("beatSensitivity");
  REQUIRE(restoredRaw != nullptr);
  CHECK(restoredRaw->load() == Catch::Approx(0.5).margin(0.001));
  CHECK(destination.editorWidth() == 777);
  CHECK(destination.editorHeight() == 333);
}

TEST_CASE("MilkDAWpAudioProcessor state round-trips the detached-controls layout",
          "[plugin][MilkDAWpAudioProcessor]") {
  // No message loop runs here, so the restored Output window is never
  // actually opened; this covers the saved layout itself.
  MilkDAWpAudioProcessor source;
  source.setControlsLayout(true, {50, 60, 700, 44});

  juce::MemoryBlock block;
  source.getStateInformation(block);

  MilkDAWpAudioProcessor destination;
  destination.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

  const auto layout = destination.windowLayout();
  CHECK(layout.controlsFloating);
  CHECK(layout.controlsWindowBounds == milkdawp::core::WindowBounds{50, 60, 700, 44});
  CHECK_FALSE(layout.outputWindowOpen);
}

TEST_CASE("MilkDAWpAudioProcessor saves its media source, even one that is missing now",
          "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor source;
  source.setMediaSourcePath("Z:/unplugged/logo.png"); // can't open: no source, but the choice is kept
  CHECK(source.mediaSourcePath() == "Z:/unplugged/logo.png");

  juce::MemoryBlock block;
  source.getStateInformation(block);
  MilkDAWpAudioProcessor destination;
  destination.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
  CHECK(destination.mediaSourcePath() == "Z:/unplugged/logo.png");
  CHECK(destination.visualizer().renderEngine().primaryLayer().mediaSource() == nullptr);
}

TEST_CASE("MilkDAWpAudioProcessor survives a garbage state blob", "[plugin][MilkDAWpAudioProcessor]") {
  MilkDAWpAudioProcessor processor;
  const std::string garbage = "editorWidth=\xff\xfe\nparam.shuffle=yes\noutputWindowBounds=a,b,c,d\n\0\0junk";
  REQUIRE_NOTHROW(processor.setStateInformation(garbage.data(), static_cast<int>(garbage.size())));
  CHECK(processor.editorWidth() == 480);
  CHECK_FALSE(processor.windowLayout().outputWindowOpen);
}

// 6.9 step 2. v1 migration was dropped (§4.8, 2026-09-26): a v1 session must
// open with v2's defaults, not crash and not take on stray values. The blob is
// what v1 0.7.x's getStateInformation wrote: its APVTS tree ("PARAMS") as XML
// via copyXmlToBinary, with v1's own parameter ids and non-default values.
TEST_CASE("MilkDAWpAudioProcessor opens a v1 session with v2 defaults", "[plugin][MilkDAWpAudioProcessor]") {
  juce::ValueTree v1("PARAMS");
  v1.setProperty("presetPath", "C:/Presets/favorite.milk", nullptr);
  v1.setProperty("playlistFolderPath", "C:/Presets", nullptr);
  v1.setProperty("editorWidth", 1024, nullptr);
  v1.setProperty("editorHeight", 700, nullptr);
  for (const auto& spec : milkdawp::core::allParameters()) {
    if (spec.v1Alias.empty()) {
      continue;
    }
    juce::ValueTree param("PARAM");
    param.setProperty("id", juce::String(spec.v1Alias), nullptr);
    param.setProperty("value", spec.maxValue, nullptr); // the top of the range: not the default for floats
    v1.appendChild(param, nullptr);
  }
  juce::MemoryBlock blob;
  const auto xml = v1.createXml();
  REQUIRE(xml != nullptr);
  juce::AudioProcessor::copyXmlToBinary(*xml, blob);

  MilkDAWpAudioProcessor processor;
  REQUIRE_NOTHROW(processor.setStateInformation(blob.getData(), static_cast<int>(blob.getSize())));
  for (const auto& spec : milkdawp::core::allParameters()) {
    INFO("parameter id: " << spec.id);
    CHECK(processor.apvts.getRawParameterValue(juce::String(spec.id))->load() ==
          Catch::Approx(spec.defaultValue).margin(0.001));
  }
  CHECK(processor.editorWidth() == 480);

  // Saving afterwards writes a normal v2 state that loads back.
  juce::MemoryBlock saved;
  processor.getStateInformation(saved);
  MilkDAWpAudioProcessor reloaded;
  REQUIRE_NOTHROW(reloaded.setStateInformation(saved.getData(), static_cast<int>(saved.getSize())));
}
