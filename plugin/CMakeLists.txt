# Plugin identity is decided (D1): keep v1's manufacturer/plugin codes and
# bundle ID so existing hosts/sessions keep resolving MilkDAWp. Do not change
# these without an ADR (see the working agreements in development_roadmap.md §11).
#
# MILKDAWP_DEV_ALT_IDENTITY (ADR-0007, default OFF) swaps in a distinct
# plugin code/bundle ID/product name so a local dev build can be loaded
# side by side with a real v1 (or another v2) install without the two
# colliding in the host's plugin database -- VST3 hosts key by class ID,
# which JUCE derives from manufacturer+plugin code, not by file path, so
# "different folder" alone does not avoid the collision ADR-0001 warns about.
if(MILKDAWP_DEV_ALT_IDENTITY)
  set(_mdw_plugin_code  "Mlk2")
  set(_mdw_bundle_id    "com.otitismedia.MilkDAWp2Dev")
  set(_mdw_product_name "MilkDAWp2 Dev")
else()
  set(_mdw_plugin_code  "Mlkw")
  set(_mdw_bundle_id    "com.otitismedia.MilkDAWp")
  set(_mdw_product_name "MilkDAWp")
endif()

set(_mdw_formats VST3)
# D2 / ADR-0002: AU on macOS (6.3). Same identity: manufacturer OMda, subtype =
# the plugin code, type aufx (an effect: IS_SYNTH and IS_MIDI_EFFECT are off).
if(APPLE)
  list(APPEND _mdw_formats AU)
endif()
if(MILKDAWP_PLUGIN_STANDALONE)
  list(APPEND _mdw_formats Standalone)
endif()

juce_add_plugin(milkdawp_plugin
  COMPANY_NAME                "Otitis Media"
  BUNDLE_ID                   ${_mdw_bundle_id}
  IS_SYNTH                    FALSE
  NEEDS_MIDI_INPUT            FALSE
  NEEDS_MIDI_OUTPUT           FALSE
  IS_MIDI_EFFECT              FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  PLUGIN_MANUFACTURER_CODE    OMda
  PLUGIN_CODE                 ${_mdw_plugin_code}
  # D8, 4.11: the Standalone wrapper is a dev convenience now (the plugin
  # editor without a DAW); the shipped app is milkdawp_app.
  FORMATS                     ${_mdw_formats}
  VST3_CATEGORIES             "Fx" "Analyzer"
  # 8.6b: for the Standalone. In a DAW, macOS asks in the host's name and uses
  # the host's own camera permission (some hosts have none: then no camera).
  CAMERA_PERMISSION_ENABLED   TRUE
  CAMERA_PERMISSION_TEXT      "MilkDAWp shows your camera in the visuals when you choose it as the media source. Nothing is recorded."
  PRODUCT_NAME                "${_mdw_product_name}"
)
unset(_mdw_plugin_code)
unset(_mdw_bundle_id)
unset(_mdw_product_name)
unset(_mdw_formats)

target_sources(milkdawp_plugin PRIVATE
  src/LayerRegistry.h
  src/LayerRegistry.cpp
  src/PluginProcessor.h
  src/PluginProcessor.cpp
  src/PluginEditor.h
  src/PluginEditor.cpp
)

target_compile_features(milkdawp_plugin PRIVATE cxx_std_20)
target_compile_definitions(milkdawp_plugin PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(milkdawp_plugin PRIVATE
  milkdawp_core
  milkdawp_engine
  milkdawp_ui
  milkdawp_warnings
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_gui_extra
  juce::juce_dsp
  juce::juce_opengl
)
milkdawp_link_external_zlib_libpng(milkdawp_plugin)

set(_mdw_format_targets milkdawp_plugin_VST3)
if(APPLE)
  list(APPEND _mdw_format_targets milkdawp_plugin_AU)
endif()
if(MILKDAWP_PLUGIN_STANDALONE)
  list(APPEND _mdw_format_targets milkdawp_plugin_Standalone)
endif()
foreach(_mdw_target IN LISTS _mdw_format_targets)
  milkdawp_check_single_zlib_libpng(${_mdw_target})
  if(MILKDAWP_WITH_PROJECTM)
    milkdawp_deploy_projectm_runtime(${_mdw_target})
  endif()
  milkdawp_check_runtime_layout(${_mdw_target})
endforeach()
unset(_mdw_format_targets)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
