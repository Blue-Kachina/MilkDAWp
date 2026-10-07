# mdw-convert (Phase 8.11): .milk -> .milkdawp with proposed Macros, written
# beside the originals. The conversion is milkdawp_core; the engine is only
# for --verify, which renders headless (and needs projectM next to the binary).
add_executable(mdw-convert src/Main.cpp src/Verifier.cpp src/Verifier.h)

target_compile_features(mdw-convert PRIVATE cxx_std_20)
target_link_libraries(mdw-convert PRIVATE milkdawp_core milkdawp_engine milkdawp_warnings)
milkdawp_link_external_zlib_libpng(mdw-convert)
milkdawp_check_single_zlib_libpng(mdw-convert)

if(MILKDAWP_WITH_PROJECTM)
  milkdawp_deploy_projectm_runtime(mdw-convert)
endif()

# 8.11: the presets MilkDAWp ships, "Cream of the CrAWp": the fetched Cream of
# the Crop pack (cmake/BundledContent.cmake) with every preset as a .milkdawp,
# made at build time (nothing generated is committed). No .milk files ship.
# Presets projectM can't load at all are left out:
# cmake/milkdawp-pack-exclusions.txt, from `mdw-convert --verify`, regenerated
# by hand after a pack bump (docs/adr/0014-mdw-convert.md). The fetched .milk
# files are only read. The stamp is named like the other content stamps, which
# packaging leaves out.
if(MILKDAWP_BUNDLE_CONTENT)
  if(CMAKE_CROSSCOMPILING)
    message(WARNING "MilkDAWp: cross-compiling, so mdw-convert can't run here and no presets are bundled")
  else()
    set(_mdw_pack "${MILKDAWP_CONTENT_DIR}/Presets/Cream of the CrAWp")
    set(_mdw_pack_exclusions "${CMAKE_SOURCE_DIR}/cmake/milkdawp-pack-exclusions.txt")
    set(_mdw_pack_stamp "${_mdw_pack}/.milkdawp-pack.stamp")
    add_custom_command(
      OUTPUT "${_mdw_pack_stamp}"
      # Made fresh each time, so nothing stale (an excluded or removed preset) survives.
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_mdw_pack}"
      # Sanitizer builds run the tool too; a leak report at exit would fail the build.
      COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0
              $<TARGET_FILE:mdw-convert> --out "${_mdw_pack}" --quiet
              --exclude "${_mdw_pack_exclusions}"
              --report "${CMAKE_BINARY_DIR}/milkdawp-pack-report.tsv"
              "${MILKDAWP_SOURCE_PRESETS_DIR}"
      # The curator's licence and credit ship unchanged (D12).
      COMMAND ${CMAKE_COMMAND} -E copy "${MILKDAWP_SOURCE_PRESETS_DIR}/LICENSE.md"
              "${MILKDAWP_SOURCE_PRESETS_DIR}/README.md" "${_mdw_pack}/"
      COMMAND ${CMAKE_COMMAND} -E touch "${_mdw_pack_stamp}"
      # Not on the mdw-convert binary, which relinks whenever anything in
      # core changes: that would redo ~10k presets on most builds. Only on
      # what decides the output: the conversion's own sources, the
      # exclusions, the removals, and the archive's stamp (rewritten when the
      # pack is re-extracted).
      DEPENDS "${_mdw_pack_exclusions}" "${MILKDAWP_SOURCE_PRESETS_DIR}/.milkdawp-presets.sha256"
              "${CMAKE_BINARY_DIR}/milkdawp-preset-removals.txt"
              "${CMAKE_SOURCE_DIR}/core/src/MilkConvert.cpp" "${CMAKE_SOURCE_DIR}/core/src/MilkdawpPreset.cpp"
      COMMENT "Making the bundled presets, Cream of the CrAWp (8.11)"
      VERBATIM)
    add_custom_target(milkdawp_preset_pack ALL DEPENDS "${_mdw_pack_stamp}")
    add_dependencies(milkdawp_preset_pack mdw-convert) # built first, but no rerun when it relinks
  endif()
endif()
