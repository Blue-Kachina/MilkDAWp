# Dependency Management Roadmap

## Overview
This document outlines MilkDAWp's migration from git submodules to vcpkg for dependency management. The goal is to achieve rock-solid, reproducible builds with locked dependency versions and easy onboarding for new developers.

## Current State (Post-Migration)

### Dependencies Managed by vcpkg
- **JUCE**: Version 7.0.12 (audio plugin framework)
- **projectM**: Version 4.1.4 (visualization library, LGPL-2.1)

### Key Files
- `vcpkg.json` - Manifest declaring dependencies and locked versions
- `vcpkg-configuration.json` - Locked vcpkg baseline for reproducibility
- `triplets/*.cmake` - Custom triplets enforcing dynamic linking
- `CMakePresets.json` - Build presets with vcpkg integration
- `.gitmodules` - Deprecated (dependencies now via vcpkg)

## Why vcpkg?

### Before (Git Submodules)
❌ Manual submodule initialization required
❌ Mixed version management (git tags + CMake variables)
❌ Nested submodule dependencies complicated builds
❌ No cross-platform binary caching
❌ Difficult to manage transitive dependencies

### After (vcpkg Manifest Mode)
✅ Single `cmake --preset` command installs everything
✅ Locked versions via baseline + overrides in vcpkg.json
✅ Binary caching speeds up CI/CD and clean builds
✅ Cross-platform portability (Windows, macOS, Linux)
✅ Automatic transitive dependency resolution
✅ Custom triplets enforce dynamic linking (LGPL compliance)

## Architecture

### Dependency Resolution Flow
```
vcpkg.json (manifest)
    ↓
vcpkg-configuration.json (baseline lock)
    ↓
Custom triplets (x64-windows-dynamic, etc.)
    ↓
vcpkg installs dependencies
    ↓
CMake find_package(JUCE/projectm CONFIG REQUIRED)
    ↓
Build succeeds with locked versions
```

### Version Locking Strategy
1. **Baseline Lock**: `vcpkg-configuration.json` pins the vcpkg registry commit
2. **Version Overrides**: `vcpkg.json` overrides section locks specific versions
3. **Triplet Enforcement**: Custom triplets ensure dynamic linking

## Setup Instructions

### Prerequisites
1. Install vcpkg:
   ```bash
   # Windows
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   setx VCPKG_ROOT "C:\vcpkg"

   # macOS/Linux
   git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
   cd ~/vcpkg
   ./bootstrap-vcpkg.sh
   export VCPKG_ROOT=~/vcpkg
   # Add to ~/.bashrc or ~/.zshrc
   ```

2. Verify installation:
   ```bash
   # Windows
   %VCPKG_ROOT%\vcpkg.exe version

   # macOS/Linux
   $VCPKG_ROOT/vcpkg version
   ```

### Building the Project

#### Windows
```bash
# Run bootstrap (validates vcpkg setup)
.\scripts\bootstrap.bat

# Configure (installs dependencies automatically)
cmake --preset dev-win

# Build
cmake --build build-win --config Release
```

#### macOS
```bash
# Run bootstrap
./scripts/bootstrap.sh

# Configure
cmake --preset dev-mac

# Build
cmake --build build-mac --config Release
```

#### Linux (CI)
```bash
# Run bootstrap
./scripts/bootstrap.sh

# Configure
cmake --preset ci-linux

# Build tests only
cmake --build build-ci --config RelWithDebInfo
```

### First-Time Build Notes
- First build will take 10-30 minutes (compiling dependencies)
- Subsequent builds are much faster (vcpkg binary cache)
- Dependencies install to `build-*/vcpkg_installed/` (git-ignored)

## Dynamic Linking Enforcement

### Why Dynamic?
- **LGPL Compliance**: projectM is LGPL-2.1, requiring dynamic linking
- **MSVC Runtime**: `/MD` flag requires dynamic CRT on Windows
- **Cross-Platform**: Consistent behavior across platforms

### Custom Triplets
Located in `triplets/`:
- `x64-windows-dynamic.cmake` - Windows x64 with dynamic libs
- `x64-osx-dynamic.cmake` - macOS Intel with dynamic libs
- `arm64-osx-dynamic.cmake` - macOS Apple Silicon with dynamic libs
- `x64-linux-dynamic.cmake` - Linux x64 with dynamic libs

All triplets set:
```cmake
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CRT_LINKAGE dynamic)
```

## Updating Dependencies

### Updating to Latest Patch Versions
1. Update `vcpkg-configuration.json` baseline to newer vcpkg commit:
   ```bash
   # Find latest vcpkg commit
   cd $VCPKG_ROOT
   git pull
   git log --oneline -n 5
   ```

2. Copy the latest commit hash to `vcpkg-configuration.json`:
   ```json
   {
     "default-registry": {
       "baseline": "NEW_COMMIT_HASH_HERE"
     }
   }
   ```

3. Test the build:
   ```bash
   # Clean and rebuild
   rm -rf build-*/vcpkg_installed
   cmake --preset dev-win  # or dev-mac, ci-linux
   cmake --build build-* --config Release
   ```

### Updating to Specific Versions
1. Edit `vcpkg.json` overrides section:
   ```json
   "overrides": [
     {
       "name": "juce",
       "version": "8.0.0"
     },
     {
       "name": "projectm",
       "version": "4.2.0"
     }
   ]
   ```

2. Verify version availability:
   ```bash
   vcpkg search juce
   vcpkg search projectm
   ```

3. Clean and rebuild as shown above.

## Troubleshooting

### vcpkg Not Found
**Error**: `VCPKG_ROOT environment variable is not set`

**Solution**: Set `VCPKG_ROOT` and restart terminal:
```bash
# Windows
setx VCPKG_ROOT "C:\vcpkg"

# macOS/Linux
export VCPKG_ROOT=~/vcpkg
# Add to ~/.bashrc or ~/.zshrc for persistence
```

### Wrong Library Linkage
**Error**: `Expected SHARED_LIBRARY for LGPL compliance`

**Solution**: Ensure custom triplet is specified in CMakePresets.json:
```json
"VCPKG_TARGET_TRIPLET": "x64-windows-dynamic",
"VCPKG_OVERLAY_TRIPLETS": "${sourceDir}/triplets"
```

### Dependency Version Mismatch
**Error**: Build fails after updating baseline

**Solution**: Clear vcpkg cache and reinstall:
```bash
# Clear installed dependencies
rm -rf build-*/vcpkg_installed

# Reconfigure
cmake --preset dev-win
```

### Binary Cache Issues
To use vcpkg binary caching (speeds up CI):
```bash
# Set binary cache location
export VCPKG_BINARY_SOURCES="clear;files,~/.vcpkg-cache,readwrite"

# Or use NuGet/Azure Artifacts for team sharing
```

## Migration from Submodules (Historical)

### What Changed
| Aspect | Before (Submodules) | After (vcpkg) |
|--------|---------------------|---------------|
| Setup | `git submodule update --init` | `cmake --preset dev-win` |
| Version Lock | Git tags + CMake vars | vcpkg.json + baseline |
| Build Time | 20-40 min (full rebuild) | 10-30 min (first), <5 min (cached) |
| Dependencies | `extern/JUCE`, `extern/projectm` | `vcpkg_installed/` |
| Updates | Manual git checkout | Edit vcpkg.json |

### Removed Files/Patterns
- Git submodule entries in `.gitmodules` (now deprecated note only)
- FetchContent blocks in `CMakeLists.txt` (replaced with find_package)
- Submodule clone fallback logic in bootstrap scripts

### New Files
- `vcpkg.json` - Dependency manifest
- `vcpkg-configuration.json` - Baseline lock
- `triplets/*.cmake` - Dynamic linking enforcement
- Updated `CMakePresets.json` - Triplet specifications

## CI/CD Integration

### GitHub Actions Example
```yaml
- name: Setup vcpkg
  run: |
    git clone https://github.com/microsoft/vcpkg.git
    ./vcpkg/bootstrap-vcpkg.sh
    echo "VCPKG_ROOT=${{ github.workspace }}/vcpkg" >> $GITHUB_ENV

- name: Configure
  run: cmake --preset ci-linux

- name: Build
  run: cmake --build build-ci --config RelWithDebInfo
```

### Binary Caching in CI
Use GitHub cache action:
```yaml
- name: Cache vcpkg
  uses: actions/cache@v3
  with:
    path: build-ci/vcpkg_installed
    key: vcpkg-${{ runner.os }}-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json') }}
```

## Best Practices

### Version Management
1. ✅ Always lock baseline in `vcpkg-configuration.json`
2. ✅ Use overrides in `vcpkg.json` for specific versions
3. ✅ Test dependency updates in a feature branch first
4. ✅ Document version changes in commit messages

### Build Hygiene
1. ✅ Add `vcpkg_installed/` to `.gitignore`
2. ✅ Clean builds when changing dependency versions
3. ✅ Use CMake presets for consistent configuration
4. ✅ Verify dynamic linking with platform tools (ldd, otool, dumpbin)

### Team Collaboration
1. ✅ Commit `vcpkg.json` and `vcpkg-configuration.json`
2. ✅ Share VCPKG_ROOT setup in onboarding docs
3. ✅ Use binary caching for faster team builds
4. ✅ Document custom triplets and their purpose

## Future Enhancements

### Potential Improvements
- [ ] Setup GitHub Actions binary cache for faster CI
- [ ] Consider vcpkg registries for internal dependencies
- [ ] Explore vcpkg asset caching for preset bundles
- [ ] Add version constraint validation in CI

### Monitoring
- Track dependency update frequency
- Monitor build time improvements with caching
- Review licensing compliance periodically
- Audit transitive dependencies for security

## References

- [vcpkg Documentation](https://vcpkg.io/en/docs/README.html)
- [vcpkg Manifest Mode](https://vcpkg.io/en/docs/users/manifests.html)
- [vcpkg Versioning](https://vcpkg.io/en/docs/users/versioning.html)
- [Custom Triplets](https://vcpkg.io/en/docs/users/triplets.html)
- [JUCE CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md)
- [projectM GitHub](https://github.com/projectM-visualizer/projectm)

---

**Status**: ✅ Migration Complete
**Last Updated**: 2025-10-15
**Maintained By**: MilkDAWp Development Team
