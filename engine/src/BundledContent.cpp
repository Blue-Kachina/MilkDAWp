// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/BundledContent.h"

namespace milkdawp::engine {

juce::File BundledContent::defaultPreset() const {
  const auto file = presetFolder().getChildFile(kDefaultPresetRelativePath);
  return file.existsAsFile() ? file : juce::File();
}

std::string BundledContent::fromOldPack(const std::string& path) {
  // "<content root>/Presets/Cream of the Crop[/...]", either slash.
  const juce::String text(path);
  const auto normalised = text.replaceCharacter('\\', '/');
  const juce::String oldSegment = juce::String("/Presets/") + kOldPackFolderName;
  const auto at = normalised.indexOf(oldSegment);
  if (at < 0) {
    return path;
  }
  const auto end = at + oldSegment.length();
  if (end < normalised.length() && normalised[end] != '/') {
    return path; // "Cream of the Cropped", not the pack
  }
  // Keep everything up to "/Presets/" as it was written, slashes included.
  juce::File file(text.substring(0, at + 9) + kPackFolderName + text.substring(end));
  if (file.hasFileExtension("milk")) {
    file = file.withFileExtension("milkdawp");
  }
  return file.exists() ? file.getFullPathName().toStdString() : path;
}

std::vector<std::string> BundledContent::textureSearchPaths() const {
  if (!texturesFolder().isDirectory()) {
    return {};
  }
  return {texturesFolder().getFullPathName().toStdString()};
}

bool BundledContent::isContentRoot(const juce::File& dir) {
  return dir != juce::File() && BundledContent{dir}.presetFolder().isDirectory();
}

std::vector<juce::File> BundledContent::candidateRoots() {
  std::vector<juce::File> roots;
  auto add = [&roots](const juce::String& path) {
    if (path.isNotEmpty() && juce::File::isAbsolutePath(path)) {
      roots.emplace_back(path);
    }
  };

  add(juce::SystemStats::getEnvironmentVariable("MILKDAWP_CONTENT_DIR", {}));

  // For a plugin this is the plugin's own binary, not the host's.
  const auto binaryDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
  add(binaryDir.getChildFile("Content").getFullPathName());
  add(binaryDir.getSiblingFile("Resources").getChildFile("Content").getFullPathName());

#if JUCE_WINDOWS
  add(juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory).getChildFile("MilkDAWp").getFullPathName());
#elif JUCE_MAC
  add("/Library/Application Support/MilkDAWp");
  add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("Application Support/MilkDAWp")
          .getFullPathName());
#elif JUCE_LINUX || JUCE_BSD
  // FHS layout relative to the binary: an AppImage's usr/bin -> usr/share (6.4).
  add(binaryDir.getSiblingFile("share").getChildFile("milkdawp").getFullPathName());
  auto dataHome = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
  if (dataHome.isEmpty()) {
    dataHome = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/share").getFullPathName();
  }
  add(juce::File(dataHome).getChildFile("milkdawp").getFullPathName());
  add("/usr/local/share/milkdawp");
  add("/usr/share/milkdawp");
#endif

#ifdef MILKDAWP_BUILD_TREE_CONTENT_DIR
  add(MILKDAWP_BUILD_TREE_CONTENT_DIR);
#endif
  return roots;
}

std::optional<BundledContent> BundledContent::find() {
  for (const auto& root : candidateRoots()) {
    if (isContentRoot(root)) {
      return BundledContent{root};
    }
  }
  return std::nullopt;
}

} // namespace milkdawp::engine
