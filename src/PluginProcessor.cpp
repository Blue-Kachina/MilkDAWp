#include <JuceHeader.h>
#include "Version.h"
#include "Logging.h"
#include "AudioAnalysisQueue.h"

class MilkDAWpAudioProcessor : public juce::AudioProcessor {
public:
    MilkDAWpAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      fft(milkdawp::AudioAnalysisSnapshot::fftOrder),
      window(milkdawp::AudioAnalysisSnapshot::fftSize, juce::dsp::WindowingFunction<float>::hann, true)
    {
        milkdawp::Logging::init("MilkDAWp", MILKDAWP_VERSION_STRING);
        MDW_LOG_INFO("AudioProcessor constructed");
        fftBuffer.malloc(milkdawp::AudioAnalysisSnapshot::fftSize * 2);
        monoAccum.resize(milkdawp::AudioAnalysisSnapshot::fftSize);
        energyHistory.fill(0.0f);
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
    }

    void releaseResources() override {}

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

    // Program/state basics (single program)
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // State info (placeholder)
    void getStateInformation(juce::MemoryBlock& destData) override { juce::ignoreUnused(destData); }
    void setStateInformation(const void* data, int sizeInBytes) override { juce::ignoreUnused(data, sizeInBytes); }

    // MIDI/latency settings per README Phase 0: audio effect, no MIDI
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Access to the queue (for viz thread later)
    milkdawp::AudioAnalysisQueue<64>& getAnalysisQueue() noexcept { return analysisQueue; }

private:
    void produceAnalysisSnapshot() noexcept {
        // Copy mono into FFT buffer and window
        const int size = milkdawp::AudioAnalysisSnapshot::fftSize;
        auto* fftData = fftBuffer.get();
        for (int n = 0; n < size; ++n) fftData[n] = monoAccum[(size_t)n];
        window.multiplyWithWindowingTable(fftData, size);
        // zero imaginary part
        std::fill(fftData + size, fftData + size * 2, 0.0f);

        // Perform real-only FFT (in-place, interleaved real/imag in fftData)
        fft.performRealOnlyForwardTransform(fftData);

        milkdawp::AudioAnalysisSnapshot snap;
        // Compute magnitudes from interleaved data: re = fftData[2*k], im = fftData[2*k+1]
        float energy = 0.0f;
        for (int k = 0; k < milkdawp::AudioAnalysisSnapshot::fftBins; ++k) {
            const float re = fftData[2 * k];
            const float im = fftData[2 * k + 1];
            const float mag = std::sqrt(re * re + im * im);
            snap.magnitudes[(size_t)k] = mag;
        }
        // Short-time energy on time-domain window
        for (int n = 0; n < size; ++n) {
            const float s = monoAccum[(size_t)n];
            energy += s * s;
        }
        energy /= static_cast<float>(size);
        snap.shortTimeEnergy = energy;

        // Update moving average and detect beat
        constexpr int historyLen = (int)energyHistorySize;
        const float old = energyHistory[(size_t)energyIndex];
        energyHistory[(size_t)energyIndex] = energy;
        energyIndex = (energyIndex + 1) % historyLen;
        energyAverage += (energy - old) / (float)historyLen;

        bool beat = false;
        const float threshold = energyAverage * 1.3f;
        if (beatCooldown == 0 && energy > threshold) {
            beat = true;
            beatCooldown = 10; // simple debounce over ~10 windows
        } else {
            beat = false;
            if (beatCooldown > 0) --beatCooldown;
        }
        snap.beatDetected = beat;
        snap.samplePosition = runningSamplePos;

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
};

class MilkDAWpAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor& proc)
        : juce::AudioProcessorEditor(&proc), processor(proc) {
        setSize(1200, 650); // per README default window size
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
        juce::String text = juce::String("MilkDAWp v") + MILKDAWP_VERSION_STRING + " (core: audio thread setup)\n" + pm;
        g.drawFittedText(text, getLocalBounds(), juce::Justification::centred, 2);
    }

    void resized() override {}

private:
    MilkDAWpAudioProcessor& processor;
};

juce::AudioProcessorEditor* MilkDAWpAudioProcessor::createEditor() {
    return new MilkDAWpAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MilkDAWpAudioProcessor();
}
