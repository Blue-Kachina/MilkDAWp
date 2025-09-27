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
void main(){ FragColor=vec4(vCol,1.0); })";

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
       // No window sizing calls for v4 C API; it derives viewport from the current OpenGL state.
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
    // Bind once to query attributes, then unbind to keep fixed-function available for projectM
    program->use();

    attrPos = std::make_unique<OpenGLShaderProgram::Attribute>(*program, "aPos");
    attrCol = std::make_unique<OpenGLShaderProgram::Attribute>(*program, "aCol");
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
    // Resolve preset dir (unchanged)...
    auto exe = File::getSpecialLocation(File::currentApplicationFile);
    auto bundleRoot = exe.getParentDirectory().getParentDirectory();
    File presetsA = bundleRoot.getChildFile("Contents").getChildFile("Resources").getChildFile("presets");
    File presetsB = bundleRoot.getChildFile("Resources").getChildFile("presets");
    File chosen = presetsA.isDirectory() ? presetsA : (presetsB.isDirectory() ? presetsB : File());
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

    const float b = juce::jlimit(0.0f, 2.0f, brightness.load());
    const float sens = juce::jlimit(0.0f, 4.0f, sensitivity.load());

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
            if (!pmPresetList.isEmpty())
            {
                const int want = desiredPresetIndex.load(std::memory_order_relaxed);
                if (want != lastLoadedPresetIndex)
                {
                    const int idx = juce::jlimit(0, pmPresetList.size() - 1, want % juce::jmax(1, pmPresetList.size()));
                    auto path = pmPresetList[idx];
                    MDW_LOG("PM", juce::String("Switching preset by index: ") + juce::String(want) + " -> [" + juce::String(idx) + "] " + path);
                    if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, path.toRawUTF8(), 1))
                        MDW_LOG("PM", "SEH: projectm_load_preset_file threw during switch; continuing");
                    lastLoadedPresetIndex = want;
                }
            }
           #endif

            feedProjectMAudioIfAvailable();
            if (shouldLog) MDW_LOG("PM", "renderOpenGL: after feedProjectMAudioIfAvailable");

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
            
            // Ensure we are drawing to the back buffer
            gl::glDrawBuffer(gl::GL_BACK);
            #ifndef GL_DRAW_BUFFER
            #define GL_DRAW_BUFFER 0x0C01
            #endif
            {
                GLint db=0; gl::glGetIntegerv(GL_DRAW_BUFFER, &db);
                if (shouldLog) MDW_LOG("GL", juce::String("renderOpenGL: drawBuffer=0x") + juce::String::toHexString(db));
            }
            
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
            
            if (shouldLog) MDW_LOG("PM", "renderOpenGL: calling renderProjectMFrame");
            renderProjectMFrame();
            if (shouldLog) MDW_LOG("PM", "renderOpenGL: after renderProjectMFrame");

           #if defined(MILKDAWP_ENABLE_DEBUG_OVERLAY)
            // Debug overlay to confirm animation/preset selection even if projectM draws nothing.
            // This does not replace projectM; it just renders on top as a diagnostic visual.
            // Attribute-free gl_VertexID quad, bright white
            if (testProgram)
            {
                auto& ext2 = context.extensions;
                // Draw overlay in a tiny viewport at the bottom-left corner
                GLint vp[4]; gl::glGetIntegerv(gl::GL_VIEWPORT, vp);
                const GLint ovpX = 0, ovpY = 0; const GLint ovpW = jmax(8, fbWidth / 10); const GLint ovpH = jmax(6, fbHeight / 10);
                gl::glViewport(ovpX, ovpY, ovpW, ovpH);
                testProgram->use();
                if (testColUniform) testColUniform->set(1.0f, 1.0f, 1.0f);
                ext2.glBindVertexArray(vao);
                gl::glDrawArrays(gl::GL_TRIANGLE_STRIP, 0, 4);
                gl::glViewport(vp[0], vp[1], vp[2], vp[3]);
                auto err = gl::glGetError();
                if (err != gl::GL_NO_ERROR)
                    MDW_LOG("GL", juce::String("Overlay glDrawArrays error=0x") + juce::String::toHexString((int)err));
            }
            else if (program)
            {
                auto& ext2 = context.extensions;
                program->use();
                ext2.glBindVertexArray(vao);
                const float cr = 1.0f, cg = 1.0f, cb = 1.0f;
                const float s = 0.1f;
                const float vertsSq[] = {
                    -0.95f, -0.95f,  cr, cg, cb,
                    -0.95f + s, -0.95f,  cr, cg, cb,
                    -0.95f, -0.95f + s,  cr, cg, cb,
                    -0.95f + s, -0.95f + s,  cr, cg, cb
                };
                ext2.glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
                ext2.glBufferData(gl::GL_ARRAY_BUFFER, sizeof(vertsSq), vertsSq, gl::GL_DYNAMIC_DRAW);
                gl::glDrawArrays(gl::GL_TRIANGLE_STRIP, 0, 4);
                auto err = gl::glGetError();
                if (err != gl::GL_NO_ERROR)
                    MDW_LOG("GL", juce::String("Overlay (fallback) glDrawArrays error=0x") + juce::String::toHexString((int)err));
            }
           #endif // MILKDAWP_ENABLE_DEBUG_OVERLAY
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
        const double dt = 1.0 / 60.0;
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
                rms = juce::jlimit(0.0f, 2.0f, rms * sens);
                const float a = 0.25f;
                level = level + a * (rms - level);
                fallbackLevel = level;
            }
            else
            {
                // No fresh audio: inject a gentle time-based oscillation to ensure visible animation
                const double dt = 1.0 / 60.0;
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

    const float base = juce::jlimit(0.0f, 2.0f, b);
    const float rc = juce::jlimit(0.0f, 1.0f, 0.15f * base + 0.85f * juce::jlimit(0.0f, 1.0f, level));
    const float gc = juce::jlimit(0.0f, 1.0f, 0.10f * base + 0.45f * juce::jlimit(0.0f, 1.0f, level));
    const float bc = juce::jlimit(0.0f, 1.0f, 0.08f * base + 0.35f * juce::jlimit(0.0f, 1.0f, level));

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

    // Build preset list for host selection and pick an initial preset
    pmPresetList.clear();
    juce::String initialPreset;
    {
        juce::File pd(pmPresetDir);
        if (pd.isDirectory())
        {
            Array<File> found;
            pd.findChildFiles(found, File::findFiles, true, "*.milk");
            for (auto& f : found)
                pmPresetList.add(f.getFullPathName());

            if (found.size() > 0)
            {
                // Prefer a clearly visible, audio-reactive test preset if available
                // 1) Strong preference: spectrum presets
                for (auto& f : found)
                {
                    auto name = f.getFileName().toLowerCase();
                    if (name.contains("spectrum"))
                    {
                        initialPreset = f.getFullPathName();
                        break;
                    }
                }
                // 2) Next: wave presets
                if (initialPreset.isEmpty())
                {
                    for (auto& f : found)
                    {
                        auto name = f.getFileName().toLowerCase();
                        if (name.contains("wave"))
                        {
                            initialPreset = f.getFullPathName();
                            break;
                        }
                    }
                }
                // Otherwise, pick the first non-empty looking preset
                if (initialPreset.isEmpty())
                {
                    for (auto& f : found)
                    {
                        auto name = f.getFileName().toLowerCase();
                        if (!name.contains("empty"))
                        {
                            initialPreset = f.getFullPathName();
                            break;
                        }
                    }
                }
                // Fallback: first entry
                if (initialPreset.isEmpty())
                    initialPreset = found.getReference(0).getFullPathName();
            }
        }
    }
    if (initialPreset.isEmpty())
        initialPreset = "idle://";

    if (!mdw_seh_projectm_load_preset_file((projectm_handle) pmHandle, initialPreset.toRawUTF8(), 0))
        MDW_LOG("PM", "SEH: projectm_load_preset_file threw; continuing");

    MDW_LOG("PM", juce::String("C API: initial preset loaded: ") + initialPreset);
   #else
    static std::atomic<bool> logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true))
        MDW_LOG("PM", "v4 headers not found at compile time");
    pmReady = false;
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
    const float sens = juce::jlimit(0.0f, 4.0f, sensitivity.load());

    do {
        int got = audioFifo->pop(tmp, kPull); // ensure non-const local
        if (got <= 0) break;
        popped += got;

        if (sens != 1.0f)
        {
            for (int i = 0; i < got; ++i)
                tmp[i] = juce::jlimit(-1.5f, 1.5f, tmp[i] * sens);
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
