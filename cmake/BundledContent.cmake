# cmake/BundledContent.cmake
#
# The preset pack and textures MilkDAWp ships (6.1, D12): projectM's "Cream of
# the Crop" pack (~9,800 presets, curated by Jason Fletcher / ISOSCELES) and
# the MilkDrop texture pack its presets reference, the same content
# projectM's own releases ship.
#
# Both are fetched at configure time as GitHub archives of a pinned commit and
# checked against a SHA-256. The presets ship converted (8.11, ADR-0014): the
# build turns the fetched pack into "Cream of the CrAWp", every preset as a
# .milkdawp with its Macros (target milkdawp_preset_pack,
# tools/mdw-convert/CMakeLists.txt). The original .milk files stay in _deps/
# as the converter's source and don't ship. The build tree is laid out the way
# an install is (engine/include/milkdawp/engine/BundledContent.h):
#
#   ${MILKDAWP_CONTENT_DIR}/
#     Presets/Cream of the CrAWp/  the converted pack, with the original's LICENSE.md and README.md
#     Textures/                    the texture pack's textures/ folder
#     Textures/README.md
#   _deps/cream-of-the-crop/       the pack as fetched (${MILKDAWP_SOURCE_PRESETS_DIR})
#
# scripts/release/package.sh copies the content folder into each release. Sizes at
# this pin (2026-10-04): 115 MB on disk, 34 MB zipped, ~3 MB with LZMA/xz,
# so the whole pack ships and no subset or in-app download is needed.
#
# To bump a pack: change its commit and hash (`curl -L <url> | sha256sum`),
# regenerate cmake/milkdawp-pack-exclusions.txt (ADR-0014), then check the
# first-run scan time and that kDefaultPresetRelativePath (BundledContent.h)
# still exists in the pack.
#
# Windows paths: the pack's longest relative path is 174 characters (178 as
# .milkdawp), so the build tree must stay within ~80 characters of a drive
# root to keep every preset under MAX_PATH (260) for JUCE's and
# std::filesystem's file APIs.

option(MILKDAWP_BUNDLE_CONTENT "Fetch the bundled preset pack and textures (6.1, D12)" ON)
option(MILKDAWP_CONTENT_FROM_BUILD_TREE
  "Dev: let binaries fall back to the build tree's content folder when nothing is installed. Release presets turn this off, so no build path is compiled into a shipped binary." ON)

set(MILKDAWP_CONTENT_DIR "${CMAKE_BINARY_DIR}/content")

if(NOT MILKDAWP_BUNDLE_CONTENT)
  return()
endif()

set(MILKDAWP_PRESETS_COMMIT "0180df21f5e0bd39b9060cc5de420ed2f1f9e509")
set(MILKDAWP_PRESETS_SHA256 "77ef8e527fb00343afdfb267f5a2e8d3d00430c563ca9f5ab2104fc306f5c674")
set(MILKDAWP_TEXTURES_COMMIT "6368812f27bc747b517218fbf89d21d59afce4d9")
set(MILKDAWP_TEXTURES_SHA256 "6e6ea0a0363334a79cf15e5c79115e1b99a24993c4238f14d222d9eea91c52b7")

# milkdawp_fetch_archive(<name> <url> <sha256> <dest>)
#
# Downloads <url> once (cached under _deps/, keyed by hash) and extracts it
# into <dest>, dropping the archive's single top-level folder. Plain
# file(DOWNLOAD)/file(ARCHIVE_EXTRACT) rather than FetchContent: FetchContent
# extracts beside SOURCE_DIR under a long temporary name, which pushes the
# pack's longest paths past MAX_PATH on Windows. A stamp file holding the
# hash makes re-configures skip the extract.
function(milkdawp_fetch_archive name url sha256 dest)
  set(stamp "${dest}/.milkdawp-${name}.sha256")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" have)
    if(have STREQUAL sha256)
      return()
    endif()
  endif()

  set(archive "${CMAKE_BINARY_DIR}/_deps/${name}-${sha256}.tar.gz")
  if(NOT EXISTS "${archive}")
    message(STATUS "MilkDAWp: downloading ${name}")
    file(DOWNLOAD "${url}" "${archive}.part" EXPECTED_HASH SHA256=${sha256} STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE "${archive}.part")
      list(GET status 1 reason)
      message(FATAL_ERROR "Downloading ${name} from ${url} failed: ${reason}. "
                          "Configure with -DMILKDAWP_BUNDLE_CONTENT=OFF to build without the bundled presets.")
    endif()
    file(RENAME "${archive}.part" "${archive}")
  endif()

  # Short temporary folder: the archive's top folder is "<repo>-<40-char sha>".
  set(tmp "${CMAKE_BINARY_DIR}/_x")
  file(REMOVE_RECURSE "${tmp}" "${dest}")
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${tmp}")
  file(GLOB top LIST_DIRECTORIES true "${tmp}/*")
  list(LENGTH top count)
  if(NOT count EQUAL 1 OR NOT IS_DIRECTORY "${top}")
    message(FATAL_ERROR "${name}: expected one top-level folder in ${archive}")
  endif()
  get_filename_component(parent "${dest}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  file(RENAME "${top}" "${dest}")
  file(REMOVE_RECURSE "${tmp}")
  file(WRITE "${stamp}" "${sha256}")
endfunction()

set(MILKDAWP_SOURCE_PRESETS_DIR "${CMAKE_BINARY_DIR}/_deps/cream-of-the-crop")
milkdawp_fetch_archive(presets
  "https://github.com/projectM-visualizer/presets-cream-of-the-crop/archive/${MILKDAWP_PRESETS_COMMIT}.tar.gz"
  ${MILKDAWP_PRESETS_SHA256}
  "${MILKDAWP_SOURCE_PRESETS_DIR}")

# Before 8.11 the pack itself sat in the content folder; packaging copies
# everything there, so a build tree from then mustn't keep it.
if(EXISTS "${MILKDAWP_CONTENT_DIR}/Presets/Cream of the Crop")
  file(REMOVE_RECURSE "${MILKDAWP_CONTENT_DIR}/Presets/Cream of the Crop")
endif()

# Presets their authors asked us not to ship (D12, docs/beta.md): paths
# relative to the pack, e.g. "Dancer/Aurora/Someone - Some preset.milk".
# Taken out of the fetched pack on every configure, so the converted pack
# never has them; drop an entry once upstream has removed the preset and the
# pin has moved past it.
set(MILKDAWP_PRESET_REMOVALS
)
foreach(_mdw_removed IN LISTS MILKDAWP_PRESET_REMOVALS)
  set(_mdw_removed_path "${MILKDAWP_SOURCE_PRESETS_DIR}/${_mdw_removed}")
  if(EXISTS "${_mdw_removed_path}")
    file(REMOVE "${_mdw_removed_path}")
    message(STATUS "MilkDAWp: removed preset at its author's request: ${_mdw_removed}")
  endif()
endforeach()
unset(_mdw_removed)
unset(_mdw_removed_path)
# Rewritten only when the list changes, so the converted pack is rebuilt then.
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/milkdawp-preset-removals.txt" CONTENT "${MILKDAWP_PRESET_REMOVALS}\n")

# The texture repo keeps its images in textures/; flatten that into Textures/.
milkdawp_fetch_archive(textures
  "https://github.com/projectM-visualizer/presets-milkdrop-texture-pack/archive/${MILKDAWP_TEXTURES_COMMIT}.tar.gz"
  ${MILKDAWP_TEXTURES_SHA256}
  "${CMAKE_BINARY_DIR}/_deps/texture-pack")
set(_mdw_textures "${MILKDAWP_CONTENT_DIR}/Textures")
if(NOT EXISTS "${_mdw_textures}/.milkdawp-textures.sha256"
   OR "${CMAKE_BINARY_DIR}/_deps/texture-pack/.milkdawp-textures.sha256" IS_NEWER_THAN "${_mdw_textures}/.milkdawp-textures.sha256")
  file(REMOVE_RECURSE "${_mdw_textures}")
  file(COPY "${CMAKE_BINARY_DIR}/_deps/texture-pack/textures/" DESTINATION "${_mdw_textures}")
  file(COPY "${CMAKE_BINARY_DIR}/_deps/texture-pack/README.md" DESTINATION "${_mdw_textures}")
  file(COPY "${CMAKE_BINARY_DIR}/_deps/texture-pack/.milkdawp-textures.sha256" DESTINATION "${_mdw_textures}")
endif()
unset(_mdw_textures)

message(STATUS "MilkDAWp: bundled presets and textures in ${MILKDAWP_CONTENT_DIR}")
