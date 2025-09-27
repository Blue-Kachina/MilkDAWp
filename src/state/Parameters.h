
#pragma once
#include <JuceHeader.h>

namespace om::milkdawp
{
    namespace ParamIDs
    {
        static constexpr const char* inputGain   = "inputGain";
        static constexpr const char* outputGain  = "outputGain";
        static constexpr const char* showWindow  = "showWindow";
        static constexpr const char* fullscreen  = "fullscreen";
        // New meaningful visual parameters
        static constexpr const char* seed        = "seed";           // integer seed for visuals
        static constexpr const char* ampScale    = "ampScale";       // amplitude multiplier for audio-reactive visuals
        static constexpr const char* colorHue    = "colorHue";       // 0..1
        static constexpr const char* colorSat    = "colorSat";       // 0..1
        static constexpr const char* speed       = "speed";          // playback/animation speed scale
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> params;
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::inputGain, "Input Gain",
            NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::outputGain, "Output Gain",
            NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::showWindow, "Show Window", false));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::fullscreen, "Fullscreen", false));
        // Replace arbitrary sliders with meaningful controls
        params.push_back(std::make_unique<AudioParameterInt>(ParamIDs::seed, "Seed",
            0, 1000000, 0));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::ampScale, "Amplitude",
            NormalisableRange<float>(0.0f, 4.0f, 0.001f), 1.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::colorHue, "Hue",
            NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::colorSat, "Saturation",
            NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 1.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::speed, "Speed",
            NormalisableRange<float>(0.1f, 3.0f, 0.001f), 1.0f));
        // Host-automatable preset selector (index). We’ll map this to available presets at runtime.
        params.push_back(std::make_unique<AudioParameterInt>("presetIndex", "Preset",
            0, 1023, 0));
        return { params.begin(), params.end() };
    }
}
