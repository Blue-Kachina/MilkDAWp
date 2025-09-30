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
    setSize (1200, 600); // increased height to provide space for docked visualization without manual resize

    // Register APVTS listeners (react to param changes ASAP)
    processor.apvts.addParameterListener("showWindow", this);
    processor.apvts.addParameterListener("fullscreen", this);
    processor.apvts.addParameterListener("presetIndex", this);
    // Also listen to visual params so we can push immediate updates to the renderer
    processor.apvts.addParameterListener("ampScale", this);
    processor.apvts.addParameterListener("speed", this);
    processor.apvts.addParameterListener("colorHue", this);
    processor.apvts.addParameterListener("colorSat", this);
    processor.apvts.addParameterListener("seed", this);
    // Listen to full APVTS state changes (e.g., when the host restores state via setStateInformation)
    processor.apvts.state.addListener(this);

    meterLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (meterLabel);

    // Removed input/output gain controls per user request

    // Visual controls: Amplitude, Speed, Hue, Saturation, Seed
    addAndMakeVisible(ampScale);
    ampScale.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    ampScale.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    ampScale.setTextValueSuffix("");
    ampScale.setRange(0.0, 4.0, 0.001);
    ampScale.setTooltip("Amplitude (audio-reactive strength)");
    ampLabel.setJustificationType(juce::Justification::centred);
    ampLabel.attachToComponent(&ampScale, false);
    ampAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "ampScale", ampScale);

    addAndMakeVisible(speed);
    speed.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    speed.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    speed.setTextValueSuffix("x");
    speed.setRange(0.1, 3.0, 0.001);
    speed.setTooltip("FPS hint for projectM (approx 10-180), derived from this speed factor");
    speedLabel.setJustificationType(juce::Justification::centred);
    speedLabel.attachToComponent(&speed, false);
    speedAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "speed", speed);

    addAndMakeVisible(hue);
    hue.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    hue.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    hue.setTextValueSuffix("");
    hue.setRange(0.0, 1.0, 0.0001);
    hue.setTooltip("Beat detection sensitivity (maps to projectM beat_sensitivity)");
    hueLabel.setJustificationType(juce::Justification::centred);
    hueLabel.attachToComponent(&hue, false);
    hueAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "colorHue", hue);

    addAndMakeVisible(saturation);
    saturation.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    saturation.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    saturation.setTextValueSuffix("");
    saturation.setRange(0.0, 1.0, 0.0001);
    saturation.setTooltip("Per-pixel mesh size (maps to projectM mesh size; even 16..160)");
    satLabel.setJustificationType(juce::Justification::centred);
    satLabel.attachToComponent(&saturation, false);
    satAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "colorSat", saturation);

    addAndMakeVisible(seed);
    seed.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    seed.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    seed.setTextValueSuffix("");
    seed.setRange(0.0, 1000000.0, 1.0);
    seed.setTooltip("Easter egg factor (maps to projectM easter_egg)");
    seedLabel.setJustificationType(juce::Justification::centred);
    seedLabel.attachToComponent(&seed, false);
    seedAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "seed", seed);

    addAndMakeVisible(btnShowWindow);
    addAndMakeVisible(btnFullscreen);
    
    // Style the fullscreen toggle as an icon button
    btnFullscreen.setTooltip("Toggle Fullscreen");
    btnFullscreen.setClickingTogglesState(true);
    {
        auto makeIcon = [](juce::Colour c) {
            auto dp = std::make_unique<juce::DrawablePath>();
            juce::Path p;
            const float s = 24.0f; // icon square
            const float m = 4.0f;  // margin
            const float a = m + 3.0f;
            const float b = s - m - 3.0f;
            const float head = 6.0f; // arrowhead size
            // Diagonal double-arrow: ↘ and ↖ along the same diagonal
            // Shaft
            p.startNewSubPath(a, a); p.lineTo(b, b);
            // Arrowhead for ↘ at (b,b)
            p.startNewSubPath(b, b); p.lineTo(b, b - head);
            p.startNewSubPath(b, b); p.lineTo(b - head, b);
            // Arrowhead for ↖ at (a,a)
            p.startNewSubPath(a, a); p.lineTo(a, a + head);
            p.startNewSubPath(a, a); p.lineTo(a + head, a);
            dp->setPath(p);
            dp->setFill(juce::Colours::transparentBlack);
            dp->setStrokeFill(c);
            dp->setStrokeThickness(2.0f);
            return dp;
        };
        auto normal   = makeIcon(juce::Colours::lightgrey);
        auto over     = makeIcon(juce::Colours::white);
        auto down     = makeIcon(juce::Colours::orange);
        auto normalOn = makeIcon(juce::Colours::aqua);
        auto overOn   = makeIcon(juce::Colours::cyan);
        auto downOn   = makeIcon(juce::Colours::skyblue);
        btnFullscreen.setImages(normal.get(), over.get(), down.get(), nullptr,
                                normalOn.get(), overOn.get(), downOn.get(), nullptr);
        // drawables will be cloned internally by setImages; locals can be discarded
    }

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
    currentPresetLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    currentPresetLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    currentPresetLabel.setColour(juce::Label::backgroundColourId, juce::Colours::darkgrey.darker(0.4f));

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
                    lastPresetPath = f.getFullPathName();
                    // Persist selected preset path in processor state so it survives editor close and host preset save
                    processor.apvts.state.setProperty("presetPath", lastPresetPath, nullptr);
                    currentPresetLabel.setText(f.getFileName(), juce::dontSendNotification);
                    btnLoadPreset.setTooltip(f.getFullPathName());
                    if (visWindow)
                        visWindow->loadPresetByPath(lastPresetPath, true);
                }
            });
    };

    btnClearPreset.onClick = [this]()
    {
        currentPresetLabel.setText("(none)", juce::dontSendNotification);
        btnLoadPreset.setTooltip("Select a projectM preset (.milk)");
        lastPresetPath.clear();
        // Persist cleared state
        processor.apvts.state.setProperty("presetPath", "", nullptr);
        if (visWindow)
            visWindow->loadPresetByPath("idle://", true);
        // Also clear the parameterized index to -1 equivalent (0 in APVTS range), to avoid unintended switches
        setPresetParam(0);
    };

    // Keep legacy box populated with a cheap placeholder (no scanning)
    populatePresetBox();

    // Restore last selected preset path from processor state (persists across editor reopen and in host presets)
    refreshPresetPathFromState();

    // New: log only. The ButtonAttachment handles syncing this toggle to the APVTS.
    btnShowWindow.onClick = [this]()
    {
        const bool want = btnShowWindow.getToggleState();
        MDW_LOG("UI", juce::String("Editor: btnShowWindow.onClick -> ") + (want ? "true" : "false"));
        // No manual APVTS write here to avoid redundant/looped updates with the ButtonAttachment
    };

    // Removed: embedded GL view creation and initial param push

    // Begin polling params to control external visualization
    startTimerHz(15);
    MDW_LOG("UI", "Editor: timer started");
}

// ===== ValueTree listener: react to state restoration (presetPath) =====
void MilkDAWpAudioProcessorEditor::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    juce::ignoreUnused(treeWhosePropertyHasChanged);
    if (property.toString() == "presetPath")
    {
        MDW_LOG("UI", "Editor: state property changed -> presetPath");
        refreshPresetPathFromState();
    }
}

void MilkDAWpAudioProcessorEditor::valueTreeRedirected(juce::ValueTree& treeWhichHasBeenChanged)
{
    juce::ignoreUnused(treeWhichHasBeenChanged);
    MDW_LOG("UI", "Editor: state redirected (likely setStateInformation); refreshing presetPath");
    refreshPresetPathFromState();
}

void MilkDAWpAudioProcessorEditor::refreshPresetPathFromState()
{
    auto v = processor.apvts.state.getProperty("presetPath");
    juce::String saved = v.isString() ? v.toString() : juce::String();
    if (saved == lastPresetPath)
        return;

    lastPresetPath = saved;
    if (lastPresetPath.isNotEmpty())
    {
        auto name = juce::File(lastPresetPath).getFileName();
        currentPresetLabel.setText(name, juce::dontSendNotification);
        btnLoadPreset.setTooltip(lastPresetPath);
    }
    else
    {
        currentPresetLabel.setText("(none)", juce::dontSendNotification);
        btnLoadPreset.setTooltip("Select a projectM preset (.milk)");
    }

    if (visWindow)
    {
        if (lastPresetPath.isNotEmpty())
            visWindow->loadPresetByPath(lastPresetPath, true);
        else
            visWindow->loadPresetByPath("idle://", true);
    }
    else
    {
        // If a preset was restored and the user wants the window, create/show it now
        const bool want = processor.apvts.getRawParameterValue("showWindow")->load() > 0.5f;
        if (want)
            handleShowWindowChangeOnUI(true);
    }
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

    // We are visible again; clear any prior shutdown gate and (re)start the timer
    glShutdownRequested.store(false);
    startTimerHz(15);
    MDW_LOG("UI", "Editor: timer (re)started");
    // Auto-start visualization if requested
    if (getPeer() != nullptr)
    {
        const bool want = processor.apvts.getRawParameterValue("showWindow")->load() > 0.5f;
        if (want && !visWindow)
            handleShowWindowChangeOnUI(true);
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
    else
    {
        // We just got a peer: clear shutdown gate, ensure our timer is running and auto-create the vis window if requested
        glShutdownRequested.store(false);
        startTimerHz(15);
        MDW_LOG("UI", "Editor: timer (re)started (parentHierarchyChanged)");
        const bool want = processor.apvts.getRawParameterValue("showWindow")->load() > 0.5f;
        if (want && !visWindow)
            handleShowWindowChangeOnUI(true);
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
    processor.apvts.removeParameterListener("ampScale", this);
    processor.apvts.removeParameterListener("speed", this);
    processor.apvts.removeParameterListener("colorHue", this);
    processor.apvts.removeParameterListener("colorSat", this);
    processor.apvts.removeParameterListener("seed", this);
    processor.apvts.state.removeListener(this);

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
    auto r = getLocalBounds().reduced(10); // a bit more padding overall

    // Top row: title + buttons
    auto top = r.removeFromTop(28);
    // Right-aligned fullscreen button area
    auto fsArea = top.removeFromRight(32);
    fsArea = fsArea.withSize(28, 24).withY(fsArea.getY() + juce::jmax(0, (top.getHeight() - 24) / 2));
    btnFullscreen.setBounds(fsArea);
    // Left controls
    meterLabel.setBounds(top.removeFromLeft(180));
    top.removeFromLeft(10);
    btnShowWindow.setBounds(top.removeFromLeft(160));

    // Padding between header and preset row
    r.removeFromTop(12);

    // Preset selector row (lazy)
    auto row = r.removeFromTop(28);
    presetLabel.setBounds(row.removeFromLeft(60));
    row.removeFromLeft(6);
    currentPresetLabel.setBounds(row.removeFromLeft(juce::jmax(180, row.getWidth() - 250)));
    row.removeFromLeft(6);
    btnLoadPreset.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(6);
    btnClearPreset.setBounds(row.removeFromLeft(80));

    // Extra breathing room before knobs (more padding per request)
    r.removeFromTop(28);

    // Knob area (centered horizontally)
    auto knobs = r.removeFromTop(110);
    const int knobW = 120;
    const int knobH = 100;
    const int gap = 8;
    const int count = 5;
    const int totalW = count * knobW + (count - 1) * gap;
    const int startX = knobs.getX() + juce::jmax(0, (knobs.getWidth() - totalW) / 2);
    const int y = knobs.getY();
    auto place = [&](juce::Component& c, int index) {
        c.setBounds(startX + index * (knobW + gap), y, knobW, knobH);
    };
    place(ampScale, 0);
    place(speed, 1);
    place(hue, 2);
    place(saturation, 3);
    place(seed, 4);

    // If visualization window is docked, occupy the remaining area
    if (visWindow && visWindow->isDocked())
    {
        // 'r' now represents remaining area under the controls
        auto visAreaLocal = r; // use remaining area as-is to avoid pushing bounds outside parent
        // If embedded as a child, set local bounds; if top-level, convert to screen
        if (visWindow->isOnDesktop())
        {
            auto visAreaScreen = localAreaToGlobal(visAreaLocal);
            visWindow->setBounds(visAreaScreen);
        }
        else
        {
            visWindow->setBounds(visAreaLocal);
        }
        // Ensure visibility and Z-order of both the container and its content
        if (!visWindow->isVisible())
            visWindow->setVisible(true);
        if (auto* content = visWindow->getContentComponent())
        {
            if (!content->isVisible())
                content->setVisible(true);
            content->toFront(false);
            content->repaint();
        }
        visWindow->toFront(false);
        visWindow->repaint();
        MDW_LOG("UI", juce::String("Editor.resized: docked vis bounds=") + visWindow->getBounds().toString() +
            ", areaLeft=" + r.toString() +
            ", visVisible=" + juce::String(visWindow->isVisible() ? 1 : 0) +
            ", contentVisible=" + juce::String(visWindow->getContentComponent() && visWindow->getContentComponent()->isVisible() ? 1 : 0));
    }
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

            // Auto-enable Show Window when a preset is selected (manual or via host preset manager)
            if (auto* pShow = dynamic_cast<juce::AudioParameterBool*>(editorSP->processor.apvts.getParameter("showWindow")))
            {
                if (pShow->get() == false)
                {
                    pShow->beginChangeGesture();
                    pShow->setValueNotifyingHost(1.0f);
                    pShow->endChangeGesture();
                }
            }
        });
    }
    else if (paramID == "ampScale" || paramID == "speed" || paramID == "colorHue" || paramID == "colorSat" || paramID == "seed")
    {
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP]()
        {
            if (editorSP == nullptr) return;
            if (editorSP->visWindow)
            {
                const float amp = editorSP->processor.apvts.getRawParameterValue("ampScale")->load();
                const float spd = editorSP->processor.apvts.getRawParameterValue("speed")->load();
                const float h   = editorSP->processor.apvts.getRawParameterValue("colorHue")->load();
                const float sat = editorSP->processor.apvts.getRawParameterValue("colorSat")->load();
                const int sd    = (int) editorSP->processor.apvts.getRawParameterValue("seed")->load();
                editorSP->visWindow->setVisualParams(amp, spd);
                editorSP->visWindow->setColorParams(h, sat);
                editorSP->visWindow->setSeed(sd);
            }
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

    // If host has suspended us (e.g., Cubase deactivated effect), do not create/show
    if (processor.isSuspended())
    {
        if (visWindow && visWindow->isVisible())
            visWindow->setVisible(false);
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
            const int initIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
            visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz(), lastPresetPath, initIdx);
            // Dock to the main editor by default (embedded)
            visWindow->dockTo(this);
            // Ensure it is visible immediately (don't rely solely on addAndMakeVisible)
            visWindow->setVisible(true);
            if (auto* content = visWindow->getContentComponent())
            {
                content->setVisible(true);
                content->toFront(false);
            }
            // Lay out immediately so the GL canvas is visible without requiring a resize
            this->resized();
            visWindow->setVisualParams(amp, spd);
            visWindow->setColorParams(h, sat);
            visWindow->setSeed(sd);
            // Apply fullscreen if requested via the proper path (will undock if needed)
            if (wantFullscreen)
                handleFullscreenChangeOnUI(true);
            // Apply preset: prefer a manually chosen path, otherwise use the automatable preset index
            if (! lastPresetPath.isEmpty())
                visWindow->loadPresetByPath(lastPresetPath, true);
            else
            {
                int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
                visWindow->setPresetIndex(presetIdx);
            }
            visWindow->toFront(true);
            visWindow->repaint();

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

            // Ensure layout stabilizes after the current message; prevents zero-sized dock area
            juce::Timer::callAfterDelay(0, [editorSP]()
            {
                if (editorSP != nullptr)
                    editorSP->resized();
            });

            creationPending.store(false);
        }

        if (visWindow && !visWindow->isVisible())
        {
            MDW_LOG("UI", "Editor: showing VisualizationWindow (event)");
            visWindow->setVisible(true);
            if (auto* content = visWindow->getContentComponent())
            {
                content->setVisible(true);
                content->toFront(false);
            }
            // Ensure the docked window gets immediate bounds without requiring manual resize
            this->resized();
            visWindow->toFront(true);
            visWindow->repaint();
            // Reapply preset on show to handle GL context resets
            if (! lastPresetPath.isEmpty())
                visWindow->loadPresetByPath(lastPresetPath, true);
            else
            {
                int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
                visWindow->setPresetIndex(presetIdx);
            }
        }

        if (visWindow)
        {
            if (wantFullscreen && visWindow->isDocked())
                handleFullscreenChangeOnUI(true);
            else
                visWindow->setFullScreenParam(wantFullscreen);
            visWindow->setVisualParams(amp, spd);
            visWindow->setColorParams(h, sat);
            visWindow->setSeed(sd);
            if (lastPresetPath.isEmpty())
            {
                int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
                visWindow->setPresetIndex(presetIdx);
            }
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
    if (!visWindow)
        return;

    if (wantFullscreen)
    {
        // Ensure the visualization is undocked before entering fullscreen
        wasDockedBeforeFullscreen = visWindow->isDocked();
        if (wasDockedBeforeFullscreen)
        {
            visWindow->undock();
        }
        visWindow->setFullScreenParam(true);
    }
    else
    {
        // Exit fullscreen first
        visWindow->setFullScreenParam(false);
        // Redock if we had been docked before entering fullscreen
        if (wasDockedBeforeFullscreen)
        {
            visWindow->dockTo(this);
            // Lay out immediately to restore docked bounds
            this->resized();
        }
    }

    // Reapply the preset after fullscreen toggles, because some hosts/drivers recreate the GL context
    if (! lastPresetPath.isEmpty())
    {
        visWindow->loadPresetByPath(lastPresetPath, true);
    }
    else
    {
        int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
        visWindow->setPresetIndex(presetIdx);
    }
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

    // If host suspended us (e.g., Cubase disabled the plugin), hide external window and sync checkbox
    if (processor.isSuspended())
    {
        if (visWindow && visWindow->isVisible())
            visWindow->setVisible(false);
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(processor.apvts.getParameter("showWindow")))
        {
            if (p->get())
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(0.0f);
                p->endChangeGesture();
            }
        }
        // Don’t try to create/update the window while suspended
        return;
    }

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
        if (lastPresetPath.isEmpty())
        {
            int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
            visWindow->setPresetIndex(presetIdx);
        }
        // Safety: if docked but not visible or zero-sized, force layout and visibility
        if (visWindow->isDocked())
        {
            auto b = visWindow->getBounds();
            if (!visWindow->isVisible() || b.isEmpty())
            {
                visWindow->setVisible(true);
                this->resized();
            }
        }
    }

    // If host delivered param change while we weren’t on desktop, act on it here
    if (wantWindow && !visWindow && !creationPending.exchange(true))
    {
        MDW_LOG("UI", "Editor: creating VisualizationWindow (timer catch-up)");
        const int initIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
        visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz(), lastPresetPath, initIdx);
        // Dock to the main editor by default (embedded)
        visWindow->dockTo(this);
        // Lay out immediately so the GL canvas is visible without requiring a resize
        this->resized();
        visWindow->setVisualParams(amp, spd);
        visWindow->setColorParams(h, sat);
        visWindow->setSeed(sd);
        // Apply fullscreen if requested via the proper path (will undock if needed)
        if (wantFullscreen)
            handleFullscreenChangeOnUI(true);
        // Apply preset: prefer a manually chosen path, otherwise use the automatable preset index
        if (! lastPresetPath.isEmpty())
            visWindow->loadPresetByPath(lastPresetPath, true);
        else
        {
            int presetIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
            visWindow->setPresetIndex(presetIdx);
        }
        // Ensure the newly created window is actually shown (it starts hidden in ctor)
        visWindow->setVisible(true);
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
