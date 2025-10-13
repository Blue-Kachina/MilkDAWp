#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "${SCRIPT_DIR}/.." && pwd )"
cd "$REPO_ROOT"

echo "[bootstrap] Initializing submodules..."
# Sync and attempt to init/update submodules (may be a no-op if gitlinks weren't committed)
git submodule sync --recursive >/dev/null 2>&1 || true
git submodule update --init --recursive --depth 1 || true

# Fallback: if gitlinks are not present and paths are empty, shallow clone into extern/
if [ ! -f "extern/JUCE/CMakeLists.txt" ]; then
  if [ ! -d "extern/JUCE/.git" ]; then
    echo "[bootstrap] JUCE submodule not found as a gitlink; shallow-cloning into extern/JUCE"
    mkdir -p extern
    git clone --depth 1 https://github.com/juce-framework/JUCE.git extern/JUCE
  fi
fi
if [ ! -f "extern/projectm/CMakeLists.txt" ]; then
  if [ ! -d "extern/projectm/.git" ]; then
    echo "[bootstrap] projectM submodule not found as a gitlink; shallow-cloning into extern/projectm at tag v4.1.4"
    mkdir -p extern
    git clone --depth 1 --branch v4.1.4 https://github.com/projectM-visualizer/projectm.git extern/projectm
  fi
fi

echo "[bootstrap] Creating default build directory (./build) if missing..."
mkdir -p build

echo "[bootstrap] Done. Next steps:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build --config Release"
