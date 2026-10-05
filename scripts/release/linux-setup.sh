#!/usr/bin/env bash
# scripts/release/linux-setup.sh -- the Linux release build environment (6.4).
#
# Run as root in a fresh ubuntu:22.04 (the release workflow's container, or
# locally: docker run -v <repo>:/src ubuntu:22.04 bash /src/scripts/release/linux-setup.sh).
# 22.04 is D9's floor (glibc 2.35): binaries built here run there and on
# anything newer, which the devcontainer's 24.04 (glibc 2.39, GCC 13) can't
# promise. Installs GCC 12 (22.04's newest, and its libstdc++ is the
# system one, so nothing newer is needed at runtime), CMake and Ninja new
# enough for toolchain.json, JUCE's Linux build dependencies, the tools
# packaging needs, and vcpkg at the commit ci.yml pins (to $VCPKG_ROOT,
# default /opt/vcpkg).
#
# Prints the environment to use afterwards (CC, CXX, VCPKG_ROOT) as
# KEY=value lines, ready for $GITHUB_ENV.

set -euo pipefail

VCPKG_COMMIT="${VCPKG_COMMIT:-319504a5326aa870edde46438c5455fa76305a56}"
VCPKG_ROOT="${VCPKG_ROOT:-/opt/vcpkg}"
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  build-essential gcc-12 g++-12 git curl ca-certificates zip unzip tar xz-utils file pkg-config \
  autoconf automake libtool bison flex python3 python3-pip python3-venv \
  libasound2-dev libjack-jackd2-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxi-dev \
  libfreetype6-dev libfontconfig1-dev \
  libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev libglu1-mesa-dev \
  xvfb xauth dpkg-dev desktop-file-utils > /dev/null

# 22.04's own CMake (3.22) and Ninja (1.10) are older than toolchain.json allows.
python3 -m venv /opt/buildtools > /dev/null
/opt/buildtools/bin/pip install -q "cmake>=3.28,<4" "ninja>=1.11"
ln -sf /opt/buildtools/bin/cmake /opt/buildtools/bin/ctest /opt/buildtools/bin/cpack /opt/buildtools/bin/ninja /usr/local/bin/

if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  git clone -q https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
  git -C "$VCPKG_ROOT" checkout -q "$VCPKG_COMMIT"
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics > /dev/null
fi

{
  echo "CC=gcc-12"
  echo "CXX=g++-12"
  echo "VCPKG_ROOT=$VCPKG_ROOT"
} | tee -a "${GITHUB_ENV:-/dev/null}"
