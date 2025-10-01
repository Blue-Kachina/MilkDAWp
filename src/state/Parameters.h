
#pragma once
#include <JuceHeader.h>

namespace om::milkdawp
{
    namespace ParamIDs
    {
        static constexpr const char* inputGain   = "inputGain";
        static constexpr const char* outputGain  = "outputGain";
        static constexpr const char* fullscreen  = "fullscreen";
        // New meaningful visual parameters
        static constexpr const char* seed        = "seed";           // integer seed for visuals
        static constexpr const char* ampScale    = "ampScale";       // amplitude multiplier for audio-reactive visuals
        static constexpr const char* colorHue    = "colorHue";       // 0..1
        static constexpr const char* colorSat    = "colorSat";       // 0..1
        static constexpr const char* speed       = "speed";          // playback/animation speed scale
        // Playlist state/controls
        static constexpr const char* playlistPresetIndex = "playlistPresetIndex"; // current item number within active playlist
        static constexpr const char* playlistPrev        = "playlistPrev";        // trigger-style: go to previous item
        static constexpr const char* playlistNext        = "playlistNext";        // trigger-style: go to next item
        // Auto-play transport controls
        static constexpr const char* autoPlay            = "autoPlay";           // enable auto advance when playlist active
        static constexpr const char* autoPlayRandom      = "autoPlayRandom";     // pick random next preset
        static constexpr const char* autoPlayHardCut     = "autoPlayHardCut";    // use hard cut when switching
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> params;
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::inputGain, "Input Gain",
            NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::outputGain, "Output Gain",
            NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::fullscreen, "Fullscreen", false));
        // Replace arbitrary sliders with meaningful controls
        params.push_back(std::make_unique<AudioParameterInt>(ParamIDs::seed, "Easter Egg",
            0, 1000000, 0));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::ampScale, "Amplitude",
            NormalisableRange<float>(0.0f, 4.0f, 0.001f), 1.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::colorHue, "Beat Sensitivity",
            NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::colorSat, "Mesh Size",
            NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 1.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::speed, "FPS Hint",
            NormalisableRange<float>(0.1f, 3.0f, 0.001f), 1.0f));
        // Host-automatable preset selector (index). We’ll map this to available presets at runtime.
        params.push_back(std::make_unique<AudioParameterInt>("presetIndex", "Preset",
            0, 1023, 0));
        // Host-automatable current playlist preset index (active only when a playlist is loaded)
        params.push_back(std::make_unique<AudioParameterInt>(ParamIDs::playlistPresetIndex, "Playlist Item",
            0, 1023, 0));
        // Momentary triggers for prev/next navigation (MIDI/automation mappable)
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::playlistPrev, "Playlist Prev", false));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::playlistNext, "Playlist Next", false));
        // Auto-play toggles
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::autoPlay, "Auto-play", false));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::autoPlayRandom, "Random", false));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::autoPlayHardCut, "Hard cut", false));
        return { params.begin(), params.end() };
    }
}
