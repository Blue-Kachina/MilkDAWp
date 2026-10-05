# cmake/BundledContent.cmake
#
# The preset pack and textures MilkDAWp ships (6.1, D12): projectM's "Cream of
# the Crop" pack (~9,800 presets, curated by Jason Fletcher / ISOSCELES) and
# the MilkDrop texture pack its presets reference, the same content
# projectM's own releases ship.
#
# Both are fetched at configure time as GitHub archives of a pinned commit,
# checked against a SHA-256, and laid out in the build tree the way an
# install lays them out (engine/include/milkdawp/engine/BundledContent.h):
#
#   ${MILKDAWP_CONTENT_DIR}/
#     Presets/Cream of the Crop/   the pack, with its LICENSE.md and README.md
#     Textures/                    the texture pack's textures/ folder
#     Textures/README.md
#
# scripts/release/package.sh copies that folder into each release. Sizes at
# this pin (2026-10-04): 115 MB on disk, 34 MB zipped, ~3 MB with LZMA/xz,
# so the whole pack ships and no subset or in-app download is needed.
#
# To bump a pack: change its commit and hash (`curl -L <url> | sha256sum`),
# then check the first-run scan time and that kDefaultPresetRelativePath
# (BundledContent.cpp) still exists in the pack.
#
# Windows paths: the pack's longest relative path is 174 characters, so the
# build tree must stay within ~85 characters of a drive root to keep every
# preset under MAX_PATH (260) for JUCE's and std::filesystem's file APIs.

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

milkdawp_fetch_archive(presets
  "https://github.com/projectM-visualizer/presets-cream-of-the-crop/archive/${MILKDAWP_PRESETS_COMMIT}.tar.gz"
  ${MILKDAWP_PRESETS_SHA256}
  "${MILKDAWP_CONTENT_DIR}/Presets/Cream of the Crop")

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
