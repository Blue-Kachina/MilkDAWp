// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "milkdawp/engine/LayerChannel.h"
#include "milkdawp/engine/RenderEngine.h"

namespace milkdawp::plugin {

/// A plugin instance as the target picker sees it.
struct InstanceInfo {
  std::string id;
  std::string name;
  /// False for an instance that already sends its picture elsewhere: Layers
  /// does not chain (layers_like_shrek.md §3).
  bool canBeTarget = true;
};

/// Every MilkDAWp instance loaded in this process, so one can send its picture
/// to another's Output window (Layers L3, layers_like_shrek.md §3).
///
/// A process-wide registry living in the plugin binary: two different builds
/// (the release plugin and the dev-identity one) are separate binaries and never
/// see each other, and a host that sandboxes plugins in separate processes
/// leaves each instance alone in its own registry, which is how the Layers UI
/// knows to say so.
///
/// Lifetime is the hard part, because instances can be created and destroyed
/// in any order, on any thread. The rules that keep it safe:
///   - `Entry` is shared (`shared_ptr`); the processor owns one, and a sender
///     holds its hub's, so neither can dangle.
///   - All bookkeeping about who is attached to a hub is under the hub entry's
///     mutex, together with every call into the hub's engine.
///   - A hub `retire()`s before its engine is destroyed: that detaches every
///     sender (blocking until the engine has let go of each channel) and then
///     nulls the engine pointer. A sender detaches before *its* engine is
///     destroyed. Whichever happens first, no render thread is left touching a
///     channel that is about to die.
class LayerRegistry {
public:
  class Entry {
  public:
    struct Hooks {
      /// Message thread. The Output window actions a sender forwards to its hub.
      std::function<void()> popOut;
      std::function<void()> toggleFullscreen;
      /// Any thread. Read or write one of this instance's parameters by id, in
      /// plain (not normalised) units, so its hub can edit it from the Sources list.
      std::function<float(const std::string& parameterId)> getParameter;
      std::function<void(const std::string& parameterId, float plainValue)> setParameter;
    };

    /// One sender as its hub's Sources list shows it.
    struct SenderInfo {
      std::string id;
      std::string name;
      float opacity = 1.0f;
      int blend = 0;
      bool mute = false;
      int order = 0;
      /// GPU time the hub's last frame spent drawing this sender's layer, in ms.
      float gpuMs = 0.0f;
    };

    [[nodiscard]] std::string id() const;
    [[nodiscard]] std::string name() const;
    void setName(std::string name);

    // ---- as a hub (called by senders, any thread) ----
    /// Adds `channel` as a layer of this entry's engine. `sender` is the entry
    /// of the instance that owns the channel; the hub keeps only a weak
    /// reference to it, for the Sources list. False if this entry is retired, is
    /// itself a sender, or the engine refuses (full, not running).
    bool attach(engine::LayerChannel& channel, const std::shared_ptr<Entry>& sender);
    /// Removes `channel`; blocks until the engine can no longer touch it.
    void detach(engine::LayerChannel& channel);
    [[nodiscard]] int attachedCount() const;
    /// The instances sending here, in the order they joined, with their layer
    /// settings. Reads each sender's parameters through its hooks.
    [[nodiscard]] std::vector<SenderInfo> senders() const;
    /// Sets one layer parameter (`layerOpacity`, `layerBlend`, `layerMute`,
    /// `layerOrder`) on the attached sender `senderId`; ignored for any other id.
    void setSenderParameter(const std::string& senderId, const std::string& parameterId, float plainValue);
    [[nodiscard]] float getParameter(const std::string& parameterId) const;
    void setParameter(const std::string& parameterId, float plainValue);
    /// False once the owning instance has gone.
    [[nodiscard]] bool isAlive() const;
    /// Message thread: forwards to the owning instance's Output window.
    void popOut();
    void toggleFullscreen();

    // ---- as a sender ----
    void setSender(bool sender);
    [[nodiscard]] bool isSender() const;

  private:
    friend class LayerRegistry;
    Entry(std::string id, std::string name, engine::RenderEngine& engine, Hooks hooks);
    void retire();

    mutable std::mutex mutex_;
    std::string id_;
    std::string name_;
    engine::RenderEngine* engine_;
    Hooks hooks_;
    struct Attachment {
      engine::LayerChannel* channel;
      std::weak_ptr<Entry> sender;
    };
    std::vector<Attachment> attached_;
    bool sender_ = false;
  };

  [[nodiscard]] static LayerRegistry& get();

  /// Registers an instance under `desiredId`; if another live instance already
  /// has that id (a duplicated project state), makes up a fresh one. The entry's
  /// `id()` says which.
  [[nodiscard]] std::shared_ptr<Entry> add(const std::string& desiredId, std::string name,
                                           engine::RenderEngine& engine, Entry::Hooks hooks);
  /// Changes an entry's id, with the same collision rule as add(). Returns the id in effect.
  std::string claimId(const std::shared_ptr<Entry>& entry, const std::string& desiredId);
  /// Retires the entry (detaching its senders) and forgets it. Call before the
  /// instance's engine is destroyed.
  void remove(const std::shared_ptr<Entry>& entry);

  [[nodiscard]] std::shared_ptr<Entry> find(const std::string& id) const;
  /// Everything registered except `excludeId`.
  [[nodiscard]] std::vector<InstanceInfo> instances(const std::string& excludeId = {}) const;

  /// Called (on whatever thread made the change) whenever an instance appears,
  /// goes, is renamed, or starts/stops sending. Listeners only schedule work;
  /// they must not call back into the registry. `removeListener` blocks until a
  /// notification in flight has finished, so a listener's owner may be destroyed
  /// right after it returns.
  int addListener(std::function<void()> listener);
  void removeListener(int token);
  void notifyChanged();

private:
  [[nodiscard]] static std::string freshId();
  [[nodiscard]] bool idTaken(const std::string& id, const Entry* except) const;

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Entry>> entries_;

  std::mutex listenerMutex_;
  std::vector<std::pair<int, std::function<void()>>> listeners_;
  int nextToken_ = 1;
};

} // namespace milkdawp::plugin
