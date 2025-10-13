// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "Version.h"
#include "Logging.h"
#include "AudioAnalysisQueue.h"
#include "VisualizationThread.h"

class MilkDAWpAudioProcessor : public juce::AudioProcessor, public juce::AudioProcessorValueTreeState::Listener {
public:
    struct AutoAdvanceTimer : juce::Timer {
        MilkDAWpAudioProcessor& proc;
        explicit AutoAdvanceTimer(MilkDAWpAudioProcessor& p) : proc(p) {}
        void timerCallback() override { proc.onAutoAdvanceTimer(); }
    };
public:
    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS& getValueTreeState() noexcept { return apvts; }
    juce::String getCurrentPresetPath() const noexcept { return currentPresetPath; }
    void setCurrentPresetPathAndPostLoad(const juce::String& path)
    {
        currentPresetPath = path;
    #if MILKDAWP_ENABLE_VIZ_THREAD
        if (vizThread)
            vizThread->postLoadPreset(path);
    #endif
    }
    static APVTS::ParameterLayout createParameterLayout() {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        params.emplace_back(std::make_unique<juce::AudioParameterFloat>(
            "beatSensitivity", "Beat Sensitivity",
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.0f, 1.0f), 1.0f));
        params.emplace_back(std::make_unique<juce::AudioParameterFloat>(
            "transitionDurationSeconds", "Transition Duration (s)",
            juce::NormalisableRange<float>(0.1f, 30.0f, 0.0f, 1.0f), 5.0f));
        params.emplace_back(std::make_unique<juce::AudioParameterBool>(
            "shuffle", "Shuffle", false));
        params.emplace_back(std::make_unique<juce::AudioParameterBool>(
            "lockCurrentPreset", "Lock Current Preset", false));
        params.emplace_back(std::make_unique<juce::AudioParameterInt>(
            "presetIndex", "Preset Index", 0, 128, 0));
        params.emplace_back(std::make_unique<juce::AudioParameterChoice>(
            "transitionStyle", "Transition Style",
            juce::StringArray{ "Cut", "Crossfade", "Blend" }, 0));
        return { params.begin(), params.end() };
    }

    MilkDAWpAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Params", createParameterLayout()),
      fft(milkdawp::AudioAnalysisSnapshot::fftOrder),
      window(milkdawp::AudioAnalysisSnapshot::fftSize, juce::dsp::WindowingFunction<float>::hann, true)
    {
        milkdawp::Logging::init("MilkDAWp", MILKDAWP_VERSION_STRING);
        MDW_LOG_INFO("AudioProcessor constructed");
        fftBuffer.malloc(milkdawp::AudioAnalysisSnapshot::fftSize * 2);
        monoAccum.resize(milkdawp::AudioAnalysisSnapshot::fftSize);
        energyHistory.fill(0.0f);

        // Register parameter listeners for wiring to visualization thread
        apvts.addParameterListener("beatSensitivity", this);
        apvts.addParameterListener("transitionDurationSeconds", this);
        apvts.addParameterListener("shuffle", this);
        apvts.addParameterListener("lockCurrentPreset", this);
        apvts.addParameterListener("presetIndex", this);
        apvts.addParameterListener("transitionStyle", this);
    }

    ~MilkDAWpAudioProcessor() override {
        if (vizThread) vizThread->stop();
        apvts.removeParameterListener("beatSensitivity", this);
        apvts.removeParameterListener("transitionDurationSeconds", this);
        apvts.removeParameterListener("shuffle", this);
        apvts.removeParameterListener("lockCurrentPreset", this);
        apvts.removeParameterListener("presetIndex", this);
        apvts.removeParameterListener("transitionStyle", this);
        milkdawp::Logging::shutdown();
    }

    const juce::String getName() const override { return "MilkDAWp"; }

    void prepareToPlay(double sampleRate, int /*samplesPerBlockExpected*/) override {
        juce::ignoreUnused(sampleRate);
        fftWritePos = 0;
        runningSamplePos = 0;
        energyIndex = 0;
        energyAverage = 0.0f;
        beatCooldown = 0;
        analysisQueue.clear();
#if !defined(MILKDAWP_ENABLE_VIZ_THREAD)
#define MILKDAWP_ENABLE_VIZ_THREAD 1
#endif
#if MILKDAWP_ENABLE_VIZ_THREAD
        if (!vizThread)
            vizThread = std::make_unique<milkdawp::VisualizationThread>(analysisQueue);
        vizThread->start();
        // Send initial parameter values to visualization thread
        sendAllParamsToViz();
#endif
    }

    void releaseResources() override {
#if MILKDAWP_ENABLE_VIZ_THREAD
        if (vizThread)
            vizThread->stop();
#endif
    }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        // Only allow matching input/output channel counts, mono or stereo
        const auto& in = layouts.getMainInputChannelSet();
        const auto& out = layouts.getMainOutputChannelSet();
        if (in != out) return false;
        return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;

        // Zero-latency passthrough: ensure extra outputs are cleared
        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());

        // Mix to mono into accumulator and produce analysis snapshots per 1024-sample window
        const int numInCh = juce::jmin(2, getTotalNumInputChannels());
        const int N = buffer.getNumSamples();
        const float* in0 = numInCh > 0 ? buffer.getReadPointer(0) : nullptr;
        const float* in1 = numInCh > 1 ? buffer.getReadPointer(1) : nullptr;

        int i = 0;
        while (i < N) {
            const int space = milkdawp::AudioAnalysisSnapshot::fftSize - fftWritePos;
            const int toCopy = juce::jmin(space, N - i);

            for (int n = 0; n < toCopy; ++n) {
                float s = 0.0f;
                if (in0) s += in0[i + n];
                if (in1) s += in1[i + n];
                if (numInCh > 1) s *= 0.5f; // average if stereo
                monoAccum[(size_t)(fftWritePos + n)] = s;
            }

            fftWritePos += toCopy;
            i += toCopy;

            if (fftWritePos == milkdawp::AudioAnalysisSnapshot::fftSize) {
                produceAnalysisSnapshot();
                fftWritePos = 0;
            }
        }

        runningSamplePos += static_cast<uint64_t>(N);
    }

    // Editor
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // APVTS listener: called on audio thread when a parameter changes
    void parameterChanged(const juce::String& parameterID, float newValue) override {
#if MILKDAWP_ENABLE_VIZ_THREAD
        if (vizThread)
            vizThread->postParameterChange(parameterID, newValue);
#endif
        if (parameterID == "shuffle") {
            if (!playlistFiles.isEmpty()) {
                rebuildPlaylistOrder();
                // Reload current selection based on new order
                goToPlaylistRelative(0);
                restartAutoAdvanceTimer();
            }
        } else if (parameterID == "transitionDurationSeconds") {
            // Restart timer with new interval if applicable
            restartAutoAdvanceTimer();
        } else if (parameterID == "lockCurrentPreset") {
            const bool locked = newValue >= 0.5f;
            if (locked) stopAutoAdvanceTimer(); else restartAutoAdvanceTimer();
        } else if (parameterID == "presetIndex") {
            if (ignorePresetIndexParamChange) return;
            if (!hasActivePlaylist()) return;
            // newValue is actual value (0..128). Clamp to available entries
            int desired = (int) juce::roundToInt(newValue);
            const int N = playlistOrder.size();
            if (N <= 0) return;
            desired = juce::jlimit(0, N - 1, desired);
            if (desired != playlistPos) {
                playlistPos = desired;
                const int idx = playlistOrder[playlistPos];
                if ((unsigned)idx < (unsigned)playlistFiles.size()) {
                    const auto& f = playlistFiles.getReference(idx);
                    setCurrentPresetPathAndPostLoad(f.getFullPathName());
                }
                // Ensure parameter reflects clamped/actual position to avoid mismatch with host automation
                syncPresetIndexParam();
                restartAutoAdvanceTimer();
            } else {
                // Even if same, if host sent an out-of-range value that clamped to current, resync param
                syncPresetIndexParam();
            }
        }
#if !MILKDAWP_ENABLE_VIZ_THREAD
        juce::ignoreUnused(newValue);
#endif
    }

    // Program/state basics (single program)
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // State info
    void getStateInformation(juce::MemoryBlock& destData) override {
        juce::ValueTree root { juce::Identifier("MilkDAWpState") };
        root.setProperty("version", juce::String(MILKDAWP_VERSION_STRING), nullptr);
        root.setProperty("presetPath", currentPresetPath, nullptr);
        root.setProperty("playlistFolderPath", currentPlaylistFolderPath, nullptr);

        // Embed parameter state
        auto paramsState = apvts.copyState();
        root.addChild(paramsState, -1, nullptr);

        juce::MemoryOutputStream mos(destData, false);
        root.writeToStream(mos);
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        auto root = juce::ValueTree::readFromData(data, (size_t) sizeInBytes);
        if (! root.isValid())
            return;

        if (root.hasType(juce::Identifier("MilkDAWpState"))) {
            currentPresetPath = root.getProperty("presetPath").toString();
            currentPlaylistFolderPath = root.getProperty("playlistFolderPath").toString();

            // Find APVTS child by type
            const auto paramsType = apvts.state.getType();
            auto paramTree = root.getChildWithName(paramsType);
            if (! paramTree.isValid() && root.getNumChildren() > 0)
                paramTree = root.getChild(0);

            if (paramTree.isValid())
                apvts.replaceState(paramTree);

            // Re-scan playlist if path present
            if (currentPlaylistFolderPath.isNotEmpty())
                setPlaylistFolderAndScan(currentPlaylistFolderPath);
        }
    }

    // MIDI/latency settings per README Phase 0: audio effect, no MIDI
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Access to the queue (for viz thread later)
    milkdawp::AudioAnalysisQueue<64>& getAnalysisQueue() noexcept { return analysisQueue; }

public:
    // Phase 3.2 public API for editor
    void setPlaylistFolderAndScanPublic(const juce::String& folderPath) { setPlaylistFolderAndScan(folderPath); }
    void clearPlaylistPublic() { clearPlaylist(); }
    bool hasActivePlaylistPublic() const noexcept { return hasActivePlaylist(); }
    void nextPresetInPlaylist() { goToPlaylistRelative(1); }
    void prevPresetInPlaylist() { goToPlaylistRelative(-1); }
    juce::String getCurrentPlaylistItemName() const {
        if (!hasActivePlaylist()) return {};
        const int idx = playlistOrder[(int)playlistPos];
        if ((unsigned)idx < (unsigned)playlistFiles.size())
            return playlistFiles[(int)idx].getFileNameWithoutExtension();
        return {};
    }

private:
    APVTS apvts;

    // Phase 2.2: Non-parameter state
    juce::String currentPresetPath;           // Full path to current preset file (may be empty)
    juce::String currentPlaylistFolderPath;   // Folder path for current playlist (may be empty)

    // Phase 3.2: Playlist state
    juce::Array<juce::File> playlistFiles;    // Files discovered in playlist folder
    juce::Array<int> playlistOrder;           // Order of indices into playlistFiles (shuffled or sequential)
    int playlistPos = -1;                     // Position within playlistOrder (-1 if no playlist)

    // Phase 3.3: Auto-advance timer and param sync
    std::unique_ptr<AutoAdvanceTimer> autoTimer; 
    bool ignorePresetIndexParamChange = false;

    void stopAutoAdvanceTimer() {
        if (autoTimer) autoTimer->stopTimer();
    }
    void restartAutoAdvanceTimer() {
        // Only if playlist active and not locked
        const bool locked = (apvts.getRawParameterValue("lockCurrentPreset") && apvts.getRawParameterValue("lockCurrentPreset")->load() >= 0.5f);
        if (!hasActivePlaylist() || locked) { stopAutoAdvanceTimer(); return; }
        float secs = 5.0f;
        if (auto* p = apvts.getRawParameterValue("transitionDurationSeconds")) secs = p->load();
        int ms = juce::jlimit(100, 10 * 60 * 1000, (int) juce::roundToInt(secs * 1000.0f));
        if (!autoTimer) autoTimer = std::make_unique<AutoAdvanceTimer>(*this);
        autoTimer->stopTimer();
        autoTimer->startTimer(ms);
    }
    void onAutoAdvanceTimer() {
        const bool locked = (apvts.getRawParameterValue("lockCurrentPreset") && apvts.getRawParameterValue("lockCurrentPreset")->load() >= 0.5f);
        if (!hasActivePlaylist() || locked) { stopAutoAdvanceTimer(); return; }
        goToPlaylistRelative(1);
        // restart for next interval
        restartAutoAdvanceTimer();
    }
    void syncPresetIndexParam() {
        if (!hasActivePlaylist()) return;
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter("presetIndex"))) {
            const float actual = (float) playlistPos;
            const float norm = rp->convertTo0to1(actual);
            const float currentNorm = rp->getValue();
            if (std::abs(currentNorm - norm) > 1.0e-6f) {
                ignorePresetIndexParamChange = true;
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(norm);
                rp->endChangeGesture();
                ignorePresetIndexParamChange = false;
            }
        }
    }

    void clearPlaylist()
    {
        playlistFiles.clear();
        playlistOrder.clear();
        playlistPos = -1;
        stopAutoAdvanceTimer();
        // Reset presetIndex parameter to 0 for single-preset mode visibility
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter("presetIndex"))) {
            ignorePresetIndexParamChange = true;
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->convertTo0to1(0.0f));
            rp->endChangeGesture();
            ignorePresetIndexParamChange = false;
        }
    }

    void rebuildPlaylistOrder()
    {
        playlistOrder.clear();
        for (int i = 0; i < playlistFiles.size(); ++i)
            playlistOrder.add(i);
        // Shuffle support via parameter
        if (auto* p = apvts.getRawParameterValue("shuffle"); p && p->load() >= 0.5f)
        {
            juce::Random rng(0xC0FFEE); // deterministic for now; can be improved later
            for (int i = playlistOrder.size() - 1; i > 0; --i)
            {
                int j = rng.nextInt(i + 1);
                std::swap(playlistOrder.getReference(i), playlistOrder.getReference(j));
            }
        }
        // Clamp current position
        if (playlistOrder.isEmpty()) playlistPos = -1; else playlistPos = juce::jlimit(0, playlistOrder.size()-1, playlistPos < 0 ? 0 : playlistPos);
    }

    bool hasActivePlaylist() const noexcept { return playlistPos >= 0 && playlistPos < playlistOrder.size(); }

    void setPlaylistFolderAndScan(const juce::String& folderPath)
    {
        currentPlaylistFolderPath = folderPath;
        playlistFiles.clear();
        playlistOrder.clear();
        playlistPos = -1;
        juce::File dir(folderPath);
        if (dir.isDirectory())
        {
            juce::Array<juce::File> found;
            juce::DirectoryIterator it(dir, false, "*.milk", juce::File::findFiles);
            while (it.next())
                found.add(it.getFile());
            struct FileNameComparator { int compareElements(juce::File a, juce::File b) const { return a.getFileName().compareIgnoreCase(b.getFileName()); } } comp;
            found.sort(comp);
            playlistFiles = found;
            rebuildPlaylistOrder();
            if (hasActivePlaylist())
            {
                const auto idx = playlistOrder[(int)playlistPos];
                if ((unsigned)idx < (unsigned)playlistFiles.size())
                {
                    const auto& f = playlistFiles.getReference(idx);
                    setCurrentPresetPathAndPostLoad(f.getFullPathName());
                }
                syncPresetIndexParam();
                restartAutoAdvanceTimer();
            }
        }
    }

    void goToPlaylistRelative(int delta)
    {
        if (!hasActivePlaylist()) return;
        if (playlistOrder.isEmpty()) return;
        const int N = playlistOrder.size();

        bool shuffleOn = false;
        if (auto* p = apvts.getRawParameterValue("shuffle")) shuffleOn = p->load() >= 0.5f;

        if (shuffleOn && delta != 0)
        {
            // Choose a random next position (different from current when possible)
            if (N > 1)
            {
                int newPos = playlistPos;
                auto& rng = juce::Random::getSystemRandom();
                do { newPos = rng.nextInt(N); } while (newPos == playlistPos);
                playlistPos = newPos;
            }
            else
            {
                playlistPos = 0;
            }
        }
        else
        {
            // Sequential step or reload when delta == 0
            playlistPos = (playlistPos + delta) % N;
            if (playlistPos < 0) playlistPos += N;
        }

        const int idx = playlistOrder[playlistPos];
        if ((unsigned)idx < (unsigned)playlistFiles.size())
        {
            const auto& f = playlistFiles.getReference(idx);
            setCurrentPresetPathAndPostLoad(f.getFullPathName());
        }
        syncPresetIndexParam();
        restartAutoAdvanceTimer();
    }

    void sendAllParamsToViz()
    {
    #if MILKDAWP_ENABLE_VIZ_THREAD
        if (!vizThread) return;
        auto send = [&](const char* id){
            if (auto* p = apvts.getRawParameterValue(id))
                vizThread->postParameterChange(id, p->load());
        };
        send("beatSensitivity");
        send("transitionDurationSeconds");
        send("shuffle");
        send("lockCurrentPreset");
        send("presetIndex");
        send("transitionStyle");
    #endif
        }

    void produceAnalysisSnapshot() noexcept {
        // Copy mono into FFT buffer and window to compute short-time energy; FFT results are reserved for future phases
        const int size = milkdawp::AudioAnalysisSnapshot::fftSize;
        auto* fftData = fftBuffer.get();
        for (int n = 0; n < size; ++n) fftData[n] = monoAccum[(size_t)n];
        window.multiplyWithWindowingTable(fftData, size);
        // zero imaginary part (not used currently)
        std::fill(fftData + size, fftData + size * 2, 0.0f);

        // Optionally perform FFT (kept for future); results currently unused
        fft.performRealOnlyForwardTransform(fftData);

        // Short-time energy on time-domain window
        float energy = 0.0f;
        for (int n = 0; n < size; ++n) {
            const float s = monoAccum[(size_t)n];
            energy += s * s;
        }
        energy /= static_cast<float>(size);

        milkdawp::AudioAnalysisSnapshot snap;
        snap.shortTimeEnergy = energy;
        snap.samplePosition = runningSamplePos;

        // Maintain moving average internally for future beat detection (no output yet)
        constexpr int historyLen = (int)energyHistorySize;
        const float old = energyHistory[(size_t)energyIndex];
        energyHistory[(size_t)energyIndex] = energy;
        energyIndex = (energyIndex + 1) % historyLen;
        energyAverage += (energy - old) / (float)historyLen;
        if (beatCooldown > 0) --beatCooldown;

        // Enqueue (drop if full)
        (void)analysisQueue.tryPush(snap);
    }

    // Analysis state
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    juce::HeapBlock<float> fftBuffer; // size = 2 * fftSize
    std::vector<float> monoAccum;     // size = fftSize
    int fftWritePos = 0;

    static constexpr size_t energyHistorySize = 43;
    std::array<float, energyHistorySize> energyHistory{};
    int energyIndex = 0;
    float energyAverage = 0.0f;
    int beatCooldown = 0;

    uint64_t runningSamplePos = 0;

    milkdawp::AudioAnalysisQueue<64> analysisQueue;
    std::unique_ptr<milkdawp::VisualizationThread> vizThread;
};

class MilkDAWpAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
private:
    struct HardwareLookAndFeel : public juce::LookAndFeel_V4 {
        HardwareLookAndFeel()
        {
            setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xFF101214));
            setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2E33));
            setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1C1F22));
            setColour(juce::ComboBox::textColourId, juce::Colours::white);
            setColour(juce::Label::textColourId, juce::Colours::white);
            setColour(juce::Slider::thumbColourId, juce::Colour(0xFFE0E0E0));
            setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF4A90E2));
            setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF2A2E33));
        }
        void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                  bool isMouseOverButton, bool isButtonDown) override
        {
            auto bounds = button.getLocalBounds().toFloat();
            auto base = backgroundColour;
            if (isButtonDown) base = base.brighter(0.1f);
            else if (isMouseOverButton) base = base.brighter(0.06f);
            g.setColour(base);
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
        }
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<float>(x, y, (float) width, (float) height).reduced(4);
            auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
            auto toAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
            auto lineW = juce::jmax(2.0f, radius * 0.08f);
            auto arcRadius = radius - lineW * 0.5f;

            // background arc
            juce::Path backgroundArc;
            backgroundArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(findColour(juce::Slider::rotarySliderOutlineColourId));
            g.strokePath(backgroundArc, juce::PathStrokeType(lineW));

            // value arc
            juce::Path valueArc;
            valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, toAngle, true);
            g.setColour(findColour(juce::Slider::rotarySliderFillColourId));
            g.strokePath(valueArc, juce::PathStrokeType(lineW));

            // thumb
            juce::Point<float> thumbPoint(bounds.getCentreX() + arcRadius * std::cos(toAngle - juce::MathConstants<float>::halfPi),
                                          bounds.getCentreY() + arcRadius * std::sin(toAngle - juce::MathConstants<float>::halfPi));
            g.setColour(findColour(juce::Slider::thumbColourId));
            g.fillEllipse(thumbPoint.x - lineW, thumbPoint.y - lineW, lineW * 2.0f, lineW * 2.0f);
        }
    };
public:
    static constexpr int topHeight = 80;
    explicit MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor& proc)
        : juce::AudioProcessorEditor(&proc), processor(proc)
    {
        setLookAndFeel(&hardwareLAF);
        setSize(1200, 650); // per README default window size

        // Logo
        addAndMakeVisible(logoLabel);
        logoLabel.setText("MilkDAWp", juce::dontSendNotification);
        logoLabel.setFont(juce::Font(24.0f, juce::Font::bold));
        logoLabel.setJustificationType(juce::Justification::centredLeft);

        // Visualization placeholder
        addAndMakeVisible(vizPlaceholder);

        // Knobs and toggles (Phase 4.2)
        addAndMakeVisible(beatLabel);
        beatLabel.setText("Beat", juce::dontSendNotification);
        beatLabel.setJustificationType(juce::Justification::centred);
        beatLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(beatSlider);
        beatSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        beatSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        beatSlider.setTooltip("Beat Sensitivity (0.0 - 2.0)");
        beatAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getValueTreeState(), "beatSensitivity", beatSlider);

        addAndMakeVisible(durationLabel);
        durationLabel.setText("Duration", juce::dontSendNotification);
        durationLabel.setJustificationType(juce::Justification::centred);
        durationLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(durationSlider);
        durationSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        durationSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        durationSlider.setTooltip("Transition Duration (seconds)");
        durationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getValueTreeState(), "transitionDurationSeconds", durationSlider);

        addAndMakeVisible(lockToggle);
        lockToggle.setButtonText("Lock");
        lockToggle.setTooltip("Lock current preset (disable auto transitions)");
        lockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.getValueTreeState(), "lockCurrentPreset", lockToggle);

        addAndMakeVisible(shuffleToggle);
        shuffleToggle.setButtonText("Shuffle");
        shuffleToggle.setTooltip("Shuffle playlist order");
        shuffleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.getValueTreeState(), "shuffle", shuffleToggle);

        addAndMakeVisible(loadButton);
        loadButton.setButtonText("Load Preset...");
        loadButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser>("Select a MilkDrop preset", juce::File(), "*.milk");
            auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
            fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                fileChooser.reset();
                if (! f.existsAsFile())
                {
                    // User may have cancelled; just return silently in that case
                    return;
                }
                if (f.getFileExtension().toLowerCase() != ".milk")
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Preset Load", "Please select a .milk preset file.");
                    return;
                }
                processor.setCurrentPresetPathAndPostLoad(f.getFullPathName());
                // Switch to single-preset mode: clear any active playlist and stop transport
                processor.clearPlaylistPublic();
                presetNameLabel.setText(f.getFileNameWithoutExtension(), juce::dontSendNotification);
                refreshTransportVisibility();
            });
        };

        addAndMakeVisible(loadFolderButton);
        loadFolderButton.setButtonText("Load Playlist Folder...");
        loadFolderButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser>("Select a folder with .milk presets", juce::File(), juce::String());
            auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
            fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                fileChooser.reset();
                if (! f.isDirectory())
                    return;
                processor.setPlaylistFolderAndScanPublic(f.getFullPathName());
                // Update UI: preset label and transport visibility
                auto name = processor.getCurrentPlaylistItemName();
                if (name.isNotEmpty())
                    presetNameLabel.setText(name, juce::dontSendNotification);
                refreshTransportVisibility();
            });
        };

        addAndMakeVisible(prevButton);
        prevButton.setButtonText("Prev");
        prevButton.onClick = [this]{ processor.prevPresetInPlaylist(); presetNameLabel.setText(currentDisplayName(), juce::dontSendNotification); };
        addAndMakeVisible(nextButton);
        nextButton.setButtonText("Next");
        nextButton.onClick = [this]{ processor.nextPresetInPlaylist(); presetNameLabel.setText(currentDisplayName(), juce::dontSendNotification); };

        // Transition Style UI
        addAndMakeVisible(transitionStyleLabel);
        transitionStyleLabel.setText("Transition:", juce::dontSendNotification);
        transitionStyleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(transitionStyleCombo);
        transitionStyleCombo.addItem("Cut", 1);
        transitionStyleCombo.addItem("Crossfade", 2);
        transitionStyleCombo.addItem("Blend", 3);
        transitionStyleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.getValueTreeState(), "transitionStyle", transitionStyleCombo);

        addAndMakeVisible(presetNameLabel);
        presetNameLabel.setText(initialPresetName(), juce::dontSendNotification);
        presetNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        presetNameLabel.setJustificationType(juce::Justification::centredLeft);

        refreshTransportVisibility();
        lastDisplayedName = currentDisplayName();
        startTimerHz(10);
    }

    ~MilkDAWpAudioProcessorEditor() override {
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        auto bg = findColour(juce::ResizableWindow::backgroundColourId);
        g.fillAll(bg);
        // Top strip
        auto r = getLocalBounds();
        auto top = r.removeFromTop(topHeight);
        juce::Colour topColour = juce::Colour(0xFF171A1E);
        g.setColour(topColour);
        g.fillRoundedRectangle(top.reduced(8).toFloat(), 8.0f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(top.reduced(8).toFloat(), 8.0f, 1.0f);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto top = bounds.removeFromTop(topHeight);
        auto innerTop = top.reduced(16);

        // Left logo
        logoLabel.setBounds(innerTop.removeFromLeft(220));
        innerTop.removeFromLeft(12);

        // File/picker controls
        loadButton.setBounds(innerTop.removeFromLeft(150));
        innerTop.removeFromLeft(8);
        loadFolderButton.setBounds(innerTop.removeFromLeft(220));
        innerTop.removeFromLeft(12);

        // Knobs area
        auto knobW = 64;
        {
            auto area = innerTop.removeFromLeft(knobW);
            auto lab = area.removeFromTop(14);
            beatLabel.setBounds(lab);
            beatSlider.setBounds(area);
        }
        innerTop.removeFromLeft(8);
        {
            auto area = innerTop.removeFromLeft(knobW);
            auto lab = area.removeFromTop(14);
            durationLabel.setBounds(lab);
            durationSlider.setBounds(area);
        }
        innerTop.removeFromLeft(12);

        // Toggle buttons
        lockToggle.setBounds(innerTop.removeFromLeft(70));
        innerTop.removeFromLeft(6);
        shuffleToggle.setBounds(innerTop.removeFromLeft(80));
        innerTop.removeFromLeft(12);

        // Transition Style controls
        transitionStyleLabel.setBounds(innerTop.removeFromLeft(100));
        innerTop.removeFromLeft(6);
        transitionStyleCombo.setBounds(innerTop.removeFromLeft(140));
        innerTop.removeFromLeft(12);

        // Preset label uses remaining, leaving space for transport
        auto transportWidth = 160;
        presetNameLabel.setBounds(innerTop.removeFromLeft(juce::jmax(0, innerTop.getWidth() - transportWidth)));

        auto right = innerTop.removeFromRight(transportWidth);
        prevButton.setBounds(right.removeFromLeft(70));
        right.removeFromLeft(10);
        nextButton.setBounds(right.removeFromLeft(70));

        // Visualization area fills remaining
        vizPlaceholder.setBounds(bounds.reduced(12));
    }

private:
    void timerCallback() override
    {
        auto name = currentDisplayName();
        if (name != lastDisplayedName)
        {
            lastDisplayedName = name;
            presetNameLabel.setText(name, juce::dontSendNotification);
        }
    }

    void refreshTransportVisibility()
    {
        const bool show = processor.hasActivePlaylistPublic();
        prevButton.setVisible(show);
        nextButton.setVisible(show);
    }

    juce::String currentDisplayName() const
    {
        if (processor.hasActivePlaylistPublic())
        {
            auto name = processor.getCurrentPlaylistItemName();
            if (name.isNotEmpty()) return name;
        }
        return initialPresetName();
    }

    juce::String initialPresetName() const
    {
        juce::File f(processor.getCurrentPresetPath());
        if (f.existsAsFile()) return f.getFileNameWithoutExtension();
        return "(no preset)";
    }

    struct VizPlaceholder : public juce::Component {
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xFF111417));
            g.setColour(juce::Colour(0xFF2A2E33));
            g.drawRect(getLocalBounds(), 1);
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.setFont(18.0f);
            g.drawFittedText("Visualization Area", getLocalBounds(), juce::Justification::centred, 1);
        }
    };

    HardwareLookAndFeel hardwareLAF;
    juce::Label logoLabel;
    VizPlaceholder vizPlaceholder;

    MilkDAWpAudioProcessor& processor;

    // Phase 4.2 controls
    juce::Label beatLabel;
    juce::Slider beatSlider;
    juce::Label durationLabel;
    juce::Slider durationSlider;
    juce::ToggleButton lockToggle;
    juce::ToggleButton shuffleToggle;

    juce::TextButton loadButton;
    juce::TextButton loadFolderButton;
    juce::TextButton prevButton;
    juce::TextButton nextButton;
    juce::Label presetNameLabel;
    juce::String lastDisplayedName;
    juce::Label transitionStyleLabel;
    juce::ComboBox transitionStyleCombo;

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> beatAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> shuffleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> transitionStyleAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;
};

juce::AudioProcessorEditor* MilkDAWpAudioProcessor::createEditor() {
    return new MilkDAWpAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MilkDAWpAudioProcessor();
}
