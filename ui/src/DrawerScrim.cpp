// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/DrawerScrim.h"

namespace milkdawp::ui {

void DrawerScrim::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black.withAlpha(0.55f)); }

} // namespace milkdawp::ui
