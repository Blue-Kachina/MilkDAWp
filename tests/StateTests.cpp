#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

using namespace juce;

// Factory function implemented in src/PluginProcessor.cpp
extern juce::AudioProcessor* createPluginFilter();

static juce::RangedAudioParameter* findParamByID(juce::AudioProcessor& p, const juce::String& paramID)
{
    auto& params = p.getParameters();
    for (auto* base : params)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(base))
            if (rp->getParameterID() == paramID)
                return rp;
    return nullptr;
}

static void setParamValue(juce::AudioProcessor& p, const juce::String& id, float actualValue)
{
    if (auto* rp = findParamByID(p, id))
        rp->setValueNotifyingHost(rp->convertTo0to1(actualValue));
}

static float getParamValue(juce::AudioProcessor& p, const juce::String& id)
{
    if (auto* rp = findParamByID(p, id))
        return rp->convertFrom0to1(rp->getValue());
    return std::numeric_limits<float>::quiet_NaN();
}

class StateRoundTripTest : public juce::UnitTest {
public:
    StateRoundTripTest() : juce::UnitTest("State Save/Restore Round-Trip", "State") {}

    void runTest() override {
        beginTest("APVTS parameter round-trip is deterministic");
        {
            std::unique_ptr<juce::AudioProcessor> proc1(createPluginFilter());
            expect(proc1 != nullptr);

            // Set a bunch of parameters to non-default values
            setParamValue(*proc1, "beatSensitivity", 1.234f);
            setParamValue(*proc1, "transitionDurationSeconds", 3.21f);
            setParamValue(*proc1, "shuffle", 1.0f);
            setParamValue(*proc1, "lockCurrentPreset", 1.0f);
            setParamValue(*proc1, "presetIndex", 42.0f);

            juce::MemoryBlock mb;
            proc1->getStateInformation(mb);

            std::unique_ptr<juce::AudioProcessor> proc2(createPluginFilter());
            expect(proc2 != nullptr);
            proc2->setStateInformation(mb.getData(), (int) mb.getSize());

            expectWithinAbsoluteError(getParamValue(*proc2, "beatSensitivity"), 1.234f, 1.0e-4f);
            expectWithinAbsoluteError(getParamValue(*proc2, "transitionDurationSeconds"), 3.21f, 1.0e-4f);
            expectWithinAbsoluteError(getParamValue(*proc2, "shuffle"), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError(getParamValue(*proc2, "lockCurrentPreset"), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError(getParamValue(*proc2, "presetIndex"), 42.0f, 1.0e-4f);
        }

        beginTest("Custom fields (presetPath/playlistFolderPath) survive round-trip");
        {
            std::unique_ptr<juce::AudioProcessor> procA(createPluginFilter());
            std::unique_ptr<juce::AudioProcessor> procB(createPluginFilter());
            expect(procA != nullptr && procB != nullptr);

            // Get default state, then inject custom properties into the root ValueTree
            juce::MemoryBlock mb;
            procA->getStateInformation(mb);
            auto root = juce::ValueTree::readFromData(mb.getData(), mb.getSize());
            expect(root.isValid());
            root.setProperty("presetPath", juce::String("C:/Presets/Cool.milk"), nullptr);
            root.setProperty("playlistFolderPath", juce::String("D:/MilkPlaylists/Show1"), nullptr);

            juce::MemoryBlock mb2;
            {
                juce::MemoryOutputStream mos(mb2, false);
                root.writeToStream(mos);
            }

            procB->setStateInformation(mb2.getData(), (int) mb2.getSize());

            // Read back B's state and check the fields
            juce::MemoryBlock mbOut;
            procB->getStateInformation(mbOut);
            auto rootOut = juce::ValueTree::readFromData(mbOut.getData(), mbOut.getSize());
            expect(rootOut.isValid());
            expectEquals(rootOut.getProperty("presetPath").toString(), juce::String("C:/Presets/Cool.milk"));
            expectEquals(rootOut.getProperty("playlistFolderPath").toString(), juce::String("D:/MilkPlaylists/Show1"));
        }
    }
};

static StateRoundTripTest stateRoundTripTest;