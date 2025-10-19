#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "${SCRIPT_DIR}/.." && pwd )"
cd "$REPO_ROOT"

echo "[bootstrap] MilkDAWp vcpkg-based dependency setup"
echo

# Ensure we have a usable VCPKG_ROOT, auto-install locally if missing (CI-friendly)
if [ -z "${VCPKG_ROOT:-}" ]; then
  export VCPKG_ROOT="$REPO_ROOT/.vcpkg"
  echo "[bootstrap] VCPKG_ROOT not set. Will use local: $VCPKG_ROOT"
fi

# Install vcpkg locally if not present
if [ ! -d "$VCPKG_ROOT/.git" ]; then
  echo "[bootstrap] vcpkg not found at $VCPKG_ROOT — cloning..."
  mkdir -p "$VCPKG_ROOT"
  rm -rf "$VCPKG_ROOT"/* 2>/dev/null || true
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi

# Bootstrap vcpkg if executable missing
VCPKG_EXE="$VCPKG_ROOT/vcpkg"
if [ ! -f "$VCPKG_EXE" ]; then
  echo "[bootstrap] Bootstrapping vcpkg..."
  if [[ "$OSTYPE" == "msys"* || "$OSTYPE" == "cygwin"* || "$OS" == "Windows_NT" ]]; then
    bash "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
    VCPKG_EXE="$VCPKG_ROOT/vcpkg.exe"
  else
    bash "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
  fi
fi

if [ ! -f "$VCPKG_EXE" ]; then
  echo "[bootstrap] ERROR: Failed to bootstrap vcpkg at $VCPKG_ROOT"
  exit 1
fi

echo "[bootstrap] vcpkg is ready at: $VCPKG_ROOT"

# Determine preset based on platform
if [[ "$OSTYPE" == "darwin"* ]]; then
  PRESET="dev-mac"
  BUILD_DIR="build-mac"
elif [[ "$OSTYPE" == "linux-gnu"* || "$OSTYPE" == "linux"* ]]; then
  PRESET="ci-linux"
  BUILD_DIR="build-ci"
else
  echo "[bootstrap] Unsupported OS: $OSTYPE"
  exit 1
fi

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
  echo "[bootstrap] Creating build directory: $BUILD_DIR"
  mkdir -p "$BUILD_DIR"
fi

echo "[bootstrap] Done. Next steps:"
echo
echo "  Configure (installs dependencies via vcpkg manifest mode):"
echo "    cmake --preset $PRESET"
echo
echo "  Build:"
echo "    cmake --build $BUILD_DIR --config Release"
echo
echo "  Note: First-time dependency installation may take 10-30 minutes."
echo "        vcpkg will cache binaries for faster subsequent builds."
