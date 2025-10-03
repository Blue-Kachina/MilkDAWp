#pragma once
#include <JuceHeader.h>

class LockFreeAudioFifo;

class ProjectMRenderer : public juce::OpenGLRenderer
{
public:
    explicit ProjectMRenderer(juce::OpenGLContext& ctx,
                              LockFreeAudioFifo* audioFifo = nullptr,
                              int sampleRate = 44100);
    ~ProjectMRenderer() override;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void setAudioSource(LockFreeAudioFifo* fifo, int sampleRate);

    // Visual controls (thread-safe setters)
    void setVisualParams(float newAmpScale, float newSpeed)
    {
        ampScale.store(newAmpScale, std::memory_order_relaxed);
        speedScale.store(newSpeed, std::memory_order_relaxed);
    }

    void setColor(float hue01, float sat01)
    {
        baseHue.store(hue01, std::memory_order_relaxed);
        baseSat.store(sat01, std::memory_order_relaxed);
    }

    void setSeed(int newSeed) noexcept
    {
        seed.store(newSeed, std::memory_order_relaxed);
    }

    // Allow turning projectM on/off at runtime (default off for stability)
    void setProjectMEnabled(bool enabled) noexcept
    {
        projectMEnabled.store(enabled, std::memory_order_relaxed);
    }

    // Auto-play/playlist integration (thread-safe signals)
    void setAutoPlay(bool enabled, bool shuffle, bool hardCut) noexcept
    {
        // Only mark dirty if something actually changed to avoid spamming the C API each frame
        const bool prevAp  = autoPlayEnabled.load(std::memory_order_relaxed);
        const bool prevShf = autoPlayShuffle.load(std::memory_order_relaxed);
        const bool prevHc  = autoPlayHardCut.load(std::memory_order_relaxed);
        autoPlayEnabled.store(enabled, std::memory_order_relaxed);
        autoPlayShuffle.store(shuffle, std::memory_order_relaxed);
        autoPlayHardCut.store(hardCut, std::memory_order_relaxed);
        if (prevAp != enabled || prevShf != shuffle || prevHc != hardCut)
            autoConfigDirty.store(true, std::memory_order_release);
    }
    // Request setting the playlist position via the projectM playlist API (applied on GL thread)
    void setPlaylistPosition(int index, bool hardCut) noexcept
    {
        desiredPlaylistPos.store(index, std::memory_order_relaxed);
        desiredPlaylistHardCut.store(hardCut, std::memory_order_relaxed);
        playlistPosDirty.store(true, std::memory_order_release);
    }
    void setPlaylistPaths(const juce::StringArray& absolutePaths)
    {
        const juce::ScopedLock sl(playlistLock);
        pendingPlaylist = absolutePaths;
        playlistDirty.store(true, std::memory_order_release);
    }

    // Query whether the projectM backend (and playlist API) is active
    bool isProjectMReady() const noexcept { return pmReady; }
    bool hasPlaylistApi() const noexcept { return pmPlaylist != nullptr; }
    // Query current playlist position as observed on the GL thread (or -1 if unknown)
    int getPlaylistPosition() const noexcept
    {
       #if defined(HAVE_PROJECTM)
        return currentPlaylistPos.load(std::memory_order_relaxed);
       #else
        return -1;
       #endif
    }

    static constexpr const char* kWindowTitle = "MilkDAWp";

    // Host-automatable preset selection
    void setPresetIndex(int index) noexcept {
        // Switching by index implies leaving explicit path mode
        pathMode.store(false, std::memory_order_relaxed);
        // Treat index > 0 as explicit; index == 0 is often a default, so only mark explicit if we already have a preset path/pending
        const bool explicitNow = (index > 0) || hasPendingPreset.load(std::memory_order_acquire) || (lastPresetPath.isNotEmpty());
        indexExplicit.store(explicitNow, std::memory_order_relaxed);
        desiredPresetIndex.store(index, std::memory_order_relaxed);
    }
    // Direct preset loading by path (skips index mechanism)
    void loadPresetByPath(const juce::String& absolutePath, bool hardCut = true);

private:
    juce::OpenGLContext& context;

    std::unique_ptr<juce::OpenGLShaderProgram> program;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attrPos;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attrCol;
    // Fallback shader uniforms to reflect UI controls
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uHueUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uSatUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uLevelUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uMeshUniform;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uSeedUniform;

    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int dummyVAO = 0; // bound before projectM to satisfy core-profile requirements
    int fbWidth = 0, fbHeight = 0;

    // Audio feed for visualization (SPSC FIFO)
    LockFreeAudioFifo* audioFifo = nullptr;
    int audioSampleRate = 44100;

    // Visual params (polled in render thread)
    std::atomic<float> ampScale { 1.0f };     // scales audio amplitude
    std::atomic<float> speedScale { 1.0f };   // scales animation speed
    std::atomic<float> baseHue { 0.0f };      // 0..1
    std::atomic<float> baseSat { 1.0f };      // 0..1
    std::atomic<int>   seed { 0 };            // seed for pseudo-random variations

    // ProjectM enable flag (default true when projectM is available; can be disabled via MILKDAWP_DISABLE_PROJECTM)
   #if defined(HAVE_PROJECTM)
    std::atomic<bool> projectMEnabled { true };
   #else
    std::atomic<bool> projectMEnabled { false };
   #endif

    // Simple fallback visual energy (updated by reading FIFO when projectM is not active)
    float fallbackLevel = 0.0f;

    bool setViewportForCurrentScale();

    // Dev-only: allow a test visualization mode via env var
    bool testVisMode = false;
    double testPhase = 0.0;

    // FIFO health metrics (dev logging)
    int fifoSamplesPoppedThisSecond = 0;
    double lastFifoLogTimeSec = 0.0;

    // Backoff management for projectM init attempts
    bool pmInitAttempted = false;
    double pmInitLastAttemptSec = 0.0;
    int pmRetryAttempts = 0; // resets per renderer/context instance

    // TEST: attribute-free shader path (gl_VertexID) for robust diagnostics
    std::unique_ptr<juce::OpenGLShaderProgram> testProgram;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> testColUniform;

    #if defined(HAVE_PROJECTM)
        void* pmHandle = nullptr;
        bool  pmReady  = false;
        juce::String pmPresetDir;
        void initProjectMIfNeeded();
        void shutdownProjectM();
        void renderProjectMFrame();
        void feedProjectMAudioIfAvailable();
        // Keep playlist state when using the C API
        void* pmPlaylist = nullptr;
        // Preset management
        juce::StringArray pmPresetList;
        std::atomic<int> desiredPresetIndex { -1 }; // -1 means "no preset requested"
        // When a path-based selection is active, ignore index switching until host changes index.
        std::atomic<bool> pathMode { false };
        // Only apply desiredPresetIndex when it was explicitly set by host/UI (not a default)
        std::atomic<bool> indexExplicit { false };
        // Rollback: do not force the very first preset to hard cut
        std::atomic<bool> firstPresetMustHardCut { false };
        // Suppress a small number of projectM frames (e.g., first after init/preset) to avoid visual flash
        std::atomic<int> suppressPmFrames { 0 };
        // Gate rendering until at least one preset has been applied
        std::atomic<bool> presetAppliedOnce { false };
        int lastLoadedPresetIndex = std::numeric_limits<int>::min();
        // Track last successfully requested preset path so we can reapply instantly on GL reinit
        juce::String lastPresetPath;
        // Pending preset request if chosen before projectM is ready
        std::atomic<bool> hasPendingPreset { false };
        juce::String pendingPresetPath; // accessed on UI thread when setting; consumed on GL thread
        std::atomic<int> pendingPresetCut { 1 }; // 1=hard, 0=soft
        // Auto-play config (thread-safe)
        std::atomic<bool> autoPlayEnabled { false };
        std::atomic<bool> autoPlayShuffle { false };
        std::atomic<bool> autoPlayHardCut { false };
        std::atomic<bool> autoConfigDirty { false };
        // Pending playlist to feed into projectM playlist manager (if available)
        juce::CriticalSection playlistLock;
        juce::StringArray pendingPlaylist;
        std::atomic<bool> playlistDirty { false };
        // Requested playlist position (applied on GL thread)
        std::atomic<int>  desiredPlaylistPos { -1 };
        std::atomic<bool> desiredPlaylistHardCut { false };
        std::atomic<bool> playlistPosDirty { false };
        // Watchdog for auto-play progression (nudges playlist if internal auto stalls)
        int lastObservedPlaylistPos { -1 };
        double lastAutoWatchdogTimeSec { 0.0 };
        // Cached playlist position (updated on GL thread)
        std::atomic<int> currentPlaylistPos { -1 };
        // Callbacks into this object from projectM (C API): must be static members
        static void onProjectMSwitchRequested(bool isHardCut, void* userData) noexcept;
       #if defined(HAVE_PROJECTM_PLAYLIST)
        static void onPlaylistPresetSwitched(bool isHardCut, unsigned int index, void* userData) noexcept;
       #endif
    #endif
};
