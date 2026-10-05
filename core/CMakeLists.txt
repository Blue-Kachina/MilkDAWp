add_library(milkdawp_core STATIC
  include/milkdawp/core/Version.h
  src/Version.cpp
  include/milkdawp/core/AudioRing.h
  src/AudioRing.cpp
  include/milkdawp/core/Messages.h
  include/milkdawp/core/Fft.h
  src/Fft.cpp
  include/milkdawp/core/Resample.h
  src/Resample.cpp
  include/milkdawp/core/AdaptiveQuality.h
  src/AdaptiveQuality.cpp
  include/milkdawp/core/Analyzer.h
  src/Analyzer.cpp
  include/milkdawp/core/OnsetDetector.h
  src/OnsetDetector.cpp
  include/milkdawp/core/TempoTracker.h
  src/TempoTracker.cpp
  include/milkdawp/core/BeatClock.h
  src/BeatClock.cpp
  include/milkdawp/core/HostTransport.h
  src/HostTransport.cpp
  include/milkdawp/core/ParameterModel.h
  src/ParameterModel.cpp
  include/milkdawp/core/Playlist.h
  src/Playlist.cpp
  include/milkdawp/core/PresetLibrary.h
  include/milkdawp/core/PresetMetadata.h
  src/PresetMetadata.cpp
  src/PresetLibrary.cpp
  include/milkdawp/core/SectionDetector.h
  src/SectionDetector.cpp
  include/milkdawp/core/TransitionScheduler.h
  src/TransitionScheduler.cpp
  include/milkdawp/core/StateSchema.h
  src/StateSchema.cpp
  include/milkdawp/core/DisplayLayout.h
  src/DisplayLayout.cpp
  include/milkdawp/core/Wav.h
  src/Wav.cpp
  include/milkdawp/core/Scoring.h
  src/Scoring.cpp
  include/milkdawp/core/SeqlockSnapshot.h
  include/milkdawp/core/Diagnostics.h
)

target_include_directories(milkdawp_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(milkdawp_core PUBLIC cxx_std_20)
target_link_libraries(milkdawp_core PRIVATE milkdawp_warnings)
target_link_libraries(milkdawp_core PUBLIC milkdawp_sanitizers)

# Only Version.cpp sees these, so changing the label recompiles one file.
set_source_files_properties(src/Version.cpp PROPERTIES COMPILE_DEFINITIONS
  "MILKDAWP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};MILKDAWP_VERSION_MINOR=${PROJECT_VERSION_MINOR};MILKDAWP_VERSION_PATCH=${PROJECT_VERSION_PATCH};MILKDAWP_VERSION_LABEL=\"${MILKDAWP_VERSION_LABEL}\"")

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
