#include "VisualizationWindow.h"
#include "../renderers/ProjectMRenderer.h"
#include "../utils/Logging.h" // logging

VisualizationWindow::VisualizationWindow(LockFreeAudioFifo* fifo, int sampleRate, const juce::String& initialPresetPath, int initialPresetIndex)
    : juce::DocumentWindow(ProjectMRenderer::kWindowTitle,
                           juce::Colours::black,
                           juce::DocumentWindow::closeButton)
{
    MDW_LOG("UI", "VisualizationWindow: constructing");
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread()); // must be UI thread
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(300, 200, 8192, 8192);
    // Hand ownership of the GLComponent to the window.
    glView = new GLComponent(fifo, sampleRate, initialPresetPath, initialPresetIndex);
    // Hand over a non-owning content; previous working state used non-owning content assignment
    setContentOwned(glView, false);
    if (auto* c = getContentComponent()) c->setBounds(getLocalBounds());
    centreWithSize(1000, 750);

    // Ensure we can receive key events (for ESC to exit fullscreen)
    setWantsKeyboardFocus(true);

    // Do not force visibility/topmost here; caller will decide (docked vs undocked)
    setAlwaysOnTop(false);
    setVisible(false);

    startTimerHz(10);
}

void VisualizationWindow::resized()
{
    // Ensure content follows the window size during interactive resize
    if (auto* c = getContentComponent())
        c->setBounds(getLocalBounds());
}

VisualizationWindow::~VisualizationWindow()
{
    MDW_LOG("UI", "VisualizationWindow: destroying");
    stopTimer();

    // Ensure GL is torn down on the message thread BEFORE removing content/peer
    if (glView)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            glView->shutdownGL();
        }
        else
        {
            juce::MessageManager::getInstance()->callFunctionOnMessageThread(
                [](void* ctx) -> void*
                {
                    auto* self = static_cast<VisualizationWindow*>(ctx);
                    if (self->glView)
                        self->glView->shutdownGL();
                    return nullptr;
                }, this);
        }
    }

    // Now it is safe to remove the content from the window
    setContentOwned(nullptr, false);

    // Content has been deleted by the window; clear our non-owning pointer.
    glView = nullptr;
}

// Implementations that were missing (causing LNK2019/LNK2001)
void VisualizationWindow::closeButtonPressed()
{
    MDW_LOG("UI", "VisualizationWindow: close button pressed");
    if (onUserClosed)
        onUserClosed();
    setVisible(false);
}

void VisualizationWindow::setFullScreenParam(bool shouldBeFullScreen)
{
    // Implement true borderless fullscreen that can be toggled and restored
    bool changed = false;
    if (shouldBeFullScreen)
    {
        if (!inFullscreen)
        {
            inFullscreen = true;
            changed = true;
            // Save current windowed bounds for later restore
            lastWindowBounds = getBounds();
            // Use borderless native window for fullscreen to avoid chrome
            setUsingNativeTitleBar(false);
            setResizable(false, false);
            setAlwaysOnTop(true);

            // Expand to the monitor bounds containing this window
            if (auto* disp = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getBounds()))
                setBounds(disp->userArea);
            setFullScreen(true);
            toFront(true);
        }
    }
    else
    {
        if (inFullscreen)
        {
            inFullscreen = false;
            changed = true;
            setFullScreen(false);
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setAlwaysOnTop(true);
            // Restore previous window bounds if valid, otherwise use a sensible default
            if (! lastWindowBounds.isEmpty())
                setBounds(lastWindowBounds);
            else
                centreWithSize(1000, 750);
            toFront(true);
        }
    }

    if (changed)
    {
        if (onFullscreenChanged)
            onFullscreenChanged(inFullscreen);
    }
}

void VisualizationWindow::syncTitleForOBS()
{
    setName(ProjectMRenderer::kWindowTitle);
}

void VisualizationWindow::dockTo(juce::Component* parentComponent)
{
    if (parentComponent == nullptr)
        return;

    MDW_LOG("UI", juce::String("VisualizationWindow::dockTo BEGIN - hasPeer=") + (getPeer()?"yes":"no"));

    // Prepare GL for peer transition (detach) before we destroy the current peer
    if (glView)
        glView->prepareForPeerChange();

    // Embed this window as a child component inside the editor instead of a separate native window
    removeFromDesktop(); // ensure we are not a top-level peer
    setUsingNativeTitleBar(false);
    setTitleBarButtonsRequired(0, false);
    setTitleBarHeight(0);
    setResizable(false, false);
    setAlwaysOnTop(false);

    // Add as a child component to the parent and make visible
    parentComponent->addAndMakeVisible(this);
    // Also ensure our content component is visible immediately
    if (auto* c = getContentComponent())
        c->setVisible(true);
    docked = true;

    // Request GL reattach on the new peer
    if (glView)
        glView->requestReattach();

    MDW_LOG("UI", juce::String("VisualizationWindow::dockTo END - hasPeer=") + (getPeer()?"yes":"no") + ", docked=yes");
}

void VisualizationWindow::undock()
{
    MDW_LOG("UI", juce::String("VisualizationWindow::undock BEGIN - hasPeer=") + (getPeer()?"yes":"no"));

    // Detach from parent if currently embedded
    if (auto* p = getParentComponent())
        p->removeChildComponent(this);

    // Prepare GL for peer transition (detach) before we destroy the current peer
    if (glView)
        glView->prepareForPeerChange();

    // Make it a standalone top-level window again
    removeFromDesktop();
    setUsingNativeTitleBar(true);
    setTitleBarButtonsRequired(juce::DocumentWindow::closeButton, false);
    setTitleBarHeight(24);
    setResizable(true, true);
    setAlwaysOnTop(true);
    addToDesktop(juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowIsResizable);
    toFront(true);
    docked = false;

    // Request GL reattach on the new peer
    if (glView)
        glView->requestReattach();

    MDW_LOG("UI", juce::String("VisualizationWindow::undock END - hasPeer=") + (getPeer()?"yes":"no") + ", docked=no");
}

bool VisualizationWindow::keyPressed(const juce::KeyPress& key)
{
    // Only handle ESC: exit fullscreen if currently in fullscreen
    if (key == juce::KeyPress::escapeKey && isFullScreen())
    {
        MDW_LOG("UI", "VisualizationWindow: ESC -> exit fullscreen");
        setFullScreenParam(false);
        return true;
    }
    return false;
}

void VisualizationWindow::timerCallback()
{
    syncTitleForOBS();
}

// Forward visual params to the GL component
void VisualizationWindow::setVisualParams(float amplitude, float speed)
{
    if (glView != nullptr)
        glView->setVisualParams(amplitude, speed);
}

void VisualizationWindow::setColorParams(float hue01, float sat01)
{
    if (glView != nullptr)
        glView->setColorParams(hue01, sat01);
}

void VisualizationWindow::setSeed(int s)
{
    if (glView != nullptr)
        glView->setSeed(s);
}

void VisualizationWindow::setPresetIndex(int index)
{
    if (glView != nullptr)
        glView->setPresetIndex(index);
}

void VisualizationWindow::loadPresetByPath(const juce::String& absolutePath, bool hardCut)
{
    if (glView != nullptr)
        glView->loadPresetByPath(absolutePath, hardCut);
}

void VisualizationWindow::setAutoPlayFlags(bool enabled, bool randomShuffle, bool hardCut)
{
    if (glView)
        glView->setAutoPlayFlags(enabled, randomShuffle, hardCut);
}

void VisualizationWindow::setProjectMPlaylist(const juce::StringArray& absolutePaths)
{
    if (glView)
        glView->setProjectMPlaylist(absolutePaths);
}

void VisualizationWindow::setProjectMPlaylistPosition(int index, bool hardCut)
{
    if (glView)
        glView->setProjectMPlaylistPosition(index, hardCut);
}

bool VisualizationWindow::supportsProjectMAuto() const
{
    if (glView)
        return glView->supportsProjectMAuto();
    return false;
}

int VisualizationWindow::getProjectMPlaylistPosition() const
{
    if (glView)
        return glView->getProjectMPlaylistPosition();
    return -1;
}

// ===== GLComponent =====

VisualizationWindow::GLComponent::GLComponent(LockFreeAudioFifo* fifo, int sampleRate, const juce::String& initialPresetPath, int initialPresetIndex)
{
    MDW_LOG("UI", "VisualizationWindow::GLComponent: ctor begin (deferred attach)");
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread()); // must be UI thread

    setWantsKeyboardFocus(true);

    // Create renderer first so we can queue the initial preset before any GL callbacks fire
    MDW_LOG("UI", "VisualizationWindow::GLComponent: creating ProjectMRenderer");
    renderer = std::make_unique<ProjectMRenderer>(glContext, fifo, sampleRate);

    // Queue initial preset request (path preferred, else index) BEFORE attaching the context
    if (renderer)
    {
        if (initialPresetPath.isNotEmpty())
            renderer->loadPresetByPath(initialPresetPath, true);
        else if (initialPresetIndex >= 0)
            renderer->setPresetIndex(initialPresetIndex);
    }

    // Defer GL context attachment to next message tick; attach only when truly showing
    juce::Timer::callAfterDelay(0, [this]() {
        attachIfReady();
    });

    MDW_LOG("UI", "VisualizationWindow::GLComponent: ctor end");
}

VisualizationWindow::GLComponent::~GLComponent()
{
    MDW_LOG("UI", "VisualizationWindow::GLComponent: dtor");
    // Destructor may be called from non-UI threads in some hosts; avoid touching the context here.
    // Ensure a clean shutdown sequence (prefer explicit shutdownGL by owner on UI thread).
    renderer.reset();
}

void VisualizationWindow::GLComponent::parentHierarchyChanged()
{
    MDW_LOG("UI", juce::String("GLComponent: parentHierarchyChanged peer=") + (getPeer() ? "yes" : "no"));
    // If we lost our peer, JUCE will have detached the GL context implicitly.
    // Ensure our internal flag reflects that so we can reattach on the next opportunity.
    if (glAttached && getPeer() == nullptr)
    {
        glAttached = false;
        MDW_LOG("UI", "GLComponent: reset glAttached (peer lost)");
    }
    attachIfReady();
}

void VisualizationWindow::GLComponent::visibilityChanged()
{
    MDW_LOG("UI", juce::String("GLComponent: visibilityChanged -> ") + (isShowing() ? "showing" : "hidden"));
    attachIfReady();
}

void VisualizationWindow::GLComponent::resized()
{
    MDW_LOG("UI", juce::String("GLComponent: resized to ") + juce::String(getWidth()) + "x" + juce::String(getHeight()));
    attachIfReady();
}

bool VisualizationWindow::GLComponent::keyPressed(const juce::KeyPress& key)
{
    // Forward key presses (like ESC) to the parent window so it can handle fullscreen exit
    if (auto* win = findParentComponentOfClass<VisualizationWindow>())
        return win->keyPressed(key);
    return false;
}

// Forward to renderer
void VisualizationWindow::GLComponent::setVisualParams(float amplitude, float speed)
{
    if (renderer)
        renderer->setVisualParams(amplitude, speed);
}

void VisualizationWindow::GLComponent::setColorParams(float hue01, float sat01)
{
    if (renderer)
        renderer->setColor(hue01, sat01);
}

void VisualizationWindow::GLComponent::setSeed(int seed)
{
    if (renderer)
        renderer->setSeed(seed);
}

void VisualizationWindow::GLComponent::setPresetIndex(int index)
{
    if (renderer)
        renderer->setPresetIndex(index);
}

void VisualizationWindow::GLComponent::loadPresetByPath(const juce::String& absolutePath, bool hardCut)
{
    if (renderer)
        renderer->loadPresetByPath(absolutePath, hardCut);
}

void VisualizationWindow::GLComponent::setAutoPlayFlags(bool enabled, bool randomShuffle, bool hardCut)
{
    if (renderer)
        renderer->setAutoPlay(enabled, randomShuffle, hardCut);
}

void VisualizationWindow::GLComponent::setProjectMPlaylist(const juce::StringArray& absolutePaths)
{
    if (renderer)
        renderer->setPlaylistPaths(absolutePaths);
}

void VisualizationWindow::GLComponent::setProjectMPlaylistPosition(int index, bool hardCut)
{
    if (renderer)
        renderer->setPlaylistPosition(index, hardCut);
}

bool VisualizationWindow::GLComponent::supportsProjectMAuto() const
{
   #if defined(HAVE_PROJECTM)
    return renderer && renderer->isProjectMReady() && renderer->hasPlaylistApi();
   #else
    return false;
   #endif
}

int VisualizationWindow::GLComponent::getProjectMPlaylistPosition() const
{
    return renderer ? renderer->getPlaylistPosition() : -1;
}

// New: explicit GL teardown (must be called on the message thread)
void VisualizationWindow::GLComponent::shutdownGL()
{
    MDW_LOG("UI", "VisualizationWindow::GLComponent::shutdownGL");
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread()); // must be UI thread
    glContext.setRenderer(nullptr);
    glContext.setContinuousRepainting(false);
    if (glContext.isAttached())
    {
        glContext.detach();
    }
    glAttached = false;
}

void VisualizationWindow::GLComponent::prepareForPeerChange()
{
    MDW_LOG("UI", "GLComponent: detached for peer change");
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    // Stop callbacks before detaching to avoid JUCE assertions during teardown
    glContext.setRenderer(nullptr);
    glContext.setContinuousRepainting(false);
    if (glContext.isAttached())
        glContext.detach();
    glAttached = false;
}

void VisualizationWindow::GLComponent::requestReattach()
{
    // Give JUCE a moment to create/settle the new peer before attempting to reattach
    juce::Timer::callAfterDelay(50, [this]() {
        this->attachIfReady();
    });
}

void VisualizationWindow::GLComponent::attachIfReady()
{
    if (glAttached)
        return;

    const bool havePeer = (getPeer() != nullptr);
    const bool showing = isShowing();
    const bool visible = isVisible();
    const bool nonZero = getWidth() > 0 && getHeight() > 0;

    // Attach when component has a peer, non-zero size, and is either showing (on-screen) or at least visible (docked case)
    if (!havePeer || !nonZero || (!showing && !visible))
    {
        MDW_LOG("UI", juce::String("GLComponent: attachIfReady waiting - peer=") + (havePeer?"yes":"no") +
            ", showing=" + (showing?"yes":"no") + ", visible=" + (visible?"yes":"no") + ", size=" + juce::String(getWidth()) + "x" + juce::String(getHeight()));
        return;
    }

    MDW_LOG("UI", "GLComponent: attaching OpenGLContext");
    glContext.setContinuousRepainting(true);
    glContext.setSwapInterval(1);
    glContext.setRenderer(renderer.get());
    glContext.attachTo(*this);
    glAttached = true;
    MDW_LOG("UI", "GLComponent: OpenGLContext attached");
}