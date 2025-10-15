@echo off
setlocal enableextensions enabledelayedexpansion

REM Determine repo root (folder containing this script is scripts\)
set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%.." >nul
set REPO_ROOT=%CD%

echo [bootstrap] MilkDAWp vcpkg-based dependency setup
echo.

REM Check if VCPKG_ROOT is set
if not defined VCPKG_ROOT (
  echo [bootstrap] ERROR: VCPKG_ROOT environment variable is not set.
  echo [bootstrap] Please install vcpkg and set VCPKG_ROOT to point to your vcpkg installation.
  echo [bootstrap]
  echo [bootstrap] Example installation steps:
  echo [bootstrap]   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
  echo [bootstrap]   cd C:\vcpkg
  echo [bootstrap]   bootstrap-vcpkg.bat
  echo [bootstrap]   setx VCPKG_ROOT "C:\vcpkg"
  echo [bootstrap]
  echo [bootstrap] After setting VCPKG_ROOT, restart your terminal and run this script again.
  popd >nul
  exit /b 1
)

echo [bootstrap] Found vcpkg at: %VCPKG_ROOT%

REM Verify vcpkg executable exists
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  echo [bootstrap] ERROR: vcpkg.exe not found at %VCPKG_ROOT%\vcpkg.exe
  echo [bootstrap] Please bootstrap vcpkg by running: %VCPKG_ROOT%\bootstrap-vcpkg.bat
  popd >nul
  exit /b 1
)

echo [bootstrap] vcpkg is ready.

REM Create build directory if it doesn't exist
if not exist build-win (
  echo [bootstrap] Creating build directory: build-win
  mkdir build-win
)

echo [bootstrap] Done. Next steps:
echo.
echo   Configure (installs dependencies via vcpkg manifest mode):
echo     cmake --preset dev-win
echo.
echo   Build:
echo     cmake --build build-win --config Release
echo.
echo   Note: First-time dependency installation may take 10-30 minutes.
echo         vcpkg will cache binaries for faster subsequent builds.

popd >nul
endlocal
