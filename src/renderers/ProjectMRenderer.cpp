#include "ProjectMRenderer.h"

#include <excpt.h>

#include "../utils/Logging.h"
using namespace juce;

// GL constants that might be missing from some headers in JUCE builds
#ifndef GL_CONTEXT_PROFILE_MASK
 #define GL_CONTEXT_PROFILE_MASK 0x9126
#endif
#ifndef GL_CONTEXT_CORE_PROFILE_BIT
 #define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef GL_CONTEXT_COMPATIBILITY_PROFILE_BIT
 #define GL_CONTEXT_COMPATIBILITY_PROFILE_BIT 0x00000002
#endif
#ifndef GL_CONTEXT_FLAGS
 #define GL_CONTEXT_FLAGS 0x821E
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
 #define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif


#if defined(HAVE_PROJECTM)
 #if __has_include(<libprojectM/projectM.hpp>)
  #include <libprojectM/projectM.hpp>
  namespace PM = libprojectM;
  #define PM_HAVE_V4 1
 #elif __has_include(<projectM/projectM.hpp>)
  #include <projectM/projectM.hpp>
  namespace PM = projectM;
  #define PM_HAVE_V4 1
 #elif __has_include(<projectM-4/projectM.hpp>)
  #include <projectM-4/projectM.hpp>
  namespace PM = projectM4; // vcpkg can expose this as a distinct namespace; adjust if needed
  #define PM_HAVE_V4 1
 #elif __has_include(<projectM-4/projectM.h>) || defined(PROJECTM4_C_API)
  extern "C" {
    #include <projectM-4/projectM.h>
    #include <projectM-4/parameters.h>
   #if defined(HAVE_PROJECTM_PLAYLIST)
    #include <projectM-4/playlist.h>
   #endif
  }
  #define PM_HAVE_V4_C_API 1
 #else
  #if defined(_MSC_VER)
   #pragma message("HAVE_PROJECTM set, but v4 headers not found")
  #else
   #warning "HAVE_PROJECTM set, but v4 headers not found"
  #endif
 #endif
#endif

// Add this include so we can call methods on LockFreeAudioFifo
#include "../utils/LockFreeAudioFifo.h"

// Weakly use GLEW if available at link-time (no headers to avoid conflicts with JUCE GL)
extern "C" unsigned int glewInit();
extern "C" const unsigned char* glewGetErrorString(unsigned int error);
// In core-profile contexts, GLEW typically requires glewExperimental = GL_TRUE prior to glewInit.
// Declare it weakly so we can set it without including glew.h
extern "C" unsigned char glewExperimental; // GLboolean proxy (1=GL_TRUE)
#ifndef GLEW_OK
 #define GLEW_OK 0u
#endif

static const char* VS_150 = R"(#version 150 core
in vec2 aPos;
in vec3 aCol;
out vec3 vCol;
void main(){ vCol=aCol; gl_Position=vec4(aPos,0.0,1.0); })";
static const char* FS_150 = R"(#version 150 core
in vec3 vCol; out vec4 FragColor;
uniform float uHue; // 0..1
uniform float uSat; // 0..1
uniform float uLevel; // 0..1
uniform float uMesh; // pixels ~ 16..160
uniform float uSeed; // arbitrary

vec3 hsv2rgb(float h, float s, float v){
    h = mod(mod(h, 1.0) + 1.0, 1.0);
    s = clamp(s, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);
    if (s <= 0.00001) return vec3(v);
    float hf = h * 6.0;
    int i = int(floor(hf));
    float f = hf - float(i);
    float p = v * (1.0 - s);
    float q = v * (1.0 - s * f);
    float t = v * (1.0 - s * (1.0 - f));
    if (i == 0) return vec3(v, t, p);
    else if (i == 1) return vec3(q, v, p);
    else if (i == 2) return vec3(p, v, t);
    else if (i == 3) return vec3(p, q, v);
    else if (i == 4) return vec3(t, p, v);
    else return vec3(v, p, q);
}

void main(){
    // simple checker/grid by mesh size and seed
    float mesh = max(1.0, uMesh);
    vec2 cell = floor(gl_FragCoord.xy / mesh + vec2(uSeed * 0.013, uSeed * 0.021));
    float checker = mod(cell.x + cell.y, 2.0);
    float v = mix(uLevel * 0.5, uLevel, checker);
    vec3 rgb = hsv2rgb(uHue, uSat, v);
    FragColor = vec4(rgb, 1.0);
})";

// TEST-only, attribute-free shader (gl_VertexID)
static const char* TEST_VS_150 = R"(#version 150 core
// Draw a full-quad using gl_VertexID with TRIANGLE_STRIP (0..3)
void main(){
    vec2 pos;
    if (gl_VertexID == 0) pos = vec2(-0.95, -0.95);
    else if (gl_VertexID == 1) pos = vec2( 0.95, -0.95);
    else if (gl_VertexID == 2) pos = vec2(-0.95,  0.95);
    else pos = vec2( 0.95,  0.95);
    gl_Position = vec4(pos, 0.0, 1.0);
})";
static const char* TEST_FS_150 = R"(#version 150 core
uniform vec3 uColor;
out vec4 FragColor;
void main(){ FragColor = vec4(uColor, 1.0); })";

#if defined(HAVE_PROJECTM) && defined(PM_HAVE_V4_C_API)
// SEH-safe minimal initializer for projectM C API.
// Important: keep this function free of C++ objects with destructors in scope.
static projectm_handle mdw_seh_projectm_minimal_init(size_t /*w*/, size_t /*h*/, int* outOk) noexcept
{
    // Note: The v4 C API (as provided by your headers) does not expose window/aspect/fps setters here.
    // Creating the instance requires a valid current OpenGL context; viewport is taken from GL state.
    if (outOk) *outOk = 0;
    projectm_handle inst = nullptr;
   #if defined(_MSC_VER)
    __try
    {
   #endif
        inst = projectm_create();
        if (inst != nullptr)
        {
            if (outOk) *outOk = 1;
        }
   #if defined(_MSC_VER)
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        inst = nullptr;
        if (outOk) *outOk = 0;
    }
   #endif
    return inst;
}

// SEH-safe wrappers for render and PCM ingestion (minimal)
static int mdw_seh_projectm_render(projectm_handle h) noexcept
{
   #if defined(_MSC_VER)
    __try { projectm_opengl_render_frame(h); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
   #else
    projectm_opengl_render_frame(h); return 1;
   #endif
}

static int mdw_seh_projectm_pcm_add_mono(projectm_handle h, const float* data, unsigned int count) noexcept
{
   #if defined(_MSC_VER)
    __try { projectm_pcm_add_float(h, data, count, PROJECTM_MONO); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
   #else
    projectm_pcm_add_float(h, data, count, PROJECTM_MONO); return 1;
   #endif
}

static int mdw_seh_projectm_load_preset_file(projectm_handle h, const char* path, int smooth) noexcept
{
   #if defined(_MSC_VER)
    __try { projectm_load_preset_file(h, path, smooth != 0); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
   #else
    projectm_load_preset_file(h, path, smooth != 0); return 1;
   #endif
}
#endif

ProjectMRenderer::ProjectMRenderer(juce::OpenGLContext& ctx, LockFreeAudioFifo* f, int sr)
    : context(ctx), audioFifo(f), audioSampleRate(sr) {}
ProjectMRenderer::~ProjectMRenderer() = default;

void ProjectMRenderer::setAudioSource(LockFreeAudioFifo* f, int sr)
{
    audioFifo = f;
    audioSampleRate = sr;
}

// Provide the missing definition (unconditional)
bool ProjectMRenderer::setViewportForCurrentScale()
{
    auto* comp = context.getTargetComponent();
    if (comp == nullptr)
        return false;

    const float scale = context.getRenderingScale();

    const int w = std::max(1, juce::roundToInt((float) comp->getWidth()  * scale));
    const int h = std::max(1, juce::roundToInt((float) comp->getHeight() * scale));

    if (w != fbWidth || h != fbHeight)
    {
        fbWidth = w;
        fbHeight = h;
        gl::glViewport(0, 0, fbWidth, fbHeight);
        // Diagnostics: log new framebuffer size and GL error (if any)
        {
            auto err = gl::glGetError();
            MDW_LOG("GL", juce::String("Viewport set: ") + juce::String(fbWidth) + "x" + juce::String(fbHeight)
                           + (err != gl::GL_NO_ERROR ? (juce::String(" glError=0x") + juce::String::toHexString((int)err)) : ""));
        }
       // Propagate size changes to projectM so it can recreate its GL resources for the new viewport
      #if defined(HAVE_PROJECTM) && defined(PM_HAVE_V4_C_API)
        if (pmReady && pmHandle)
        {
            projectm_set_window_size((projectm_handle) pmHandle, (size_t) fbWidth, (size_t) fbHeight);
            // Reassert locked preset and disabled transitions after renderer reset
            projectm_set_preset_locked((projectm_handle) pmHandle, true);
            projectm_set_hard_cut_enabled((projectm_handle) pmHandle, false);
            projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
            // Make preset/transition timers effectively infinite to avoid library-driven cuts
            projectm_set_preset_duration((projectm_handle) pmHandle, 36000.0);
            projectm_set_hard_cut_duration((projectm_handle) pmHandle, 36000.0);
        }
      #endif
    }
    return true;
}

void ProjectMRenderer::newOpenGLContextCreated()
{
    MDW_LOG("GL", "newOpenGLContextCreated: begin");

    // Initialize GLEW (if linked) now that the context is current. Some projectM builds rely on GLEW.
    #if defined(HAVE_PROJECTM)
    {
        glewExperimental = 1; // enable modern core/forward-compatible function loading
        unsigned int gerr = glewInit();
        if (gerr == GLEW_OK)
        {
            MDW_LOG("GL", "GLEW initialized successfully");
        }
        else
        {
            const unsigned char* es = glewGetErrorString(gerr);
            MDW_LOG("GL", juce::String("GLEW init failed code=") + String((int) gerr) + " msg=" + String((const char*) es));
        }
        // Some drivers set a benign GL error during glewInit; clear it to avoid confusion
        (void) gl::glGetError();
    }
    #endif

    DBG("[GL] Version: "  + String((const char*) gl::glGetString(gl::GL_VERSION)));
    DBG("[GL] Renderer: " + String((const char*) gl::glGetString(gl::GL_RENDERER)));
    DBG("[GL] Vendor: "   + String((const char*) gl::glGetString(gl::GL_VENDOR)));

    // Dev: latch test visualization mode from env once per context life
    {
        const char* env = std::getenv("MILKDAWP_TEST_VIS");
        testVisMode = (env && (env[0] == '1' || env[0] == 'T' || env[0] == 't' || env[0] == 'Y' || env[0] == 'y'));
        MDW_LOG("GL", juce::String("TestVisMode=") + (testVisMode ? "ON" : "OFF") + " (MILKDAWP_TEST_VIS)");
    }
    lastFifoLogTimeSec = Time::getMillisecondCounterHiRes() * 0.001;

    auto tryMakeProgram = [&](const char* vsrc, const char* fsrc) -> std::unique_ptr<OpenGLShaderProgram>
    {
        auto prog = std::make_unique<OpenGLShaderProgram>(context);
        bool ok = prog->addVertexShader(CharPointer_UTF8(vsrc))
               && prog->addFragmentShader(CharPointer_UTF8(fsrc))
               && prog->link();
        if (!ok)
        {
            DBG(String("[GL] Shader compile/link failed: ") + prog->getLastError());
            return nullptr;
        }
        return prog;
    };

    program = tryMakeProgram(VS_150, FS_150);
    if (!program) { MDW_LOG("GL", "newOpenGLContextCreated: shader program null"); return; }
    // Bind once to query attributes and uniforms, then unbind to keep fixed-function available for projectM
    program->use();

    attrPos = std::make_unique<OpenGLShaderProgram::Attribute>(*program, "aPos");
    attrCol = std::make_unique<OpenGLShaderProgram::Attribute>(*program, "aCol");
    // Fallback shader uniforms
    uHueUniform.reset(new OpenGLShaderProgram::Uniform(*program, "uHue"));
    uSatUniform.reset(new OpenGLShaderProgram::Uniform(*program, "uSat"));
    uLevelUniform.reset(new OpenGLShaderProgram::Uniform(*program, "uLevel"));
    uMeshUniform.reset(new OpenGLShaderProgram::Uniform(*program, "uMesh"));
    uSeedUniform.reset(new OpenGLShaderProgram::Uniform(*program, "uSeed"));

    // Important: leave no program bound so projectM (fixed-function) can render if needed
    context.extensions.glUseProgram(0);
    MDW_LOG("GL", juce::String("Attributes: aPos=") + juce::String(attrPos ? attrPos->attributeID : -999)
                   + " aCol=" + juce::String(attrCol ? attrCol->attributeID : -999));

    // TEST-only program (no attributes, no buffers)
    testProgram = tryMakeProgram(TEST_VS_150, TEST_FS_150);
    if (testProgram)
    {
        testColUniform.reset(new OpenGLShaderProgram::Uniform(*testProgram, "uColor"));
        MDW_LOG("GL", "Test program compiled (gl_VertexID path)");
    }
    else
    {
        MDW_LOG("GL", "Test program failed to compile");
    }


    auto& ext = context.extensions;
    const float verts[] = {
        -0.95f, -0.95f, 1.f, 1.f, 1.f, // bottom-left
         0.95f, -0.95f, 1.f, 1.f, 1.f, // bottom-right
        -0.95f,  0.95f, 1.f, 1.f, 1.f, // top-left
         0.95f,  0.95f, 1.f, 1.f, 1.f  // top-right
    };

    ext.glGenVertexArrays(1, &vao);
    ext.glBindVertexArray(vao);
    
    // Create a dummy VAO to satisfy core-profile contexts for libraries that assume a VAO is bound
    ext.glGenVertexArrays(1, &dummyVAO);

    ext.glGenBuffers(1, &vbo);
    ext.glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
    ext.glBufferData(gl::GL_ARRAY_BUFFER, sizeof(verts), verts, gl::GL_DYNAMIC_DRAW);

    const GLsizei stride = (GLsizei) (sizeof(float) * 5);
    const GLvoid* posPtr = (const GLvoid*) 0;
    const GLvoid* colPtr = (const GLvoid*) (sizeof(float) * 2);

    if (attrPos && attrPos->attributeID >= 0)
    {
        ext.glEnableVertexAttribArray((GLuint) attrPos->attributeID);
        ext.glVertexAttribPointer((GLuint) attrPos->attributeID, 2, gl::GL_FLOAT, gl::GL_FALSE, stride, posPtr);
    }
    if (attrCol && attrCol->attributeID >= 0)
    {
        ext.glEnableVertexAttribArray((GLuint) attrCol->attributeID);
        ext.glVertexAttribPointer((GLuint) attrCol->attributeID, 3, gl::GL_FLOAT, gl::GL_FALSE, stride, colPtr);
    }
    {
        auto err = gl::glGetError();
        if (err != gl::GL_NO_ERROR)
            MDW_LOG("GL", juce::String("After VAO/VBO setup glError=0x") + juce::String::toHexString((int)err));
    }


    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glDisable(gl::GL_CULL_FACE);
    gl::glDisable(gl::GL_SCISSOR_TEST);
    gl::glClearColor(0.f, 0.f, 0.f, 1.f);

    #if defined(HAVE_PROJECTM)
    // Resolve preset dir with multiple fallbacks (bundle layouts, dev tree, system installs)
    File chosen;

    auto exe = File::getSpecialLocation(File::currentApplicationFile);
    auto bundleRoot = exe.getParentDirectory().getParentDirectory();
    File presetsA = bundleRoot.getChildFile("Contents").getChildFile("Resources").getChildFile("presets");
    File presetsB = bundleRoot.getChildFile("Resources").getChildFile("presets");
    if (! chosen.isDirectory())
        chosen = presetsA.isDirectory() ? presetsA : (presetsB.isDirectory() ? presetsB : File());

    // 1) Dev fallback: search upwards for resources/presets from the executable location
    if (! chosen.isDirectory())
    {
        File dir = exe.getParentDirectory();
        for (int i = 0; i < 8 && ! chosen.isDirectory(); ++i)
        {
            File test = dir.getChildFile("resources").getChildFile("presets");
            if (test.isDirectory()) { chosen = test; break; }
            dir = dir.getParentDirectory();
        }
    }

    // 2) Current working directory (useful when launched from IDE)
    if (! chosen.isDirectory())
    {
        File cwd = File::getCurrentWorkingDirectory().getChildFile("resources").getChildFile("presets");
        if (cwd.isDirectory()) chosen = cwd;
    }

    // 3) vcpkg-style shared data directory near the exe (../share/projectM/presets)
    if (! chosen.isDirectory())
    {
        File dir = exe.getParentDirectory();
        for (int i = 0; i < 6 && ! chosen.isDirectory(); ++i)
        {
            File test = dir.getChildFile("share").getChildFile("projectM").getChildFile("presets");
            if (test.isDirectory()) { chosen = test; break; }
            dir = dir.getParentDirectory();
        }
    }

    // 4) Repo-local projectM-4/presets (for developer checkouts)
    if (! chosen.isDirectory())
    {
        File dir = exe.getParentDirectory();
        for (int i = 0; i < 6 && ! chosen.isDirectory(); ++i)
        {
            File test = dir.getChildFile("projectM-4").getChildFile("presets");
            if (test.isDirectory()) { chosen = test; break; }
            dir = dir.getParentDirectory();
        }
    }

    // 5) Common Windows locations
    if (! chosen.isDirectory())
    {
        File appData = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("projectM").getChildFile("presets");
        if (appData.isDirectory()) chosen = appData;
    }
    if (! chosen.isDirectory())
    {
        File pf = File("C:\\Program Files\\projectM\\presets");
        if (pf.isDirectory()) chosen = pf;
    }

    pmPresetDir = chosen.getFullPathName();
    const bool exists = chosen.isDirectory();
    MDW_LOG("PM", "PresetDir resolved: " + (exists ? pmPresetDir : String("(not found)"))
                    + " exists=" + String(exists ? "true" : "false"));

    // NEW: derive runtime enable flag from DISABLE env only (no separate ENABLE var).
    {
        const char* env = std::getenv("MILKDAWP_DISABLE_PROJECTM");
        const bool disable = env && (env[0] == '1' || env[0] == 'T' || env[0] == 't' || env[0] == 'Y' || env[0] == 'y');
        projectMEnabled.store(!disable, std::memory_order_relaxed);
        juce::String envStr = env ? juce::String(env) : "(null)";
        MDW_LOG("PM", juce::String("Env MILKDAWP_DISABLE_PROJECTM=") + envStr
                        + " -> runtime projectMEnabled=" + (disable ? "false" : "true"));
    }
    #endif

    // Log additional context diagnostics (profile, flags, FBO binding)
        {
            GLint prof = 0, flags = 0, drawFbo = 0;
            gl::glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &prof);
            gl::glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
            gl::glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
            juce::String profStr;
            if (prof & GL_CONTEXT_CORE_PROFILE_BIT) profStr << "core";
            if (prof & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) { if (!profStr.isEmpty()) profStr << ","; profStr << "compat"; }
            if (profStr.isEmpty()) profStr = "unknown";
            MDW_LOG("GL", juce::String("Context profile=") + profStr + ", flags=0x" + juce::String::toHexString(flags) + ", drawFBO=" + juce::String(drawFbo));
        }

        MDW_LOG("GL", "newOpenGLContextCreated: end");
}

// Provide the missing definition (unconditional)

// Utility: HSV (0..1) to RGB (0..1)
static inline void mdw_hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b)
{
    h = std::fmod(std::fmod(h, 1.0f) + 1.0f, 1.0f);
    s = juce::jlimit(0.0f, 1.0f, s);
    v = juce::jlimit(0.0f, 1.0f, v);
    if (s <= 0.00001f) { r = g = b = v; return; }
    float hf = h * 6.0f;
    int i = (int) std::floor(hf);
    float f = hf - (float) i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

void ProjectMRenderer::renderOpenGL()
{
    static unsigned frameCount = 0;
    ++frameCount;
    const bool shouldLog = (frameCount % 60u) == 0;

    if (shouldLog) MDW_LOG("GL", "renderOpenGL: begin");

        // Log framebuffer binding periodically to ensure we draw to the visible target
        if (shouldLog)
        {
            GLint drawFbo = 0;
            gl::glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
            MDW_LOG("GL", juce::String("renderOpenGL: drawFBO=") + juce::String(drawFbo));
        }

    if (!setViewportForCurrentScale())
    {
        if (shouldLog) MDW_LOG("GL", "renderOpenGL: no target component");
        return;
    }

    // Always start with a clear to avoid presenting undefined backbuffer contents (prevents brown flicker)
    gl::glClearColor(0.f, 0.f, 0.f, 1.0f);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);

    const float amp = juce::jlimit(0.0f, 4.0f, ampScale.load());
    const float spd = juce::jlimit(0.1f, 3.0f, speedScale.load());
    const float hue = juce::jlimit(0.0f, 1.0f, baseHue.load());
    const float sat = juce::jlimit(0.0f, 1.0f, baseSat.load());
    const int   sd  = seed.load();
    const float hueAdj = std::fmod(hue + (sd * 0.000113f), 1.0f);

    static bool pmDisabledEnv = []() {
        if (const char* env = std::getenv("MILKDAWP_DISABLE_PROJECTM"))
            return (env[0] == '1' || env[0] == 'T' || env[0] == 't' || env[0] == 'Y' || env[0] == 'y');
        return false;
    }();

    // Log once whether the env var is seen and its effect
    static std::atomic<bool> loggedOnce{false};
    bool expected = false;
    if (loggedOnce.compare_exchange_strong(expected, true))
    {
        const char* env = std::getenv("MILKDAWP_DISABLE_PROJECTM");
        juce::String envStr = env ? juce::String(env) : "(null)";
        MDW_LOG("PM", juce::String("Env MILKDAWP_DISABLE_PROJECTM=") + envStr
                        + " -> pmDisabledEnv=" + (pmDisabledEnv ? "true" : "false"));
        MDW_LOG("PM", juce::String("Initial projectMEnabled flag = ")
                        + (projectMEnabled.load(std::memory_order_relaxed) ? "true" : "false"));
    }

    const bool pmDisabled = pmDisabledEnv || !projectMEnabled.load(std::memory_order_relaxed);

   #if defined(HAVE_PROJECTM)
    if (!pmDisabled)
    {
        // Backoff: try init once, then at most every 5 seconds if it failed.
        const double nowSec = Time::getMillisecondCounterHiRes() * 0.001;
        const double retryIntervalSec = 5.0;

        if (!pmReady)
        {
            // Delay initialization until we have a valid framebuffer size and at least a couple of frames rendered
            const bool viewportReady = (fbWidth > 0 && fbHeight > 0);
            const bool warmedUp = (frameCount > 2u);
            if (viewportReady && warmedUp)
            {
                if (!pmInitAttempted || (nowSec - pmInitLastAttemptSec) >= retryIntervalSec)
                {
                    pmInitAttempted = true;
                    pmInitLastAttemptSec = nowSec;

                    MDW_LOG("PM", juce::String("renderOpenGL: initProjectMIfNeeded (fb=") + String(fbWidth) + "x" + String(fbHeight) + ")");
                    initProjectMIfNeeded();
                    MDW_LOG("PM", juce::String("renderOpenGL: pmReady=") + (pmReady ? "true" : "false"));
                }
            }
            else if (shouldLog)
            {
                MDW_LOG("PM", juce::String("renderOpenGL: deferring projectM init (viewportReady=") + (viewportReady?"true":"false") + ", warmedUp=" + (warmedUp?"true":"false") + ")");
            }
        }

        if (pmReady)
        {
            // Apply host-requested preset change if any
           #if defined(HAVE_PROJECTM) && defined(PM_HAVE_V4_C_API)
            const int want = desiredPresetIndex.load(std::memory_order_relaxed);
            if (want >= 0 && !pmPresetList.isEmpty())
            {
                if (want != lastLoadedPresetIndex)
                {
                    const int idx = juce::jlimit(0, pmPresetList.size() - 1, want % juce::jmax(1, pmPresetList.size()));
                    auto path = pmPresetList[idx];
                    MDW_LOG("PM", juce::String("Switching preset by index: ") + juce::String(want) + " -> [" + juce::String(idx) + "] " + path);
                    if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, path.toRawUTF8(), 1))
                        MDW_LOG("PM", "SEH: projectm_load_preset_file threw during switch; continuing");
                    // Force-disable blending for this hard-cut switch to avoid any visual transition
                    projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
                    // Reassert locked preset and disabled transitions
                    projectm_set_preset_locked((projectm_handle) pmHandle, true);
                    projectm_set_hard_cut_enabled((projectm_handle) pmHandle, false);
                    projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
                    lastLoadedPresetIndex = want;
                    lastPresetPath = path;
                }
            }
           #endif

            // If a preset was queued before init completed, load it now
           #if defined(HAVE_PROJECTM)
           #if defined(PM_HAVE_V4) || defined(PM_HAVE_V4_C_API)
            if (hasPendingPreset.load(std::memory_order_acquire))
            {
                juce::String path = pendingPresetPath; // local copy
                const int cut = pendingPresetCut.load(std::memory_order_relaxed);
               #if defined(PM_HAVE_V4_C_API)
                MDW_LOG("PM", juce::String("Processing queued preset -> ") + path + (cut ? " (hard)" : " (soft)"));
                if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, path.toRawUTF8(), cut))
                    MDW_LOG("PM", "SEH: projectm_load_preset_file threw while processing queued preset; continuing");
                // If this was a hard cut, also zero the soft cut duration to avoid any residual blending
                if (cut != 0)
                    projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
                // Reassert locked preset and disabled transitions to prevent auto fades
                projectm_set_preset_locked((projectm_handle) pmHandle, true);
                projectm_set_hard_cut_enabled((projectm_handle) pmHandle, false);
                projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
                projectm_set_preset_duration((projectm_handle) pmHandle, 36000.0);
                projectm_set_hard_cut_duration((projectm_handle) pmHandle, 36000.0);
               #elif defined(PM_HAVE_V4)
                try {
                    MDW_LOG("PM", juce::String("Processing queued preset (C++ API) -> ") + path + (cut ? " (hard)" : " (soft)"));
                    static_cast<PM::ProjectM*>(pmHandle)->loadPresetFile(path.toStdString(), cut != 0);
                } catch (...) {
                    MDW_LOG("PM", "Exception in C++ API while processing queued preset; continuing");
                }
               #endif
                lastLoadedPresetIndex = std::numeric_limits<int>::min();
                hasPendingPreset.store(false, std::memory_order_release);
            }
           #endif
           #endif
            feedProjectMAudioIfAvailable();
            if (shouldLog) MDW_LOG("PM", "renderOpenGL: after feedProjectMAudioIfAvailable");

            // Map UI parameters to projectM runtime parameters (C API)
           #if defined(PM_HAVE_V4_C_API)
            {
                auto inst = (projectm_handle) pmHandle;
                // Speed: map to FPS hint (presets may use it); clamp 10..180
                const int fpsHint = juce::jlimit(10, 180, (int) juce::roundToInt(60.0f * spd));
                static int lastFps = -1;
                if (fpsHint != lastFps)
                {
                    projectm_set_fps(inst, fpsHint);
                    lastFps = fpsHint;
                    if (shouldLog) MDW_LOG("PM", juce::String("Applied speed->fps ") + juce::String(fpsHint));
                }

                // Hue: map to beat sensitivity (0.5..3.0). Higher sensitivity => more reactive/harder cuts.
                const float beatSens = juce::jmap(hue, 0.0f, 1.0f, 0.5f, 3.0f);
                static float lastBeat = std::numeric_limits<float>::quiet_NaN();
                if (beatSens != lastBeat)
                {
                    projectm_set_beat_sensitivity(inst, beatSens);
                    lastBeat = beatSens;
                    if (shouldLog) MDW_LOG("PM", juce::String("Applied hue->beatSensitivity ") + juce::String(beatSens, 3));
                }

                // Saturation: map to mesh size (avoid fighting transition lock). Range: even 16..160
                const int mesh = juce::jlimit(16, 160, (int) juce::roundToInt(16.0f + sat * (160.0f - 16.0f)));
                static int lastMesh = -1;
                if (mesh != lastMesh)
                {
                    const int meshEven = (mesh % 2) ? mesh + 1 : mesh;
                    projectm_set_mesh_size(inst, (size_t) meshEven, (size_t) meshEven);
                    lastMesh = meshEven;
                    if (shouldLog) MDW_LOG("PM", juce::String("Applied sat->meshSize ") + juce::String(meshEven));
                }

                // Seed: map deterministically to the easter_egg parameter (affects preset duration distribution)
                // Map seed to range [0.5, 4.0]
                const float seedF = (float) (0.5 + std::fmod(std::abs((double) sd) * 0.000123, 3.5));
                static float lastEgg = std::numeric_limits<float>::quiet_NaN();
                if (seedF != lastEgg)
                {
                    projectm_set_easter_egg(inst, seedF);
                    lastEgg = seedF;
                    if (shouldLog) MDW_LOG("PM", juce::String("Applied seed->easterEgg ") + juce::String(seedF, 3));
                }

                // Also adapt preset duration inversely to speed for a more pronounced effect (5..45 sec baseline)
                const double baseDuration = juce::jmap(spd, 0.1f, 3.0f, 45.0f, 5.0f);
                static double lastDur = std::numeric_limits<double>::quiet_NaN();
                if (baseDuration != lastDur)
                {
                    projectm_set_preset_duration(inst, baseDuration);
                    lastDur = baseDuration;
                    if (shouldLog) MDW_LOG("PM", juce::String("Applied speed->presetDuration ") + juce::String(baseDuration, 2));
                }
            }
           #endif

            // Neutral background for projectM; avoid animated clears that may obscure faint output
            gl::glClearColor(0.f, 0.f, 0.f, 1.0f);
            gl::glClear(gl::GL_COLOR_BUFFER_BIT);
            if (shouldLog) MDW_LOG("GL", "renderOpenGL: BG clear rgb=0,0,0");

            // Ensure no JUCE program is bound so projectM can use its own pipeline
            context.extensions.glUseProgram(0);

            // In core-profile contexts, a VAO must be bound for any draw calls to work.
            // Bind a dummy VAO to satisfy libraries that don't create their own VAO.
            if (dummyVAO != 0)
                context.extensions.glBindVertexArray(dummyVAO);
            else
                context.extensions.glBindVertexArray(0);
            
            // Additional GL state sanitation before projectM render (helps fixed-function renderers)
            // Keep the currently bound framebuffer (JUCE's target) to ensure output is visible.
            gl::glDisable(gl::GL_DEPTH_TEST);
            gl::glDisable(gl::GL_CULL_FACE);
            gl::glDisable(gl::GL_SCISSOR_TEST);
            gl::glDisable(gl::GL_STENCIL_TEST);
            
            // Allow blending for projectM compositing
            gl::glEnable(gl::GL_BLEND);
            gl::glBlendFunc(gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA);
            
            // Reset pixel storage to defaults
            gl::glPixelStorei(gl::GL_UNPACK_ALIGNMENT, 4);
            gl::glPixelStorei(gl::GL_PACK_ALIGNMENT, 4);
            
            // Ensure texture unit 0 active and unbound (some projectM builds bind their own)
            context.extensions.glActiveTexture(gl::GL_TEXTURE0);
            gl::glBindTexture(gl::GL_TEXTURE_2D, 0);
            gl::glBindTexture(gl::GL_TEXTURE_1D, 0);
            gl::glBindTexture(gl::GL_TEXTURE_3D, 0);
            
            // Ensure color writes enabled
            gl::glColorMask(gl::GL_TRUE, gl::GL_TRUE, gl::GL_TRUE, gl::GL_TRUE);
            
            // Apply auto-play configuration and pending playlist updates
           #if defined(PM_HAVE_V4_C_API)
            {
                auto inst = (projectm_handle) pmHandle;
                // If auto config changed, push it to projectM
                if (autoConfigDirty.load(std::memory_order_acquire))
                {
                    const bool ap  = autoPlayEnabled.load(std::memory_order_relaxed);
                    const bool shf = autoPlayShuffle.load(std::memory_order_relaxed);
                    const bool hcut= autoPlayHardCut.load(std::memory_order_relaxed);
                    // Unlock to allow automatic transitions when enabled; lock to fully disable when off
                    projectm_set_preset_locked(inst, !ap);
                    // Configure durations: use a sane default max display time (20s) and soft cut around 3s
                    const double presetSecs = 20.0;
                    const double softSecs   = 3.0;
                    const double hardMin    = 5.0; // minimum time before a hard cut may occur on beats
                    projectm_set_preset_duration(inst, presetSecs);
                    projectm_set_soft_cut_duration(inst, softSecs);
                    projectm_set_hard_cut_duration(inst, hardMin);
                    // Enable/disable hard cuts (beat-based) according to UI
                    projectm_set_hard_cut_enabled(inst, hcut);
                   #if defined(HAVE_PROJECTM_PLAYLIST)
                    if (pmPlaylist != nullptr)
                    {
                        projectm_playlist_set_shuffle((projectm_playlist_handle) pmPlaylist, shf);
                    }
                   #endif
                    MDW_LOG("PM", juce::String("Applied auto config: ap=") + (ap?"1":"0") + ", shuffle=" + (shf?"1":"0") + ", hardCut=" + (hcut?"1":"0"));
                    autoConfigDirty.store(false, std::memory_order_release);
                }
                // If a new playlist was provided, feed it into the playlist manager
               #if defined(HAVE_PROJECTM_PLAYLIST)
                if (pmPlaylist != nullptr && playlistDirty.load(std::memory_order_acquire))
                {
                    juce::StringArray local;
                    {
                        const juce::ScopedLock sl(playlistLock);
                        local = pendingPlaylist;
                    }
                    auto pl = (projectm_playlist_handle) pmPlaylist;
                    projectm_playlist_clear(pl);
                    if (local.size() > 0)
                    {
                        std::vector<std::string> holder; holder.reserve((size_t)local.size());
                        std::vector<const char*> cstrs; cstrs.reserve((size_t)local.size());
                        for (auto& s : local) { holder.emplace_back(s.toStdString()); cstrs.push_back(holder.back().c_str()); }
                        projectm_playlist_add_presets(pl, cstrs.data(), (uint32_t) cstrs.size(), false);
                    }
                    playlistDirty.store(false, std::memory_order_release);
                }
                // Apply requested playlist position changes on the GL thread
                if (pmPlaylist != nullptr && playlistPosDirty.load(std::memory_order_acquire))
                {
                    int pos = desiredPlaylistPos.load(std::memory_order_relaxed);
                    const bool hard = desiredPlaylistHardCut.load(std::memory_order_relaxed);
                    if (pos < 0) pos = 0;
                    const uint32_t newPos = projectm_playlist_set_position((projectm_playlist_handle) pmPlaylist, (uint32_t) pos, hard);
                    MDW_LOG("PM", juce::String("Playlist set_position -> ") + juce::String(pos) + ", applied pos=" + juce::String((int)newPos) + (hard?" (hard)":" (soft)"));
                    playlistPosDirty.store(false, std::memory_order_release);
                }
               #endif
            }
           #endif
            // Diagnostics: log projectM auto/playlist state periodically to aid debugging
           #if defined(PM_HAVE_V4_C_API)
            if (pmHandle)
            {
                auto inst = (projectm_handle) pmHandle;
                const bool locked = projectm_get_preset_locked(inst);
                const double dPreset = projectm_get_preset_duration(inst);
                const double dSoft   = projectm_get_soft_cut_duration(inst);
                const double dHard   = projectm_get_hard_cut_duration(inst);
                const bool hardEn    = projectm_get_hard_cut_enabled(inst);
                if (shouldLog)
                {
                    MDW_LOG("PM", juce::String("Auto diag: locked=") + (locked?"1":"0") +
                                   ", presetSec=" + juce::String(dPreset, 2) +
                                   ", softSec=" + juce::String(dSoft, 2) +
                                   ", hardMin=" + juce::String(dHard, 2) +
                                   ", hardEnabled=" + (hardEn?"1":"0"));
                }
               #if defined(HAVE_PROJECTM_PLAYLIST)
                if (pmPlaylist)
                {
                    auto pl = (projectm_playlist_handle) pmPlaylist;
                    const bool shuffle = projectm_playlist_get_shuffle(pl);
                    const uint32_t size = projectm_playlist_size(pl);
                    const uint32_t pos  = projectm_playlist_get_position(pl);
                    if (shouldLog)
                    {
                        MDW_LOG("PM", juce::String("Playlist diag: shuffle=") + (shuffle?"1":"0") +
                                       ", size=" + juce::String((int) size) +
                                       ", pos=" + juce::String((int) pos));
                    }
                    // Update cached position for UI sync
                    currentPlaylistPos.store((int) pos, std::memory_order_relaxed);
                    // Watchdog: nudge playlist if auto-play is enabled but position doesn’t change for too long
                    const bool ap = autoPlayEnabled.load(std::memory_order_relaxed);
                    if (ap && !locked && size > 0)
                    {
                        const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
                        if (lastObservedPlaylistPos != (int) pos)
                        {
                            lastObservedPlaylistPos = (int) pos;
                            lastAutoWatchdogTimeSec = nowSec;
                        }
                        else
                        {
                            // Allow preset duration + soft cut + 1.0s grace
                            const double allowSec = juce::jmax(1.0, dPreset + dSoft + 1.0);
                            if (lastAutoWatchdogTimeSec <= 0.0)
                                lastAutoWatchdogTimeSec = nowSec;
                            else if ((nowSec - lastAutoWatchdogTimeSec) > allowSec)
                            {
                                const bool hardCut = autoPlayHardCut.load(std::memory_order_relaxed);
                                uint32_t newPos = projectm_playlist_play_next(pl, hardCut);
                                lastObservedPlaylistPos = (int) newPos;
                                lastAutoWatchdogTimeSec = nowSec;
                                MDW_LOG("PM", juce::String("Auto-play watchdog: play_next -> ") + juce::String((int)newPos) + (hardCut?" (hard)":" (soft)"));
                            }
                        }
                    }
                }
               #endif
            }
           #endif
            // Ensure core-profile compatibility: unbind any program and bind a dummy VAO before letting projectM render
            context.extensions.glUseProgram(0);
            context.extensions.glBindVertexArray(dummyVAO);
            if (shouldLog) MDW_LOG("PM", "renderOpenGL: calling renderProjectMFrame");
            renderProjectMFrame();
            if (shouldLog) MDW_LOG("PM", "renderOpenGL: after renderProjectMFrame");

            // Optional runtime debug overlay and black-frame sampling (no heavy cost)
            static bool dbgOverlay = [](){ if (const char* e = std::getenv("MILKDAWP_DEBUG_OVERLAY")) return e[0]=='1'||e[0]=='T'||e[0]=='t'||e[0]=='Y'||e[0]=='y'; return false; }();
            static int blackCtr = 0;
            if (dbgOverlay)
            {
                // Sample one pixel at the center every 30 frames to detect prolonged black output
                if ((frameCount % 30u) == 0u)
                {
                    unsigned char px[4] = {0,0,0,0};
                    const int cx = jmax(0, fbWidth/2), cy = jmax(0, fbHeight/2);
                    gl::glReadPixels(cx, cy, 1, 1, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, px);
                    const float lum = (0.2126f*px[0] + 0.7152f*px[1] + 0.0722f*px[2]) / 255.0f;
                    if (lum < 0.02f) { if (++blackCtr == 5) MDW_LOG("PM", "Detected ~150 consecutive frames near-black"); }
                    else blackCtr = 0;
                }
                // Draw a tiny green square in the bottom-left to verify presentation
                auto& ext2 = context.extensions;
                GLint vp[4]; gl::glGetIntegerv(gl::GL_VIEWPORT, vp);
                const GLint ovpX = 0, ovpY = 0; const GLint ovpW = jmax(6, fbWidth / 16); const GLint ovpH = jmax(6, fbHeight / 16);
                gl::glViewport(ovpX, ovpY, ovpW, ovpH);
                if (testProgram)
                {
                    testProgram->use();
                    if (testColUniform) testColUniform->set(0.0f, 1.0f, 0.0f);
                    ext2.glBindVertexArray(vao);
                    gl::glDrawArrays(gl::GL_TRIANGLE_STRIP, 0, 4);
                }
                else if (program)
                {
                    program->use();
                    ext2.glBindVertexArray(vao);
                    const float cr = 0.0f, cg = 1.0f, cb = 0.0f; const float s = 0.1f;
                    const float vertsSq[] = {
                        -0.95f, -0.95f,  cr, cg, cb,
                        -0.95f + s, -0.95f,  cr, cg, cb,
                        -0.95f, -0.95f + s,  cr, cg, cb,
                        -0.95f + s, -0.95f + s,  cr, cg, cb
                    };
                    ext2.glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
                    ext2.glBufferData(gl::GL_ARRAY_BUFFER, sizeof(vertsSq), vertsSq, gl::GL_DYNAMIC_DRAW);
                    gl::glDrawArrays(gl::GL_TRIANGLE_STRIP, 0, 4);
                }
                gl::glViewport(vp[0], vp[1], vp[2], vp[3]);
            }
            return;
        }
    }
    else
    {
        static std::atomic<bool> logged{false};
        bool expected2=false;
        if (logged.compare_exchange_strong(expected2, true))
            MDW_LOG("PM", "ProjectM disabled (env or runtime flag); using fallback renderer");
    }
   #endif

    // Fallback visualization
    float level = fallbackLevel;

    // In test mode: for the first 60 frames, clear bright red to prove presentation, then proceed.
    if (testVisMode && frameCount <= 60u)
    {
        gl::glClearColor(1.f, 0.f, 0.f, 1.0f);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT);
        if (shouldLog) MDW_LOG("GL", "renderOpenGL: testVis first frames -> solid red clear");
        return;
    }

    if (testVisMode)
    {
        const double dt = (1.0 / 60.0) * (double) spd;
        const double freq = 1.2;
        testPhase += 2.0 * double_Pi * freq * dt;
        if (testPhase > 2.0 * double_Pi)
            testPhase -= 2.0 * double_Pi;
        const float osc = 0.5f * (1.0f + std::sin((float) testPhase));
        level = 0.7f * level + 0.3f * juce::jlimit(0.0f, 1.0f, osc);
        fallbackLevel = level;
    }
    else
    {
        if (audioFifo != nullptr)
        {
            constexpr int N = 512;
            float tmp[N];
            int got = audioFifo->pop(tmp, N);
            if (got > 0)
            {
                // FIFO health metrics
                fifoSamplesPoppedThisSecond += got;
                const double tNow = Time::getMillisecondCounterHiRes() * 0.001;
                if (tNow - lastFifoLogTimeSec >= 1.0)
                {
                    MDW_LOG("Audio", "FIFO popped ~" + String(fifoSamplesPoppedThisSecond) + " samples in last second");
                    fifoSamplesPoppedThisSecond = 0;
                    lastFifoLogTimeSec = tNow;
                }

                double acc = 0.0;
                for (int i = 0; i < got; ++i) acc += (double) tmp[i] * tmp[i];
                float rms = std::sqrt((float)(acc / jmax(1, got)));
                rms = juce::jlimit(0.0f, 2.0f, rms * amp);
                // Beat sensitivity affects envelope responsiveness: low=slow, high=snappy
                const float a = juce::jmap(hue, 0.0f, 1.0f, 0.05f, 0.6f);
                level = level + a * (rms - level);
                fallbackLevel = level;
            }
            else
            {
                // No fresh audio: inject a gentle time-based oscillation to ensure visible animation
                const double dt = (1.0 / 60.0) * (double) spd;
                const double freq = 0.5; // slow breathing when silence
                testPhase += 2.0 * double_Pi * freq * dt;
                if (testPhase > 2.0 * double_Pi)
                    testPhase -= 2.0 * double_Pi;
                const float osc = 0.5f * (1.0f + std::sin((float) testPhase));
                level = 0.7f * level + 0.3f * osc;
                fallbackLevel = level;
            }
        }
        else
        {
            level *= 0.98f;
            fallbackLevel = level;
        }
    }

    float rc, gc, bc;
    mdw_hsv_to_rgb(hueAdj, sat, juce::jlimit(0.0f, 1.0f, level), rc, gc, bc);

    gl::glClearColor(0.f, 0.f, 0.f, 1.0f);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);

    auto& ext = context.extensions;

    if (testVisMode && testProgram)
    {
        // Attribute-free path using gl_VertexID, no VBO needed (but VAO must be bound)
        testProgram->use();
        if (testColUniform)
            testColUniform->set((GLfloat) rc, (GLfloat) gc, (GLfloat) bc);
        ext.glBindVertexArray(vao);
        gl::glDrawArrays(gl::GL_TRIANGLE_STRIP, 0, 4);
        if (shouldLog) MDW_LOG("GL", "renderOpenGL: testVis gl_VertexID strip drawn");
        return;
    }

    if (!program) { MDW_LOG("GL", "renderOpenGL: program missing"); return; }
    program->use();

    // Update fallback uniforms so UI controls have visible impact without projectM
    if (uHueUniform)  uHueUniform->set((GLfloat) hueAdj);
    if (uSatUniform)  uSatUniform->set((GLfloat) sat);
    if (uLevelUniform) uLevelUniform->set((GLfloat) juce::jlimit(0.0f, 1.0f, level));
    // Map mesh size similarly to projectM: even 16..160 pixels
    int meshPix = juce::jlimit(16, 160, (int) juce::roundToInt(16.0f + sat * (160.0f - 16.0f)));
    if (meshPix % 2) meshPix += 1;
    if (uMeshUniform) uMeshUniform->set((GLfloat) meshPix);
    if (uSeedUniform) uSeedUniform->set((GLfloat) sd);

    {
        auto err = gl::glGetError();
        if (err != gl::GL_NO_ERROR)
            MDW_LOG("GL", juce::String("After program->use (render) glError=0x") + juce::String::toHexString((int)err));
    }


    const float verts[] = {
        // TRIANGLE_STRIP order
        -0.95f, -0.95f, rc, gc, bc, // bottom-left
         0.95f, -0.95f, rc, gc, bc, // bottom-right
        -0.95f,  0.95f, rc, gc, bc, // top-left
         0.95f,  0.95f, rc, gc, bc  // top-right
    };
    ext.glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
    ext.glBufferSubData(gl::GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    ext.glBindVertexArray(vao);
    gl::glDrawArrays(gl::GL_TRIANGLE_STRIP, 0, 4);
    {
        auto err = gl::glGetError();
        if (err != gl::GL_NO_ERROR)
            MDW_LOG("GL", juce::String("After glDrawArrays glError=0x") + juce::String::toHexString((int)err));
    }

    if (shouldLog) MDW_LOG("GL", "renderOpenGL: end");
}

void ProjectMRenderer::openGLContextClosing()
{
    #if defined(HAVE_PROJECTM)
        shutdownProjectM();
    #endif
    auto& ext = context.extensions;
    if (vbo != 0) { ext.glDeleteBuffers(1, &vbo); vbo = 0; }
    if (vao != 0) { ext.glDeleteVertexArrays(1, &vao); vao = 0; }
    program.reset();
}

#if defined(HAVE_PROJECTM)
void ProjectMRenderer::initProjectMIfNeeded()
{
    if (pmReady) return;
   #if defined(PM_HAVE_V4)
    MDW_LOG("PM", "initProjectMIfNeeded: using v4 C++ API");
    // Guard: require a valid preset directory, or skip initializing projectM
    {
        juce::File pd(pmPresetDir);
        if (!pd.isDirectory())
        {
            MDW_LOG("PM", "Preset directory missing; skipping projectM init (fallback renderer will be used)");
            pmReady = false;
            return;
        }
    }
    try {
        PM::Settings settings{};
        juce::File pd(pmPresetDir);
        if (pd.isDirectory())
            settings.presetPath = pd.getFullPathName().toStdString();
        settings.windowWidth  = juce::jmax(1, fbWidth);
        settings.windowHeight = juce::jmax(1, fbHeight);
        settings.meshX = 48; settings.meshY = 36; // typical defaults
        MDW_LOG("PM", "C++ API: constructing PM::ProjectM");
        auto* engine = new PM::ProjectM(settings);
        pmHandle = static_cast<void*>(engine);
        pmReady = true;
        MDW_LOG("PM", "projectM v4 initialized (C++ API)");

        // Scan presets from pmPresetDir so desiredPresetIndex can be applied immediately
        if (pmPresetDir.isNotEmpty())
        {
            juce::File pd(pmPresetDir);
            if (pd.isDirectory())
            {
                pmPresetList.clearQuick();
                juce::DirectoryIterator it(pd, true, "*.milk", juce::File::findFiles);
                while (it.next())
                    pmPresetList.add(it.getFile().getFullPathName());
                MDW_LOG("PM", juce::String("Preset scan: found ") + juce::String(pmPresetList.size()) + " presets under " + pmPresetDir);
            }
        }

        // Apply any queued or last-known preset immediately so first frame is correct
        bool appliedInitial = false;
        if (hasPendingPreset.load(std::memory_order_acquire))
        {
            juce::String path = pendingPresetPath;
            const bool hard = pendingPresetCut.load(std::memory_order_relaxed) != 0;
            try {
                MDW_LOG("PM", juce::String("C++ API Init: applying queued preset -> ") + path + (hard ? " (hard)" : " (soft)"));
                static_cast<PM::ProjectM*>(pmHandle)->loadPresetFile(path.toStdString(), hard);
            } catch (...) {
                MDW_LOG("PM", "Exception while applying queued preset at init (C++ API)");
            }
            hasPendingPreset.store(false, std::memory_order_release);
            lastLoadedPresetIndex = std::numeric_limits<int>::min();
            appliedInitial = true;
        }
        if (!appliedInitial && lastPresetPath.isNotEmpty())
        {
            try {
                MDW_LOG("PM", juce::String("C++ API Init: applying last preset -> ") + lastPresetPath);
                static_cast<PM::ProjectM*>(pmHandle)->loadPresetFile(lastPresetPath.toStdString(), true);
            } catch (...) {}
            appliedInitial = true;
        }
        if (!appliedInitial)
        {
            const int wantIdx = desiredPresetIndex.load(std::memory_order_relaxed);
            if (wantIdx >= 0 && !pmPresetList.isEmpty())
            {
                const int idx = juce::jlimit(0, pmPresetList.size() - 1, wantIdx % juce::jmax(1, pmPresetList.size()));
                auto path = pmPresetList[idx];
                try {
                    MDW_LOG("PM", juce::String("C++ API Init: applying desired preset index -> ") + juce::String(wantIdx) + " => [" + juce::String(idx) + "] " + path);
                    static_cast<PM::ProjectM*>(pmHandle)->loadPresetFile(path.toStdString(), true);
                } catch (...) {}
                lastLoadedPresetIndex = wantIdx;
                lastPresetPath = path;
                appliedInitial = true;
            }
        }
        if (!appliedInitial)
        {
            MDW_LOG("PM", "C++ API Init: no preset to apply (will render default state)");
        }
    } catch (const std::exception& e) {
        MDW_LOG("PM", juce::String("Init failed: ") + e.what());
        pmReady = false;
    } catch (...) {
        MDW_LOG("PM", "Init failed: unknown exception");
        pmReady = false;
    }
   #elif defined(PM_HAVE_V4_C_API)
    MDW_LOG("PM", "initProjectMIfNeeded: using v4 C API");
    // Log GL strings and framebuffer just before creating the instance
    MDW_LOG("GL", juce::String("GL before projectM create -> Version=") + String((const char*) gl::glGetString(gl::GL_VERSION))
                    + ", Renderer=" + String((const char*) gl::glGetString(gl::GL_RENDERER))
                    + ", Vendor=" + String((const char*) gl::glGetString(gl::GL_VENDOR))
                    + ", FB=" + String(fbWidth) + "x" + String(fbHeight));
    const size_t w = (size_t) jmax(1, fbWidth);
    const size_t h = (size_t) jmax(1, fbHeight);
    int ok = 0;
    projectm_handle inst = mdw_seh_projectm_minimal_init(w, h, &ok);
    if (inst == nullptr || ok == 0)
    {
        MDW_LOG("PM", "C API init failed or was trapped by SEH; staying on fallback");
        pmHandle = nullptr;
        pmReady = false;
        return;
    }
    // Inform projectM of the current viewport size and basic runtime params
    projectm_set_window_size(inst, (size_t) w, (size_t) h);
    projectm_set_aspect_correction(inst, true);
    projectm_set_fps(inst, 60);

    pmHandle = (void*) inst;
    pmReady = true;
    MDW_LOG("PM", "projectM v4 initialized (C API)");

   #if defined(HAVE_PROJECTM_PLAYLIST)
    {
        auto pl = projectm_playlist_create(inst);
        if (pl != nullptr)
        {
            projectm_playlist_connect(pl, inst);
            pmPlaylist = (void*) pl;
            MDW_LOG("PM", "projectM playlist API available: playlist manager created and connected");
        }
        else
        {
            MDW_LOG("PM", "projectM playlist API requested but creation failed");
        }
    }
   #else
    MDW_LOG("PM", "projectM playlist API not available at build time");
   #endif

    // Disable all automatic transitions and lock the current preset to prevent fade-to-black
    projectm_set_preset_locked(inst, true);
    projectm_set_hard_cut_enabled(inst, false);
    projectm_set_soft_cut_duration(inst, 0.0);
    // Also make soft and hard cut timers effectively inert
    projectm_set_preset_duration(inst, 36000.0);
    projectm_set_hard_cut_duration(inst, 36000.0);

    // Provide texture search paths to projectM (preset dir + common textures folders) to avoid asset lookup failures
    {
        juce::StringArray paths;
        if (pmPresetDir.isNotEmpty())
        {
            paths.addIfNotAlreadyThere(pmPresetDir);
            juce::File texA(juce::File(pmPresetDir).getChildFile("textures"));
            if (texA.isDirectory()) paths.addIfNotAlreadyThere(texA.getFullPathName());
        }
        // Dev-tree resources textures
        {
            auto exe = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            juce::File dir = exe.getParentDirectory();
            for (int i = 0; i < 6; ++i)
            {
                juce::File test = dir.getChildFile("resources").getChildFile("textures");
                if (test.isDirectory()) { paths.addIfNotAlreadyThere(test.getFullPathName()); break; }
                dir = dir.getParentDirectory();
            }
        }
        if (paths.size() > 0)
        {
            std::vector<std::string> holder; holder.reserve((size_t)paths.size());
            std::vector<const char*> cstrs; cstrs.reserve((size_t)paths.size());
            for (auto& p : paths)
            {
                holder.emplace_back(p.toStdString());
                cstrs.push_back(holder.back().c_str());
            }
            projectm_set_texture_search_paths(inst, cstrs.data(), cstrs.size());
            MDW_LOG("PM", juce::String("Set texture search paths (") + juce::String((int) cstrs.size()) + ")");
        }
    }

    // Initialize with the user's chosen preset immediately if available to avoid a flash of the default
    pmPresetList.clear();

    // Scan presets from pmPresetDir so desiredPresetIndex can be applied immediately
    if (pmPresetDir.isNotEmpty())
    {
        juce::File pd(pmPresetDir);
        if (pd.isDirectory())
        {
            pmPresetList.clearQuick();
            juce::DirectoryIterator it(pd, true, "*.milk", juce::File::findFiles);
            while (it.next())
                pmPresetList.add(it.getFile().getFullPathName());
            MDW_LOG("PM", juce::String("Preset scan: found ") + juce::String(pmPresetList.size()) + " presets under " + pmPresetDir);
        }
    }

    bool appliedInitial = false;

    // If a preset was queued before init, apply it synchronously now so the first frame uses it
    if (hasPendingPreset.load(std::memory_order_acquire))
    {
        juce::String path = pendingPresetPath; // local copy
        const int cut = pendingPresetCut.load(std::memory_order_relaxed);
        MDW_LOG("PM", juce::String("Init: applying queued preset -> ") + path + (cut ? " (hard)" : " (soft)"));
        if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, path.toRawUTF8(), cut))
            MDW_LOG("PM", "SEH: projectm_load_preset_file threw during init; continuing");
        // If hard cut, kill soft cut duration once to avoid initial blending
        if (cut != 0)
            projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
        // Reassert locked preset and disabled transitions
        projectm_set_preset_locked((projectm_handle) pmHandle, true);
        projectm_set_hard_cut_enabled((projectm_handle) pmHandle, false);
        projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
        hasPendingPreset.store(false, std::memory_order_release);
        lastLoadedPresetIndex = std::numeric_limits<int>::min();
        appliedInitial = true;
    }

    // Otherwise, if a desired index is known and our list is populated, load it now
    if (!appliedInitial)
    {
        const int wantIdx = desiredPresetIndex.load(std::memory_order_relaxed);
        if (wantIdx >= 0 && !pmPresetList.isEmpty())
        {
            const int idx = juce::jlimit(0, pmPresetList.size() - 1, wantIdx % juce::jmax(1, pmPresetList.size()));
            auto path = pmPresetList[idx];
            MDW_LOG("PM", juce::String("Init: applying desired preset index -> ") + juce::String(wantIdx) + " => [" + juce::String(idx) + "] " + path);
            if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, path.toRawUTF8(), 1))
                MDW_LOG("PM", "SEH: projectm_load_preset_file threw during init index load; continuing");
            // Ensure no initial blending when forcing the first preset
            projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
            // Reassert locked preset and disabled transitions
            projectm_set_preset_locked((projectm_handle) pmHandle, true);
            projectm_set_hard_cut_enabled((projectm_handle) pmHandle, false);
            projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
            lastLoadedPresetIndex = wantIdx;
            lastPresetPath = path;
            appliedInitial = true;
        }
    }

    // Otherwise, try the last-known preset path if available
    if (!appliedInitial && lastPresetPath.isNotEmpty())
    {
        if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, lastPresetPath.toRawUTF8(), 1))
            MDW_LOG("PM", "SEH: projectm_load_preset_file threw during init lastPresetPath load; continuing");
        // Reassert locked preset and disabled transitions
        projectm_set_preset_locked((projectm_handle) pmHandle, true);
        projectm_set_hard_cut_enabled((projectm_handle) pmHandle, false);
        projectm_set_soft_cut_duration((projectm_handle) pmHandle, 0.0);
        MDW_LOG("PM", juce::String("C API: applied last-known preset: ") + lastPresetPath);
        appliedInitial = true;
    }

    // Fallback: use idle (no preset) only if nothing else was available
    if (!appliedInitial)
    {
        const juce::String initialPreset = "idle://";
        if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, initialPreset.toRawUTF8(), 0))
            MDW_LOG("PM", "SEH: projectm_load_preset_file threw; continuing");
        MDW_LOG("PM", juce::String("C API: initial preset loaded: ") + initialPreset + " (no preset scanned)");
    }
   #else
    static std::atomic<bool> logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true))
        MDW_LOG("PM", "v4 headers not found at compile time");
    pmReady = false;
   #endif
}

void ProjectMRenderer::loadPresetByPath(const juce::String& absolutePath, bool hardCut)
{
   #if defined(HAVE_PROJECTM)
    // Remember last requested preset so GL re-inits can apply it immediately.
    lastPresetPath = absolutePath;
    // Always queue and let the GL thread apply the preset. This avoids calling
    // projectM API functions off the GL thread (no current context), which can
    // result in the change not taking effect.
    pendingPresetPath = absolutePath;
    pendingPresetCut.store(hardCut ? 1 : 0, std::memory_order_relaxed);
    hasPendingPreset.store(true, std::memory_order_release);
    if (!pmReady || !pmHandle)
        MDW_LOG("PM", juce::String("loadPresetByPath: queued until projectM ready -> ") + absolutePath);
    else
        MDW_LOG("PM", juce::String("loadPresetByPath: queued for GL thread -> ") + absolutePath + (hardCut ? " (hard)" : " (soft)"));
    // Reset index tracking so that future desiredPresetIndex switches (if any) will reload appropriately
    lastLoadedPresetIndex = std::numeric_limits<int>::min();
   #else
    juce::ignoreUnused(absolutePath, hardCut);
   #endif
}

void ProjectMRenderer::shutdownProjectM()
{
    if (!pmHandle) return;
   #if defined(PM_HAVE_V4)
    try { delete static_cast<PM::ProjectM*>(pmHandle); } catch (...) {}
    pmHandle = nullptr;
    pmReady = false;
    MDW_LOG("PM", "Shutdown (C++ API)");
   #elif defined(PM_HAVE_V4_C_API)
   #if defined(HAVE_PROJECTM_PLAYLIST)
    if (pmPlaylist != nullptr)
    {
        projectm_playlist_destroy((projectm_playlist_handle) pmPlaylist);
        pmPlaylist = nullptr;
    }
   #endif
    projectm_destroy((projectm_handle) pmHandle);
    pmHandle = nullptr;
    pmReady = false;
    MDW_LOG("PM", "Shutdown (C API)");
   #endif
}

void ProjectMRenderer::renderProjectMFrame()
{
    if (!pmReady || !pmHandle) return;
   #if defined(PM_HAVE_V4)
    static_cast<PM::ProjectM*>(pmHandle)->renderFrame();
   #elif defined(PM_HAVE_V4_C_API)
    // Diagnose GL errors around projectM render
    {
        GLenum preErr = gl::glGetError();
        if (preErr != gl::GL_NO_ERROR)
            MDW_LOG("PM", juce::String("GL error before projectM render: 0x") + juce::String::toHexString((int)preErr));
    }
    if (!mdw_seh_projectm_render((projectm_handle) pmHandle))
    {
        MDW_LOG("PM", "SEH: exception during projectM render; shutting down and falling back");
        shutdownProjectM();
    }
    else
    {
        GLenum postErr = gl::glGetError();
        if (postErr != gl::GL_NO_ERROR)
            MDW_LOG("PM", juce::String("GL error after projectM render: 0x") + juce::String::toHexString((int)postErr));
    }
   #endif
}

void ProjectMRenderer::feedProjectMAudioIfAvailable()
{
   #if defined(PM_HAVE_V4)
    if (!pmReady || !pmHandle || audioFifo == nullptr) return;

    constexpr int kPull = 1024;
    float tmp[kPull];
    int popped = 0;
    do {
        int got = audioFifo->pop(tmp, kPull); // ensure non-const local
        if (got <= 0) break;
        popped += got;

        try {
            auto* engine = static_cast<PM::ProjectM*>(pmHandle);
            // TODO: Replace with the correct audio ingestion API for your C++ projectM build, if used.
            // e.g., engine->addPCMfloat(tmp, got, audioSampleRate);
        } catch (...) {
        }
    } while (popped < 8192);
   #elif defined(PM_HAVE_V4_C_API)
    if (!pmReady || !pmHandle || audioFifo == nullptr) return;

    constexpr int kPull = 1024;
    float tmp[kPull];
    int popped = 0;
    const float amp = juce::jlimit(0.0f, 4.0f, ampScale.load());

    do {
        int got = audioFifo->pop(tmp, kPull); // ensure non-const local
        if (got <= 0) break;
        popped += got;

        if (amp != 1.0f)
        {
            for (int i = 0; i < got; ++i)
                tmp[i] = juce::jlimit(-1.5f, 1.5f, tmp[i] * amp);
        }

        if (!mdw_seh_projectm_pcm_add_mono((projectm_handle) pmHandle, tmp, (unsigned int) got))
        {
            MDW_LOG("PM", "SEH: exception during projectM PCM add; shutting down and falling back");
            shutdownProjectM();
            break;
        }
    } while (popped < 8192);

    // Log feed rate once per second
    static int pmSamplesFedThisSecond = 0;
    static double pmLastFeedLogSec = 0.0;
    pmSamplesFedThisSecond += popped;
    const double tNow = Time::getMillisecondCounterHiRes() * 0.001;
    if (tNow - pmLastFeedLogSec >= 1.0)
    {
        MDW_LOG("PM", "PM Audio fed ~" + String(pmSamplesFedThisSecond) + " samples in last second");
        pmSamplesFedThisSecond = 0;
        pmLastFeedLogSec = tNow;
    }
   #endif
}
#endif
