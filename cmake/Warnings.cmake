# cmake/Warnings.cmake
#
# Provides `milkdawp_warnings`, an INTERFACE target that turns on
# warnings-as-errors (D5) for MilkDAWp's own targets. Link it PRIVATE into
# milkdawp_core/engine/ui/plugin/app targets, never into third-party targets
# (JUCE, projectM) so their warnings can't fail our build.
#
# JUCE is fetched with FetchContent's SYSTEM option (cmake/FetchJuce.cmake),
# so its headers are treated as system includes and won't trip -Wpedantic
# et al. when included from our translation units.

add_library(milkdawp_warnings INTERFACE)

if(MSVC)
  target_compile_options(milkdawp_warnings INTERFACE
    /W4
    /WX
    /permissive-
  )
else()
  target_compile_options(milkdawp_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
  )
endif()

# JUCE module sources (juce_graphics_Harfbuzz.cpp etc.) compile *inside* the
# targets that link juce::juce_*, so they get these flags despite SYSTEM
# headers. GCC's -Wmaybe-uninitialized false-positives in JUCE's bundled
# HarfBuzz at -O2; keep it a warning, not an error, on GCC only.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(milkdawp_warnings INTERFACE -Wno-error=maybe-uninitialized)
endif()

# GCC 12 (the Linux release builds with it on Ubuntu 22.04, 6.4) reports
# -Wuse-after-free and -Wrestrict inside libstdc++'s own std::string at -O3
# (basic_string.tcc, and `"literal" + std::string&&` -> char_traits::copy),
# known false positives fixed in GCC 13 (GCC PR 105329). Warnings, not
# errors, on GCC 12 only.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  target_compile_options(milkdawp_warnings INTERFACE -Wno-error=use-after-free -Wno-error=restrict)
endif()
