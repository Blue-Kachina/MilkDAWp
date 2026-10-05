// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/OscRemote.h"

#include <juce_osc/juce_osc.h>

namespace milkdawp::engine {

juce::File oscSettingsFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
      .getChildFile("Application Support")
#endif
      .getChildFile(MILKDAWP_USER_DATA_FOLDER)
      .getChildFile("osc.json");
}

core::OscSettings loadOscSettings(const juce::File& file) {
  core::OscSettings settings;
  const auto parsed = juce::JSON::parse(file.loadFileAsString());
  if (parsed.isObject()) {
    settings.enabled = static_cast<bool>(parsed.getProperty("enabled", false));
    settings.receivePort = juce::jlimit(1, 65535, static_cast<int>(parsed.getProperty("receivePort", 9000)));
    settings.sendHost = parsed.getProperty("sendHost", "127.0.0.1").toString().toStdString();
    settings.sendPort = juce::jlimit(1, 65535, static_cast<int>(parsed.getProperty("sendPort", 9001)));
  }
  return settings;
}

bool saveOscSettings(const core::OscSettings& settings, const juce::File& file) {
  auto* object = new juce::DynamicObject();
  object->setProperty("enabled", settings.enabled);
  object->setProperty("receivePort", settings.receivePort);
  object->setProperty("sendHost", juce::String(settings.sendHost));
  object->setProperty("sendPort", settings.sendPort);
  file.getParentDirectory().createDirectory();
  return file.replaceWithText(juce::JSON::toString(juce::var(object)));
}

struct OscRemote::Impl final : juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback> {
  explicit Impl(OscRemote& owner) : remote(owner) {}

  void oscMessageReceived(const juce::OSCMessage& message) override {
    juce::String text;
    float value = 0.0f;
    bool hasValue = false;
    if (!message.isEmpty()) {
      const auto& argument = message[0];
      if (argument.isFloat32()) {
        value = argument.getFloat32();
        hasValue = true;
      } else if (argument.isInt32()) {
        value = static_cast<float>(argument.getInt32());
        hasValue = true;
      } else if (argument.isString()) {
        text = argument.getString();
      }
    }
    remote.dispatch(message.getAddressPattern().toString(), text, value, hasValue);
  }

  void oscBundleReceived(const juce::OSCBundle& bundle) override {
    for (const auto& element : bundle) {
      if (element.isMessage()) {
        oscMessageReceived(element.getMessage());
      } else if (element.isBundle()) {
        oscBundleReceived(element.getBundle());
      }
    }
  }

  // What each endpoint last sent, so only changes go out.
  struct Sent {
    std::uint64_t beat = 0;
    std::uint32_t bar = 0;
    std::uint32_t drops = 0;
    std::string preset;
    bool primed = false;
  };

  OscRemote& remote;
  juce::OSCReceiver receiver;
  juce::OSCSender sender;
  bool listening = false;
  bool sending = false;
  juce::String error;
  std::map<int, Sent> sent;
};

OscRemote::OscRemote() : impl_(std::make_unique<Impl>(*this)) {
  impl_->receiver.addListener(impl_.get());
  apply(loadOscSettings(oscSettingsFile()));
}

OscRemote::~OscRemote() {
  stopTimer();
  impl_->receiver.removeListener(impl_.get());
  impl_->receiver.disconnect();
  impl_->sender.disconnect();
}

int OscRemote::add(Endpoint endpoint) {
  const int handle = nextHandle_++;
  endpoints_.emplace(handle, std::move(endpoint));
  return handle;
}

void OscRemote::remove(int handle) {
  endpoints_.erase(handle);
  impl_->sent.erase(handle);
}

void OscRemote::apply(const core::OscSettings& settings) {
  settings_ = settings;
  auto& impl = *impl_;
  impl.receiver.disconnect();
  impl.sender.disconnect();
  impl.listening = false;
  impl.sending = false;
  impl.error.clear();
  stopTimer();
  if (!settings.enabled) {
    return;
  }
  impl.listening = impl.receiver.connect(settings.receivePort);
  if (!impl.listening) {
    impl.error = "Can't listen on port " + juce::String(settings.receivePort) + " (is another program using it?)";
  }
  impl.sending = impl.sender.connect(juce::String(settings.sendHost), settings.sendPort);
  impl.sent.clear();
  startTimerHz(60); // the outbound beat: about a frame of jitter at most
}

juce::String OscRemote::statusText() const {
  if (!settings_.enabled) {
    return "Off";
  }
  if (impl_->error.isNotEmpty()) {
    return impl_->error;
  }
  return "Listening on port " + juce::String(settings_.receivePort) + ", sending to " +
         juce::String(settings_.sendHost) + ":" + juce::String(settings_.sendPort);
}

int OscRemote::dispatch(const juce::String& address, const juce::String& firstArgument, float value, bool hasValue) {
  juce::ignoreUnused(firstArgument);
  const auto command = core::parseOscAddress(address.toStdString());
  if (!command) {
    return 0;
  }
  int reached = 0;
  // A copy: a move may make a shell add or remove endpoints.
  const auto endpoints = endpoints_;
  for (const auto& [handle, endpoint] : endpoints) {
    const auto name = endpoint.name ? endpoint.name() : std::string{};
    const auto id = endpoint.id ? endpoint.id() : std::string{};
    if (!core::oscAddresses(*command, name, id)) {
      continue;
    }
    switch (command->kind) {
    case core::OscCommand::Kind::Next:
      // A button sends 1 on press and 0 on release: act on the press only.
      if (endpoint.next && (!hasValue || value > 0.5f)) {
        endpoint.next();
        ++reached;
      }
      break;
    case core::OscCommand::Kind::Previous:
      if (endpoint.previous && (!hasValue || value > 0.5f)) {
        endpoint.previous();
        ++reached;
      }
      break;
    case core::OscCommand::Kind::Parameter:
      if (hasValue && endpoint.setParameter && endpoint.setParameter(command->parameterId, value, command->normalized)) {
        ++reached;
      }
      break;
    }
  }
  return reached;
}

void OscRemote::timerCallback() {
  auto& impl = *impl_;
  if (!impl.sending) {
    return;
  }
  for (const auto& [handle, endpoint] : endpoints_) {
    if (!endpoint.status) {
      continue;
    }
    const auto status = endpoint.status();
    const auto preset = endpoint.presetName ? endpoint.presetName() : std::string{};
    auto name = core::oscName(endpoint.name ? endpoint.name() : std::string{});
    if (name.empty()) {
      name = "milkdawp";
    }
    const juce::String base = "/milkdawp/" + juce::String(name) + "/";
    auto& sent = impl.sent[handle];
    if (!sent.primed) {
      // Start from where things are: no burst of stale "events" on connect.
      sent = {status.beatIndex, status.barIndex, status.dropsDetected, preset, true};
      if (!preset.empty()) {
        impl.sender.send(base + "preset", juce::String(preset));
      }
      continue;
    }
    if (status.beatConfidence > 0.0f && status.beatIndex != sent.beat) {
      impl.sender.send(base + "beat", static_cast<int>(status.beatIndex));
      if (status.barIndex != sent.bar) {
        impl.sender.send(base + "bar", static_cast<int>(status.barIndex));
      }
    }
    sent.beat = status.beatIndex;
    sent.bar = status.barIndex;
    if (status.dropsDetected != sent.drops) {
      impl.sender.send(juce::OSCMessage(juce::OSCAddressPattern(base + "drop")));
      sent.drops = status.dropsDetected;
    }
    if (preset != sent.preset) {
      if (!preset.empty()) {
        impl.sender.send(base + "preset", juce::String(preset));
      }
      sent.preset = preset;
    }
  }
}

} // namespace milkdawp::engine
