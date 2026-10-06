add_library(milkdawp_engine STATIC
  include/milkdawp/engine/BundledContent.h
  include/milkdawp/engine/UpdateCheck.h
  src/UpdateCheck.cpp
  src/BundledContent.cpp
  include/milkdawp/engine/EngineInfo.h
  src/EngineInfo.cpp
  include/milkdawp/engine/ProjectMLibrary.h
  src/ProjectMLibrary.cpp
  include/milkdawp/engine/RenderEngine.h
  src/RenderEngine.cpp
  include/milkdawp/engine/PresetLoader.h
  src/PresetLoader.cpp
  include/milkdawp/engine/TransitionExecutor.h
  src/TransitionExecutor.cpp
  include/milkdawp/engine/OutputSurface.h
  src/OutputSurface.cpp
  include/milkdawp/engine/OffscreenGLContext.h
  src/OffscreenGLContext.cpp
  include/milkdawp/engine/ProjectMInstance.h
  src/ProjectMInstance.cpp
  include/milkdawp/engine/GlFrameTarget.h
  src/GlFrameTarget.cpp
  include/milkdawp/engine/PcmFeeder.h
  include/milkdawp/engine/RecentErrors.h
  include/milkdawp/engine/FrameReadbackExchange.h
  include/milkdawp/engine/PresetHandoff.h
  include/milkdawp/engine/LayerChannel.h
  include/milkdawp/engine/LayerCompositor.h
  src/LayerCompositor.cpp
  include/milkdawp/engine/EffectsChain.h
  src/EffectsChain.cpp
  src/GlUtil.h
  src/GlUtil.cpp
  include/milkdawp/engine/OscRemote.h
  src/OscRemote.cpp
  include/milkdawp/engine/MediaSource.h
  src/MediaSource.cpp
  src/VideoMediaSource.cpp
  include/milkdawp/engine/VideoDecoder.h
  include/milkdawp/engine/PresetCompileTimeCache.h
  include/milkdawp/engine/PresetMetadataStore.h
  src/PresetMetadataStore.cpp
  include/milkdawp/engine/Director.h
  include/milkdawp/engine/ControlMapping.h
  src/ControlMapping.cpp
  src/Director.cpp
  include/milkdawp/engine/Visualizer.h
  src/Visualizer.cpp
  include/milkdawp/engine/OutputWindow.h
  src/OutputWindow.cpp
)

target_include_directories(milkdawp_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(milkdawp_engine PUBLIC cxx_std_20)
# Same as ui/plugin/app. PUBLIC because juce_opengl (below) is: every target
# that compiles JUCE's module sources through this one needs them, or on Linux
# juce_gui_extra includes GTK's WebKit headers.
target_compile_definitions(milkdawp_engine PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  # 8.6b: JUCE's CameraDevice exists on Windows and macOS only (Linux: V4L2, 8.6d).
  $<IF:$<OR:$<PLATFORM_ID:Windows>,$<PLATFORM_ID:Darwin>>,JUCE_USE_CAMERA=1,JUCE_USE_CAMERA=0>
)
target_link_libraries(milkdawp_engine
  PUBLIC
    milkdawp_core
    milkdawp_sanitizers
    juce::juce_core   # ProjectMLibrary.h is a public header exposing juce::File/DynamicLibrary
    juce::juce_opengl # RenderEngine.h/OutputSurface.h are public headers exposing juce::OpenGLContext
    juce::juce_osc    # OscRemote (8.4)
    juce::juce_video  # camera media sources (8.6b)
  PRIVATE
    milkdawp_warnings
)
milkdawp_link_external_zlib_libpng(milkdawp_engine)

# OffscreenGLContext (2.3) uses EGL directly on Linux: a surfaceless context
# for the render thread and headless tests. Windows uses WGL from opengl32,
# which juce_opengl already links.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(OpenGL REQUIRED COMPONENTS EGL)
  target_link_libraries(milkdawp_engine PRIVATE OpenGL::EGL)
endif()

# ProjectMLibrary (Phase 2.1) deliberately never links MILKDAWP_PROJECTM_TARGET
# -- it resolves every symbol at runtime via juce::DynamicLibrary/GetProcAddress
# (see that class's header comment) specifically so a build compiles and loads
# cleanly with no projectM SDK present at all. This target check only records
# whether the SDK was available at configure time for EngineInfo::hasProjectM();
# it must NOT actually link the target -- doing so used to pull vcpkg's
# zlib/libpng in transitively (the whole reason SingleZlibLibpngCheck.cmake's
# external-zlib/libpng path existed), which broke loading in real, independent
# VST3 hosts (REAPER, Cubase) even though it passed pluginval and Standalone --
# see development_roadmap.md §4.11 for the full investigation.
if(MILKDAWP_WITH_PROJECTM AND MILKDAWP_PROJECTM_TARGET AND TARGET ${MILKDAWP_PROJECTM_TARGET})
  target_compile_definitions(milkdawp_engine PRIVATE MILKDAWP_HAS_PROJECTM=1)
else()
  target_compile_definitions(milkdawp_engine PRIVATE MILKDAWP_HAS_PROJECTM=0)
endif()

# 8.6b-d: video through the platform's own decoders, and cameras: Media
# Foundation and JUCE's CameraDevice on Windows; AVFoundation and CameraDevice
# on macOS; GStreamer (loaded at run time, never linked) and V4L2 on Linux.
target_sources(milkdawp_engine PRIVATE src/Cameras.h)
if(WIN32)
  target_sources(milkdawp_engine PRIVATE src/VideoDecoderWindows.cpp src/CameraJuce.cpp)
  target_link_libraries(milkdawp_engine PRIVATE mfplat mfreadwrite mfuuid propsys ole32)
elseif(APPLE)
  target_sources(milkdawp_engine PRIVATE src/VideoDecoderMac.mm src/CameraJuce.cpp)
  target_link_libraries(milkdawp_engine PRIVATE "-framework AVFoundation" "-framework CoreMedia"
                                                "-framework CoreVideo" "-framework Foundation")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(milkdawp_engine PRIVATE src/VideoDecoderLinux.cpp src/CameraLinux.cpp)
  target_link_libraries(milkdawp_engine PRIVATE ${CMAKE_DL_LIBS})
else()
  target_sources(milkdawp_engine PRIVATE src/VideoDecoderUnsupported.cpp src/CameraJuce.cpp)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# PresetMetadataStore (5.2): the user data folder its file lives in, the same
# one the app's settings use, so the app and plugin share ratings and tags.
if(MILKDAWP_DEV_ALT_IDENTITY)
  target_compile_definitions(milkdawp_engine PRIVATE MILKDAWP_USER_DATA_FOLDER="MilkDAWp2 Dev")
else()
  target_compile_definitions(milkdawp_engine PRIVATE MILKDAWP_USER_DATA_FOLDER="MilkDAWp")
endif()

# BundledContent (6.1): dev builds may find the presets in the build tree
# (cmake/BundledContent.cmake). Release presets turn this off, so no build
# path is compiled into a shipped binary.
if(MILKDAWP_BUNDLE_CONTENT AND MILKDAWP_CONTENT_FROM_BUILD_TREE)
  set_source_files_properties(src/BundledContent.cpp PROPERTIES COMPILE_DEFINITIONS
    "MILKDAWP_BUILD_TREE_CONTENT_DIR=\"${MILKDAWP_CONTENT_DIR}\"")
endif()
