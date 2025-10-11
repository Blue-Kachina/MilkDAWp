#include <JuceHeader.h>
#include "Version.h"
#include "Logging.h"

class MilkDAWpAudioProcessor : public juce::AudioProcessor {
public:
    MilkDAWpAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        milkdawp::Logging::init("MilkDAWp", MILKDAWP_VERSION_STRING);
        MDW_LOG_INFO("AudioProcessor constructed");
    }

    const juce::String getName() const override { return "MilkDAWp"; }
    void prepareToPlay(double, int) override {}
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
        // Zero-latency passthrough for Phase 1 target; for now, do nothing (in-place)
        // Ensure input copied to output if needed
        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) {
            buffer.clear(ch, 0, buffer.getNumSamples());
        }
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
        juce::String text = juce::String("MilkDAWp v") + MILKDAWP_VERSION_STRING + " (scaffold)\n" + pm;
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
