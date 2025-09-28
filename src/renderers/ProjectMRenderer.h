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

    static constexpr const char* kWindowTitle = "MilkDAWp";

    // Host-automatable preset selection
    void setPresetIndex(int index) noexcept { desiredPresetIndex.store(index, std::memory_order_relaxed); }
    // Direct preset loading by path (skips index mechanism)
    void loadPresetByPath(const juce::String& absolutePath, bool hardCut = true);

private:
    juce::OpenGLContext& context;

    std::unique_ptr<juce::OpenGLShaderProgram> program;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attrPos;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attrCol;

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
        int lastLoadedPresetIndex = std::numeric_limits<int>::min();
        // Pending preset request if chosen before projectM is ready
        std::atomic<bool> hasPendingPreset { false };
        juce::String pendingPresetPath; // accessed on UI thread when setting; consumed on GL thread
        std::atomic<int> pendingPresetCut { 1 }; // 1=hard, 0=soft
    #endif
};
