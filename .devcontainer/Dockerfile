# MilkDAWp 2 devcontainer / CI image (Phase 0.9-0.10, D14).
#
# One image for: local devcontainers (VS Code / CLion), CI (`container:` in
# .github/workflows/ci.yml), and Claude Code web sessions (.claude/ hook,
# Phase 0.11). Covers everything that doesn't need a native macOS/Windows
# toolchain: milkdawp_core, mdw-analyze, headless engine render tests under
# Mesa/EGL, lint, and docs (§4.10).
#
# vcpkg installs projectM (and its transitive deps) for x64-linux-dynamic at
# image-build time so the 10-30 minute build happens once here, not on every
# checkout. JUCE 9's source is pre-fetched to the same end (FetchContent
# picks it up via FETCHCONTENT_SOURCE_DIR_JUCE, no code changes needed).
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
# 20+ for RealtimeSanitizer (-fsanitize=realtime, the ci-linux-rtsan job).
# Ubuntu 24.04's own archive stops at 18, so LLVM comes from apt.llvm.org.
ARG LLVM_VERSION=20

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl gnupg \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/noble/ llvm-toolchain-noble-${LLVM_VERSION} main" \
       > /etc/apt/sources.list.d/llvm.list \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Toolchains: GCC + Clang (D5), CMake, Ninja, ccache, and vcpkg's own build
# dependencies (autoconf/automake/libtool/bison/pkg-config -- several ports
# projectM depends on use autotools).
# ---------------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential gcc-13 g++-13 \
      clang-${LLVM_VERSION} clang-format-${LLVM_VERSION} clang-tidy-${LLVM_VERSION} lld-${LLVM_VERSION} \
      libclang-rt-${LLVM_VERSION}-dev llvm-${LLVM_VERSION} \
      cmake ninja-build ccache git curl ca-certificates zip unzip tar pkg-config \
      autoconf automake libtool bison flex python3 python3-pip \
      # JUCE 9 Linux build deps (§4.11: EGL replaces GLX; libxi-dev is new)
      libasound2-dev libjack-jackd2-dev \
      libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxi-dev \
      libfreetype6-dev libfontconfig1-dev \
      libcurl4-openssl-dev libwebkit2gtk-4.1-dev \
      # Headless GL: EGL surfaceless (JUCE 9 native path) + Mesa llvmpipe software
      # rasterizer fallback, and Xvfb for anything that still wants an X display.
      libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev mesa-utils libglu1-mesa-dev \
      xvfb x11-utils \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${LLVM_VERSION} 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${LLVM_VERSION} 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-${LLVM_VERSION} 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-${LLVM_VERSION} 100 \
    # Sanitizer reports print "<null>" frames without it on PATH.
    && update-alternatives --install /usr/bin/llvm-symbolizer llvm-symbolizer /usr/lib/llvm-${LLVM_VERSION}/bin/llvm-symbolizer 100 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# vcpkg, pinned to the same commit as vcpkg-configuration.json's baseline (D4).
# ---------------------------------------------------------------------------
ARG VCPKG_COMMIT=319504a5326aa870edde46438c5455fa76305a56
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && git -C ${VCPKG_ROOT} checkout ${VCPKG_COMMIT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Pre-build the manifest's dependencies (projectM and friends) for
# x64-linux-dynamic into vcpkg's *binary cache* (not just this one-off
# install tree), so that a real checkout's own `cmake --preset ci-linux`
# -- which installs into its own build-ci-linux/vcpkg_installed -- finds
# every package already built in the cache and just unpacks it instead of
# rebuilding from source. This is what makes the image's 10-30 minute vcpkg
# build happen once here rather than on every CI run or checkout (0.3/0.9).
ENV VCPKG_DEFAULT_BINARY_CACHE=/opt/vcpkg-binary-cache
RUN mkdir -p ${VCPKG_DEFAULT_BINARY_CACHE}

WORKDIR /opt/milkdawp-manifest
COPY vcpkg.json vcpkg-configuration.json ./
COPY triplets ./triplets
COPY vcpkg-overlays ./vcpkg-overlays
RUN ${VCPKG_ROOT}/vcpkg install --triplet x64-linux-dynamic --overlay-triplets=./triplets

# ---------------------------------------------------------------------------
# JUCE 9 source, pre-fetched (D4/§4.11). Kept in lockstep with cmake/FetchJuce.cmake.
# ---------------------------------------------------------------------------
ARG JUCE_COMMIT=72782788ce18c2d4d760b28e0921d6ffc6431102
ENV JUCE_SOURCE_DIR=/opt/juce-src
RUN git clone https://github.com/juce-framework/JUCE.git ${JUCE_SOURCE_DIR} \
    && git -C ${JUCE_SOURCE_DIR} checkout ${JUCE_COMMIT}
ENV FETCHCONTENT_SOURCE_DIR_JUCE=${JUCE_SOURCE_DIR}

# ---------------------------------------------------------------------------
# pluginval (Phase 3.9). Linux build only; no GUI needed for CLI validation.
# ---------------------------------------------------------------------------
ARG PLUGINVAL_VERSION=v1.0.4
RUN curl -fL -o /tmp/pluginval.zip \
      "https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_Linux.zip" \
    && unzip -q /tmp/pluginval.zip -d /opt/pluginval \
    && ln -s /opt/pluginval/pluginval /usr/local/bin/pluginval \
    && rm /tmp/pluginval.zip

ENV CCACHE_DIR=/opt/ccache
RUN mkdir -p ${CCACHE_DIR}

# ---------------------------------------------------------------------------
# Claude Code CLI, for running/resuming an interactive session from a
# terminal *inside* this container (VS Code Dev Containers / CLion Gateway).
# Distinct from the .claude/hooks/session-start.sh hook above: that hook
# runs *from inside* an already-running Claude Code session (e.g. the
# platform's own web/cloud session mode, which supplies its own CLI) to warm
# up the C++ build; this is what makes `claude` exist as a command at all
# for a developer's own local devcontainer.
# ---------------------------------------------------------------------------
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && npm install -g @anthropic-ai/claude-code \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
