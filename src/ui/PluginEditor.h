
#pragma once
#include <JuceHeader.h>

class ProjectMRenderer;
class MilkDAWpAudioProcessor;
class VisualizationWindow;
// Forward-declare the FIFO used by the embedded GL view
class LockFreeAudioFifo;

class MilkDAWpAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::AudioProcessorValueTreeState::Listener, // NEW: react to param changes
                                     private juce::ValueTree::Listener
{
public:
    explicit MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor&);
    ~MilkDAWpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Ensure graceful shutdown before destruction
    void editorBeingDeleted(); // not a JUCE override

protected:
    // Pause GL when editor is hidden, resume when shown
    void visibilityChanged() override;
    void parentHierarchyChanged() override; // detach when leaving desktop

private:
    MilkDAWpAudioProcessor& processor;

    // Tooltip host to enable tooltips across the editor
    juce::TooltipWindow tooltipWindow { this, 800 };

    juce::Label meterLabel;

    juce::DrawableButton btnFullscreen { "Fullscreen", juce::DrawableButton::ImageOnButtonBackground };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fullAtt;

    // New: Pop Out (dock/undock) toggle button
    juce::DrawableButton btnPopOut { "PopOut", juce::DrawableButton::ImageOnButtonBackground };
    bool popOutDesired { false }; // track whether user wants separate window

    // Visual controls (meaningful)
    juce::Slider ampScale, speed, hue, saturation, seed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampAtt, speedAtt, hueAtt, satAtt, seedAtt;
    // Labels for visual sliders
    juce::Label ampLabel { {}, "Amplitude" };
    juce::Label speedLabel { {}, "FPS Hint" };
    juce::Label hueLabel { {}, "Beat Sensitivity" };
    juce::Label satLabel { {}, "Mesh Size" };
    juce::Label seedLabel { {}, "Easter Egg" };

    // Preset selector UI
    juce::Label presetLabel { {}, "Preset:" };
    juce::ComboBox presetBox;
    juce::StringArray presetPaths; // full paths to match renderer
    // New: lazy loading UI (avoids heavy scanning)
    juce::TextButton btnLoadPreset { "Load Preset" };
    juce::TextButton btnClearPreset { "Clear" };
    juce::Label currentPresetLabel { {}, "(none)" };

    // External visualization window
    std::unique_ptr<VisualizationWindow> visWindow;

    void timerCallback() override;

    // Ensure GL teardown runs on message thread exactly once
    void shutdownGLOnMessageThread();
    std::atomic<bool> glShutdownRequested { false };

    // destroy VisualizationWindow synchronously on the message thread
    void destroyVisWindowOnMessageThread();

    // Race-safe external window creation state
    std::atomic<bool> creationPending { false };
    bool lastVisibleStateLogged { false };

    // Helper: consider ourselves on desktop as soon as we have a peer (visibility may follow shortly)
    bool isOnDesktop() const noexcept { return getPeer() != nullptr; }

    // NEW: APVTS listener hook
    void parameterChanged(const juce::String& paramID, float newValue) override;

    // ValueTree listener hooks (react to state replacement or property changes like presetPath)
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
    void valueTreeRedirected(juce::ValueTree& treeWhichHasBeenChanged) override;

    // NEW: UI-thread handlers
    void handleShowWindowChangeOnUI(bool wantWindow);
    void handleFullscreenChangeOnUI(bool wantFullscreen);

    // Helpers
    void populatePresetBox();
    void setPresetParam(int newIndex);
    void refreshPresetPathFromState();

    // Remember last user-selected preset path (via Load Preset...)
    juce::String lastPresetPath;

    // Track docking state around fullscreen transitions
    bool wasDockedBeforeFullscreen { false };

    // Remember the pop-out state before entering fullscreen; used to restore on ESC/exit
    bool previousPopoutWasDocked { false };
    // Guard: ensure we only capture previousPopoutWasDocked once per fullscreen entry
    bool previousPopoutStateCaptured { false };

    // Logo image loaded from BinaryData
    juce::Image logoImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MilkDAWpAudioProcessorEditor)
};
