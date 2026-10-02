// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Layers L3: several plugin instances in one process sending their picture to
// one another's Output window. These drive real processors, each with a real
// engine, through the same calls the editor and the host make. They skip the
// attach checks where projectM or a GL context is unavailable.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "PluginProcessor.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/StateSchema.h"

using namespace milkdawp::plugin;
using namespace std::chrono_literals;

namespace {

template <typename Predicate> bool waitUntil(Predicate predicate) {
  for (int i = 0; i < 400 && !predicate(); ++i) {
    std::this_thread::sleep_for(10ms);
  }
  return predicate();
}

// Brings the engine up and gives it a visible surface so it really renders.
// False where projectM or a GL context is missing.
bool startEngine(MilkDAWpAudioProcessor& processor) {
  auto& engine = processor.renderEngine();
  waitUntil([&] { return engine.isAvailable() || !engine.unavailableReason().empty(); });
  if (!engine.isAvailable()) {
    return false;
  }
  const int slot = engine.registerSurface();
  engine.reportSurfaceSize(slot, 160, 90, true);
  return true;
}

bool layerCountBecomes(MilkDAWpAudioProcessor& processor, int count) {
  return waitUntil([&] { return processor.renderEngine().stats().layers == count; });
}

void setParameter(MilkDAWpAudioProcessor& processor, const char* id, float plainValue) {
  auto* parameter = processor.apvts.getParameter(id);
  REQUIRE(parameter != nullptr);
  parameter->setValueNotifyingHost(processor.apvts.getParameterRange(id).convertTo0to1(plainValue));
}

std::string stateWithIdentity(const std::string& id, const std::string& label = {}, const std::string& target = {}) {
  milkdawp::core::StateSchemaV2 state;
  state.instanceId = id;
  state.instanceLabel = label;
  state.windows.outputTargetInstance = target;
  return milkdawp::core::serializeStateSchemaV2(state);
}

void restore(MilkDAWpAudioProcessor& processor, const std::string& text) {
  processor.setStateInformation(text.data(), static_cast<int>(text.size()));
}

} // namespace

TEST_CASE("Every instance has its own id, and sees the others in its target picker", "[plugin][layers]") {
  MilkDAWpAudioProcessor first;
  MilkDAWpAudioProcessor second;
  CHECK_FALSE(first.instanceId().empty());
  CHECK(first.instanceId() != second.instanceId());

  const auto seenByFirst = first.otherInstances();
  const auto found = std::find_if(seenByFirst.begin(), seenByFirst.end(),
                                  [&](const InstanceInfo& info) { return info.id == second.instanceId(); });
  REQUIRE(found != seenByFirst.end());
  CHECK(found->canBeTarget);
  CHECK(std::none_of(seenByFirst.begin(), seenByFirst.end(),
                     [&](const InstanceInfo& info) { return info.id == first.instanceId(); })); // never itself
}

TEST_CASE("An instance's name in the picker is its label, else the host's track name, else a short id",
          "[plugin][layers]") {
  MilkDAWpAudioProcessor observed;
  MilkDAWpAudioProcessor processor;
  const auto nameSeenByObserver = [&] {
    for (const auto& info : observed.otherInstances()) {
      if (info.id == processor.instanceId()) {
        return info.name;
      }
    }
    return std::string{};
  };

  CHECK(nameSeenByObserver() == "Instance " + processor.instanceId().substr(0, 4));

  juce::AudioProcessor::TrackProperties track;
  track.name = "Bass";
  processor.updateTrackProperties(track);
  CHECK(nameSeenByObserver() == "Bass");

  processor.setInstanceLabel("Sub bass");
  CHECK(nameSeenByObserver() == "Sub bass"); // the user's label wins

  processor.setInstanceLabel({});
  CHECK(nameSeenByObserver() == "Bass");
}

TEST_CASE("The layer parameters reach the engine's primary layer", "[plugin][layers]") {
  MilkDAWpAudioProcessor processor;
  auto& channel = processor.renderEngine().primaryLayer();
  CHECK(channel.opacity() == Catch::Approx(1.0f));
  CHECK(channel.blend() == milkdawp::engine::LayerBlend::Normal);
  CHECK(channel.visible());
  CHECK(channel.order() == 0);

  setParameter(processor, "layerOpacity", 0.4f);
  setParameter(processor, "layerBlend", 2.0f);
  setParameter(processor, "layerMute", 1.0f);
  setParameter(processor, "layerOrder", 3.0f);
  CHECK(channel.opacity() == Catch::Approx(0.4f).margin(0.01));
  CHECK(channel.blend() == milkdawp::engine::LayerBlend::Screen);
  CHECK_FALSE(channel.visible());
  CHECK(channel.order() == 3);
}

TEST_CASE("The instance id, label and Output target survive a save and restore", "[plugin][layers]") {
  std::string saved;
  std::string id;
  {
    MilkDAWpAudioProcessor processor;
    processor.setInstanceLabel("Kick");
    processor.setOutputTargetInstance("some-other-instance"); // does not exist: only remembered
    id = processor.instanceId();
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    saved.assign(static_cast<const char*>(block.getData()), block.getSize());
  }
  MilkDAWpAudioProcessor restored;
  restore(restored, saved);
  CHECK(restored.instanceId() == id); // the original is gone, so the id is free again
  CHECK(restored.instanceLabel() == "Kick");
  CHECK(restored.windowLayout().outputTargetInstance == "some-other-instance");
  CHECK_FALSE(restored.isSendingToOtherInstance()); // and with no such instance it stays on its own window
}

TEST_CASE("A copied state does not give two live instances the same id", "[plugin][layers]") {
  MilkDAWpAudioProcessor original;
  MilkDAWpAudioProcessor duplicate;
  juce::MemoryBlock block;
  original.getStateInformation(block);
  const std::string saved(static_cast<const char*>(block.getData()), block.getSize());

  restore(duplicate, saved); // e.g. a duplicated track
  CHECK(duplicate.instanceId() != original.instanceId());
  CHECK_FALSE(duplicate.instanceId().empty());
}

TEST_CASE("An instance sends its picture to another's engine, and takes it back", "[plugin][layers]") {
  MilkDAWpAudioProcessor hub;
  MilkDAWpAudioProcessor sender;
  if (!startEngine(hub) || !startEngine(sender)) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  CHECK(hub.canChooseOutputTarget());
  CHECK(hub.layerSenderCount() == 0);

  sender.setOutputTargetInstance(hub.instanceId());
  REQUIRE(sender.isSendingToOtherInstance());
  CHECK(hub.layerSenderCount() == 1);
  CHECK_FALSE(hub.canChooseOutputTarget()); // a hub with senders cannot itself send: no chains
  CHECK(layerCountBecomes(hub, 2));
  CHECK(sender.renderEngine().isPrimaryLayerYielded()); // the sender's own engine stands down

  // The hub shows up as a target; the sender, being a sender, does not.
  MilkDAWpAudioProcessor third;
  const auto seenByThird = third.otherInstances();
  const auto senderInfo = std::find_if(seenByThird.begin(), seenByThird.end(),
                                       [&](const InstanceInfo& info) { return info.id == sender.instanceId(); });
  REQUIRE(senderInfo != seenByThird.end());
  CHECK_FALSE(senderInfo->canBeTarget);

  // Pointing a third instance at the sender is refused rather than chained.
  if (startEngine(third)) {
    third.setOutputTargetInstance(sender.instanceId());
    CHECK_FALSE(third.isSendingToOtherInstance());
    CHECK(sender.layerSenderCount() == 0);
  }

  sender.setOutputTargetInstance({});
  CHECK_FALSE(sender.isSendingToOtherInstance());
  CHECK(hub.layerSenderCount() == 0);
  CHECK(hub.canChooseOutputTarget());
  CHECK(layerCountBecomes(hub, 1));
  CHECK_FALSE(sender.renderEngine().isPrimaryLayerYielded());
}

TEST_CASE("A sender's layer shows the preset it was already playing, not projectM's idle one",
          "[plugin][layers]") {
  // Attaching makes the hub create a fresh projectM instance for the sender's
  // layer. It knows nothing of the preset the sender was showing, and the
  // sender's Director only sends one on a cut, so without a re-send the layer
  // would sit on projectM's idle "M with headphones" preset until the next cut.
  MilkDAWpAudioProcessor hub;
  MilkDAWpAudioProcessor sender;
  if (!startEngine(hub) || !startEngine(sender)) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  sender.visualizer().director().setPresetFolder(std::string(MILKDAWP_FIXTURES_DIR) + "/presets");
  // `presetsLoaded()` counts loads on whichever render thread drains this channel.
  auto& channel = sender.renderEngine().primaryLayer();
  REQUIRE(waitUntil([&] { return channel.presetsLoaded() >= 1; })); // the sender's own engine loaded its preset

  const auto beforeAttach = channel.presetsLoaded();
  sender.setOutputTargetInstance(hub.instanceId());
  REQUIRE(sender.isSendingToOtherInstance());
  // The hub's render thread now loads that same preset into the new layer.
  CHECK(waitUntil([&] { return channel.presetsLoaded() > beforeAttach; }));

  // And when it comes home, its own instance missed every cut meanwhile: it is brought up to date too.
  const auto beforeDetach = channel.presetsLoaded();
  sender.setOutputTargetInstance({});
  REQUIRE_FALSE(sender.isSendingToOtherInstance());
  CHECK(waitUntil([&] { return channel.presetsLoaded() > beforeDetach; }));
}

TEST_CASE("Mixing a sender's layer follows its layer parameters", "[plugin][layers]") {
  MilkDAWpAudioProcessor hub;
  MilkDAWpAudioProcessor sender;
  if (!startEngine(hub) || !startEngine(sender)) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  sender.setOutputTargetInstance(hub.instanceId());
  REQUIRE(sender.isSendingToOtherInstance());

  // The channel the hub's engine reads is the sender's primary one, so the
  // sender's parameters are what the compositor sees.
  auto& channel = sender.renderEngine().primaryLayer();
  setParameter(sender, "layerOpacity", 0.25f);
  setParameter(sender, "layerBlend", 1.0f);
  setParameter(sender, "layerOrder", 5.0f);
  CHECK(channel.opacity() == Catch::Approx(0.25f).margin(0.01));
  CHECK(channel.blend() == milkdawp::engine::LayerBlend::Add);
  CHECK(channel.order() == 5);
}

TEST_CASE("A hub lists its senders and edits how each one is mixed", "[plugin][layers]") {
  MilkDAWpAudioProcessor hub;
  auto first = std::make_unique<MilkDAWpAudioProcessor>();
  MilkDAWpAudioProcessor second;
  if (!startEngine(hub) || !startEngine(*first) || !startEngine(second)) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  first->setInstanceLabel("Kick");
  second.setInstanceLabel("Vocal");
  first->setOutputTargetInstance(hub.instanceId());
  second.setOutputTargetInstance(hub.instanceId());
  REQUIRE(hub.layerSenderCount() == 2);

  auto senders = hub.layerSenders();
  REQUIRE(senders.size() == 2);
  CHECK(senders[0].name == "Kick"); // in the order they joined
  CHECK(senders[1].name == "Vocal");
  CHECK(senders[0].id == first->instanceId());
  CHECK(senders[0].opacity == Catch::Approx(1.0f));
  CHECK_FALSE(senders[0].mute);

  // The hub edits the sender's real parameters, so the engine's channel follows.
  hub.setLayerSenderParameter(first->instanceId(), "layerOpacity", 0.3f);
  hub.setLayerSenderParameter(first->instanceId(), "layerBlend", 4.0f);
  hub.setLayerSenderParameter(first->instanceId(), "layerMute", 1.0f);
  hub.setLayerSenderParameter(first->instanceId(), "layerOrder", 6.0f);
  auto& channel = first->renderEngine().primaryLayer();
  CHECK(channel.opacity() == Catch::Approx(0.3f).margin(0.01));
  CHECK(channel.blend() == milkdawp::engine::LayerBlend::LumaKey);
  CHECK_FALSE(channel.visible());
  CHECK(channel.order() == 6);
  senders = hub.layerSenders();
  CHECK(senders[0].opacity == Catch::Approx(0.3f).margin(0.01));
  CHECK(senders[0].blend == 4);
  CHECK(senders[0].mute);
  CHECK(senders[0].order == 6);
  CHECK(senders[1].opacity == Catch::Approx(1.0f)); // the other sender is untouched

  // Only the mixing parameters can be edited this way, and only on a sender.
  hub.setLayerSenderParameter(first->instanceId(), "presetIndex", 9.0f);
  CHECK(first->apvts.getRawParameterValue("presetIndex")->load() == Catch::Approx(0.0f));
  hub.setLayerSenderParameter("no-such-instance", "layerOpacity", 0.1f);
  CHECK(second.renderEngine().primaryLayer().opacity() == Catch::Approx(1.0f));

  first.reset(); // a sender goes: it drops off the list
  senders = hub.layerSenders();
  REQUIRE(senders.size() == 1);
  CHECK(senders[0].name == "Vocal");
}

TEST_CASE("A sender whose target does not exist yet links up when it appears", "[plugin][layers]") {
  const std::string hubId = "hub-that-loads-later";
  MilkDAWpAudioProcessor sender;
  if (!startEngine(sender)) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  sender.setOutputTargetInstance(hubId);
  CHECK_FALSE(sender.isSendingToOtherInstance()); // nothing to send to yet; own window as before

  MilkDAWpAudioProcessor hub;
  REQUIRE(startEngine(hub));
  restore(hub, stateWithIdentity(hubId)); // the host restores the hub's state: it now has that id
  CHECK(hub.instanceId() == hubId);

  sender.reconcileLayers(); // what the registry's notification triggers on the message thread
  CHECK(sender.isSendingToOtherInstance());
  CHECK(layerCountBecomes(hub, 2));
}

TEST_CASE("Destroying the hub first lets its senders stand on their own again", "[plugin][layers]") {
  auto hub = std::make_unique<MilkDAWpAudioProcessor>();
  MilkDAWpAudioProcessor sender;
  if (!startEngine(*hub) || !startEngine(sender)) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  sender.setOutputTargetInstance(hub->instanceId());
  REQUIRE(sender.isSendingToOtherInstance());

  hub.reset(); // retires the hub: its engine lets go of the sender's channel before it is destroyed
  sender.reconcileLayers();
  CHECK_FALSE(sender.isSendingToOtherInstance());
  CHECK_FALSE(sender.renderEngine().isPrimaryLayerYielded());
  // The saved link is kept: if that instance comes back, the sender follows it.
  CHECK_FALSE(sender.windowLayout().outputTargetInstance.empty());
}

TEST_CASE("Destroying a sender first leaves the hub with its own layer only", "[plugin][layers]") {
  MilkDAWpAudioProcessor hub;
  auto sender = std::make_unique<MilkDAWpAudioProcessor>();
  if (!startEngine(hub) || !startEngine(*sender)) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  sender->setOutputTargetInstance(hub.instanceId());
  REQUIRE(hub.layerSenderCount() == 1);
  REQUIRE(layerCountBecomes(hub, 2));

  sender.reset();
  CHECK(hub.layerSenderCount() == 0);
  CHECK(layerCountBecomes(hub, 1));
  CHECK(hub.canChooseOutputTarget());
}

TEST_CASE("Instances can be created and destroyed in any order while linked", "[plugin][layers]") {
  // A small shuffle of the lifetimes hosts really produce (project load, track
  // delete, undo): several senders on one hub, torn down in a mixed order.
  auto hub = std::make_unique<MilkDAWpAudioProcessor>();
  std::vector<std::unique_ptr<MilkDAWpAudioProcessor>> senders;
  for (int i = 0; i < 3; ++i) {
    senders.push_back(std::make_unique<MilkDAWpAudioProcessor>());
  }
  if (!startEngine(*hub) || !std::all_of(senders.begin(), senders.end(), [](auto& s) { return startEngine(*s); })) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  for (auto& sender : senders) {
    sender->setOutputTargetInstance(hub->instanceId());
  }
  CHECK(hub->layerSenderCount() == 3);
  CHECK(layerCountBecomes(*hub, 4));

  senders[1].reset();            // a sender goes
  CHECK(hub->layerSenderCount() == 2);
  hub.reset();                   // then the hub, with senders still attached
  senders[0]->reconcileLayers();
  senders[2]->reconcileLayers();
  CHECK_FALSE(senders[0]->isSendingToOtherInstance());
  CHECK_FALSE(senders[2]->isSendingToOtherInstance());
  senders.clear();               // and the rest, with nothing left attached
  SUCCEED("no crash, no hang");
}
