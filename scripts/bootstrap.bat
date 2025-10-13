@echo off
setlocal enableextensions enabledelayedexpansion

REM Determine repo root (folder containing this script is scripts\)
set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%.." >nul
set REPO_ROOT=%CD%

REM Ensure git submodule metadata is in sync, then try to init/update
echo [bootstrap] Initializing submodules...
git submodule sync --recursive >nul 2>&1
git submodule update --init --recursive --depth 1

REM Fallback: if gitlinks are not recorded (no submodules checked out),
REM shallow-clone JUCE and projectM into extern/ so local builds work.
if not exist "extern\JUCE\CMakeLists.txt" (
  if not exist "extern\JUCE\.git" (
    echo [bootstrap] JUCE submodule not found as a gitlink; shallow-cloning into extern\JUCE
    git clone --depth 1 https://github.com/juce-framework/JUCE.git extern/JUCE
  )
)
if not exist "extern\projectm\CMakeLists.txt" (
  if not exist "extern\projectm\.git" (
    echo [bootstrap] projectM submodule not found as a gitlink; shallow-cloning into extern\projectm at tag v4.1.4
    git clone --depth 1 --branch v4.1.4 https://github.com/projectM-visualizer/projectm.git extern/projectm
  )
)

if not exist build (
  echo [bootstrap] Creating default build directory: build
  mkdir build
)

echo [bootstrap] Done. Next steps:
echo   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
echo   cmake --build build --config Release

popd >nul
endlocal
