// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

// Engine-internal: the camera back end of this platform (8.6b/d), behind the
// `camera:<name>` media paths. Windows and macOS: JUCE's CameraDevice
// (CameraJuce.cpp). Linux: V4L2 (CameraLinux.cpp).

#include <memory>
#include <string>

#include <juce_core/juce_core.h>

#include "milkdawp/engine/MediaSource.h"

namespace milkdawp::engine::cameras {

[[nodiscard]] bool supported() noexcept;
/// The cameras attached now, by name. Opens none.
[[nodiscard]] juce::StringArray available();
/// The camera called `name`, shared by everyone in the process who shows it.
/// Null, with `error` set, if it isn't attached.
[[nodiscard]] std::shared_ptr<MediaSource> open(const juce::String& name, std::string& error);

} // namespace milkdawp::engine::cameras
