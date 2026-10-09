include(${CMAKE_SOURCE_DIR}/cmake/FetchCatch2.cmake)

add_executable(milkdawp_core_tests
  VersionTests.cpp
  AudioRingTests.cpp
  MessagesTests.cpp
  FftTests.cpp
  ResampleTests.cpp
  AdaptiveQualityTests.cpp
  AnalyzerTests.cpp
  OnsetDetectorTests.cpp
  TempoTrackerTests.cpp
  BeatClockTests.cpp
  HostTransportTests.cpp
  ParameterModelTests.cpp
  PlaylistTests.cpp
  PresetMetadataTests.cpp
  PresetLibraryTests.cpp
  SectionDetectorTests.cpp
  TransitionSchedulerTests.cpp
  StateSchemaTests.cpp
  DisplayLayoutTests.cpp
  WavTests.cpp
  ScoringTests.cpp
  SeqlockSnapshotTests.cpp
  VisualControlsTests.cpp
  LayerGateTests.cpp
  OscAddressTests.cpp
  MilkdawpPresetTests.cpp
  MilkConvertTests.cpp
  PresetInputsTests.cpp
  Sha256Tests.cpp
)
milkdawp_test_no_crash_dialogs(milkdawp_core_tests)

target_link_libraries(milkdawp_core_tests PRIVATE
  milkdawp_core
  milkdawp_warnings
  milkdawp_sanitizers
  Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(milkdawp_core_tests)
