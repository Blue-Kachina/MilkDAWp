// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/OffscreenGLContext.h"

#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#endif

namespace milkdawp::engine {

namespace {

constexpr unsigned int kGlVendor = 0x1F00;
constexpr unsigned int kGlRenderer = 0x1F01;
constexpr unsigned int kGlVersion = 0x1F02;
using GlGetStringFn = const unsigned char*(
#if defined(_WIN32)
    __stdcall
#endif
        *)(unsigned int);

std::string glString(unsigned int name) {
  auto* getString = reinterpret_cast<GlGetStringFn>(OffscreenGLContext::getProcAddress("glGetString", nullptr));
  if (getString == nullptr) {
    return "?";
  }
  const auto* text = getString(name);
  return text != nullptr ? reinterpret_cast<const char*>(text) : "?";
}

// "4.6.0 NVIDIA 560.94" -> {4, 6}; "OpenGL ES 3.2 Mesa" -> {3, 2}.
bool parseGlVersion(const std::string& text, int& major, int& minor) {
  auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
  std::size_t i = 0;
  while (i < text.size() && !isDigit(text[i])) {
    ++i;
  }
  auto readNumber = [&](int& out) {
    if (i >= text.size() || !isDigit(text[i])) {
      return false;
    }
    out = 0;
    while (i < text.size() && isDigit(text[i])) {
      out = out * 10 + (text[i] - '0');
      ++i;
    }
    return true;
  };
  if (!readNumber(major) || i >= text.size() || text[i] != '.') {
    return false;
  }
  ++i;
  return readNumber(minor);
}

} // namespace

#if defined(_WIN32)

struct OffscreenGLContext::Impl {
  HWND window = nullptr;
  HDC dc = nullptr;
  HGLRC context = nullptr;

  ~Impl() {
    if (context != nullptr) {
      if (wglGetCurrentContext() == context) {
        wglMakeCurrent(nullptr, nullptr);
      }
      wglDeleteContext(context);
    }
    if (dc != nullptr) {
      ReleaseDC(window, dc);
    }
    if (window != nullptr) {
      DestroyWindow(window);
    }
  }
};

namespace {
const wchar_t* registerWindowClass() {
  static const wchar_t* className = [] {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MilkDAWpOffscreenGL";
    // Already registered (another plugin instance, or a second copy of this
    // module) is fine: the class is identical.
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return static_cast<const wchar_t*>(nullptr);
    }
    return static_cast<const wchar_t*>(L"MilkDAWpOffscreenGL");
  }();
  return className;
}
} // namespace

OffscreenGLContext::CreateResult OffscreenGLContext::create() {
  CreateResult result;
  auto impl = std::make_unique<Impl>();

  const wchar_t* className = registerWindowClass();
  if (className == nullptr) {
    result.error = "RegisterClassExW failed for the offscreen GL window";
    return result;
  }

  // Never shown. WS_EX_TOOLWINDOW keeps it off the taskbar and Alt-Tab even
  // if something did show it.
  impl->window = CreateWindowExW(WS_EX_TOOLWINDOW, className, L"MilkDAWp render", WS_POPUP, 0, 0, 1, 1, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
  if (impl->window == nullptr) {
    result.error = "CreateWindowExW failed for the offscreen GL window";
    return result;
  }
  impl->dc = GetDC(impl->window);

  // Same descriptor JUCE builds from its default OpenGLPixelFormat
  // (8-bit RGBA, 16-bit depth, no stencil), so the two sides of
  // wglShareLists have matching pixel formats.
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.iLayerType = PFD_MAIN_PLANE;
  pfd.cColorBits = 32;
  pfd.cRedBits = 8;
  pfd.cGreenBits = 8;
  pfd.cBlueBits = 8;
  pfd.cAlphaBits = 8;
  pfd.cDepthBits = 16;

  const int format = ChoosePixelFormat(impl->dc, &pfd);
  if (format == 0 || !SetPixelFormat(impl->dc, format, &pfd)) {
    result.error = "no usable pixel format for the offscreen GL window";
    return result;
  }

  // Plain wglCreateContext, as JUCE does when no GL version is requested:
  // drivers return their highest compatibility-profile version, and both
  // sides of the share come from the same creation path.
  impl->context = wglCreateContext(impl->dc);
  if (impl->context == nullptr || !wglMakeCurrent(impl->dc, impl->context)) {
    result.error = "wglCreateContext/wglMakeCurrent failed for the offscreen GL context";
    return result;
  }

  std::unique_ptr<OffscreenGLContext> context(new OffscreenGLContext(std::move(impl)));
  const auto version = glString(kGlVersion);
  context->description_ = glString(kGlVendor) + " / " + glString(kGlRenderer) + " / GL " + version;

  int major = 0;
  int minor = 0;
  if (!parseGlVersion(version, major, minor) || major < 3 || (major == 3 && minor < 3)) {
    result.error = "OpenGL 3.3 or later is required, the driver reports '" + version + "'";
    return result;
  }

  result.context = std::move(context);
  return result;
}

bool OffscreenGLContext::makeCurrent() noexcept { return wglMakeCurrent(impl_->dc, impl_->context) != FALSE; }

void OffscreenGLContext::doneCurrent() noexcept { wglMakeCurrent(nullptr, nullptr); }

void* OffscreenGLContext::nativeShareHandle() const noexcept { return impl_->context; }

void* OffscreenGLContext::getProcAddress(const char* name, void*) noexcept {
  auto* proc = reinterpret_cast<void*>(wglGetProcAddress(name));
  const auto value = reinterpret_cast<std::intptr_t>(proc);
  // wglGetProcAddress returns null (or 1/2/3/-1 on some drivers) for GL 1.1
  // entry points, which only opengl32.dll exports.
  if (value == 0 || value == 1 || value == 2 || value == 3 || value == -1) {
    static HMODULE openGl = LoadLibraryW(L"opengl32.dll");
    proc = openGl != nullptr ? reinterpret_cast<void*>(GetProcAddress(openGl, name)) : nullptr;
  }
  return proc;
}

bool OffscreenGLContext::shareWithCurrentContext(void* offscreenHandle) noexcept {
  const HGLRC theirs = wglGetCurrentContext();
  const HDC theirDc = wglGetCurrentDC();
  if (theirs == nullptr || offscreenHandle == nullptr) {
    return false;
  }
  // Neither side current anywhere while the driver links them.
  wglMakeCurrent(nullptr, nullptr);
  const BOOL shared = wglShareLists(static_cast<HGLRC>(offscreenHandle), theirs);
  wglMakeCurrent(theirDc, theirs);
  return shared != FALSE;
}

void OffscreenGLContext::pumpPlatformEvents() noexcept {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

#elif defined(__linux__)

// EGL shares only at context creation; nothing to do after the fact.
bool OffscreenGLContext::shareWithCurrentContext(void*) noexcept { return false; }

struct OffscreenGLContext::Impl {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;

  ~Impl() {
    if (display != EGL_NO_DISPLAY) {
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (context != EGL_NO_CONTEXT) {
        eglDestroyContext(display, context);
      }
      eglTerminate(display);
    }
  }
};

OffscreenGLContext::CreateResult OffscreenGLContext::create() {
  CreateResult result;
  auto impl = std::make_unique<Impl>();

  impl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (impl->display == EGL_NO_DISPLAY || eglInitialize(impl->display, nullptr, nullptr) != EGL_TRUE) {
    result.error = "eglGetDisplay/eglInitialize failed";
    return result;
  }
  const char* extensions = eglQueryString(impl->display, EGL_EXTENSIONS);
  if (extensions == nullptr || std::string(extensions).find("EGL_KHR_surfaceless_context") == std::string::npos) {
    result.error = "EGL_KHR_surfaceless_context is not supported";
    return result;
  }
  if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
    result.error = "eglBindAPI(EGL_OPENGL_API) failed";
    return result;
  }

  const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, 0, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint numConfigs = 0;
  if (eglChooseConfig(impl->display, configAttribs, &config, 1, &numConfigs) != EGL_TRUE || numConfigs < 1) {
    result.error = "eglChooseConfig found no OpenGL config";
    return result;
  }

  const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION,
                                   3,
                                   EGL_CONTEXT_MINOR_VERSION,
                                   3,
                                   EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                   EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                   EGL_NONE};
  impl->context = eglCreateContext(impl->display, config, EGL_NO_CONTEXT, contextAttribs);
  if (impl->context == EGL_NO_CONTEXT ||
      eglMakeCurrent(impl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, impl->context) != EGL_TRUE) {
    result.error = "eglCreateContext/eglMakeCurrent (GL 3.3 core, surfaceless) failed";
    return result;
  }

  std::unique_ptr<OffscreenGLContext> context(new OffscreenGLContext(std::move(impl)));
  const auto version = glString(kGlVersion);
  context->description_ = glString(kGlVendor) + " / " + glString(kGlRenderer) + " / GL " + version;
  result.context = std::move(context);
  return result;
}

bool OffscreenGLContext::makeCurrent() noexcept {
  return eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE, impl_->context) == EGL_TRUE;
}

void OffscreenGLContext::doneCurrent() noexcept {
  eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void* OffscreenGLContext::nativeShareHandle() const noexcept { return impl_->context; }

void* OffscreenGLContext::getProcAddress(const char* name, void*) noexcept {
  return reinterpret_cast<void*>(eglGetProcAddress(name));
}

void OffscreenGLContext::pumpPlatformEvents() noexcept {}

#else

struct OffscreenGLContext::Impl {};

OffscreenGLContext::CreateResult OffscreenGLContext::create() {
  CreateResult result;
  result.error = "the offscreen render context is not implemented on this platform yet (roadmap 2.3)";
  return result;
}

bool OffscreenGLContext::makeCurrent() noexcept { return false; }
void OffscreenGLContext::doneCurrent() noexcept {}
void* OffscreenGLContext::nativeShareHandle() const noexcept { return nullptr; }
void* OffscreenGLContext::getProcAddress(const char*, void*) noexcept { return nullptr; }
bool OffscreenGLContext::shareWithCurrentContext(void*) noexcept { return false; }
void OffscreenGLContext::pumpPlatformEvents() noexcept {}

#endif

OffscreenGLContext::OffscreenGLContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

OffscreenGLContext::~OffscreenGLContext() = default;

} // namespace milkdawp::engine
