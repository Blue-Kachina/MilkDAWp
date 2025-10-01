#include "PluginEditor.h"
#include "../processor/PluginProcessor.h"
#include "../renderers/ProjectMRenderer.h"
#include "../utils/Logging.h"
#include "VisualizationWindow.h"

namespace Icons {
    // Helper: create a Drawable from an inline SVG string and tint it to the given colour
    static std::unique_ptr<juce::Drawable> createFromSvgString(const juce::String& svg, juce::Colour tint)
    {
        juce::XmlDocument doc(svg);
        auto xml = doc.getDocumentElement();
        if (!xml)
            return {};
        auto drawable = juce::Drawable::createFromSVG(*xml);
        if (!drawable)
            return {};
        // The bundled SVG paths use black fills; replace them with the requested tint
        drawable->replaceColour(juce::Colours::black, tint);
        // Also normalize any white to tint (in case of stroke usage)
        drawable->replaceColour(juce::Colours::white, tint);
        return drawable;
    }

    // Material Design icon: "fullscreen" (Outlined), 24px grid, Apache-2.0
    static std::unique_ptr<juce::Drawable> createFullscreenIcon(juce::Colour colour)
    {
        static const char* svg =
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
            "<path fill='#000000' d='M7 14H5v5h5v-2H7v-3zm0-9H5v5h2V7h3V5H7zm10 0h-5v2h3v3h2V5zm-5 14h5v-5h-2v3h-3v2z'/>"
            "</svg>";
        return createFromSvgString(svg, colour);
    }

    // Material Design icon: "open_in_new" (Outlined), 24px grid, Apache-2.0
    static std::unique_ptr<juce::Drawable> createPopOutIcon(juce::Colour colour)
    {
        static const char* svg =
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
            "<path fill='#000000' d='M14 3h7v7h-2V6.41l-9.29 9.3-1.42-1.42L17.59 5H14V3z'/>"
            "<path fill='#000000' d='M5 5h5V3H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2v-5h-2v5H5V5z'/>"
            "</svg>";
        return createFromSvgString(svg, colour);
    }

    // Simple chevrons for prev/next
    static std::unique_ptr<juce::Drawable> createChevronLeft(juce::Colour colour)
    {
        static const char* svg =
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
            "<path fill='#000000' d='M15.41 7.41 14 6l-6 6 6 6 1.41-1.41L10.83 12z'/>"
            "</svg>";
        return createFromSvgString(svg, colour);
    }

    static std::unique_ptr<juce::Drawable> createChevronRight(juce::Colour colour)
    {
        static const char* svg =
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
            "<path fill='#000000' d='m8.59 16.59 1.41 1.41 6-6-6-6-1.41 1.41L13.17 12z'/>"
            "</svg>";
        return createFromSvgString(svg, colour);
    }

    static void setIconStates(juce::DrawableButton& btn,
                              std::unique_ptr<juce::Drawable> normal,
                              std::unique_ptr<juce::Drawable> over,
                              std::unique_ptr<juce::Drawable> down,
                              std::unique_ptr<juce::Drawable> normalOn,
                              std::unique_ptr<juce::Drawable> overOn,
                              std::unique_ptr<juce::Drawable> downOn) {
        btn.setImages(normal.get(), over.get(), down.get(), nullptr,
                      normalOn.get(), overOn.get(), downOn.get(), nullptr);
    }
}

// ===== EditorGLComponent (embedded GL) =====
// Removed: embedded OpenGL preview inside the editor

// ===== Editor =====

MilkDAWpAudioProcessorEditor::MilkDAWpAudioProcessorEditor (MilkDAWpAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    MDW_LOG("UI", "Editor: constructed");
    setResizable(true, true);
    setSize (1200, 650); // increased height to provide space for docked visualization without manual resize (+50 per request)

    // Register APVTS listeners (react to param changes ASAP)
    processor.apvts.addParameterListener("fullscreen", this);
    processor.apvts.addParameterListener("presetIndex", this);
    processor.apvts.addParameterListener("playlistPresetIndex", this);
    processor.apvts.addParameterListener("playlistPrev", this);
    processor.apvts.addParameterListener("playlistNext", this);
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

    addAndMakeVisible(btnFullscreen);
    addAndMakeVisible(btnPopOut);
    
    // Style the fullscreen toggle using the Icons library (four-corner fullscreen glyph)
    btnFullscreen.setTooltip("Toggle fullscreen");
    btnFullscreen.setClickingTogglesState(true);
    {
        auto normal   = Icons::createFullscreenIcon(juce::Colours::lightgrey);
        auto over     = Icons::createFullscreenIcon(juce::Colours::white);
        auto down     = Icons::createFullscreenIcon(juce::Colours::orange);
        auto normalOn = Icons::createFullscreenIcon(juce::Colours::aqua);
        auto overOn   = Icons::createFullscreenIcon(juce::Colours::cyan);
        auto downOn   = Icons::createFullscreenIcon(juce::Colours::skyblue);
        Icons::setIconStates(btnFullscreen, std::move(normal), std::move(over), std::move(down),
                             std::move(normalOn), std::move(overOn), std::move(downOn));
    }

    // Style the Pop Out toggle using the Icons library (window with pop-out arrow)
    btnPopOut.setTooltip("Pop out visualization to a separate window");
    btnPopOut.setClickingTogglesState(true);
    {
        auto normal   = Icons::createPopOutIcon(juce::Colours::lightgrey);
        auto over     = Icons::createPopOutIcon(juce::Colours::white);
        auto down     = Icons::createPopOutIcon(juce::Colours::orange);
        auto normalOn = Icons::createPopOutIcon(juce::Colours::aqua);
        auto overOn   = Icons::createPopOutIcon(juce::Colours::cyan);
        auto downOn   = Icons::createPopOutIcon(juce::Colours::skyblue);
        Icons::setIconStates(btnPopOut, std::move(normal), std::move(over), std::move(down),
                             std::move(normalOn), std::move(overOn), std::move(downOn));
    }

    // Attach fullscreen to APVTS param
    fullAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "fullscreen", btnFullscreen);

    // Pop-out click handler: dock/undock the vis window
    btnPopOut.onClick = [this]()
    {
        // While in fullscreen, allow disabling Pop Out by exiting fullscreen and docking back
        const bool isFS = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;
        if (isFS)
        {
            const bool wantPopOut = btnPopOut.getToggleState();
            if (!wantPopOut)
            {
                // User wants to pop back in while fullscreen: exit fullscreen, dock, and keep Pop Out off
                popOutDesired = false;
                if (auto* p = dynamic_cast<juce::AudioParameterBool*>(processor.apvts.getParameter("fullscreen")))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(0.0f);
                    p->endChangeGesture();
                }
                if (!visWindow)
                    handleShowWindowChangeOnUI(true);
                if (visWindow && !visWindow->isDocked())
                {
                    visWindow->dockTo(this);
                    this->resized();
                }
                if (visWindow)
                {
                    visWindow->setVisible(true);
                    visWindow->toFront(false);
                }
                return;
            }
            // If still wanting Pop Out while fullscreen, ensure undocked and visible
            popOutDesired = true;
            if (!visWindow)
                handleShowWindowChangeOnUI(true);
            if (visWindow)
            {
                if (visWindow->isDocked())
                    visWindow->undock();
                visWindow->setVisible(true);
                visWindow->toFront(true);
            }
            return;
        }

        popOutDesired = btnPopOut.getToggleState();
        // Ensure we have a visualization window
        if (!visWindow)
        {
            handleShowWindowChangeOnUI(true);
        }
        if (visWindow)
        {
            if (popOutDesired)
            {
                if (visWindow->isDocked())
                    visWindow->undock();
                visWindow->setVisible(true);
                visWindow->toFront(true);
            }
            else
            {
                // Only dock back if not in fullscreen (redundant here since !isFS)
                if (!visWindow->isDocked())
                {
                    visWindow->dockTo(this);
                    this->resized();
                }
            }
        }
    };

    // Preset selector UI
    presetLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(presetLabel);
    addAndMakeVisible(presetBox);
    presetBox.setVisible(false); // used only as playlist dropdown
    presetBox.onChange = [this]() {
        const int idx = presetBox.getSelectedItemIndex();
        if (playlistActive && idx >= 0 && idx < playlistItems.size())
        {
            // Drive via parameter so it’s automatable/MIDI-mappable
            setPlaylistIndexParam(idx);
            // State persistence
            processor.apvts.state.setProperty("playlistIndex", idx, nullptr);
            auto path = playlistItems[(int) idx].getFullPathName();
            lastPresetPath = path;
            processor.apvts.state.setProperty("presetPath", lastPresetPath, nullptr);
            currentPresetLabel.setText(playlistItems[(int) idx].getFileName(), juce::dontSendNotification);
            if (visWindow)
                visWindow->loadPresetByPath(lastPresetPath, true);
        }
        else
        {
            setPresetParam(idx);
            if (visWindow)
                visWindow->setPresetIndex(idx);
        }
    };

    // New: lazy preset loading controls
    addAndMakeVisible(btnLoadPreset);
    addAndMakeVisible(btnClearPreset);
    addAndMakeVisible(currentPresetLabel);
    currentPresetLabel.setJustificationType(juce::Justification::centredLeft);
    currentPresetLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    currentPresetLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    currentPresetLabel.setColour(juce::Label::backgroundColourId, juce::Colours::darkgrey.darker(0.4f));

    // Transport buttons (prev/next) for playlist mode
    addAndMakeVisible(btnPrev);
    addAndMakeVisible(btnNext);
    btnPrev.setVisible(false);
    btnNext.setVisible(false);
    btnPrev.setTooltip("Previous preset in playlist");
    btnNext.setTooltip("Next preset in playlist");
    {
        auto normal   = Icons::createChevronLeft(juce::Colours::lightgrey);
        auto over     = Icons::createChevronLeft(juce::Colours::white);
        auto down     = Icons::createChevronLeft(juce::Colours::orange);
        auto normalOn = Icons::createChevronLeft(juce::Colours::aqua);
        auto overOn   = Icons::createChevronLeft(juce::Colours::cyan);
        auto downOn   = Icons::createChevronLeft(juce::Colours::skyblue);
        Icons::setIconStates(btnPrev, std::move(normal), std::move(over), std::move(down), std::move(normalOn), std::move(overOn), std::move(downOn));
    }
    {
        auto normal   = Icons::createChevronRight(juce::Colours::lightgrey);
        auto over     = Icons::createChevronRight(juce::Colours::white);
        auto down     = Icons::createChevronRight(juce::Colours::orange);
        auto normalOn = Icons::createChevronRight(juce::Colours::aqua);
        auto overOn   = Icons::createChevronRight(juce::Colours::cyan);
        auto downOn   = Icons::createChevronRight(juce::Colours::skyblue);
        Icons::setIconStates(btnNext, std::move(normal), std::move(over), std::move(down), std::move(normalOn), std::move(overOn), std::move(downOn));
    }

    // Bind prev/next buttons to parameters so they’re MIDI/automation mappable
    prevAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "playlistPrev", btnPrev);
    nextAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "playlistNext", btnNext);

    btnPrev.onClick = [this]() {
        if (!playlistActive || playlistItems.isEmpty()) return;
        const int size = playlistItems.size();
        const int cur  = juce::jlimit(0, size - 1, playlistIndex < 0 ? 0 : playlistIndex);
        const int next = (cur - 1 + size) % size;
        setPlaylistIndexParam(next);
    };
    btnNext.onClick = [this]() {
        if (!playlistActive || playlistItems.isEmpty()) return;
        const int size = playlistItems.size();
        const int cur  = juce::jlimit(0, size - 1, playlistIndex < 0 ? 0 : playlistIndex);
        const int next = (cur + 1) % size;
        setPlaylistIndexParam(next);
    };

    btnLoadPreset.onClick = [this]()
    {
        // Resolve presets root directory (same heuristics as before)
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

        auto chooser = std::make_shared<juce::FileChooser>("Load preset (.milk) or playlist folder", initialDir, "*.milk");
        int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories;
        chooser->launchAsync(flags,
            [this, chooser](const juce::FileChooser& fc) mutable
            {
                juce::File f = fc.getResult();
                if (! f.exists()) return;

                if (f.isDirectory())
                {
                    // Treat directory as a playlist: scan for .milk (non-recursive)
                    playlistItems.clear();
                    playlistRoot = f;
                    juce::DirectoryIterator it(f, false, "*.milk", juce::File::findFiles);
                    while (it.next())
                        playlistItems.add(it.getFile());

                    struct FileNameLess { int compareElements(const juce::File& a, const juce::File& b) const { return a.getFileName().toLowerCase().compare(b.getFileName().toLowerCase()); } } cmp;
                    playlistItems.sort(cmp);

                    playlistActive = playlistItems.size() > 0;
                    if (playlistActive)
                    {
                        // Persist playlist activation and root
                        processor.apvts.state.setProperty("playlistActive", true, nullptr);
                        processor.apvts.state.setProperty("playlistRootPath", playlistRoot.getFullPathName(), nullptr);
                        // Populate dropdown with playlist items
                        presetBox.clear(juce::dontSendNotification);
                        for (int i = 0; i < playlistItems.size(); ++i)
                            presetBox.addItem(playlistItems.getReference(i).getFileName(), i + 1);
                        presetBox.setSelectedItemIndex(0, juce::dontSendNotification);
                        presetBox.setVisible(true);
                        currentPresetLabel.setVisible(false);
                        // Show transport
                        btnPrev.setVisible(true);
                        btnNext.setVisible(true);
                        // Load first preset
                        playlistIndex = 0;
                        processor.apvts.state.setProperty("playlistIndex", 0, nullptr);
                        lastPresetPath = playlistItems[0].getFullPathName();
                        processor.apvts.state.setProperty("presetPath", lastPresetPath, nullptr);
                        currentPresetLabel.setText(playlistItems[0].getFileName(), juce::dontSendNotification);
                        btnLoadPreset.setTooltip(playlistRoot.getFullPathName());
                        if (visWindow)
                            visWindow->loadPresetByPath(lastPresetPath, true);
                        // Re-layout now that controls changed visibility
                        this->resized();
                        // Drive parameter so hosts/MIDI reflect current index
                        setPlaylistIndexParam(0);
                    }
                    else
                    {
                        // Empty folder: deactivate playlist UI
                        playlistActive = false;
                        presetBox.setVisible(false);
                        currentPresetLabel.setVisible(true);
                        btnPrev.setVisible(false);
                        btnNext.setVisible(false);
                        // Re-layout now that controls changed visibility
                        this->resized();
                    }
                }
                else if (f.existsAsFile())
                {
                    // Single preset
                    playlistActive = false;
                    playlistItems.clear();
                    playlistIndex = -1;
                    presetBox.setVisible(false);
                    currentPresetLabel.setVisible(true);
                    btnPrev.setVisible(false);
                    btnNext.setVisible(false);

                    // Persist playlist deactivation
                    processor.apvts.state.setProperty("playlistActive", false, nullptr);
                    processor.apvts.state.setProperty("playlistRootPath", "", nullptr);
                    processor.apvts.state.setProperty("playlistIndex", -1, nullptr);

                    lastPresetPath = f.getFullPathName();
                    processor.apvts.state.setProperty("presetPath", lastPresetPath, nullptr);
                    currentPresetLabel.setText(f.getFileName(), juce::dontSendNotification);
                    btnLoadPreset.setTooltip(f.getFullPathName());
                    if (visWindow)
                        visWindow->loadPresetByPath(lastPresetPath, true);
                    // Re-layout now that controls changed visibility
                    this->resized();
                }
            });
    };

    btnClearPreset.onClick = [this]()
    {
        // Deactivate playlist mode and clear current preset
        playlistActive = false;
        playlistItems.clear();
        playlistIndex = -1;
        presetBox.clear(juce::dontSendNotification);
        presetBox.setVisible(false);
        btnPrev.setVisible(false);
        btnNext.setVisible(false);

        currentPresetLabel.setVisible(true);
        currentPresetLabel.setText("(none)", juce::dontSendNotification);
        btnLoadPreset.setTooltip("Load preset (.milk) or playlist folder");
        lastPresetPath.clear();
        // Persist cleared state
        processor.apvts.state.setProperty("presetPath", "", nullptr);
        processor.apvts.state.setProperty("playlistActive", false, nullptr);
        processor.apvts.state.setProperty("playlistRootPath", "", nullptr);
        processor.apvts.state.setProperty("playlistIndex", -1, nullptr);
        if (visWindow)
            visWindow->loadPresetByPath("idle://", true);
        // Also clear the parameterized indices to safe defaults
        setPresetParam(0);
        setPlaylistIndexParam(0);
        // Re-layout now that controls changed visibility
        this->resized();
    };

    // Keep legacy box populated with a cheap placeholder (no scanning)
    populatePresetBox();

    // Restore last selected preset path from processor state (persists across editor reopen and in host presets)
    refreshPresetPathFromState();

    // Removed: embedded GL view creation and initial param push

    // Load logo image from embedded BinaryData
    {
        int dataSize = 0;
        // JUCE BinaryData sanitizes names: spaces become underscores; dots become underscores
        const char* resourceNamePrimary = "MilkDAWp_Logo_Transparent_png"; // exact match for "MilkDAWp Logo Transparent.png"
        const char* resourceNameFallback = "MilkDAWpLogoTransparent_png";   // legacy guess used previously
        const void* data = BinaryData::getNamedResource(resourceNamePrimary, dataSize);
        if (data == nullptr)
            data = BinaryData::getNamedResource(resourceNameFallback, dataSize);
        if (data != nullptr)
        {
            juce::MemoryInputStream mis(data, static_cast<size_t>(dataSize), false);
            auto img = juce::ImageFileFormat::loadFrom(mis);
            if (img.isValid())
            {
                logoImage = img;
                MDW_LOG("UI", juce::String("Logo loaded: ") + juce::String(logoImage.getWidth()) + "x" + juce::String(logoImage.getHeight()));
            }
            else
            {
                MDW_LOG("UI", "Failed to decode embedded logo image");
            }
        }
        else
        {
            MDW_LOG("UI", "Embedded logo image not found (tried MilkDAWp_Logo_Transparent_png and MilkDAWpLogoTransparent_png)");
        }
    }

    // Begin polling params to control external visualization
    startTimerHz(15);
    MDW_LOG("UI", "Editor: timer started");
}

// ===== ValueTree listener: react to state restoration (presetPath) =====
void MilkDAWpAudioProcessorEditor::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    juce::ignoreUnused(treeWhosePropertyHasChanged);
    auto name = property.toString();
    if (name == "presetPath")
    {
        MDW_LOG("UI", "Editor: state property changed -> presetPath");
        refreshPresetPathFromState();
    }
    else if (name == "playlistActive" || name == "playlistRootPath" || name == "playlistIndex")
    {
        MDW_LOG("UI", juce::String("Editor: state property changed -> ") + name);
        restorePlaylistFromState();
    }
}

void MilkDAWpAudioProcessorEditor::valueTreeRedirected(juce::ValueTree& treeWhichHasBeenChanged)
{
    juce::ignoreUnused(treeWhichHasBeenChanged);
    MDW_LOG("UI", "Editor: state redirected (likely setStateInformation); refreshing state");
    restorePlaylistFromState();
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
        btnLoadPreset.setTooltip("Load preset (.milk) or playlist folder");
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
        // If a preset was restored and the window is not created yet, create/show it now
        if (isOnDesktop() && !processor.isSuspended())
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
    // Auto-start visualization
    if (getPeer() != nullptr && !visWindow)
        handleShowWindowChangeOnUI(true);
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
        // We just got a peer: clear shutdown gate, ensure our timer is running and auto-create the vis window
        glShutdownRequested.store(false);
        startTimerHz(15);
        MDW_LOG("UI", "Editor: timer (re)started (parentHierarchyChanged)");
        if (!visWindow)
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

    // Draw logo at ~50% scale in the header area
    if (logoImage.isValid())
    {
        const int nativeW = logoImage.getWidth();
        const int nativeH = logoImage.getHeight();
        const int drawW = juce::roundToInt(nativeW * 0.5f);
        const int drawH = juce::roundToInt(nativeH * 0.5f);
        const int x = 10;
        const int y = 4;
        juce::RectanglePlacement rp(juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid | juce::RectanglePlacement::onlyReduceInSize);
        g.drawImageWithin(logoImage, x, y, drawW, drawH, rp);
    }

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
    // Right-aligned buttons: Pop Out and Fullscreen
    auto btnArea = top.removeFromRight(68);
    auto fsArea = btnArea.removeFromRight(32);
    auto poArea = btnArea.removeFromRight(32);
    fsArea = fsArea.withSize(28, 24).withY(fsArea.getY() + juce::jmax(0, (top.getHeight() - 24) / 2));
    poArea = poArea.withSize(28, 24).withY(poArea.getY() + juce::jmax(0, (top.getHeight() - 24) / 2));
    btnPopOut.setBounds(poArea);
    btnFullscreen.setBounds(fsArea);
    // Left controls
    meterLabel.setBounds(top.removeFromLeft(180));

    // Padding between header and preset row (increased by +20 to make room for taller logo)
    r.removeFromTop(32);

    // Preset/playlist row
    auto row = r.removeFromTop(28);
    // Transport area (expanded, replaces type label)
    auto transportArea = row.removeFromLeft(240);
    if (playlistActive)
    {
        auto ta = transportArea;
        auto h = juce::jmin(24, ta.getHeight());
        auto ymid = ta.getY() + (ta.getHeight() - h) / 2;
        auto leftBtn = ta.removeFromLeft(28).withSizeKeepingCentre(24, h).withY(ymid);
        ta.removeFromLeft(4);
        auto rightBtn = ta.removeFromLeft(28).withSizeKeepingCentre(24, h).withY(ymid);
        btnPrev.setBounds(leftBtn);
        btnNext.setBounds(rightBtn);
        btnPrev.setVisible(true);
        btnNext.setVisible(true);
    }
    else
    {
        btnPrev.setVisible(false);
        btnNext.setVisible(false);
    }
    row.removeFromLeft(6);
    if (playlistActive)
    {
        presetBox.setVisible(true);
        currentPresetLabel.setVisible(false);
        presetBox.setBounds(row.removeFromLeft(juce::jmax(180, row.getWidth() - 250)));
    }
    else
    {
        presetBox.setVisible(false);
        currentPresetLabel.setVisible(true);
        currentPresetLabel.setBounds(row.removeFromLeft(juce::jmax(180, row.getWidth() - 250)));
    }
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
    if (paramID == "fullscreen")
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
    else if (paramID == "playlistPresetIndex")
    {
        const int idx = (int) newValue;
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP, idx]()
        {
            if (editorSP == nullptr) return;
            if (!editorSP->playlistActive || editorSP->playlistItems.isEmpty()) return;
            const int size = editorSP->playlistItems.size();
            const int clamped = juce::jlimit(0, juce::jmax(0, size - 1), idx);
            editorSP->playlistIndex = clamped;
            // Update UI dropdown if needed
            if (editorSP->presetBox.getSelectedItemIndex() != clamped)
                editorSP->presetBox.setSelectedItemIndex(clamped, juce::dontSendNotification);
            // Update labels and state
            auto file = editorSP->playlistItems[clamped];
            editorSP->currentPresetLabel.setText(file.getFileName(), juce::dontSendNotification);
            editorSP->lastPresetPath = file.getFullPathName();
            editorSP->processor.apvts.state.setProperty("presetPath", editorSP->lastPresetPath, nullptr);
            editorSP->processor.apvts.state.setProperty("playlistIndex", clamped, nullptr);
            if (editorSP->visWindow)
                editorSP->visWindow->loadPresetByPath(editorSP->lastPresetPath, true);
        });
    }
    else if (paramID == "playlistPrev" || paramID == "playlistNext")
    {
        const bool pressed = newValue > 0.5f;
        if (!pressed) return; // act only on press
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP, isPrev = (paramID == "playlistPrev")]()
        {
            if (editorSP == nullptr) return;
            if (!editorSP->playlistActive || editorSP->playlistItems.isEmpty()) return;
            const int size = editorSP->playlistItems.size();
            const int cur  = juce::jlimit(0, size - 1, editorSP->playlistIndex < 0 ? 0 : editorSP->playlistIndex);
            const int next = isPrev ? (cur - 1 + size) % size : (cur + 1) % size;
            editorSP->setPlaylistIndexParam(next);
            // Reset trigger param back to false
            auto* param = editorSP->processor.apvts.getParameter(isPrev ? "playlistPrev" : "playlistNext");
            if (auto* pb = dynamic_cast<juce::AudioParameterBool*>(param))
            {
                pb->beginChangeGesture();
                pb->setValueNotifyingHost(0.0f);
                pb->endChangeGesture();
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
            // If user requested pop-out, immediately undock now that it exists
            if (popOutDesired && visWindow->isDocked())
            {
                visWindow->undock();
                visWindow->setVisible(true);
                visWindow->toFront(true);
            }
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
                MDW_LOG("UI", "Editor: onUserClose -> hide visualization");
                if (editorSP->visWindow)
                    editorSP->visWindow->setVisible(false);
            });

            visWindow->setOnFullscreenChanged([editorSP](bool isFS)
            {
                if (editorSP == nullptr) return;
                MDW_LOG("UI", juce::String("Editor: onFullscreenChanged -> ") + (isFS ? "true" : "false"));
                // When entering fullscreen (including via ESC toggles or host), remember prior pop-out state
                if (isFS && editorSP->visWindow)
                {
                    if (!editorSP->previousPopoutStateCaptured)
                    {
                        editorSP->previousPopoutWasDocked = editorSP->visWindow->isDocked();
                        editorSP->wasDockedBeforeFullscreen = editorSP->previousPopoutWasDocked;
                        editorSP->previousPopoutStateCaptured = true;
                    }
                }
                else if (!isFS)
                {
                    // Reset capture guard when exiting fullscreen
                    editorSP->previousPopoutStateCaptured = false;
                }
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
            // Ensure docking state matches the Pop Out toggle (unless fullscreen)
            if (popOutDesired)
            {
                if (visWindow->isDocked())
                    visWindow->undock();
            }
            else
            {
                if (!wantFullscreen && !visWindow->isDocked())
                {
                    visWindow->dockTo(this);
                    this->resized();
                }
            }

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
        // Remember prior pop-out state for ESC/exit restoration
        previousPopoutWasDocked = visWindow->isDocked();
        previousPopoutStateCaptured = true;
        // Ensure the visualization is undocked before entering fullscreen
        wasDockedBeforeFullscreen = previousPopoutWasDocked;
        if (wasDockedBeforeFullscreen)
        {
            visWindow->undock();
        }
        // Enforce Pop Out state while in fullscreen (UI reflects that it's a separate window now)
        popOutDesired = true;
        if (!btnPopOut.getToggleState())
            btnPopOut.setToggleState(true, juce::dontSendNotification);
        visWindow->setFullScreenParam(true);
    }
    else
    {
        // Exit fullscreen first
        visWindow->setFullScreenParam(false);
        // Restore to the previously remembered pop-out state regardless of current toggle
        const bool shouldDock = previousPopoutWasDocked;
        if (shouldDock)
        {
            visWindow->dockTo(this);
            // Lay out immediately to restore docked bounds
            this->resized();
        }
        else
        {
            // Ensure it stays undocked (popped-out)
            if (visWindow->isDocked())
            {
                visWindow->undock();
            }
            visWindow->toFront(true);
        }
        // Update the Pop Out UI/toggle to reflect the actual state after exiting fullscreen
        popOutDesired = !shouldDock;
        if (btnPopOut.getToggleState() != popOutDesired)
            btnPopOut.setToggleState(popOutDesired, juce::dontSendNotification);
        // Reset guard after exiting fullscreen
        previousPopoutStateCaptured = false;
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
    const bool wantFullscreen = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;

    const float amp = processor.apvts.getRawParameterValue("ampScale")->load();
    const float spd = processor.apvts.getRawParameterValue("speed")->load();
    const float h = processor.apvts.getRawParameterValue("colorHue")->load();
    const float sat = processor.apvts.getRawParameterValue("colorSat")->load();
    const int sd = (int) processor.apvts.getRawParameterValue("seed")->load();

    // If host suspended us (e.g., Cubase disabled the plugin), hide external window
    if (processor.isSuspended())
    {
        if (visWindow && visWindow->isVisible())
            visWindow->setVisible(false);
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

    // If we weren’t on desktop before, act on it here by creating the window when needed
    if (!visWindow && !creationPending.exchange(true))
    {
        MDW_LOG("UI", "Editor: creating VisualizationWindow (timer catch-up)");
        const int initIdx = (int) processor.apvts.getRawParameterValue("presetIndex")->load();
        visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz(), lastPresetPath, initIdx);
        // Dock to the main editor by default (embedded)
        visWindow->dockTo(this);
        // Lay out immediately so the GL canvas is visible without requiring a resize
        this->resized();
        // If user requested pop-out, undock now
        if (popOutDesired && visWindow->isDocked())
        {
            visWindow->undock();
            visWindow->setVisible(true);
            visWindow->toFront(true);
        }
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
            MDW_LOG("UI", "Editor: onUserClose -> hide visualization");
            if (editorSP->visWindow)
                editorSP->visWindow->setVisible(false);
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

void MilkDAWpAudioProcessorEditor::setPlaylistIndexParam(int newIndex)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(processor.apvts.getParameter("playlistPresetIndex")))
    {
        int maxIdx = 1023;
        if (playlistActive && playlistItems.size() > 0)
            maxIdx = juce::jmax(0, playlistItems.size() - 1);
        newIndex = juce::jlimit(0, maxIdx, newIndex);
        const float norm = p->convertTo0to1(newIndex);
        p->beginChangeGesture();
        p->setValueNotifyingHost(norm);
        p->endChangeGesture();
    }
}

void MilkDAWpAudioProcessorEditor::restorePlaylistFromState()
{
    // Read state properties
    const bool wasActive = processor.apvts.state.getProperty("playlistActive");
    const juce::var rootVar = processor.apvts.state.getProperty("playlistRootPath");
    const juce::var idxVar  = processor.apvts.state.getProperty("playlistIndex");
    const juce::String rootPath = rootVar.isString() ? rootVar.toString() : juce::String();
    const int savedIndex = idxVar.isInt() ? (int) idxVar : -1;

    if (!wasActive || rootPath.isEmpty())
    {
        // Deactivate playlist UI
        playlistActive = false;
        playlistItems.clear();
        playlistIndex = -1;
        presetBox.clear(juce::dontSendNotification);
        presetBox.setVisible(false);
        btnPrev.setVisible(false);
        btnNext.setVisible(false);
        currentPresetLabel.setVisible(true);
        this->resized();
        return;
    }

    // Scan folder for .milk and rebuild items
    juce::File root(rootPath);
    if (!root.isDirectory())
        return;

    playlistRoot = root;
    playlistItems.clear();
    juce::DirectoryIterator it(root, false, "*.milk", juce::File::findFiles);
    while (it.next())
        playlistItems.add(it.getFile());
    struct FileNameLess { int compareElements(const juce::File& a, const juce::File& b) const { return a.getFileName().toLowerCase().compare(b.getFileName().toLowerCase()); } } cmp;
    playlistItems.sort(cmp);

    playlistActive = playlistItems.size() > 0;
    if (!playlistActive)
    {
        // Nothing to show
        processor.apvts.state.setProperty("playlistActive", false, nullptr);
        return;
    }

    // Populate dropdown
    presetBox.clear(juce::dontSendNotification);
    for (int i = 0; i < playlistItems.size(); ++i)
        presetBox.addItem(playlistItems.getReference(i).getFileName(), i + 1);
    presetBox.setVisible(true);
    currentPresetLabel.setVisible(false);
    btnPrev.setVisible(true);
    btnNext.setVisible(true);

    int idx = savedIndex >= 0 ? savedIndex : 0;
    idx = juce::jlimit(0, playlistItems.size() - 1, idx);
    playlistIndex = idx;
    processor.apvts.state.setProperty("playlistIndex", idx, nullptr);
    presetBox.setSelectedItemIndex(idx, juce::dontSendNotification);

    // Update labels and load
    lastPresetPath = playlistItems[idx].getFullPathName();
    processor.apvts.state.setProperty("presetPath", lastPresetPath, nullptr);
    currentPresetLabel.setText(playlistItems[idx].getFileName(), juce::dontSendNotification);
    btnLoadPreset.setTooltip(playlistRoot.getFullPathName());
    if (visWindow)
        visWindow->loadPresetByPath(lastPresetPath, true);

    // Drive param to reflect
    setPlaylistIndexParam(idx);

    // Layout
    this->resized();
}
