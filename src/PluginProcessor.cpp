// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include "Version.h"
#include "Logging.h"
#include "BinaryData.h"
#include "AudioAnalysisQueue.h"
#include "VisualizationThread.h"
#include <cstdint>
#include <optional>
#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
#endif

namespace {
    std::unique_ptr<juce::Drawable> loadSvgFromBinary(const void* data, int size)
    {
        if (data == nullptr || size <= 0) return nullptr;
        auto svg = juce::parseXML(juce::String::fromUTF8(static_cast<const char*>(data), size));
        if (!svg) return nullptr;
        return std::unique_ptr<juce::Drawable>(juce::Drawable::createFromSVG(*svg));
    }

    void tintDrawable(juce::Drawable& d, juce::Colour colour)
    {
        // Replace common base colours with the desired tint; this affects both fills and strokes in SVGs
        d.replaceColour(juce::Colours::black, colour);
        d.replaceColour(juce::Colours::white, colour);
        d.replaceColour(juce::Colours::darkgrey, colour);
        d.replaceColour(juce::Colours::grey, colour);
    }

    std::unique_ptr<juce::Drawable> makeTintedClone(const juce::Drawable& src, juce::Colour colour)
    {
        auto clone = src.createCopy();
        if (clone)
        {
            tintDrawable(*clone, colour);
        }
        return clone;
    }
}

#if MILKDAWP_HAS_PROJECTM
// C API headers for projectM v4 (via vcpkg): convenience include
#include <projectM-4/projectM.h>
#endif

#if MILKDAWP_HAS_PROJECTM && defined(_WIN32)
namespace {
    // Runtime-resolved projectM API set to avoid linker delay-load crashes and allow both debug/release DLL names.
    typedef projectm_handle (__cdecl *PFN_PM_CREATE)();
    typedef void (__cdecl *PFN_PM_DESTROY)(projectm_handle);
    typedef void (__cdecl *PFN_PM_SET_WINDOW_SIZE)(projectm_handle, size_t, size_t);
    typedef void (__cdecl *PFN_PM_SET_FPS)(projectm_handle, int);
    typedef void (__cdecl *PFN_PM_SET_ASPECT)(projectm_handle, bool);
    typedef void (__cdecl *PFN_PM_LOAD_PRESET_FILE)(projectm_handle, const char*, bool);
    typedef void (__cdecl *PFN_PM_OPENGL_RENDER_FRAME)(projectm_handle);

    static HMODULE g_pmModule = nullptr;
    static PFN_PM_CREATE                g_pm_create = nullptr;
    static PFN_PM_DESTROY               g_pm_destroy = nullptr;
    static PFN_PM_SET_WINDOW_SIZE       g_pm_set_window_size = nullptr;
    static PFN_PM_SET_FPS               g_pm_set_fps = nullptr;
    static PFN_PM_SET_ASPECT            g_pm_set_aspect = nullptr;
    static PFN_PM_LOAD_PRESET_FILE      g_pm_load_preset_file = nullptr;
    static PFN_PM_OPENGL_RENDER_FRAME   g_pm_opengl_render_frame = nullptr;
}
#endif

// Local anchor to resolve this module's HMODULE at runtime (Windows)
#ifdef _WIN32
extern "C" void mdw_module_anchor() {}
#endif

class MilkDAWpAudioProcessor : public juce::AudioProcessor, public juce::AudioProcessorValueTreeState::Listener {
public:
    void ensureVizThreadStartedForUI() {
    #if !defined(MILKDAWP_ENABLE_VIZ_THREAD)
    #define MILKDAWP_ENABLE_VIZ_THREAD 1
    #endif
    #if MILKDAWP_ENABLE_VIZ_THREAD
        if (!vizThread)
            vizThread = std::make_unique<milkdawp::VisualizationThread>(analysisQueue);
        vizThread->start();
        // Make sure visualization thread has latest params and preset
        sendAllParamsToViz();
        if (currentPresetPath.isNotEmpty())
            vizThread->postLoadPreset(currentPresetPath);
    #endif
    }
public:
    struct AutoAdvanceTimer : juce::Timer {
        MilkDAWpAudioProcessor& proc;
        explicit AutoAdvanceTimer(MilkDAWpAudioProcessor& p) : proc(p) {}
        void timerCallback() override { proc.onAutoAdvanceTimer(); }
    };
public:
    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS& getValueTreeState() noexcept { return apvts; }
    milkdawp::VisualizationThread* getVizThread() noexcept { return vizThread.get(); }
    juce::String getCurrentPresetPath() const noexcept { return currentPresetPath; }
    void setCurrentPresetPathAndPostLoad(const juce::String& path)
    {
        if (path == currentPresetPath)
            return; // no change
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
       #if MILKDAWP_HAS_PROJECTM
        MDW_LOG_INFO("projectM compiled: ON");
       #else
        MDW_LOG_INFO("projectM compiled: OFF");
       #endif

        // Log active build configuration
       #if defined(_DEBUG)
        MDW_LOG_INFO("Build config: Debug");
       #else
        MDW_LOG_INFO("Build config: Release");
       #endif

       #ifdef _WIN32
        // Log absolute path of the loaded plugin module to disambiguate scanned copies
        {
            HMODULE hSelf = nullptr;
            wchar_t modulePath[MAX_PATH] = {0};
           #if MILKDAWP_HAS_PROJECTM
            // Anchor symbol is defined near projectM include guard region for a stable address in this module
           #else
            // Even without projectM, the anchor is still available due to unconditional compilation unit inclusion
           #endif
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCWSTR)&mdw_module_anchor, &hSelf))
            {
                DWORD len = GetModuleFileNameW(hSelf, modulePath, MAX_PATH);
                if (len > 0 && len < MAX_PATH) {
                    MDW_LOG_INFO(juce::String("Plugin module path: ") + juce::String((juce::CharPointer_UTF16)modulePath));
                }
            }

           #if MILKDAWP_HAS_PROJECTM
            // Check if a projectM DLL is already loaded (helps distinguish PATH vs local resolution)
            HMODULE hPM = GetModuleHandleW(L"projectM-4d.dll");
            if (!hPM) hPM = GetModuleHandleW(L"projectM-4.dll");
            if (hPM)
                MDW_LOG_INFO("projectM DLL presence: already loaded (GetModuleHandle succeeded)");
            else
                MDW_LOG_INFO("projectM DLL presence: not loaded yet (GetModuleHandle failed)");
           #endif
        }
       #endif
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
        // If a preset path was already selected (e.g., user loaded before audio started or restored state),
        // post it now so the viz thread applies it immediately.
        if (currentPresetPath.isNotEmpty())
            vizThread->postLoadPreset(currentPresetPath);
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

        // Feed raw PCM to visualization path (for GL thread/projectM)
       #if MILKDAWP_ENABLE_VIZ_THREAD
        if (vizThread)
        {
            std::vector<float> interleaved;
            interleaved.resize((size_t)N * 2);
            if (numInCh == 0) {
                std::fill(interleaved.begin(), interleaved.end(), 0.0f);
            } else if (numInCh == 1) {
                for (int n = 0; n < N; ++n) {
                    const float s = in0[n];
                    interleaved[(size_t)n * 2 + 0] = s;
                    interleaved[(size_t)n * 2 + 1] = s;
                }
            } else {
                for (int n = 0; n < N; ++n) {
                    interleaved[(size_t)n * 2 + 0] = in0[n];
                    interleaved[(size_t)n * 2 + 1] = in1[n];
                }
            }
            const double sr = getSampleRate();
            vizThread->postAudioBlockInterleaved(interleaved.data(), N, sr > 0.0 ? sr : 44100.0);
        }
       #endif

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
    // Phase 6.2: expose playlist for UI combobox
    int getPlaylistSizePublic() const noexcept { return playlistOrder.size(); }
    juce::String getPlaylistItemNameAtOrderedPublic(int orderedIndex) const {
        if ((unsigned)orderedIndex < (unsigned)playlistOrder.size()) {
            const int idx = playlistOrder[(int)orderedIndex];
            if ((unsigned)idx < (unsigned)playlistFiles.size())
                return playlistFiles[(int)idx].getFileNameWithoutExtension();
        }
        return {};
    }
    int getPlaylistPosPublic() const noexcept { return hasActivePlaylist() ? playlistPos : -1; }

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

            // Phase 6.3: Handle preset blacklist/filtering via optional ignore file in folder
            // Supported filenames: ".milkignore" or ".milkdrop-ignore.txt" with one pattern or filename per line
            juce::StringArray ignoreEntries;
            auto ignoreA = dir.getChildFile(".milkignore");
            auto ignoreB = dir.getChildFile(".milkdrop-ignore.txt");
            auto readIgnore = [&](const juce::File& f){
                if (f.existsAsFile()) {
                    juce::String text = f.loadFileAsString();
                    juce::StringArray lines;
                    lines.addLines(text);
                    for (auto& ln : lines) {
                        auto t = ln.trim();
                        if (t.isEmpty()) continue;
                        if (t.startsWithIgnoreCase("#")) continue;
                        ignoreEntries.addIfNotAlreadyThere(t.toLowerCase());
                    }
                }
            };
            readIgnore(ignoreA);
            readIgnore(ignoreB);

            // Apply filtering: entries can be exact filename matches or wildcard substrings
            if (!ignoreEntries.isEmpty()) {
                juce::Array<juce::File> filtered;
                for (auto& f : found) {
                    const auto name = f.getFileName().toLowerCase();
                    bool skip = false;
                    for (auto& pat : ignoreEntries) {
                        if (pat.containsChar('*') || pat.containsChar('?')) {
                            if (juce::String(name).matchesWildcard(pat, true)) { skip = true; break; }
                        } else if (name == pat || name.contains(pat)) { // allow substring match
                            skip = true; break;
                        }
                    }
                    if (!skip) filtered.add(f);
                }
                found.swapWith(filtered);
            }

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
    // A top-level window to host the visualization when detached.
    class ExternalVisualizationWindow : public juce::DocumentWindow { 
    public:
        struct Content : public juce::Component {
            std::unique_ptr<juce::TooltipWindow> tooltipWindow;
            juce::TextButton dockButton { "Dock" };
            juce::TextButton hoverFsButton { "FS" }; // Hover fullscreen toggle
            juce::TextButton prevButton { "Prev" };
            juce::TextButton nextButton { "Next" };
            juce::Label presetLabel;
            juce::Component* attachedCanvas { nullptr };
            std::function<void()> onToggleFullscreen;
            std::function<void()> onPrev;
            std::function<void()> onNext;
            Content()
            {
                setOpaque(false);
                // Create a TooltipWindow for this top-level window so tooltips appear over its children
                tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);
                addAndMakeVisible(dockButton);
                // Transport + preset name
                addAndMakeVisible(prevButton);
                addAndMakeVisible(nextButton);
                addAndMakeVisible(presetLabel);
                presetLabel.setText("(no preset)", juce::dontSendNotification);
                presetLabel.setJustificationType(juce::Justification::centredLeft);
                presetLabel.setColour(juce::Label::textColourId, juce::Colours::white);
                prevButton.setWantsKeyboardFocus(false);
                nextButton.setWantsKeyboardFocus(false);
                prevButton.onClick = [this]{ if (onPrev) onPrev(); };
                nextButton.onClick = [this]{ if (onNext) onNext(); };
                // Hover Fullscreen button (hidden until mouse over)
                addAndMakeVisible(hoverFsButton);
                hoverFsButton.setTooltip("Toggle Fullscreen (F11)");
                hoverFsButton.setAlpha(0.75f);
                hoverFsButton.setVisible(false);
                hoverFsButton.setWantsKeyboardFocus(false);
                hoverFsButton.onClick = [this]{ if (onToggleFullscreen) onToggleFullscreen(); };
            }
            void attachCanvas(juce::Component* c)
            {
                if (attachedCanvas == c) return;
                if (attachedCanvas != nullptr)
                    removeChildComponent(attachedCanvas);
                attachedCanvas = c;
                if (attachedCanvas != nullptr)
                    addAndMakeVisible(attachedCanvas);
                // Keep hover button above canvas
                hoverFsButton.toFront(true);
                resized();
            }
            juce::Component* detachCanvas()
            {
                auto* c = attachedCanvas;
                if (c != nullptr)
                {
                    removeChildComponent(c);
                    attachedCanvas = nullptr;
                }
                return c;
            }
            void setPresetName(const juce::String& name)
            {
                presetLabel.setText(name, juce::dontSendNotification);
            }
            void setTransportEnabled(bool enabled)
            {
                prevButton.setEnabled(enabled);
                nextButton.setEnabled(enabled);
                prevButton.setVisible(true);
                nextButton.setVisible(true);
            }
            void mouseEnter(const juce::MouseEvent&) override { hoverFsButton.setVisible(true); hoverFsButton.toFront(true); }
            void mouseExit(const juce::MouseEvent&) override { hoverFsButton.setVisible(false); }
            void resized() override
            {
                auto r = getLocalBounds();
                auto top = r.removeFromTop(36);
                // Right: dock
                auto dockArea = top.removeFromRight(90).reduced(6);
                dockButton.setBounds(dockArea);
                // Left: transport + preset name
                int prevW = juce::jmin(70, top.getWidth());
                prevButton.setBounds(top.removeFromLeft(prevW));
                if (top.getWidth() > 0) top.removeFromLeft(6);
                int nextW = juce::jmin(70, top.getWidth());
                nextButton.setBounds(top.removeFromLeft(nextW));
                if (top.getWidth() > 0) top.removeFromLeft(8);
                presetLabel.setBounds(top);
                if (attachedCanvas != nullptr)
                    attachedCanvas->setBounds(r);
                // Place hover button at bottom-right with margin
                const int btnW = 28, btnH = 24, margin = 8;
                juce::Rectangle<int> br = getLocalBounds().removeFromBottom(btnH + margin).removeFromRight(btnW + margin);
                hoverFsButton.setBounds({ br.getRight() - btnW, br.getBottom() - btnH, btnW, btnH });
                hoverFsButton.toFront(true);
            }
        };

        ExternalVisualizationWindow(const juce::String& name, std::function<void()> onDockCb, std::function<void()> onToggleFs)
            : juce::DocumentWindow(name, juce::Colours::black, DocumentWindow::allButtons), onDock(std::move(onDockCb)), onToggleFullscreen(std::move(onToggleFs))
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setOpaque(false);
            setBackgroundColour(juce::Colours::transparentBlack);
            content = std::make_unique<Content>();
            content->dockButton.onClick = [this]{ if (onDock) onDock(); };
            content->onToggleFullscreen = onToggleFullscreen;
            setContentOwned(content.get(), false); // do not transfer ownership to window, we keep unique_ptr here
            centreWithSize(960, 540);
            setVisible(true);
        }
        ~ExternalVisualizationWindow() override
        {
            // Ensure canvas is detached to avoid accidental deletion
            if (content)
                content->detachCanvas();
        }
        void closeButtonPressed() override
        {
            if (onDock) onDock();
        }
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::F11Key) { if (onToggleFullscreen) onToggleFullscreen(); return true; }
            return juce::DocumentWindow::keyPressed(key);
        }
        Content* getContent() const { return content.get(); }
    private:
        std::function<void()> onDock;
        std::function<void()> onToggleFullscreen;
        std::unique_ptr<Content> content;
    };
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
        // Reduce font size in dropdown lists (PopupMenu used by ComboBox)
        juce::Font getPopupMenuFont() override { return juce::Font(12.0f); }
        // Also reduce the font used in the closed ComboBox text for consistency
        juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(12.0f); }
        
        // Make popup menu items more compact by reducing ideal height
        void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                       int& idealWidth, int& idealHeight) override
        {
            auto font = getPopupMenuFont();
            if (isSeparator)
            {
                idealHeight = juce::roundToInt(font.getHeight() * 0.6f);
                idealWidth = 50;
                return;
            }
            idealHeight = juce::jmax(juce::roundToInt(font.getHeight() + 4.0f), 14);
            idealWidth = juce::roundToInt(font.getStringWidthFloat(text) + idealHeight + 16.0f);
            juce::ignoreUnused(standardMenuItemHeight);
        }

        // Custom, smaller checkmark and tighter item rendering
        void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                               bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                               const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override
        {
            auto r = area;
            auto bg = findColour(juce::ResizableWindow::backgroundColourId);
            auto hi = juce::Colour(0xFF2A2E33);
            if (isSeparator)
            {
                g.setColour(bg.brighter(0.1f));
                auto y = r.getCentreY();
                g.drawLine((float)r.getX() + 4.0f, (float)y, (float)r.getRight() - 4.0f, (float)y);
                return;
            }
            if (isHighlighted)
            {
                g.setColour(hi);
                g.fillRect(r);
            }

            auto font = getPopupMenuFont();
            g.setFont(font);
            auto col = textColour != nullptr ? *textColour : findColour(juce::ComboBox::textColourId);
            g.setColour(isActive ? col : col.withAlpha(0.4f));

            // Left gutter for tick/icon
            const int gutter = juce::roundToInt(font.getHeight() * 1.0f);
            auto textArea = r.reduced(6, juce::jmax(0, (r.getHeight() - juce::roundToInt(font.getHeight())) / 2));
            textArea.removeFromLeft(gutter);

            // Draw small tick if selected
            if (isTicked)
            {
                const int t = juce::roundToInt(font.getHeight() * 0.75f);
                const int tx = r.getX() + 6;
                const int ty = r.getCentreY() - t / 2;
                juce::Path check;
                // simple tick
                check.startNewSubPath((float)tx, (float)(ty + t * 0.55f));
                check.lineTo((float)(tx + t * 0.35f), (float)(ty + t));
                check.lineTo((float)(tx + t), (float)(ty));
                g.setColour(juce::Colours::white);
                g.strokePath(check, juce::PathStrokeType(2.0f));
            }

            // Icon if provided (scaled small)
            if (icon != nullptr)
            {
                const int sz = juce::roundToInt(font.getHeight());
                auto iconBounds = juce::Rectangle<int>(r.getX() + 4, r.getCentreY() - sz / 2, sz, sz);
                icon->draw(g, isActive ? 1.0f : 0.4f, juce::AffineTransform::fromTargetPoints(0,0,(float)iconBounds.getX(),(float)iconBounds.getY(), 1,0,(float)iconBounds.getRight(),(float)iconBounds.getY(), 0,1,(float)iconBounds.getX(),(float)iconBounds.getBottom()));
            }

            // Text and optional shortcut key
            auto right = textArea;
            if (shortcutKeyText.isNotEmpty())
            {
                auto w = g.getCurrentFont().getStringWidth(shortcutKeyText);
                auto scArea = right.removeFromRight(w + 10);
                g.setColour(isActive ? col.withAlpha(0.8f) : col.withAlpha(0.3f));
                g.drawText(shortcutKeyText, scArea, juce::Justification::centredRight, false);
            }
            g.setColour(isActive ? col : col.withAlpha(0.4f));
            g.drawFittedText(text, right, juce::Justification::centredLeft, 1);

            // Submenu arrow
            if (hasSubMenu)
            {
                const int sz = juce::roundToInt(font.getHeight() * 0.6f);
                juce::Path arrow;
                const int x = r.getRight() - sz - 6;
                const int y = r.getCentreY() - sz / 2;
                arrow.startNewSubPath((float)x, (float)y);
                arrow.lineTo((float)(x + sz), (float)(y + sz / 2));
                arrow.lineTo((float)x, (float)(y + sz));
                g.setColour(col);
                g.strokePath(arrow, juce::PathStrokeType(2.0f));
            }
        }
    };
public:
    static constexpr int topHeight = 80;
    explicit MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor& proc)
        : juce::AudioProcessorEditor(&proc), processor(proc)
    {
        setLookAndFeel(&hardwareLAF);
        setSize(1200, 650); // per README default window size
        // Ensure tooltips are enabled for this editor by creating a TooltipWindow attached to it
        tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);

        // Logo: prefer image from resources/images/MilkDAWp_Logo.png; fallback to text label
        addAndMakeVisible(logoLabel);
        logoLabel.setText("MilkDAWp", juce::dontSendNotification);
        logoLabel.setFont(juce::Font(24.0f, juce::Font::bold));
        logoLabel.setJustificationType(juce::Justification::centredLeft);
        
        // Try embedded asset first (preferred to avoid filesystem dependencies)
        if (!logoLoaded) {
            if (BinaryData::MilkDAWp_Logo_pngSize > 0) {
                juce::MemoryInputStream mis(BinaryData::MilkDAWp_Logo_png, BinaryData::MilkDAWp_Logo_pngSize, false);
                auto img = juce::ImageFileFormat::loadFrom(mis);
                if (img.isValid()) {
                    logoImageData = img;
                    logoImage.setImage(logoImageData);
                    logoImage.setImagePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::stretchToFit);
                    logoImage.setInterceptsMouseClicks(false, false);
                    addAndMakeVisible(logoImage);
                    logoImage.setVisible(true);
                    logoLoaded = true;
                    logoLabel.setVisible(false);
                    MDW_LOG_INFO(juce::String("Logo loaded (embedded): ") + juce::String(logoImageData.getWidth()) + "x" + juce::String(logoImageData.getHeight()));
                    // Ensure layout updates now that the logo is available
                    resized();
                    logoImage.toFront(false);
                    repaint();
                }
            }
        }

        // Try to locate the logo image near the plugin binary first (preferred),
        // then fall back to legacy resources/images for backward compatibility.
        {
            auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            auto dir = appFile.getParentDirectory();
            bool found = false;
            for (int i = 0; i < 7 && dir.exists() && !found; ++i)
            {
                juce::Array<juce::File> candidates;
                candidates.add(dir.getChildFile("MilkDAWp_Logo.png")); // preferred new location
                candidates.add(dir.getChildFile("resources").getChildFile("images").getChildFile("MilkDAWp_Logo.png")); // legacy path
                for (auto& candidate : candidates)
                {
                    if (!candidate.existsAsFile()) continue;
                    std::unique_ptr<juce::FileInputStream> stream(candidate.createInputStream());
                    if (stream == nullptr) continue;
                    auto img = juce::ImageFileFormat::loadFrom(*stream);
                    if (!img.isValid()) continue;
                    logoImageData = img;
                    logoImage.setImage(logoImageData);
                    // Ensure the image scales to our bounds, avoiding centre-crop invisibility
                    logoImage.setImagePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::stretchToFit);
                    logoImage.setInterceptsMouseClicks(false, false);
                    addAndMakeVisible(logoImage);
                    logoImage.setVisible(true);
                    logoLoaded = true;
                    logoLabel.setVisible(false);
                    MDW_LOG_INFO(juce::String("Logo loaded: ") + candidate.getFullPathName() + 
                                 juce::String(" (") + juce::String(logoImageData.getWidth()) + "x" + juce::String(logoImageData.getHeight()) + ")");
                    // Ensure layout updates now that the logo is available
                    resized();
                    logoImage.toFront(false);
                    repaint();
                    found = true;
                    break;
                }
                dir = dir.getParentDirectory();
            }
            if (!found)
            {
                MDW_LOG_INFO("Logo not found near plugin binary; using text label fallback");
            }
        }

        // Pop-out support
        addAndMakeVisible(popOutButton);
        popOutButton.setTooltip("Detach visualization to an external window");
        popOutButton.onClick = [this]
        {
            if (!isDetached)
            {
                // Create external window and move canvas into it
                externalWindow = std::make_unique<ExternalVisualizationWindow>("MilkDAWp Visualization (OBS)", [this]{ this->dockCanvas(); }, [this]{ this->toggleFullscreen(); });
                // Transfer canvas: remove from editor to avoid double-parenting
                removeChildComponent(&vizCanvas);
                if (auto* content = externalWindow->getContent()) {
                    content->attachCanvas(&vizCanvas);
                    // Wire window communication callbacks
                    content->onPrev = [this]{ this->processor.prevPresetInPlaylist(); this->presetNameLabel.setText(this->currentDisplayName(), juce::dontSendNotification); if (this->externalWindow) { if (auto* ec = this->externalWindow->getContent()) { ec->setPresetName(this->currentDisplayName()); ec->setTransportEnabled(this->processor.hasActivePlaylistPublic()); } } };
                    content->onNext = [this]{ this->processor.nextPresetInPlaylist(); this->presetNameLabel.setText(this->currentDisplayName(), juce::dontSendNotification); if (this->externalWindow) { if (auto* ec = this->externalWindow->getContent()) { ec->setPresetName(this->currentDisplayName()); ec->setTransportEnabled(this->processor.hasActivePlaylistPublic()); } } };
                    // Initial UI state
                    content->setPresetName(this->currentDisplayName());
                    content->setTransportEnabled(this->processor.hasActivePlaylistPublic());
                }
                isDetached = true;
                // Show notice in main editor
                detachedNotice.setText("Visualization is detached. Click 'Dock' in the external window to reattach.", juce::dontSendNotification);
                detachedNotice.setJustificationType(juce::Justification::centred);
                detachedNotice.setColour(juce::Label::textColourId, juce::Colours::white);
                addAndMakeVisible(detachedNotice);
                resized();
            }
        };

        // Fullscreen support
        addAndMakeVisible(fullscreenButton);
        fullscreenButton.setTooltip("Toggle fullscreen visualization (F11)");
        fullscreenButton.onClick = [this]{ this->toggleFullscreen(); };
 
         // Visualization canvas (embedded OpenGL)
         addAndMakeVisible(vizCanvas);
        vizCanvas.setOwner(&processor);
        // Ensure visualization thread runs even when host hasn't prepared audio yet
        processor.ensureVizThreadStartedForUI();

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

        // Phase 6.4: Icon-based toggle buttons for Lock and Shuffle (Phosphor)
        // Lock button
        addAndMakeVisible(lockButton);
        lockButton.setTooltip("Lock");
        lockButton.setClickingTogglesState(true);
        {
            auto lockSvg = loadSvgFromBinary(BinaryData::locksimple_svg, BinaryData::locksimple_svgSize);
            const auto GreyN = juce::Colour(0xFF7F7F7F);
            const auto GreyO = juce::Colour(0xFF9F9F9F);
            const auto GreyD = juce::Colour(0xFF5F5F5F);
            const auto BlueN = juce::Colour(0xFF6A8CAF);
            const auto BlueO = juce::Colour(0xFF8FB3D1);
            const auto BlueD = juce::Colour(0xFF4E6E8C);

            if (lockSvg == nullptr)
            {
                // Fallback simple path if asset missing
                auto makeFallback = [](juce::Colour c)
                {
                    auto dp = std::make_unique<juce::DrawablePath>();
                    juce::Path p;
                    p.addRoundedRectangle(4.0f, 9.0f, 18.0f, 12.0f, 3.0f);
                    juce::Path sh; sh.addArc(6.0f, 3.0f, 14.0f, 12.0f, juce::MathConstants<float>::pi, 0.0f);
                    p.addPath(sh); dp->setPath(p); dp->setFill(c); return dp;
                };
                auto off  = makeFallback(GreyN);
                auto over = makeFallback(GreyO);
                auto down = makeFallback(GreyD);
                auto onN  = makeFallback(BlueN);
                auto onO  = makeFallback(BlueO);
                auto onD  = makeFallback(BlueD);
                lockButton.setImages(off.get(), over.get(), down.get(), onN.get(), onO.get(), onD.get(), nullptr);
            }
            else
            {
                auto off  = makeTintedClone(*lockSvg, GreyN);
                auto over = makeTintedClone(*lockSvg, GreyO);
                auto down = makeTintedClone(*lockSvg, GreyD);
                auto onN  = makeTintedClone(*lockSvg, BlueN);
                auto onO  = makeTintedClone(*lockSvg, BlueO);
                auto onD  = makeTintedClone(*lockSvg, BlueD);
                lockButton.setImages(off.get(), over.get(), down.get(), onN.get(), onO.get(), onD.get(), nullptr);
            }
        }
        lockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.getValueTreeState(), "lockCurrentPreset", lockButton);

        // Shuffle button
        addAndMakeVisible(shuffleButton);
        shuffleButton.setTooltip("Shuffle");
        shuffleButton.setClickingTogglesState(true);
        {
            auto shuffleSvg = loadSvgFromBinary(BinaryData::shuffle_svg, BinaryData::shuffle_svgSize);
            const auto GreyN = juce::Colour(0xFF7F7F7F);
            const auto GreyO = juce::Colour(0xFF9F9F9F);
            const auto GreyD = juce::Colour(0xFF5F5F5F);
            const auto BlueN = juce::Colour(0xFF6A8CAF);
            const auto BlueO = juce::Colour(0xFF8FB3D1);
            const auto BlueD = juce::Colour(0xFF4E6E8C);

            if (shuffleSvg == nullptr)
            {
                auto makeFallback = [](juce::Colour c)
                {
                    auto dp = std::make_unique<juce::DrawablePath>();
                    juce::Path p;
                    p.startNewSubPath(4.0f, 8.0f); p.cubicTo(10.0f, 8.0f, 12.0f, 14.0f, 18.0f, 14.0f);
                    p.lineTo(16.0f, 12.0f); p.lineTo(20.0f, 14.0f); p.lineTo(16.0f, 16.0f);
                    p.startNewSubPath(4.0f, 16.0f); p.cubicTo(10.0f, 16.0f, 12.0f, 10.0f, 18.0f, 10.0f);
                    p.lineTo(16.0f, 8.0f); p.lineTo(20.0f, 10.0f); p.lineTo(16.0f, 12.0f);
                    dp->setPath(p); dp->setFill(c); return dp;
                };
                auto off  = makeFallback(GreyN);
                auto over = makeFallback(GreyO);
                auto down = makeFallback(GreyD);
                auto onN  = makeFallback(BlueN);
                auto onO  = makeFallback(BlueO);
                auto onD  = makeFallback(BlueD);
                shuffleButton.setImages(off.get(), over.get(), down.get(), onN.get(), onO.get(), onD.get(), nullptr);
            }
            else
            {
                auto off  = makeTintedClone(*shuffleSvg, GreyN);
                auto over = makeTintedClone(*shuffleSvg, GreyO);
                auto down = makeTintedClone(*shuffleSvg, GreyD);
                auto onN  = makeTintedClone(*shuffleSvg, BlueN);
                auto onO  = makeTintedClone(*shuffleSvg, BlueO);
                auto onD  = makeTintedClone(*shuffleSvg, BlueD);
                shuffleButton.setImages(off.get(), over.get(), down.get(), onN.get(), onO.get(), onD.get(), nullptr);
            }
        }
        shuffleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.getValueTreeState(), "shuffle", shuffleButton);

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
                // Ensure viz thread is running so the preset takes effect immediately
                processor.ensureVizThreadStartedForUI();
                // Switch to single-preset mode: clear any active playlist and stop transport
                processor.clearPlaylistPublic();
                presetNameLabel.setText(f.getFileNameWithoutExtension(), juce::dontSendNotification);
                refreshTransportVisibility();
            });
        };

        addAndMakeVisible(loadFolderButton);
        loadFolderButton.setButtonText("Load Playlist Folder...");
        loadFolderButton.setVisible(false); // Phase 6.3: replaced by compact picker button
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

        // Phase 6.2: Preset selection ComboBox replacing "Load Preset..." button
        addAndMakeVisible(presetCombo);
        presetCombo.setTextWhenNoChoicesAvailable("No playlist");
        presetCombo.setTextWhenNothingSelected("Select preset");
        presetCombo.onChange = [this]
        {
            if (updatingPresetCombo) return; // avoid feedback
            if (!processor.hasActivePlaylistPublic()) return;
            const int sel = presetCombo.getSelectedItemIndex(); // 0-based
            if (sel < 0) return;
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(processor.getValueTreeState().getParameter("presetIndex"))) {
                const float norm = rp->convertTo0to1((float) sel);
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(norm);
                rp->endChangeGesture();
            }
        };
        // Hide old button per Phase 6.2
        loadButton.setVisible(false);
        // Phase 6.3: Compact playlist/preset picker icon button
        {
            addAndMakeVisible(playlistPickerButton);
            playlistPickerButton.setTooltip("Load Preset");
            // Build a simple folder drawable icon
            auto makeFolder = [](juce::Colour c){
                auto dp = std::make_unique<juce::DrawablePath>();
                juce::Path p;
                p.startNewSubPath(2, 8);
                p.lineTo(10, 8);
                p.lineTo(12, 4);
                p.lineTo(22, 4);
                p.lineTo(22, 20);
                p.lineTo(2, 20);
                p.closeSubPath();
                dp->setPath(p);
                dp->setFill(c);
                return dp;
            };
            auto normal = makeFolder(juce::Colours::white);
            auto over = makeFolder(juce::Colours::white);
            auto down = makeFolder(juce::Colours::white);
            playlistPickerButton.setImages(normal.get(), over.get(), down.get(), nullptr, nullptr, nullptr, nullptr);
            // Keep ownership of drawables via unique_ptrs stored in the button
            playlistPickerButton.setClickingTogglesState(false);
            playlistPickerButton.onClick = [this]
            {
                fileChooser = std::make_unique<juce::FileChooser>("Select a MilkDrop preset", juce::File(), "*.milk");
                auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
                fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    fileChooser.reset();
                    if (! f.existsAsFile()) return;
                    if (f.getFileExtension().toLowerCase() != ".milk") {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Preset Picker", "Please select a .milk preset file.");
                        return;
                    }
                    auto parent = f.getParentDirectory();
                    if (!parent.isDirectory()) return;
                    // Set playlist from parent folder
                    processor.setPlaylistFolderAndScanPublic(parent.getFullPathName());
                    // Ensure viz thread is running so the preset takes effect immediately
                    processor.ensureVizThreadStartedForUI();
                    // Find ordered index of the selected preset by name and set parameter
                    const auto targetName = f.getFileNameWithoutExtension();
                    const int N = processor.getPlaylistSizePublic();
                    int foundIdx = -1;
                    for (int i = 0; i < N; ++i) {
                        if (processor.getPlaylistItemNameAtOrderedPublic(i).equalsIgnoreCase(targetName)) { foundIdx = i; break; }
                    }
                    if (foundIdx >= 0) {
                        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(processor.getValueTreeState().getParameter("presetIndex"))) {
                            const float norm = rp->convertTo0to1((float) foundIdx);
                            rp->beginChangeGesture();
                            rp->setValueNotifyingHost(norm);
                            rp->endChangeGesture();
                        }
                    } else {
                        // If not found (filtered out?), just load directly and clear playlist
                        processor.setCurrentPresetPathAndPostLoad(f.getFullPathName());
                        processor.clearPlaylistPublic();
                    }
                    // Update UI bits
                    presetNameLabel.setText(currentDisplayName(), juce::dontSendNotification);
                    refreshTransportVisibility();
                });
            };
        }
        // Initial population
        {
            const bool havePL = processor.hasActivePlaylistPublic();
            presetCombo.setEnabled(havePL);
            presetCombo.clear(juce::dontSendNotification);
            if (havePL) {
                const int N = processor.getPlaylistSizePublic();
                for (int i = 0; i < N; ++i) {
                    auto name = processor.getPlaylistItemNameAtOrderedPublic(i);
                    presetCombo.addItem(juce::String(i + 1) + ". " + name, i + 1);
                }
                const int pos = processor.getPlaylistPosPublic();
                updatingPresetCombo = true;
                presetCombo.setSelectedItemIndex(pos >= 0 ? pos : 0, juce::dontSendNotification);
                updatingPresetCombo = false;
                lastKnownPlaylistSize = N;
            } else {
                lastKnownPlaylistSize = 0;
            }
        }

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
        presetNameLabel.setMinimumHorizontalScale(0.5f);

        refreshTransportVisibility();
        lastDisplayedName = currentDisplayName();
        startTimerHz(10);
        if (logoLoaded)
            logoImage.toFront(false);
    }

    ~MilkDAWpAudioProcessorEditor() override {
        // Ensure external window is closed and canvas is owned by editor
        if (isDetached)
            dockCanvas();
        setLookAndFeel(nullptr);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getKeyCode() == juce::KeyPress::F11Key) { toggleFullscreen(); return true; }
        return false;
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

    void paintOverChildren(juce::Graphics& g) override {
        // As a safety net, draw the logo on top of all children too
        if (logoLoaded && logoImageData.isValid() && logoTargetBounds.getWidth() > 0 && logoTargetBounds.getHeight() > 0) {
            g.drawImage(logoImageData, logoTargetBounds.toFloat());
        }
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto top = bounds.removeFromTop(topHeight);
        auto innerTop = top.reduced(16);

        // Left logo
        if (logoLoaded && logoImageData.isValid())
        {
            // Scale to ~50% of original while preserving aspect ratio, and vertically center in the top strip
            const int maxH = innerTop.getHeight();
            const int desiredH = juce::jmin(maxH, (int) juce::roundToInt(logoImageData.getHeight() * 0.5f));
            const double scale = desiredH > 0 ? (desiredH / (double) logoImageData.getHeight()) : 0.5;
            const int desiredW = (int) juce::roundToInt(logoImageData.getWidth() * scale);
            auto logoArea = innerTop.removeFromLeft(desiredW);
            // add a small right padding after the logo
            innerTop.removeFromLeft(12);
            // vertically center the image inside the allocated height
            int y = logoArea.getY() + (logoArea.getHeight() - desiredH) / 2;
            logoImage.setBounds(logoArea.getX(), y, desiredW, desiredH);
            logoTargetBounds = juce::Rectangle<int>(logoArea.getX(), y, desiredW, desiredH);
        }
        else
        {
            logoLabel.setBounds(innerTop.removeFromLeft(220));
            innerTop.removeFromLeft(12);
        }

        // File/picker controls
        presetCombo.setBounds(innerTop.removeFromLeft(250));
        innerTop.removeFromLeft(8);
        {
            auto b = innerTop.removeFromLeft(28);
            const int sz = juce::jmin(b.getHeight(), 24);
            // vertically center square button
            playlistPickerButton.setBounds(b.getX(), b.getCentreY() - sz/2, sz, sz);
        }
        innerTop.removeFromLeft(8);
        // Phase 6.4: place Lock and Shuffle icon buttons next to picker
        {
            auto b1 = innerTop.removeFromLeft(28);
            const int sz = juce::jmin(b1.getHeight(), 24);
            lockButton.setBounds(b1.getX(), b1.getCentreY() - sz/2, sz, sz);
        }
        innerTop.removeFromLeft(4);
        {
            auto b2 = innerTop.removeFromLeft(28);
            const int sz = juce::jmin(b2.getHeight(), 24);
            shuffleButton.setBounds(b2.getX(), b2.getCentreY() - sz/2, sz, sz);
        }
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

        // (Phase 6.4) Toggle buttons moved next to playlist picker

        // Transition Style controls
        transitionStyleLabel.setBounds(innerTop.removeFromLeft(100));
        innerTop.removeFromLeft(6);
        transitionStyleCombo.setBounds(innerTop.removeFromLeft(140));
        innerTop.removeFromLeft(12);

        // Prioritize Fullscreen and Pop-out visibility: allocate them first from the right
        const int transportWidth = 160;
        const int btnWidth = 96;
        const int gapPx = 8;

        // Fullscreen at far right
        if (innerTop.getWidth() > 0) {
            auto fsArea = innerTop.removeFromRight(juce::jmin(btnWidth, innerTop.getWidth()));
            fullscreenButton.setBounds(fsArea);
        }
        // Gap
        if (innerTop.getWidth() > 0)
            innerTop.removeFromRight(juce::jmin(gapPx, innerTop.getWidth()));
        // Pop-out next
        if (innerTop.getWidth() > 0) {
            auto popArea = innerTop.removeFromRight(juce::jmin(btnWidth, innerTop.getWidth()));
            popOutButton.setBounds(popArea);
        }
        // Gap
        if (innerTop.getWidth() > 0)
            innerTop.removeFromRight(juce::jmin(gapPx, innerTop.getWidth()));

        // Reserve space for transport controls if any width remains
        int avail = innerTop.getWidth();
        juce::Rectangle<int> transportArea;
        if (avail > 0)
            transportArea = innerTop.removeFromRight(juce::jmin(transportWidth, avail));
        auto t = transportArea;
        // Layout Prev/Next compactly within whatever space we have
        int prevW = juce::jmin(70, t.getWidth());
        prevButton.setBounds(t.removeFromLeft(prevW));
        int sepW = juce::jmin(10, t.getWidth());
        if (sepW > 0) t.removeFromLeft(sepW);
        int nextW = juce::jmin(70, t.getWidth());
        nextButton.setBounds(t.removeFromLeft(nextW));

        // Preset label consumes remaining space on the left, scales down if tight
        presetNameLabel.setBounds(innerTop);

        // Visualization area fills remaining
        if (isDetached)
        {
            detachedNotice.setBounds(bounds.reduced(12));
        }
        else
        {
            vizCanvas.setBounds(bounds.reduced(12));
        }
    }

private:
    void dockCanvas()
    {
        if (!isDetached)
            return;
        // Ensure we exit fullscreen before docking
        if (externalWindow && externalWindow->isFullScreen()) {
            externalWindow->setFullScreen(false);
        }
        isFullscreen = false;
        // Detach canvas from external window and add back to editor
        if (externalWindow)
        {
            if (auto* content = externalWindow->getContent())
                content->detachCanvas();
            externalWindow->setVisible(false);
            externalWindow.reset();
        }
        addAndMakeVisible(vizCanvas);
        isDetached = false;
        detachedNotice.setText({}, juce::dontSendNotification);
        detachedNotice.setVisible(false);
        resized();
    }

    void toggleFullscreen()
    {
        // If not detached yet, auto-detach to external window first
        if (!isDetached)
        {
            externalWindow = std::make_unique<ExternalVisualizationWindow>("MilkDAWp Visualization (OBS)", [this]{ this->dockCanvas(); }, [this]{ this->toggleFullscreen(); });
            removeChildComponent(&vizCanvas);
            if (auto* content = externalWindow->getContent()) {
                content->attachCanvas(&vizCanvas);
                content->onPrev = [this]{ this->processor.prevPresetInPlaylist(); this->presetNameLabel.setText(this->currentDisplayName(), juce::dontSendNotification); if (this->externalWindow) { if (auto* ec = this->externalWindow->getContent()) { ec->setPresetName(this->currentDisplayName()); ec->setTransportEnabled(this->processor.hasActivePlaylistPublic()); } } };
                content->onNext = [this]{ this->processor.nextPresetInPlaylist(); this->presetNameLabel.setText(this->currentDisplayName(), juce::dontSendNotification); if (this->externalWindow) { if (auto* ec = this->externalWindow->getContent()) { ec->setPresetName(this->currentDisplayName()); ec->setTransportEnabled(this->processor.hasActivePlaylistPublic()); } } };
                content->setPresetName(this->currentDisplayName());
                content->setTransportEnabled(this->processor.hasActivePlaylistPublic());
            }
            isDetached = true;
            detachedNotice.setText("Visualization is detached. Click 'Dock' in the external window to reattach.", juce::dontSendNotification);
            detachedNotice.setJustificationType(juce::Justification::centred);
            detachedNotice.setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(detachedNotice);
            resized();
        }
        if (!externalWindow)
            return;
        if (!isFullscreen)
        {
            if (auto* d = getTargetDisplay())
                ensureExternalWindowOnDisplay(*d);
            externalWindow->setFullScreen(true);
            isFullscreen = true;
        }
        else
        {
            externalWindow->setFullScreen(false);
            isFullscreen = false;
        }
    }

    void ensureExternalWindowOnDisplay(const juce::Displays::Display& d)
    {
        if (!externalWindow)
            return;
        // Move the window to the target display before entering fullscreen
        externalWindow->setTopLeftPosition(d.userArea.getX(), d.userArea.getY());
    }

    const juce::Displays::Display* getTargetDisplay() const
    {
        auto& desktop = juce::Desktop::getInstance();
        auto& displays = desktop.getDisplays();
        auto editorBounds = getScreenBounds();
        return displays.getDisplayForRect(editorBounds);
    }

    void timerCallback() override
    {
        auto name = currentDisplayName();
        if (name != lastDisplayedName)
        {
            lastDisplayedName = name;
            presetNameLabel.setText(name, juce::dontSendNotification);
        }
        // Keep preset combobox in sync with playlist state
        const bool havePL = processor.hasActivePlaylistPublic();
        presetCombo.setEnabled(havePL);
        if (havePL)
        {
            const int N = processor.getPlaylistSizePublic();
            bool needRebuild = (N != lastKnownPlaylistSize);
            if (!needRebuild && N > 0)
            {
                // Detect order changes by comparing first item's text
                auto expected0 = juce::String("1. ") + processor.getPlaylistItemNameAtOrderedPublic(0);
                if (presetCombo.getNumItems() < 1 || presetCombo.getItemText(0) != expected0)
                    needRebuild = true;
            }
            if (needRebuild)
            {
                // Rebuild items
                updatingPresetCombo = true;
                presetCombo.clear(juce::dontSendNotification);
                for (int i = 0; i < N; ++i) {
                    auto nm = processor.getPlaylistItemNameAtOrderedPublic(i);
                    presetCombo.addItem(juce::String(i + 1) + ". " + nm, i + 1);
                }
                lastKnownPlaylistSize = N;
                updatingPresetCombo = false;
            }
            // Update selection to current position
            int pos = processor.getPlaylistPosPublic();
            if (pos >= 0 && pos != presetCombo.getSelectedItemIndex())
            {
                updatingPresetCombo = true;
                presetCombo.setSelectedItemIndex(pos, juce::dontSendNotification);
                updatingPresetCombo = false;
            }
        }
        else
        {
            if (lastKnownPlaylistSize != 0)
            {
                updatingPresetCombo = true;
                presetCombo.clear(juce::dontSendNotification);
                updatingPresetCombo = false;
                lastKnownPlaylistSize = 0;
            }
        }
        // Push updates to external window UI when detached
        if (isDetached && externalWindow)
        {
            if (auto* content = externalWindow->getContent())
            {
                content->setPresetName(name);
                content->setTransportEnabled(processor.hasActivePlaylistPublic());
            }
        }
    }

    void refreshTransportVisibility()
    {
        const bool show = processor.hasActivePlaylistPublic();
        prevButton.setVisible(show);
        nextButton.setVisible(show);
        if (isDetached && externalWindow)
        {
            if (auto* content = externalWindow->getContent())
                content->setTransportEnabled(show);
        }
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

    struct VizOpenGLCanvas : public juce::Component, public juce::OpenGLRenderer, private juce::Timer {
        VizOpenGLCanvas()
        {
            // Configure GL context for an embedded canvas
            // Do not force a core-profile context. Let JUCE create a default/compatibility context on Windows,
            // since projectM’s renderer may rely on fixed-function pipeline features not available in core profiles.
            // (Previously requested openGL3_2.)
            setOpaque(false); // allow per-pixel transparency for OBS/window compositing
            context.setRenderer(this);
            context.setContinuousRepainting(true);
            // Re-enable JUCE component painting so we can show a CPU fallback when projectM/GL is unavailable.
            // The paint() implementation will avoid overdrawing when GL is active.
            context.setComponentPaintingEnabled(true);
            context.attachTo(*this);
            // Drive CPU fallback repaints at ~60 FPS
            startTimerHz(60);
        }
        ~VizOpenGLCanvas() override
        {
            stopTimer();
            context.detach();
        }
        void newOpenGLContextCreated() override
        {
            // Initialise GL resources if needed later (textures, FBOs). For now just set a start time.
            startTimeMs = juce::Time::getMillisecondCounterHiRes();
            glContextCreated.store(true, std::memory_order_relaxed);
            MDW_LOG_INFO("VizOpenGLCanvas: OpenGL context created");
            // Log GL strings for diagnosis
            const GLubyte* ver = juce::gl::glGetString(juce::gl::GL_VERSION);
            const GLubyte* ven = juce::gl::glGetString(juce::gl::GL_VENDOR);
            const GLubyte* ren = juce::gl::glGetString(juce::gl::GL_RENDERER);
            if (ver) MDW_LOG_INFO(juce::String("OpenGL Version: ") + juce::String((const char*)ver));
            if (ven) MDW_LOG_INFO(juce::String("OpenGL Vendor: ") + juce::String((const char*)ven));
            if (ren) MDW_LOG_INFO(juce::String("OpenGL Renderer: ") + juce::String((const char*)ren));
           #if defined(_DEBUG)
            // In Debug builds, projectM is disabled by default to avoid upstream asserts.
            // Developers can opt-in either by defining MDW_ENABLE_PROJECTM_DEBUG at compile time
            // or by setting the environment variable MDW_FORCE_PM_DEBUG=1 at runtime.
            const bool forcePM = juce::SystemStats::getEnvironmentVariable("MDW_FORCE_PM_DEBUG", "0") == "1";
           #ifndef MDW_ENABLE_PROJECTM_DEBUG
            if (!forcePM) {
                MDW_LOG_INFO("projectM (Debug): disabled by default; set MDW_FORCE_PM_DEBUG=1 or define MDW_ENABLE_PROJECTM_DEBUG to enable");
                return;
            }
           #else
            if (!forcePM) {
                MDW_LOG_INFO("projectM (Debug): enabled via MDW_ENABLE_PROJECTM_DEBUG");
            } else {
                MDW_LOG_INFO("projectM (Debug): enabled via MDW_FORCE_PM_DEBUG=1");
            }
           #endif
           #endif
        #if MILKDAWP_HAS_PROJECTM
            // On Windows, proactively probe dependent DLLs to avoid hard crashes on first call
           #ifdef _WIN32
            // Build a list of candidate DLL names for both Debug/Release vcpkg conventions
           #if defined(_DEBUG)
            const wchar_t* pmCandidates[] = { L"projectM-4d.dll", L"projectM-4.dll" };
            const wchar_t* glewCandidates[] = { L"glew32d.dll", L"glew32.dll" };
           #else
            const wchar_t* pmCandidates[] = { L"projectM-4.dll" };
            const wchar_t* glewCandidates[] = { L"glew32.dll" };
           #endif

            // Resolve the directory of our plugin module (not the host EXE)
            HMODULE hSelf = nullptr;
            wchar_t modulePath[MAX_PATH] = {0};
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCWSTR)&mdw_module_anchor, &hSelf))
            {
                DWORD len = GetModuleFileNameW(hSelf, modulePath, MAX_PATH);
                if (len > 0 && len < MAX_PATH) {
                    // Trim to directory
                    wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
                    if (lastSlash) *lastSlash = 0; // now modulePath is the directory
                } else {
                    modulePath[0] = 0;
                }
            }

            auto tryLoadFromDir = [&](const wchar_t* dir, const wchar_t* name) -> HMODULE {
                if (dir && dir[0] != 0) {
                    wchar_t full[MAX_PATH];
                    swprintf(full, MAX_PATH, L"%s\\%s", dir, name);
                    HMODULE h = LoadLibraryExW(full, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                    if (h) {
                        MDW_LOG_INFO(juce::String("Loaded DLL: ") + juce::String((juce::CharPointer_UTF16)full));
                        return h;
                    } else {
                        DWORD err = GetLastError();
                        LPWSTR msgBuf = nullptr;
                        DWORD c = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                                 nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msgBuf, 0, nullptr);
                        juce::String msg = (c && msgBuf) ? juce::String((juce::CharPointer_UTF16)msgBuf).trim() : juce::String();
                        if (msgBuf) LocalFree(msgBuf);
                        MDW_LOG_INFO(juce::String("Failed to load ") + juce::String((juce::CharPointer_UTF16)full) +
                                      juce::String(" (GetLastError=") + juce::String((int)err) + juce::String(") ") + msg);
                    }
                }
                return (HMODULE)nullptr;
            };

            // First preload GLEW so projectM's imports can resolve
            HMODULE hGlewProbe = nullptr;
            for (auto* n : glewCandidates) {
                if ((hGlewProbe = tryLoadFromDir(modulePath, n)) != nullptr) break;
            }
            if (!hGlewProbe) {
                for (auto* n : glewCandidates) {
                    hGlewProbe = LoadLibraryExW(n, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                    if (hGlewProbe) {
                        MDW_LOG_INFO(juce::String("Loaded DLL from system path: ") + juce::String((juce::CharPointer_UTF16)n));
                        break;
                    }
                }
            }
            if (!hGlewProbe) {
                MDW_LOG_ERROR("GLEW DLL not found. Searched next to plugin and in system paths for: glew32d.dll, glew32.dll");
                return;
            }

            // Then load projectM from the plugin's directory
            HMODULE hPMProbe = nullptr;
            for (auto* n : pmCandidates) {
                if ((hPMProbe = tryLoadFromDir(modulePath, n)) != nullptr) break;
            }
            // Fallback: system search path (developer machines with vcpkg installed to PATH)
            if (!hPMProbe) {
                for (auto* n : pmCandidates) {
                    hPMProbe = LoadLibraryExW(n, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                    if (hPMProbe) {
                        MDW_LOG_INFO(juce::String("Loaded DLL from system path: ") + juce::String((juce::CharPointer_UTF16)n));
                        break;
                    }
                }
            }
            if (!hPMProbe) {
                MDW_LOG_ERROR("projectM DLL not found. Searched next to plugin and in system paths for: projectM-4d.dll, projectM-4.dll");
                return; // leave pmReady=false; CPU fallback remains active
            }
            // Keep the libraries loaded for the process lifetime; no FreeLibrary on success to avoid refcount churn
            // Initialise GLEW against the current context (required for core profile function pointers)
            typedef int (WINAPI *PFNGLEWINIT)(void);
            PFNGLEWINIT pGlewInit = (PFNGLEWINIT) GetProcAddress(hGlewProbe, "glewInit");
            if (pGlewInit == nullptr) {
                MDW_LOG_ERROR("GLEW: glewInit not found in DLL");
                return;
            }
            // Set glewExperimental=GL_TRUE to ensure core profile entry points are exposed (samplers, etc.)
            typedef unsigned char GLboolean;
            GLboolean* pGlewExperimental = (GLboolean*) GetProcAddress(hGlewProbe, "glewExperimental");
            if (pGlewExperimental) *pGlewExperimental = (GLboolean)1;
            int glewRc = pGlewInit();
            if (glewRc != 0) {
                MDW_LOG_ERROR(juce::String("GLEW initialisation failed, code=") + juce::String(glewRc));
                return;
            } else {
                MDW_LOG_INFO("GLEW initialised");
            }
            // Resolve projectM C API entry points dynamically to avoid link-time delay-load
           #if MILKDAWP_HAS_PROJECTM && defined(_WIN32)
            g_pmModule = hPMProbe;
            auto gp = [&](const char* name){ FARPROC p = GetProcAddress(g_pmModule, name); if (!p) MDW_LOG_INFO(juce::String("projectM symbol not found: ") + name); return p; };
            g_pm_create              = (PFN_PM_CREATE) gp("projectm_create");
            g_pm_destroy             = (PFN_PM_DESTROY) gp("projectm_destroy");
            g_pm_set_window_size     = (PFN_PM_SET_WINDOW_SIZE) gp("projectm_set_window_size");
            g_pm_set_fps             = (PFN_PM_SET_FPS) gp("projectm_set_fps");
            g_pm_set_aspect          = (PFN_PM_SET_ASPECT) gp("projectm_set_aspect_correction");
            g_pm_load_preset_file    = (PFN_PM_LOAD_PRESET_FILE) gp("projectm_load_preset_file");
            g_pm_opengl_render_frame = (PFN_PM_OPENGL_RENDER_FRAME) gp("projectm_opengl_render_frame");
            if (!g_pm_create || !g_pm_destroy || !g_pm_set_window_size || !g_pm_set_fps || !g_pm_set_aspect || !g_pm_load_preset_file || !g_pm_opengl_render_frame) {
                MDW_LOG_ERROR("projectM: one or more required API symbols missing; will use CPU fallback");
            } else {
                MDW_LOG_INFO("projectM: C API resolved successfully (runtime)");
            }
           #endif
           #endif
            // Create a projectM instance bound to this GL context
            // Defer creating projectM until we have a preset path to load (avoids upstream Debug asserts on idle preset)
            MDW_LOG_INFO("projectM: deferring instance creation until a preset is selected");
        #endif
        }
        void renderOpenGL() override
        {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            lastGLFrameMs.store((uint64_t) nowMs, std::memory_order_relaxed);
            juce::OpenGLHelpers::clear(juce::Colours::transparentBlack);
            // Ensure viewport matches the component size each frame
            {
                const int w = juce::jmax(1, getWidth());
                const int h = juce::jmax(1, getHeight());
                // Use JUCE's OpenGL function table
                juce::gl::glViewport(0, 0, w, h);
                
                // Keep projectM informed of the current drawable size (prevents asserts and wrong aspect)
               #if MILKDAWP_HAS_PROJECTM
                if (pmHandle != nullptr) {
                    if (g_pm_set_window_size) g_pm_set_window_size(pmHandle, (size_t) w, (size_t) h);
                }
               #endif
            }
        #if MILKDAWP_HAS_PROJECTM
            // Create projectM lazily once a preset path is available to avoid idle preset asserts in Debug builds
            if (pmHandle == nullptr && owner != nullptr) {
                auto initialPath = owner->getCurrentPresetPath();
                if (initialPath.isNotEmpty()) {
                    pmHandle = (g_pm_create ? g_pm_create() : nullptr);
                    if (pmHandle == nullptr) {
                        MDW_LOG_ERROR("projectM: failed to create instance (is GL context current?)");
                    } else {
                        MDW_LOG_INFO("projectM: instance created (lazy)");
                        // Optional: set preset search directory to the preset's parent folder to keep internal lookups happy
                        // Note: projectM v4 C API may not expose set_preset_directory; we proceed to explicit file load below.
                        if (g_pm_set_fps) g_pm_set_fps(pmHandle, 60);
                        if (g_pm_set_aspect) g_pm_set_aspect(pmHandle, true);
                        const int w0 = juce::jmax(2, getWidth());
                        const int h0 = juce::jmax(2, getHeight());
                        if (g_pm_set_window_size) g_pm_set_window_size(pmHandle, (size_t) w0, (size_t) h0);
                        pmReady = true;
                        pmCanRender = false;
                        lastPMPath.clear();
                    }
                }
            }
            if (pmHandle != nullptr)
            {
                // If user selected a new preset path, load it here on the GL thread
                if (owner != nullptr) {
                    auto path = owner->getCurrentPresetPath();
                    if (path.isNotEmpty() && path != lastPMPath) {
                        isLoadingPreset = true;
                        pmCanRender = false;
                        // Ensure window size is valid before swap
                        const int pw = juce::jmax(2, getWidth());
                        const int ph = juce::jmax(2, getHeight());
                        if (g_pm_set_window_size) g_pm_set_window_size(pmHandle, (size_t) pw, (size_t) ph);
                        if (g_pm_load_preset_file) g_pm_load_preset_file(pmHandle, path.toRawUTF8(), true);
                        MDW_LOG_INFO(juce::String("Loaded preset (GL): ") + path);
                        lastPMPath = path;
                        // After preset swap, re-affirm window size and allow rendering
                        if (g_pm_set_window_size) g_pm_set_window_size(pmHandle, (size_t) pw, (size_t) ph);
                        isLoadingPreset = false;
                        pmCanRender = true;
                    }
                }
                // Render the projectM frame into the current framebuffer (guarded)
                {
                    const int cw = juce::jmax(2, getWidth());
                    const int ch = juce::jmax(2, getHeight());
                    if (pmCanRender && !isLoadingPreset && cw >= 2 && ch >= 2) {
                        // Reset critical GL state to ensure projectM draws to the default framebuffer
                        juce::gl::glBindFramebuffer(juce::gl::GL_FRAMEBUFFER, 0);
                        juce::gl::glDisable(juce::gl::GL_SCISSOR_TEST);
                        juce::gl::glDisable(juce::gl::GL_DEPTH_TEST);
                        juce::gl::glDisable(juce::gl::GL_STENCIL_TEST);
                        juce::gl::glColorMask(juce::gl::GL_TRUE, juce::gl::GL_TRUE, juce::gl::GL_TRUE, juce::gl::GL_TRUE);
                        // Enable standard alpha blending (projectM composites layers and trails)
                        juce::gl::glDisable(juce::gl::GL_BLEND);
                        juce::gl::glDisable(juce::gl::GL_CULL_FACE);
                        juce::gl::glDisable(juce::gl::GL_DITHER);
                        // Feed latest PCM to projectM (if audio available)
                        if (owner != nullptr) {
                            if (auto* vt = owner->getVizThread()) {
                                std::vector<float> pcm;
                                double sr = 0.0;
                                // Request around 1024 frames for responsiveness; VisualizationThread will clamp to available
                                vt->getLatestPcmWindow(pcm, 1024, sr);
                                // Apply Beat Sensitivity scaling to input amplitude before feeding
                                float scale = 1.0f;
                                if (auto* pBeat = owner->getValueTreeState().getRawParameterValue("beatSensitivity"))
                                    scale = pBeat->load();
                                if (scale != 1.0f) {
                                    for (auto& s : pcm) s *= scale;
                                }
                                // Determine bypass and recent-audio status
                                bool bypassed = false;
                                if (auto* bp = owner->getBypassParameter())
                                    bypassed = (bp->getValue() >= 0.5f);
                                const bool haveRecent = vt->hasRecentPcm(200.0);
                                // If bypassed or no recent audio, feed a small silence block
                                if (bypassed || !haveRecent || pcm.empty()) {
                                    pcm.assign((size_t)512 * 2, 0.0f);
                                    if (sr <= 0.0) sr = 44100.0;
                                }
                               #ifdef _WIN32
                                // Runtime resolve projectM audio input APIs to avoid hard coupling to headers/symbol variants
                                typedef void (__cdecl *PFN_PM_PCM_ADD_FLOAT)(projectm_handle, const float*, size_t, int);
                                typedef void (__cdecl *PFN_PM_PCM_ADD_INT16)(projectm_handle, const int16_t*, size_t, int);
                                static PFN_PM_PCM_ADD_FLOAT s_pmAddFloat = nullptr;
                                static PFN_PM_PCM_ADD_INT16 s_pmAddI16 = nullptr;
                                static bool s_triedResolve = false;
                                if (!s_triedResolve) {
                                    s_triedResolve = true;
                                    HMODULE hPM = GetModuleHandleW(L"projectM-4d.dll");
                                    if (!hPM) hPM = GetModuleHandleW(L"projectM-4.dll");
                                    if (hPM) {
                                        s_pmAddFloat = (PFN_PM_PCM_ADD_FLOAT) GetProcAddress(hPM, "projectm_pcm_add_float");
                                        if (!s_pmAddFloat) s_pmAddFloat = (PFN_PM_PCM_ADD_FLOAT) GetProcAddress(hPM, "projectm_pcm_add_f32");
                                        s_pmAddI16 = (PFN_PM_PCM_ADD_INT16) GetProcAddress(hPM, "projectm_pcm_add_int16");
                                        if (!s_pmAddI16) s_pmAddI16 = (PFN_PM_PCM_ADD_INT16) GetProcAddress(hPM, "projectm_pcm_add_s16");
                                        if (!s_pmAddFloat && !s_pmAddI16) {
                                            MDW_LOG_ERROR("projectM: no compatible PCM feed API found (float/int16)");
                                        } else {
                                            MDW_LOG_INFO("projectM: PCM feed API resolved");
                                        }
                                    } else {
                                        MDW_LOG_ERROR("projectM: module handle not found for dynamic PCM API resolve");
                                    }
                                }
                                const size_t frames = pcm.size() / 2;
                                if (frames > 0) {
                                    if (s_pmAddFloat) {
                                        s_pmAddFloat(pmHandle, pcm.data(), frames, 2);
                                    } else if (s_pmAddI16) {
                                        // Convert to int16 with clipping
                                        std::vector<int16_t> tmpI16;
                                        tmpI16.resize(pcm.size());
                                        for (size_t i = 0; i < pcm.size(); ++i) {
                                            float v = juce::jlimit(-1.0f, 1.0f, pcm[i]);
                                            int iv = (int) std::lround(v * 32767.0f);
                                            tmpI16[i] = (int16_t) juce::jlimit(-32768, 32767, iv);
                                        }
                                        s_pmAddI16(pmHandle, tmpI16.data(), frames, 2);
                                    }
                                }
                                // Throttled responsiveness log
                               #if defined(_DEBUG)
                                static double lastPcmLogMs = 0.0;
                                const double nowDbg = juce::Time::getMillisecondCounterHiRes();
                                if (nowDbg - lastPcmLogMs > 3000.0) {
                                    MDW_LOG_INFO(juce::String("projectM PCM feed: ") + juce::String((int)frames) + " frames @ " + juce::String(sr, 1) + " Hz"
                                                 + (bypassed ? " (bypassed→silence)" : haveRecent ? "" : " (stale→silence)"));
                                    lastPcmLogMs = nowDbg;
                                }
                               #endif
                               #endif // _WIN32
                            }
                        }
                        if (g_pm_opengl_render_frame) g_pm_opengl_render_frame(pmHandle);
                    } else {
                        static double lastSkipLogMs = 0.0;
                        if (nowMs - lastSkipLogMs > 3000.0) {
                            MDW_LOG_INFO("projectM GL: skipping render (initialising/preset swap or small surface)");
                            lastSkipLogMs = nowMs;
                        }
                    }
                }

                // Periodic heartbeat log so we can confirm GL path is active without spamming logs
               #if defined(_DEBUG)
                static double lastLogMs = 0.0;
                if (nowMs - lastLogMs > 2000.0) {
                    MDW_LOG_INFO("projectM GL: rendered frame");
                    lastLogMs = nowMs;
                }
               #endif
            }
        #endif
            // When projectM is not available, nothing is drawn here; CPU blit in paint() remains as fallback.
        }
        void openGLContextClosing() override
        {
            // Free GL resources if any
        #if MILKDAWP_HAS_PROJECTM
            if (pmHandle != nullptr) {
                if (g_pm_destroy) g_pm_destroy(pmHandle);
                pmHandle = nullptr;
                pmReady = false;
                lastPMPath.clear();
            }
        #endif
        }
        void timerCallback() override
        {
            // Unconditionally repaint; ensures CPU fallback animates even if a host attaches a GL context but never renders.
            repaint();
        }
        void paint(juce::Graphics& g) override
        {
           #if MILKDAWP_HAS_PROJECTM
            // If projectM is ready and can render into GL, avoid drawing over it
            if (pmHandle != nullptr && pmCanRender) {
                return; // GL renderOpenGL will draw the frame
            }
           #endif
            // CPU fallback: blit the latest frame produced by the VisualizationThread
            if (owner != nullptr) {
                if (auto* vt = owner->getVizThread()) {
                    milkdawp::VisualizationThread::FrameSnapshot snap;
                    if (vt->getFrameSnapshot(snap) && snap.image.isValid()) {
                        g.drawImageWithin(snap.image, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::stretchToFit);
                        return;
                    }
                }
            }
            // If no snapshot yet, clear to a neutral colour (transparent for OBS compositing)
            g.fillAll(juce::Colours::transparentBlack);
        }
        void resized() override
        {
            if (owner != nullptr) {
                if (auto* vt = owner->getVizThread()) {
                    vt->setSurfaceSize(getWidth(), getHeight());
                }
            }
        }
        void setOwner(MilkDAWpAudioProcessor* p) { owner = p; }
        juce::OpenGLContext context;
        double startTimeMs { 0.0 };
        std::atomic<bool> glContextCreated { false };
        std::atomic<uint64_t> lastGLFrameMs { 0 };
        MilkDAWpAudioProcessor* owner { nullptr };

       #if MILKDAWP_HAS_PROJECTM
        projectm_handle pmHandle { nullptr };
        juce::String lastPMPath;
        bool pmReady { false };
        bool pmCanRender { false };
        bool isLoadingPreset { false };
       #ifdef _WIN32
        /* Removed experimental PCM injection hooks after instability reports */
       #endif
       #endif
    };

    HardwareLookAndFeel hardwareLAF;
    juce::Label logoLabel;
    juce::ImageComponent logoImage;
    juce::Image logoImageData;
    bool logoLoaded { false };
    juce::Rectangle<int> logoTargetBounds;
    VizOpenGLCanvas vizCanvas;

    // Detached window support (Phase 5.1)
    juce::TextButton popOutButton { "Pop-out" };
    juce::TextButton fullscreenButton { "Fullscreen" };
    juce::Label detachedNotice;
    std::unique_ptr<ExternalVisualizationWindow> externalWindow;
    bool isDetached { false };
    bool isFullscreen { false };

    MilkDAWpAudioProcessor& processor;

    // Phase 4.2 controls
    juce::Label beatLabel;
    juce::Slider beatSlider;
    juce::Label durationLabel;
    juce::Slider durationSlider;
    juce::DrawableButton lockButton { "lockButton", juce::DrawableButton::ImageFitted };
    juce::DrawableButton shuffleButton { "shuffleButton", juce::DrawableButton::ImageFitted };

    juce::TextButton loadButton;
    juce::TextButton loadFolderButton;
    juce::ComboBox presetCombo; // Phase 6.2
    juce::DrawableButton playlistPickerButton { "playlistPicker", juce::DrawableButton::ImageFitted };
    juce::TextButton prevButton;
    juce::TextButton nextButton;
    juce::Label presetNameLabel;
    juce::String lastDisplayedName;
    juce::Label transitionStyleLabel;
    juce::ComboBox transitionStyleCombo;
    int lastKnownPlaylistSize { 0 }; // Phase 6.2 tracking
    bool updatingPresetCombo { false }; // guard to avoid feedback

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> beatAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> shuffleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> transitionStyleAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Tooltip window for this editor (required by JUCE to display tooltips)
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
};

juce::AudioProcessorEditor* MilkDAWpAudioProcessor::createEditor() {
    return new MilkDAWpAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MilkDAWpAudioProcessor();
}
