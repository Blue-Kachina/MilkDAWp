# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Overlay port for projectM 4.2 (pre-release), ADR-0008 / D15. Adapted from the
# vcpkg registry's projectm 4.1.7 port. Differences from that port:
#   - REF is a pinned upstream master commit, not a release tag.
#   - No GLEW dependency: 4.2 resolves GL functions itself via vendored glad.
#   - No macos-pkgconfig.patch: master no longer adds the "opengl" pkg-config
#     requirement that the patch removed on Apple.
# To bump: change REF, set SHA512 to 0, run the install once, and copy the
# actual hash from the error message. Then update ADR-0008 and roadmap 2.12.
# The pin must be a full commit hash, never a branch name.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO projectM-visualizer/projectm
    REF 1e7ef7803b69024d1e0656705670adda2ffac817 # master, 2026-09-10
    SHA512 2f07e13b1f67c9ca2988510b08cb04f9abefa1b46fb4a265a35aca02f52d99b7bd9c263916c2f4582d2f6378357875a483cb8a8fb24134f2684b0d1b1570d1ca
    HEAD_REF master
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        "boost-filesystem" ENABLE_BOOST_FILESYSTEM
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}

        # Use projectm-eval and GLM from ports. GitHub archives omit the
        # vendor/projectm-eval submodule, so the external copy is required.
        -DENABLE_SYSTEM_PROJECTM_EVAL=ON
        -DENABLE_SYSTEM_GLM=ON

        -DENABLE_PLAYLIST=ON
        -DENABLE_SDL_UI=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_DOCS=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME "projectM4"
    CONFIG_PATH "lib/cmake/projectM4"
    DO_NOT_DELETE_PARENT_CONFIG_PATH
)

vcpkg_cmake_config_fixup(
    PACKAGE_NAME "projectM4Playlist"
    CONFIG_PATH "lib/cmake/projectM4Playlist"
)

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(
    COMMENT "Pre-release build of upstream commit 1e7ef7803b69024d1e0656705670adda2ffac817. The bundled SOIL2 sources include MIT-0 code, Apache-2.0-licensed ETC1 code, and MIT-licensed PowerVR code, but upstream does not provide their complete license texts as separate files. The vendored glad loader is (WTFPL OR CC0-1.0) AND Apache-2.0 per its SPDX headers."
    FILE_LIST
        "${SOURCE_PATH}/LICENSE.txt"
        "${SOURCE_PATH}/vendor/hlslparser/LICENSE"
)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
