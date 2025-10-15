#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "${SCRIPT_DIR}/.." && pwd )"
cd "$REPO_ROOT"

echo "[bootstrap] MilkDAWp vcpkg-based dependency setup"
echo

# Check if VCPKG_ROOT is set
if [ -z "${VCPKG_ROOT:-}" ]; then
  echo "[bootstrap] ERROR: VCPKG_ROOT environment variable is not set."
  echo "[bootstrap] Please install vcpkg and set VCPKG_ROOT to point to your vcpkg installation."
  echo
  echo "[bootstrap] Example installation steps:"
  echo "[bootstrap]   git clone https://github.com/microsoft/vcpkg.git ~/vcpkg"
  echo "[bootstrap]   cd ~/vcpkg"
  echo "[bootstrap]   ./bootstrap-vcpkg.sh"
  echo "[bootstrap]   export VCPKG_ROOT=~/vcpkg"
  echo "[bootstrap]   # Add to ~/.bashrc or ~/.zshrc: export VCPKG_ROOT=~/vcpkg"
  echo
  echo "[bootstrap] After setting VCPKG_ROOT, restart your terminal and run this script again."
  exit 1
fi

echo "[bootstrap] Found vcpkg at: $VCPKG_ROOT"

# Verify vcpkg executable exists
if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
  echo "[bootstrap] ERROR: vcpkg executable not found at $VCPKG_ROOT/vcpkg"
  echo "[bootstrap] Please bootstrap vcpkg by running: $VCPKG_ROOT/bootstrap-vcpkg.sh"
  exit 1
fi

echo "[bootstrap] vcpkg is ready."

# Determine preset based on platform
if [[ "$OSTYPE" == "darwin"* ]]; then
  PRESET="dev-mac"
  BUILD_DIR="build-mac"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
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
