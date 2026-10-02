// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "LayerRegistry.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace milkdawp::plugin {

// ---- Entry ----

LayerRegistry::Entry::Entry(std::string id, std::string name, engine::RenderEngine& engine, Hooks hooks)
    : id_(std::move(id)), name_(std::move(name)), engine_(&engine), hooks_(std::move(hooks)) {}

std::string LayerRegistry::Entry::id() const {
  const std::lock_guard lock(mutex_);
  return id_;
}

std::string LayerRegistry::Entry::name() const {
  const std::lock_guard lock(mutex_);
  return name_;
}

void LayerRegistry::Entry::setName(std::string name) {
  {
    const std::lock_guard lock(mutex_);
    if (name_ == name) {
      return;
    }
    name_ = std::move(name);
  }
  LayerRegistry::get().notifyChanged();
}

bool LayerRegistry::Entry::attach(engine::LayerChannel& channel, const std::shared_ptr<Entry>& sender) {
  const std::lock_guard lock(mutex_);
  if (engine_ == nullptr || sender_) {
    return false;
  }
  const auto same = [&](const Attachment& a) { return a.channel == &channel; };
  if (std::any_of(attached_.begin(), attached_.end(), same)) {
    return true;
  }
  // Blocks until the render thread has created the layer. Holding the mutex
  // keeps retire() from pulling the engine out from under us meanwhile.
  if (!engine_->addLayer(channel)) {
    return false;
  }
  attached_.push_back({&channel, sender});
  return true;
}

std::vector<LayerRegistry::Entry::SenderInfo> LayerRegistry::Entry::senders() const {
  // Copy the references first: reading a sender takes that sender's own mutex,
  // and no two entry mutexes are ever held together.
  std::vector<std::pair<std::shared_ptr<Entry>, float>> list;
  {
    const std::lock_guard lock(mutex_);
    for (const auto& attachment : attached_) {
      if (auto sender = attachment.sender.lock()) {
        // The channel is alive while it is attached, and we hold the mutex.
        list.emplace_back(std::move(sender), attachment.channel->gpuMs());
      }
    }
  }
  std::vector<SenderInfo> result;
  for (const auto& [sender, gpuMs] : list) {
    SenderInfo info;
    info.gpuMs = std::round(gpuMs * 10.0f) / 10.0f; // 0.1 ms steps: a steady layer reads steady
    info.id = sender->id();
    info.name = sender->name();
    info.opacity = sender->getParameter("layerOpacity");
    info.blend = static_cast<int>(sender->getParameter("layerBlend") + 0.5f);
    info.mute = sender->getParameter("layerMute") > 0.5f;
    info.order = static_cast<int>(sender->getParameter("layerOrder") + 0.5f);
    result.push_back(std::move(info));
  }
  return result;
}

void LayerRegistry::Entry::setSenderParameter(const std::string& senderId, const std::string& parameterId,
                                              float plainValue) {
  if (parameterId != "layerOpacity" && parameterId != "layerBlend" && parameterId != "layerMute" &&
      parameterId != "layerOrder") {
    return; // the hub edits how a sender is mixed, nothing else about it
  }
  std::vector<std::shared_ptr<Entry>> candidates; // never hold two entry mutexes together
  {
    const std::lock_guard lock(mutex_);
    for (const auto& attachment : attached_) {
      if (auto sender = attachment.sender.lock()) {
        candidates.push_back(std::move(sender));
      }
    }
  }
  for (const auto& sender : candidates) {
    if (sender->id() == senderId) {
      sender->setParameter(parameterId, plainValue);
      return;
    }
  }
}

float LayerRegistry::Entry::getParameter(const std::string& parameterId) const {
  const std::lock_guard lock(mutex_);
  return hooks_.getParameter ? hooks_.getParameter(parameterId) : 0.0f;
}

void LayerRegistry::Entry::setParameter(const std::string& parameterId, float plainValue) {
  const std::lock_guard lock(mutex_);
  if (hooks_.setParameter) {
    hooks_.setParameter(parameterId, plainValue);
  }
}

void LayerRegistry::Entry::detach(engine::LayerChannel& channel) {
  const std::lock_guard lock(mutex_);
  const auto it = std::find_if(attached_.begin(), attached_.end(),
                               [&](const Attachment& a) { return a.channel == &channel; });
  if (it == attached_.end()) {
    return; // never attached, or retire() already let it go
  }
  if (engine_ != nullptr) {
    engine_->removeLayer(channel);
  }
  attached_.erase(it);
}

int LayerRegistry::Entry::attachedCount() const {
  const std::lock_guard lock(mutex_);
  return static_cast<int>(attached_.size());
}

bool LayerRegistry::Entry::isAlive() const {
  const std::lock_guard lock(mutex_);
  return engine_ != nullptr;
}

void LayerRegistry::Entry::popOut() {
  const std::lock_guard lock(mutex_);
  if (hooks_.popOut) {
    hooks_.popOut();
  }
}

void LayerRegistry::Entry::toggleFullscreen() {
  const std::lock_guard lock(mutex_);
  if (hooks_.toggleFullscreen) {
    hooks_.toggleFullscreen();
  }
}

void LayerRegistry::Entry::setSender(bool sender) {
  {
    const std::lock_guard lock(mutex_);
    if (sender_ == sender) {
      return;
    }
    sender_ = sender;
  }
  LayerRegistry::get().notifyChanged();
}

bool LayerRegistry::Entry::isSender() const {
  const std::lock_guard lock(mutex_);
  return sender_;
}

void LayerRegistry::Entry::retire() {
  const std::lock_guard lock(mutex_);
  if (engine_ != nullptr) {
    for (const auto& attachment : attached_) {
      engine_->removeLayer(*attachment.channel); // blocks until the render thread has let go of it
    }
  }
  attached_.clear();
  engine_ = nullptr;
  hooks_ = {};
}

// ---- LayerRegistry ----

LayerRegistry& LayerRegistry::get() {
  static LayerRegistry registry;
  return registry;
}

std::string LayerRegistry::freshId() { return juce::Uuid().toString().toStdString(); }

bool LayerRegistry::idTaken(const std::string& id, const Entry* except) const {
  // Caller holds mutex_. Entry::id() takes the entry's own mutex; nothing that
  // holds an entry mutex ever takes this one, so the order is safe.
  return std::any_of(entries_.begin(), entries_.end(),
                     [&](const auto& entry) { return entry.get() != except && entry->id() == id; });
}

std::shared_ptr<LayerRegistry::Entry> LayerRegistry::add(const std::string& desiredId, std::string name,
                                                         engine::RenderEngine& engine, Entry::Hooks hooks) {
  std::shared_ptr<Entry> entry;
  {
    const std::lock_guard lock(mutex_);
    auto id = desiredId;
    while (id.empty() || idTaken(id, nullptr)) {
      id = freshId();
    }
    entry = std::shared_ptr<Entry>(new Entry(std::move(id), std::move(name), engine, std::move(hooks)));
    entries_.push_back(entry);
  }
  notifyChanged();
  return entry;
}

std::string LayerRegistry::claimId(const std::shared_ptr<Entry>& entry, const std::string& desiredId) {
  std::string result;
  {
    const std::lock_guard lock(mutex_);
    result = desiredId;
    while (result.empty() || idTaken(result, entry.get())) {
      result = freshId();
    }
    const std::lock_guard entryLock(entry->mutex_);
    if (entry->id_ == result) {
      return result;
    }
    entry->id_ = result;
  }
  notifyChanged();
  return result;
}

void LayerRegistry::remove(const std::shared_ptr<Entry>& entry) {
  {
    const std::lock_guard lock(mutex_);
    entries_.erase(std::remove(entries_.begin(), entries_.end(), entry), entries_.end());
  }
  // Outside the registry lock: this blocks on the engine's render thread.
  entry->retire();
  notifyChanged();
}

std::shared_ptr<LayerRegistry::Entry> LayerRegistry::find(const std::string& id) const {
  if (id.empty()) {
    return nullptr;
  }
  const std::lock_guard lock(mutex_);
  const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry->id() == id; });
  return it != entries_.end() ? *it : nullptr;
}

std::vector<InstanceInfo> LayerRegistry::instances(const std::string& excludeId) const {
  std::vector<InstanceInfo> result;
  const std::lock_guard lock(mutex_);
  for (const auto& entry : entries_) {
    auto id = entry->id();
    if (id == excludeId) {
      continue;
    }
    result.push_back({std::move(id), entry->name(), !entry->isSender()});
  }
  return result;
}

int LayerRegistry::addListener(std::function<void()> listener) {
  const std::lock_guard lock(listenerMutex_);
  const int token = nextToken_++;
  listeners_.emplace_back(token, std::move(listener));
  return token;
}

void LayerRegistry::removeListener(int token) {
  const std::lock_guard lock(listenerMutex_);
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [&](const auto& listener) { return listener.first == token; }),
                   listeners_.end());
}

void LayerRegistry::notifyChanged() {
  // Under the listener lock on purpose: removeListener() then waits for a
  // notification in flight, so a listener's owner can be destroyed safely.
  const std::lock_guard lock(listenerMutex_);
  for (const auto& listener : listeners_) {
    listener.second();
  }
}

} // namespace milkdawp::plugin
