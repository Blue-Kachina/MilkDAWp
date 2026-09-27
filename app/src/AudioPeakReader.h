// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace milkdawp::app {

/// Who reads an audio source's input peak: each reader has its own slot, so
/// the level meter and the "no signal" monitor never take each other's
/// peaks. Shared between `AudioInput` and `SystemAudioCapture` (§4.7), which
/// track peaks the same way over two different audio paths.
enum class PeakReader { Meter, Monitor };

} // namespace milkdawp::app
