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
    }

    ~MilkDAWpAudioProcessor() override {
        if (vizThread) vizThread->stop();
        apvts.removeParameterListener("beatSensitivity", this);
        apvts.removeParameterListener("transitionDurationSeconds", this);
        apvts.removeParameterListener("shuffle", this);
        apvts.removeParameterListener("lockCurrentPreset", this);
        apvts.removeParameterListener("presetIndex", this);
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
#else
        juce::ignoreUnused(parameterID, newValue);
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
        }
    }

    // MIDI/latency settings per README Phase 0: audio effect, no MIDI
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Access to the queue (for viz thread later)
    milkdawp::AudioAnalysisQueue<64>& getAnalysisQueue() noexcept { return analysisQueue; }

private:
    APVTS apvts;

    // Phase 2.2: Non-parameter state
    juce::String currentPresetPath;           // Full path to current preset file (may be empty)
    juce::String currentPlaylistFolderPath;   // Folder path for current playlist (may be empty)

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

class MilkDAWpAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor& proc)
        : juce::AudioProcessorEditor(&proc), processor(proc)
    {
        setSize(1200, 650); // per README default window size

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
                presetNameLabel.setText(f.getFileNameWithoutExtension(), juce::dontSendNotification);
            });
        };

        addAndMakeVisible(presetNameLabel);
        presetNameLabel.setText(initialPresetName(), juce::dontSendNotification);
        presetNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        presetNameLabel.setJustificationType(juce::Justification::centredLeft);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.setFont(18.0f);
#if MILKDAWP_HAS_PROJECTM
        constexpr const char* pm = "projectM: enabled";
#else
        constexpr const char* pm = "projectM: disabled";
#endif
        juce::String text = juce::String("MilkDAWp v") + MILKDAWP_VERSION_STRING + "\n" + pm;
        g.drawFittedText(text, getLocalBounds().removeFromTop(80), juce::Justification::centred, 2);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(16);
        auto top = r.removeFromTop(40);
        loadButton.setBounds(top.removeFromLeft(180));
        top.removeFromLeft(12);
        presetNameLabel.setBounds(top);
    }

private:
    juce::String initialPresetName() const
    {
        juce::File f(processor.getCurrentPresetPath());
        if (f.existsAsFile()) return f.getFileNameWithoutExtension();
        return "(no preset)";
    }

    MilkDAWpAudioProcessor& processor;
    juce::TextButton loadButton;
    juce::Label presetNameLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

juce::AudioProcessorEditor* MilkDAWpAudioProcessor::createEditor() {
    return new MilkDAWpAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MilkDAWpAudioProcessor();
}
