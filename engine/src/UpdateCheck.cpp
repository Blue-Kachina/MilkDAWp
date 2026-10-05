// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/UpdateCheck.h"

#include <algorithm>
#include <charconv>
#include <vector>

#include <juce_events/juce_events.h>

#include "milkdawp/core/Version.h"

namespace milkdawp::engine {

namespace {

constexpr juce::int64 kDayMs = 24LL * 60 * 60 * 1000;

std::vector<std::string_view> split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (true) {
    const auto end = text.find(separator, start);
    parts.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
    if (end == std::string_view::npos) {
      return parts;
    }
    start = end + 1;
  }
}

bool isNumber(std::string_view text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

long long toNumber(std::string_view text) {
  long long value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

int sign(long long value) { return value < 0 ? -1 : (value > 0 ? 1 : 0); }

std::string_view trimV(std::string_view version) {
  if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) {
    version.remove_prefix(1);
  }
  return version;
}

} // namespace

bool isPreRelease(std::string_view version) { return trimV(version).find('-') != std::string_view::npos; }

int compareVersions(std::string_view a, std::string_view b) {
  a = trimV(a);
  b = trimV(b);
  const auto dashA = a.find('-');
  const auto dashB = b.find('-');
  const auto coreA = split(a.substr(0, dashA), '.');
  const auto coreB = split(b.substr(0, dashB), '.');
  for (std::size_t i = 0; i < 3; ++i) {
    const auto x = i < coreA.size() ? toNumber(coreA[i]) : 0;
    const auto y = i < coreB.size() ? toNumber(coreB[i]) : 0;
    if (x != y) {
      return sign(x - y);
    }
  }
  const bool preA = dashA != std::string_view::npos;
  const bool preB = dashB != std::string_view::npos;
  if (preA != preB) {
    return preA ? -1 : 1; // 1.0.0-beta.1 < 1.0.0
  }
  if (!preA) {
    return 0;
  }
  const auto partsA = split(a.substr(dashA + 1), '.');
  const auto partsB = split(b.substr(dashB + 1), '.');
  for (std::size_t i = 0; i < std::max(partsA.size(), partsB.size()); ++i) {
    if (i >= partsA.size()) {
      return -1; // fewer parts sorts first (semver)
    }
    if (i >= partsB.size()) {
      return 1;
    }
    const auto& x = partsA[i];
    const auto& y = partsB[i];
    if (isNumber(x) && isNumber(y)) {
      if (toNumber(x) != toNumber(y)) {
        return sign(toNumber(x) - toNumber(y));
      }
    } else if (x != y) {
      return x < y ? -1 : 1; // "alpha" < "beta" < "rc"
    }
  }
  return 0;
}

std::optional<ReleaseInfo> newerRelease(const juce::String& releasesJson, std::string_view current) {
  const auto parsed = juce::JSON::parse(releasesJson);
  const auto* releases = parsed.getArray();
  if (releases == nullptr) {
    return std::nullopt;
  }
  const bool onPreRelease = isPreRelease(current);
  std::optional<ReleaseInfo> best;
  for (const auto& release : *releases) {
    if (static_cast<bool>(release.getProperty("draft", false))) {
      continue;
    }
    ReleaseInfo info;
    info.version = std::string(trimV(release.getProperty("tag_name", "").toString().toStdString()));
    info.url = release.getProperty("html_url", "").toString().toStdString();
    info.preRelease = static_cast<bool>(release.getProperty("prerelease", false)) || isPreRelease(info.version);
    if (info.version.empty() || (info.preRelease && !onPreRelease)) {
      continue;
    }
    if (compareVersions(info.version, current) > 0 && (!best || compareVersions(info.version, best->version) > 0)) {
      best = info;
    }
  }
  return best;
}

juce::File UpdateSettings::defaultFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
      .getChildFile("Application Support")
#endif
      .getChildFile(MILKDAWP_USER_DATA_FOLDER)
      .getChildFile("update-check.json");
}

UpdateSettings UpdateSettings::load(const juce::File& file) {
  UpdateSettings settings;
  const auto parsed = juce::JSON::parse(file.loadFileAsString());
  if (parsed.isObject()) {
    settings.enabled = static_cast<bool>(parsed.getProperty("enabled", false));
    settings.lastCheckMs = static_cast<juce::int64>(parsed.getProperty("lastCheckMs", 0));
    settings.latestVersion = parsed.getProperty("latestVersion", "").toString().toStdString();
    settings.latestUrl = parsed.getProperty("latestUrl", "").toString().toStdString();
  }
  return settings;
}

bool UpdateSettings::save(const juce::File& file) const {
  auto* object = new juce::DynamicObject();
  object->setProperty("enabled", enabled);
  object->setProperty("lastCheckMs", lastCheckMs);
  object->setProperty("latestVersion", juce::String(latestVersion));
  object->setProperty("latestUrl", juce::String(latestUrl));
  file.getParentDirectory().createDirectory();
  return file.replaceWithText(juce::JSON::toString(juce::var(object)));
}

bool UpdateSettings::isDue(juce::int64 nowMs) const { return enabled && nowMs - lastCheckMs >= kDayMs; }

UpdateChecker::UpdateChecker(juce::File settingsFile, Fetcher fetcher)
    : file_(std::move(settingsFile)), fetcher_(fetcher ? std::move(fetcher) : Fetcher(&UpdateChecker::fetchReleases)) {
  settings_ = UpdateSettings::load(file_);
  status_.state = settings_.enabled ? State::Idle : State::Off;
  status_.checkedAtMs = settings_.lastCheckMs;
  if (!settings_.latestVersion.empty() && compareVersions(settings_.latestVersion, core::versionString()) > 0) {
    status_.state = State::Available; // remembered from the last check
    status_.version = settings_.latestVersion;
    status_.url = settings_.latestUrl;
  }
}

UpdateChecker::~UpdateChecker() {
  alive_->store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool UpdateChecker::enabled() const {
  const std::lock_guard lock(mutex_);
  return settings_.enabled;
}

void UpdateChecker::setEnabled(bool enabled) {
  {
    const std::lock_guard lock(mutex_);
    settings_.enabled = enabled;
    settings_.save(file_);
    if (status_.state == State::Off || status_.state == State::Idle) {
      status_.state = enabled ? State::Idle : State::Off;
    }
  }
  if (enabled) {
    checkIfDue();
  }
}

void UpdateChecker::checkIfDue() {
  bool due = false;
  {
    const std::lock_guard lock(mutex_);
    due = settings_.isDue(juce::Time::currentTimeMillis()) && status_.state != State::Checking;
  }
  if (due) {
    start();
  }
}

void UpdateChecker::checkNow() {
  {
    const std::lock_guard lock(mutex_);
    if (status_.state == State::Checking) {
      return;
    }
  }
  start();
}

UpdateChecker::Status UpdateChecker::status() const {
  const std::lock_guard lock(mutex_);
  return status_;
}

void UpdateChecker::start() {
  if (worker_.joinable()) {
    worker_.join(); // the previous check has finished (state was not Checking)
  }
  {
    const std::lock_guard lock(mutex_);
    status_.state = State::Checking;
  }
  worker_ = std::thread([this] {
    std::string error;
    const auto json = fetcher_(error);
    finish(json, error);
  });
}

void UpdateChecker::finish(const juce::String& json, const std::string& error) {
  const auto now = juce::Time::currentTimeMillis();
  {
    const std::lock_guard lock(mutex_);
    status_.checkedAtMs = now;
    if (json.isEmpty()) {
      status_.state = State::Failed;
      status_.error = error.empty() ? "no answer" : error;
    } else if (!juce::JSON::parse(json).isArray()) {
      status_.state = State::Failed;
      status_.error = "unexpected answer from GitHub";
    } else {
      const auto newer = newerRelease(json, core::versionString());
      status_.state = newer ? State::Available : State::UpToDate;
      status_.version = newer ? newer->version : std::string{};
      status_.url = newer ? newer->url : std::string{};
      settings_.lastCheckMs = now;
      settings_.latestVersion = status_.version;
      settings_.latestUrl = status_.url;
      settings_.save(file_);
    }
  }
  // Tell the owner on the message thread, unless it is gone by then.
  if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
    juce::MessageManager::callAsync([this, alive = alive_] {
      if (alive->load() && onFinished) {
        onFinished();
      }
    });
  }
}

juce::String UpdateChecker::fetchReleases(std::string& error) {
  const juce::String api = juce::String("https://api.github.com/repos/") + kReleasesRepository + "/releases?per_page=30";
#if JUCE_LINUX || JUCE_BSD
  // JUCE is built without libcurl on Linux (no extra runtime dependency), so
  // ask the curl command instead.
  juce::ChildProcess curl;
  const juce::StringArray command{"curl", "-fsSL", "--max-time", "15", "-H", "Accept: application/vnd.github+json",
                                  "-A", "MilkDAWp", api};
  if (!curl.start(command, juce::ChildProcess::wantStdOut)) {
    error = "the curl command isn't installed";
    return {};
  }
  const auto output = curl.readAllProcessOutput();
  if (!curl.waitForProcessToFinish(20000) || curl.getExitCode() != 0) {
    error = "couldn't reach GitHub (curl exit " + std::to_string(curl.getExitCode()) + ")";
    return {};
  }
  return output;
#else
  int statusCode = 0;
  auto stream = juce::URL(api).createInputStream(
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: MilkDAWp")
          .withConnectionTimeoutMs(15000)
          .withStatusCode(&statusCode)
          .withNumRedirectsToFollow(5));
  if (stream == nullptr || statusCode != 200) {
    error = stream == nullptr ? "couldn't reach GitHub" : "GitHub answered " + std::to_string(statusCode);
    return {};
  }
  return stream->readEntireStreamAsString();
#endif
}

} // namespace milkdawp::engine

namespace milkdawp::engine {

juce::String describeUpdateStatus(const UpdateChecker::Status& status) {
  const auto when = status.checkedAtMs > 0 ? juce::Time(status.checkedAtMs).formatted("%e %b %Y").trim() : juce::String();
  switch (status.state) {
  case UpdateChecker::State::Off:
    return "Update checks are off.";
  case UpdateChecker::State::Idle:
    return when.isNotEmpty() ? "Up to date (checked " + when + ")." : juce::String("Not checked yet.");
  case UpdateChecker::State::Checking:
    return "Checking...";
  case UpdateChecker::State::UpToDate:
    return "Up to date (checked " + when + ").";
  case UpdateChecker::State::Available:
    return "MilkDAWp " + juce::String(status.version) + " is available: open the release page";
  case UpdateChecker::State::Failed:
    return "Couldn't check: " + juce::String(status.error) + ".";
  }
  return {};
}

} // namespace milkdawp::engine
