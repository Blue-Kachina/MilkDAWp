#include "PluginEditor.h"
#include "../processor/PluginProcessor.h"
#include "../renderers/ProjectMRenderer.h"
#include "../utils/Logging.h"
#include "VisualizationWindow.h"

// ===== EditorGLComponent (embedded GL) =====
// Removed: embedded OpenGL preview inside the editor

// ===== Editor =====

MilkDAWpAudioProcessorEditor::MilkDAWpAudioProcessorEditor (MilkDAWpAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    MDW_LOG("UI", "Editor: constructed");
    setResizable(true, true);
    setSize (740, 170); // smaller height after removing input/output controls

    // Register APVTS listeners (react to param changes ASAP)
    processor.apvts.addParameterListener("showWindow", this);
    processor.apvts.addParameterListener("fullscreen", this);
    processor.apvts.addParameterListener("presetIndex", this);

    meterLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (meterLabel);

    // Removed input/output gain controls per user request

    // Visual controls: Amplitude, Speed, Hue, Saturation, Seed
    addAndMakeVisible(ampScale);
    ampScale.setTextValueSuffix("");
    ampScale.setRange(0.0, 4.0, 0.001);
    ampScale.setTooltip("Amplitude (audio-reactive strength)");
    ampLabel.setJustificationType(juce::Justification::centredRight);
    ampLabel.attachToComponent(&ampScale, false);
    ampAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "ampScale", ampScale);

    addAndMakeVisible(speed);
    speed.setTextValueSuffix("x");
    speed.setRange(0.1, 3.0, 0.001);
    speed.setTooltip("Animation speed");
    speedLabel.setJustificationType(juce::Justification::centredRight);
    speedLabel.attachToComponent(&speed, false);
    speedAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "speed", speed);

    addAndMakeVisible(hue);
    hue.setTextValueSuffix("");
    hue.setRange(0.0, 1.0, 0.0001);
    hue.setTooltip("Colour hue");
    hueLabel.setJustificationType(juce::Justification::centredRight);
    hueLabel.attachToComponent(&hue, false);
    hueAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "colorHue", hue);

    addAndMakeVisible(saturation);
    saturation.setTextValueSuffix("");
    saturation.setRange(0.0, 1.0, 0.0001);
    saturation.setTooltip("Colour saturation");
    satLabel.setJustificationType(juce::Justification::centredRight);
    satLabel.attachToComponent(&saturation, false);
    satAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "colorSat", saturation);

    addAndMakeVisible(seed);
    seed.setTextValueSuffix("");
    seed.setRange(0.0, 1000000.0, 1.0);
    seed.setTooltip("Random seed");
    seedLabel.setJustificationType(juce::Justification::centredRight);
    seedLabel.attachToComponent(&seed, false);
    seedAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "seed", seed);

    addAndMakeVisible(btnShowWindow);
    addAndMakeVisible(btnFullscreen);
    showAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "showWindow", btnShowWindow);
    fullAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "fullscreen", btnFullscreen);

    // Preset selector UI
    presetLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(presetLabel);
    addAndMakeVisible(presetBox);
    presetBox.setVisible(false); // avoid heavy scanning UI; use Load Preset button instead
    presetBox.onChange = [this]() {
        const int idx = presetBox.getSelectedItemIndex();
        setPresetParam(idx);
        if (visWindow)
            visWindow->setPresetIndex(idx);
    };

    // New: lazy preset loading controls
    addAndMakeVisible(btnLoadPreset);
    addAndMakeVisible(btnClearPreset);
    addAndMakeVisible(currentPresetLabel);
    currentPresetLabel.setJustificationType(juce::Justification::centredLeft);

    btnLoadPreset.onClick = [this]()
    {
        // Resolve presets root directory (same heuristics as before, but no scanning here)
        juce::File initialDir;
        auto exe = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
        auto bundleRoot = exe.getParentDirectory().getParentDirectory();
        juce::File presetsA = bundleRoot.getChildFile("Contents").getChildFile("Resources").getChildFile("presets");
        juce::File presetsB = bundleRoot.getChildFile("Resources").getChildFile("presets");
        if (presetsA.isDirectory()) initialDir = presetsA;
        else if (presetsB.isDirectory()) initialDir = presetsB;
        if (! initialDir.isDirectory())
        {
            auto dir = exe.getParentDirectory();
            for (int i = 0; i < 8 && ! initialDir.isDirectory(); ++i)
            {
                auto test = dir.getChildFile("resources").getChildFile("presets");
                if (test.isDirectory()) { initialDir = test; break; }
                dir = dir.getParentDirectory();
            }
        }
        if (! initialDir.isDirectory())
        {
            auto cwd = juce::File::getCurrentWorkingDirectory().getChildFile("resources").getChildFile("presets");
            if (cwd.isDirectory()) initialDir = cwd;
        }

        auto chooser = std::make_shared<juce::FileChooser>("Select a projectM preset (.milk)", initialDir, "*.milk");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc) mutable
            {
                juce::File f = fc.getResult();
                if (f.existsAsFile())
                {
                    currentPresetLabel.setText(f.getFileName(), juce::dontSendNotification);
                    if (visWindow)
                        visWindow->loadPresetByPath(f.getFullPathName(), true);
                }
            });
    };

    btnClearPreset.onClick = [this]()
    {
        currentPresetLabel.setText("(none)", juce::dontSendNotification);
        if (visWindow)
            visWindow->loadPresetByPath("idle://", true);
        // Also clear the parameterized index to -1 equivalent (0 in APVTS range), to avoid unintended switches
        setPresetParam(0);
    };

    // Keep legacy box populated with a cheap placeholder (no scanning)
    populatePresetBox();

    // New: log and ensure APVTS reflects button click (guarded to avoid redundant sets)
    btnShowWindow.onClick = [this]()
    {
        const bool want = btnShowWindow.getToggleState();
        MDW_LOG("UI", juce::String("Editor: btnShowWindow.onClick -> ") + (want ? "true" : "false"));
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(processor.apvts.getParameter("showWindow")))
        {
            const bool cur = p->get();
            if (cur != want)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(want ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
        }
    };

    // Removed: embedded GL view creation and initial param push

    // Begin polling params to control external visualization
    startTimerHz(15);
    MDW_LOG("UI", "Editor: timer started");
}

// Ensure GL teardown runs on the UI thread and only once
void MilkDAWpAudioProcessorEditor::shutdownGLOnMessageThread()
{
    if (glShutdownRequested.exchange(true))
        return; // already done or scheduled

    MDW_LOG("UI", "Editor: shutdownGLOnMessageThread");
    // Removed: no embedded GL to shut down anymore
}

void MilkDAWpAudioProcessorEditor::destroyVisWindowOnMessageThread()
{
    if (!visWindow)
        return;

    MDW_LOG("UI", "Editor: destroyVisWindowOnMessageThread");
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        visWindow->setVisible(false);
        visWindow.reset();
    }
    else
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread(
            [](void* ctx) -> void*
            {
                auto* self = static_cast<MilkDAWpAudioProcessorEditor*>(ctx);
                if (self->visWindow)
                {
                    self->visWindow->setVisible(false);
                    self->visWindow.reset();
                }
                return nullptr;
            }, this);
    }
}

void MilkDAWpAudioProcessorEditor::visibilityChanged()
{
    const bool visible = isShowing();
    MDW_LOG("UI", juce::String("Editor: visibilityChanged -> ") + (visible ? "showing" : "hidden") + ", hasPeer=" + (getPeer() ? "yes" : "no"));

    if (!visible)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            shutdownGLOnMessageThread();
        else
        {
            juce::MessageManager::getInstance()->callFunctionOnMessageThread(
                [](void* ctx) -> void*
                {
                    static_cast<MilkDAWpAudioProcessorEditor*>(ctx)->shutdownGLOnMessageThread();
                    return nullptr;
                }, this);
        }

        destroyVisWindowOnMessageThread();
        stopTimer();
        MDW_LOG("UI", "Editor: timer stopped");
        return;
    }

    if (!glShutdownRequested.load())
    {
        startTimerHz(15);
        MDW_LOG("UI", "Editor: timer (re)started");
    }
}

void MilkDAWpAudioProcessorEditor::parentHierarchyChanged()
{
    // Called on message thread; detect removal from desktop (peer becomes null)
    MDW_LOG("UI", juce::String("Editor: parentHierarchyChanged, hasPeer=") + (getPeer() ? "yes" : "no"));
    if (getPeer() == nullptr)
    {
        shutdownGLOnMessageThread();
        destroyVisWindowOnMessageThread();
    }
}

// Ensure graceful shutdown before destruction, while Component peer still exists
void MilkDAWpAudioProcessorEditor::editorBeingDeleted()
{
    MDW_LOG("UI", "Editor: editorBeingDeleted");
    stopTimer();

    // Ensure both the embedded GL and any external window are torn down on UI thread
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        shutdownGLOnMessageThread();
        destroyVisWindowOnMessageThread();
    }
    else
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread(
            [](void* ctx) -> void*
            {
                auto* self = static_cast<MilkDAWpAudioProcessorEditor*>(ctx);
                self->shutdownGLOnMessageThread();
                self->destroyVisWindowOnMessageThread();
                return nullptr;
            }, this);
    }
}

MilkDAWpAudioProcessorEditor::~MilkDAWpAudioProcessorEditor()
{
    MDW_LOG("UI", "Editor: destructor");

    // Unregister APVTS listeners
    processor.apvts.removeParameterListener("showWindow", this);
    processor.apvts.removeParameterListener("fullscreen", this);
    processor.apvts.removeParameterListener("presetIndex", this);

    stopTimer();

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        shutdownGLOnMessageThread();
        destroyVisWindowOnMessageThread();
    }
    else
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread(
            [](void* ctx) -> void*
            {
                auto* self = static_cast<MilkDAWpAudioProcessorEditor*>(ctx);
                self->shutdownGLOnMessageThread();
                self->destroyVisWindowOnMessageThread();
                return nullptr;
            }, this);
    }
}

// ===== Add back the missing virtuals =====
void MilkDAWpAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    // Simple header text
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(16.0f);
    g.drawText("MilkDAWp", 10, 10, 200, 24, juce::Justification::left);

    // Meter text (if you wire it up later)
    g.setColour(juce::Colours::lightgrey);
    g.setFont(14.0f);
    g.drawText(meterLabel.getText(), meterLabel.getBounds().toFloat(), juce::Justification::centredLeft, true);
}

void MilkDAWpAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(8);

    // Top row: title + buttons
    auto top = r.removeFromTop(24);
    meterLabel.setBounds(top.removeFromLeft(160));
    top.removeFromLeft(8);
    btnShowWindow.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(8);
    btnFullscreen.setBounds(top.removeFromLeft(120));

    r.removeFromTop(6);

    // Sliders area
    auto sliders = r.removeFromTop(56);
    const int sliderWidth = 140;
    const int sliderHeight = 44;
    ampScale.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
    sliders.removeFromLeft(8);
    speed.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
    sliders.removeFromLeft(8);
    hue.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
    sliders.removeFromLeft(8);
    saturation.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
    sliders.removeFromLeft(8);
    seed.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));

    // second row reserved (currently unused)
    r.removeFromTop(4);

    r.removeFromTop(6);

    // Preset selector row (lazy)
    auto row = r.removeFromTop(26);
    presetLabel.setBounds(row.removeFromLeft(60));
    row.removeFromLeft(6);
    currentPresetLabel.setBounds(row.removeFromLeft(juce::jmax(140, row.getWidth() - 220)));
    row.removeFromLeft(6);
    btnLoadPreset.setBounds(row.removeFromLeft(140));
    row.removeFromLeft(6);
    btnClearPreset.setBounds(row.removeFromLeft(70));
}

void MilkDAWpAudioProcessorEditor::parameterChanged(const juce::String& paramID, float newValue)
{
    if (paramID == "showWindow")
    {
        const bool want = newValue > 0.5f;
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP, want]()
        {
            if (editorSP == nullptr) return;
            editorSP->handleShowWindowChangeOnUI(want);
        });
    }
    else if (paramID == "fullscreen")
    {
        const bool wantFS = newValue > 0.5f;
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP, wantFS]()
        {
            if (editorSP == nullptr) return;
            editorSP->handleFullscreenChangeOnUI(wantFS);
        });
    }
    else if (paramID == "presetIndex")
    {
        const int idx = (int) newValue; // APVTS passes raw value for AudioParameterInt
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP, idx]()
        {
            if (editorSP == nullptr) return;
            // Update UI selection if needed (avoid re-entrancy)
            if (editorSP->presetBox.getSelectedItemIndex() != idx)
                editorSP->presetBox.setSelectedItemIndex(idx, juce::dontSendNotification);
            if (editorSP->visWindow)
                editorSP->visWindow->setPresetIndex(idx);
        });
    }
}

// NEW: UI-thread logic for showWindow changes
void MilkDAWpAudioProcessorEditor::handleShowWindowChangeOnUI(bool wantWindow)
{
    MDW_LOG("UI", juce::String("Editor: handleShowWindowChangeOnUI -> ") + (wantWindow ? "true" : "false"));

    if (!isOnDesktop())
    {
        MDW_LOG("UI", "Editor: not on desktop; deferring to timer");
        return;
    }

    const float amp = processor.apvts.getRawParameterValue("ampScale")->load();
    const float spd = processor.apvts.getRawParameterValue("speed")->load();
    const float h = processor.apvts.getRawParameterValue("colorHue")->load();
    const float sat = processor.apvts.getRawParameterValue("colorSat")->load();
    const int sd = (int) processor.apvts.getRawParameterValue("seed")->load();
    const bool wantFullscreen = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;

    if (wantWindow)
    {
        if (!visWindow && !creationPending.exchange(true))
        {
            MDW_LOG("UI", "Editor: creating VisualizationWindow (event)");
            visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz());
            visWindow->setVisualParams(amp, spd);
            visWindow->setColorParams(h, sat);
            visWindow->setSeed(sd);
            visWindow->setFullScreenParam(wantFullscreen);
            // push current preset index to window
            int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
            visWindow->setPresetIndex(presetIdx);
            visWindow->toFront(true);

            juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
            visWindow->setOnUserClose([editorSP]()
            {
                if (editorSP == nullptr) return;
                MDW_LOG("UI", "Editor: onUserClose -> set showWindow = false");
                if (auto* p = dynamic_cast<juce::AudioParameterBool*>(editorSP->processor.apvts.getParameter("showWindow")))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(0.0f);
                    p->endChangeGesture();
                }
            });

            visWindow->setOnFullscreenChanged([editorSP](bool isFS)
            {
                if (editorSP == nullptr) return;
                MDW_LOG("UI", juce::String("Editor: onFullscreenChanged -> ") + (isFS ? "true" : "false"));
                if (auto* p = dynamic_cast<juce::AudioParameterBool*>(editorSP->processor.apvts.getParameter("fullscreen")))
                {
                    const float want = isFS ? 1.0f : 0.0f;
                    if (p->get() != (isFS))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(want);
                        p->endChangeGesture();
                    }
                }
            });

            creationPending.store(false);
        }

        if (visWindow && !visWindow->isVisible())
        {
            MDW_LOG("UI", "Editor: showing VisualizationWindow (event)");
            visWindow->setVisible(true);
            visWindow->toFront(true);
        }

        if (visWindow)
        {
            visWindow->setFullScreenParam(wantFullscreen);
            visWindow->setVisualParams(amp, spd);
            visWindow->setColorParams(h, sat);
            visWindow->setSeed(sd);
            int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
            visWindow->setPresetIndex(presetIdx);
        }
    }
    else
    {
        if (visWindow && visWindow->isVisible())
        {
            MDW_LOG("UI", "Editor: hiding VisualizationWindow (event)");
            visWindow->setVisible(false);
        }
    }
}

// NEW: UI-thread logic for fullscreen changes
void MilkDAWpAudioProcessorEditor::handleFullscreenChangeOnUI(bool wantFullscreen)
{
    if (visWindow)
        visWindow->setFullScreenParam(wantFullscreen);
}

// ===== timerCallback remains (visual param pushes and fallback pickup) =====
void MilkDAWpAudioProcessorEditor::timerCallback()
{
    const bool wantWindow = processor.apvts.getRawParameterValue("showWindow")->load() > 0.5f;
    const bool wantFullscreen = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;

    if (wantWindow != lastWantWindow)
    {
        MDW_LOG("UI", juce::String("Editor: showWindow param changed -> ") + (wantWindow ? "true" : "false"));
        lastWantWindow = wantWindow;
    }

    const float amp = processor.apvts.getRawParameterValue("ampScale")->load();
    const float spd = processor.apvts.getRawParameterValue("speed")->load();
    const float h = processor.apvts.getRawParameterValue("colorHue")->load();
    const float sat = processor.apvts.getRawParameterValue("colorSat")->load();
    const int sd = (int) processor.apvts.getRawParameterValue("seed")->load();

    // Removed: embedded GL param push

    if (!isOnDesktop())
    {
        if (!lastVisibleStateLogged)
        {
            MDW_LOG("UI", "Editor: timer blocked (not on desktop yet)");
            lastVisibleStateLogged = true;
        }
        return;
    }
    else if (lastVisibleStateLogged)
    {
        MDW_LOG("UI", "Editor: now on desktop; timer active");
        lastVisibleStateLogged = false;
    }

    // Keep external window visuals synced while open
    if (visWindow)
    {
        visWindow->setFullScreenParam(wantFullscreen);
        visWindow->setVisualParams(amp, spd);
        visWindow->setColorParams(h, sat);
        visWindow->setSeed(sd);
        int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
        visWindow->setPresetIndex(presetIdx);
    }

    // If host delivered param change while we weren’t on desktop, act on it here
    if (wantWindow && !visWindow && !creationPending.exchange(true))
    {
        MDW_LOG("UI", "Editor: creating VisualizationWindow (timer catch-up)");
        visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz());
        visWindow->setVisualParams(amp, spd);
        visWindow->setColorParams(h, sat);
        visWindow->setSeed(sd);
        visWindow->setFullScreenParam(wantFullscreen);
        {
            int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
            visWindow->setPresetIndex(presetIdx);
        }
        visWindow->toFront(true);

        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        visWindow->setOnUserClose([editorSP]()
        {
            if (editorSP == nullptr) return;
            MDW_LOG("UI", "Editor: onUserClose -> set showWindow = false");
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(editorSP->processor.apvts.getParameter("showWindow")))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(0.0f);
                p->endChangeGesture();
            }
        });

        creationPending.store(false);
    }
}

// ===== Helpers: preset scanning and param set =====
void MilkDAWpAudioProcessorEditor::populatePresetBox()
{
    presetBox.clear(juce::dontSendNotification);
    presetPaths.clear();

    // Resolve preset dir similarly to ProjectMRenderer, with dev fallbacks
    auto exe = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto bundleRoot = exe.getParentDirectory().getParentDirectory();
    juce::File presetsA = bundleRoot.getChildFile("Contents").getChildFile("Resources").getChildFile("presets");
    juce::File presetsB = bundleRoot.getChildFile("Resources").getChildFile("presets");
    juce::File chosen = presetsA.isDirectory() ? presetsA : (presetsB.isDirectory() ? presetsB : juce::File());

    if (! chosen.isDirectory())
    {
        auto dir = exe.getParentDirectory();
        for (int i = 0; i < 8 && ! chosen.isDirectory(); ++i)
        {
            auto test = dir.getChildFile("resources").getChildFile("presets");
            if (test.isDirectory()) { chosen = test; break; }
            dir = dir.getParentDirectory();
        }
    }
    if (! chosen.isDirectory())
    {
        auto cwd = juce::File::getCurrentWorkingDirectory().getChildFile("resources").getChildFile("presets");
        if (cwd.isDirectory()) chosen = cwd;
    }

    juce::StringArray names;
    if (chosen.isDirectory())
    {
        juce::Array<juce::File> found;
        chosen.findChildFiles(found, juce::File::findFiles, true, "*.milk");
        for (auto& f : found)
        {
            presetPaths.add(f.getFullPathName());
            names.add(f.getFileName());
        }
    }

    if (names.isEmpty())
    {
        presetBox.addItem("(no presets found)", 1);
        presetBox.setEnabled(false);
    }
    else
    {
        for (int i = 0; i < names.size(); ++i)
            presetBox.addItem(names[i], i + 1); // ComboBox IDs are 1-based
        presetBox.setEnabled(true);
        int idx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
        idx = juce::jlimit(0, names.size() - 1, idx);
        presetBox.setSelectedItemIndex(idx, juce::dontSendNotification);
    }
}

void MilkDAWpAudioProcessorEditor::setPresetParam(int newIndex)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(processor.apvts.getParameter("presetIndex")))
    {
        newIndex = juce::jlimit(0, 1023, newIndex); // must match Parameters.h
        const float norm = p->convertTo0to1(newIndex);
        p->beginChangeGesture();
        p->setValueNotifyingHost(norm);
        p->endChangeGesture();
    }
}
