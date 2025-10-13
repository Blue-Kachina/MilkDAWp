// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
// Placeholder editor translation unit. The minimal editor is implemented in PluginProcessor.cpp for scaffolding.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace milkdawp_ui_stub {

// Minimal UI stub: a small component exposing an About → Licenses button which opens THIRD_PARTY_NOTICES.md.
struct LicensesButtonComponent : public juce::Component {
    juce::TextButton licensesButton { "Licenses" };
    LicensesButtonComponent()
    {
        addAndMakeVisible(licensesButton);
        licensesButton.onClick = []{
            // Try to open the repo-local THIRD_PARTY_NOTICES.md during development
            juce::File notices = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                                    .getParentDirectory() // .../MilkDAWp.vst3 or exe dir
                                    .getChildFile("THIRD_PARTY_NOTICES.md");
            if (! notices.existsAsFile()) {
                // Fallback to project URL
                juce::URL url ("https://example.com/THIRD_PARTY_NOTICES.md");
                url.launchInDefaultBrowser();
                return;
            }
            juce::URL(notices).launchInDefaultBrowser();
        };
    }
    void resized() override
    {
        licensesButton.setBounds(getLocalBounds().reduced(4));
    }
};

} // namespace milkdawp_ui_stub
