// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <juce_core/juce_core.h>

namespace milkdawp::engine {

/// 6.7: the opt-in update check. Off until the user turns it on in About.
/// Only the standalone app goes online: a network thread inside a host's
/// process, in a plugin the host may unload at any moment, is a risk no
/// update notice is worth. The plugin's About shows what the app last found
/// (the settings file below is shared).

/// The GitHub repository whose releases are checked ("owner/name").
inline constexpr const char* kReleasesRepository = "Blue-Kachina/MilkDAWp";
/// Where a person sees the releases.
inline constexpr const char* kReleasesPage = "https://github.com/Blue-Kachina/MilkDAWp/releases";

/// Orders "major.minor.patch[-pre]" versions (a leading "v" is ignored): a
/// pre-release sorts before its release, and pre-release parts compare
/// numerically when both are numbers ("beta.10" > "beta.9", "rc" > "beta").
/// Negative, zero or positive, like strcmp. Unparsable parts count as 0.
[[nodiscard]] int compareVersions(std::string_view a, std::string_view b);
[[nodiscard]] bool isPreRelease(std::string_view version);

struct ReleaseInfo {
  std::string version; // tag without the "v"
  std::string url;     // the release's page
  bool preRelease = false;
};

/// From GitHub's "list releases" JSON: the newest release someone running
/// `current` should hear about, if it is newer. Drafts never count;
/// pre-releases count only for someone already on a pre-release (a beta
/// tester hears about the next beta; a 1.0 user only about 1.0.1).
[[nodiscard]] std::optional<ReleaseInfo> newerRelease(const juce::String& releasesJson, std::string_view current);

/// What the check remembers, in the user data folder beside the preset
/// metadata (`update-check.json`), shared by the app and every plugin.
struct UpdateSettings {
  bool enabled = false;
  juce::int64 lastCheckMs = 0;      // when a check last completed
  std::string latestVersion;        // newest release seen (empty: none newer)
  std::string latestUrl;

  [[nodiscard]] static juce::File defaultFile();
  [[nodiscard]] static UpdateSettings load(const juce::File& file);
  bool save(const juce::File& file) const;

  /// Enabled, and a day or more since the last check.
  [[nodiscard]] bool isDue(juce::int64 nowMs) const;
};

/// Runs the check on its own thread (the app's), so nothing waits on the
/// network. Message thread for everything but the worker.
class UpdateChecker {
public:
  enum class State { Off, Idle, Checking, UpToDate, Available, Failed };
  struct Status {
    State state = State::Off;
    std::string version; // Available: the newer version
    std::string url;     // Available: its release page
    std::string error;   // Failed: why
    juce::int64 checkedAtMs = 0;
  };

  /// Fetches the releases JSON; empty plus `error` on failure. Replaceable
  /// for tests.
  using Fetcher = std::function<juce::String(std::string& error)>;

  explicit UpdateChecker(juce::File settingsFile = UpdateSettings::defaultFile(), Fetcher fetcher = {});
  ~UpdateChecker(); // waits for a check in flight (bounded by the fetch timeouts)
  UpdateChecker(const UpdateChecker&) = delete;
  UpdateChecker& operator=(const UpdateChecker&) = delete;

  [[nodiscard]] bool enabled() const;
  void setEnabled(bool enabled);

  /// Starts a check if enabled and one is due (app start-up).
  void checkIfDue();
  /// Starts a check now, enabled or not ("Check now").
  void checkNow();

  [[nodiscard]] Status status() const;

  /// Called on the message thread when a check finishes.
  std::function<void()> onFinished;

  /// The default fetcher: HTTPS GET of the releases API with JUCE on
  /// Windows and macOS; on Linux, where JUCE is built without curl, the
  /// `curl` command (present on practically every desktop).
  [[nodiscard]] static juce::String fetchReleases(std::string& error);

private:
  void start();
  void finish(const juce::String& json, const std::string& error);

  juce::File file_;
  Fetcher fetcher_;
  mutable std::mutex mutex_;
  UpdateSettings settings_;
  Status status_;
  std::thread worker_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

/// One line for About: "MilkDAWp 1.0.1 is available", "Up to date (checked
/// 5 Oct 2026)", "Couldn't check: ...", and so on.
[[nodiscard]] juce::String describeUpdateStatus(const UpdateChecker::Status& status);

} // namespace milkdawp::engine
