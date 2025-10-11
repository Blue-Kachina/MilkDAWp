# MilkDAWp Development Roadmap

## Phase 0: Project Foundation
**Goal:** Establish build system, project structure, and dependencies

### 0.1 Project Scaffolding
- [x] Initialize JUCE project with Projucer/CMake
- [x] Configure VST3 plugin format with audio effect type
- [x] Set up multi-platform build targets (Windows, macOS, Linux)
- [x] Configure static linking preferences per README technical details
- [x] Create basic project directory structure

### 0.2 Dependency Integration
- [x] Integrate JUCE framework (latest stable version)
- [x] Integrate libprojectM library
- [ ] Verify licenses compatibility for static linking
- [x] Configure build scripts to link dependencies
- [x] Create minimal "hello world" plugin that loads successfully

### 0.3 Development Infrastructure
- [x] Set up logging framework
- [x] Create test framework structure (unit + integration)
- [x] Configure CI/CD pipelines for three platforms
- [x] Create initial build documentation

---

## Phase 1: Core Audio & Threading Architecture
**Goal:** Establish zero-latency audio path and proper threading model

### 1.1 Audio Thread Setup
- [x] Implement plugin audio processor skeleton
- [x] Verify zero-latency passthrough (audio in = audio out)
- [x] Create FFT analysis capture on audio thread
- [x] Implement beat detection capture on audio thread
- [x] Add thread-safe queue for audio→visualization data

### 1.2 Visualization Thread
- [x] Create dedicated visualization rendering thread
- [x] Initialize projectM instance on visualization thread
- [x] Implement thread-safe data consumption from audio thread
- [x] Set up basic GPU surface rendering
- [x] Verify frame rate independence from audio thread

### 1.3 Message Thread
- [ ] Implement JUCE message thread for UI updates
- [ ] Create parameter change notification system
- [ ] Wire up thread-safe communication: Message↔Audio
- [ ] Wire up thread-safe communication: Message↔Visualization
- [ ] Add basic thread safety tests

---

## Phase 2: Parameter System & State Management
**Goal:** All controls automatable, MIDI-assignable, and state-persistent

### 2.1 Parameter Definitions
- [ ] Define Beat Sensitivity parameter (0.0 - 2.0)
- [ ] Define Transition Duration parameter (0.1 - 30.0 seconds)
- [ ] Define Shuffle toggle parameter
- [ ] Define Lock Current Preset toggle parameter
- [ ] Define Preset Index parameter (0-128)
- [ ] Mark all parameters as automatable

### 2.2 State Management
- [ ] Implement plugin state save/restore
- [ ] Save/restore current preset path
- [ ] Save/restore current playlist folder path
- [ ] Save/restore preset index in playlist
- [ ] Save/restore all control values
- [ ] Test deterministic state restoration

### 2.3 Parameter→projectM Wiring
- [ ] Wire Beat Sensitivity to libprojectM
- [ ] Wire Transition Duration to libprojectM
- [ ] Wire shuffle/lock state to playlist manager
- [ ] Ensure parameters update visualization thread safely
- [ ] Test parameter automation in test DAW

---

## Phase 3: ProjectM Preset Management
**Goal:** Load single presets and preset playlists

### 3.1 Single Preset Loading
- [ ] Implement file picker UI component (initially basic)
- [ ] Load single .milk preset into libprojectM
- [ ] Display current preset name in UI
- [ ] Handle preset load errors gracefully
- [ ] Store current preset in state

### 3.2 Playlist System
- [ ] Implement folder picker UI component
- [ ] Scan folder for .milk files and build playlist
- [ ] Implement playlist data structure with shuffle support
- [ ] Implement Next/Prev navigation logic
- [ ] Show playlist transport panel conditionally

### 3.3 Playlist Transport Logic
- [ ] Wire Lock toggle (prevents auto-transitions)
- [ ] Wire Shuffle toggle (randomizes order)
- [ ] Implement timed auto-transitions based on duration
- [ ] Wire Prev/Next buttons
- [ ] Handle playlist index automation parameter

### 3.4 Transition Styles
- [ ] Research libprojectM transition options
- [ ] Implement transition style picker UI
- [ ] Wire transition style to libprojectM
- [ ] Store transition style in state

---

## Phase 4: Basic UI & Embedded Visualization
**Goal:** Main plugin window with embedded visualization

### 4.1 Main Window Layout
- [ ] Create 1200x650 default window size
- [ ] Design layout: controls displayed across the top, visualization area below
- [ ] Add branded logo display
- [ ] Create professional hardware-style control appearance

### 4.2 Control Widgets
- [ ] Implement Beat Sensitivity knob (hardware style)
- [ ] Implement Transition Duration knob
- [ ] Implement Lock/Shuffle toggle buttons
- [ ] Implement Prev/Next transport buttons
- [ ] Implement file/folder picker widgets
- [ ] Style all controls per README spec

### 4.3 Embedded Visualization Display
- [ ] Create embedded OpenGL/GPU canvas component
- [ ] Render libprojectM output to embedded canvas
- [ ] Ensure visualization respects allocated space
- [ ] Handle resize events properly
- [ ] Test visualization appears on plugin load

---

## Phase 5: Window Management (Detached & Fullscreen)
**Goal:** Pop-out window, fullscreen modes, window communication

### 5.1 Detached Window
- [ ] Implement "Pop-out" button on main window
- [ ] Create external window class for visualization
- [ ] Transfer visualization context to external window
- [ ] Implement "Dock" button on external window
- [ ] Destroy external window when main window closes

### 5.2 Fullscreen Support
- [ ] Implement fullscreen toggle button
- [ ] Detect which monitor contains visualization window
- [ ] Enter fullscreen on correct monitor
- [ ] Auto-detach when entering fullscreen
- [ ] Auto-exit fullscreen when docking
- [ ] Implement F11 keyboard shortcut

### 5.3 Visualization Window Features
- [ ] Add transparent hover button (bottom-right corner)
- [ ] Make hover button toggle fullscreen
- [ ] Support window transparency (for OBS)
- [ ] Set window title/class for easy OBS identification
- [ ] Test all window state transitions

### 5.4 Window Communication
- [ ] Implement real-time parameter updates: Main→External
- [ ] Implement real-time parameter updates: External→Main
- [ ] Test parameter changes propagate instantly
- [ ] Handle edge cases (window closed during update, etc.)

---

## Phase 6: Multi-Instance & Resource Optimization
**Goal:** Efficient resource sharing between plugin instances

### 6.1 Asset Caching
- [ ] Implement shared preset cache across instances
- [ ] Implement shared texture/resource pool
- [ ] Add reference counting for shared resources
- [ ] Test load time improvements with multiple instances

### 6.2 Memory Management
- [ ] Profile memory usage per instance
- [ ] Optimize projectM memory footprint
- [ ] Implement lazy loading where appropriate
- [ ] Test stability with 8+ simultaneous instances

---

## Phase 7: Performance & Adaptive Quality
**Goal:** Maintain performance under heavy CPU/GPU load

### 7.1 Performance Monitoring
- [ ] Implement FPS counter
- [ ] Monitor CPU usage per thread
- [ ] Monitor GPU usage/frame time
- [ ] Add performance metrics logging

### 7.2 Adaptive Quality System
- [ ] Define quality levels (resolution, particle count, etc.)
- [ ] Implement automatic quality reduction under load
- [ ] Implement automatic quality increase when headroom available
- [ ] Add manual quality override option
- [ ] Test under various load conditions

---

## Phase 8: Diagnostics & First-Run Experience
**Goal:** Help users troubleshoot and optimize on first launch

### 8.1 Diagnostic System
- [ ] Implement GPU capability detection
- [ ] Implement system resource detection
- [ ] Create diagnostic report generator
- [ ] Add troubleshooting hints in UI

### 8.2 First-Run Benchmark
- [ ] Create automated benchmark on first launch
- [ ] Test various quality settings
- [ ] Auto-select optimal default quality
- [ ] Store benchmark results for future diagnostics
- [ ] Make benchmark re-runnable from menu

---

## Phase 9: Bundled Content & Packaging
**Goal:** Ship with presets and create installers

### 9.1 Bundled Presets
- [ ] Curate/license preset collection
- [ ] Bundle presets with plugin
- [ ] Set default preset for first launch
- [ ] Test preset loading from bundle location

### 9.2 Installer Creation
- [ ] Create Windows installer (self-contained)
- [ ] Create macOS installer (self-contained)
- [ ] Create Linux installer (self-contained)
- [ ] Include all required DLLs/dylibs per README spec
- [ ] Test installation on clean systems

### 9.3 Documentation
- [ ] Create user manual
- [ ] Create quick-start guide
- [ ] Document MIDI assignment workflow
- [ ] Document OBS integration workflow

---

## Phase 10: Testing & MVP Release
**Goal:** Meet all acceptance criteria and release MVP

### 10.1 Acceptance Criteria Verification
- [ ] Verify builds on Windows, macOS, Linux
- [ ] Verify VST3 + standalone formats
- [ ] Verify zero audio latency (measure in DAW)
- [ ] Verify all controls automatable
- [ ] Verify all controls MIDI-assignable
- [ ] Verify state restores deterministically
- [ ] Verify detached/fullscreen modes work
- [ ] Verify adaptive quality functions
- [ ] Verify diagnostics/benchmark operational
- [ ] Verify bundled presets load correctly

### 10.2 Integration Testing
- [ ] Test in multiple DAWs (Ableton, FL Studio, Reaper, Logic)
- [ ] Test with various audio interfaces
- [ ] Test multi-monitor setups
- [ ] Test OBS capture workflow
- [ ] Test MIDI controller assignment

### 10.3 Edge Case Testing
- [ ] Test plugin reload/state persistence
- [ ] Test rapid parameter changes
- [ ] Test window close/reopen scenarios
- [ ] Test invalid preset file handling
- [ ] Test empty playlist folder handling
- [ ] Test system sleep/wake
- [ ] Test multiple instances simultaneously

### 10.4 Performance Testing
- [ ] Benchmark CPU usage vs visualization quality
- [ ] Test long-duration sessions (4+ hours)
- [ ] Profile memory leaks
- [ ] Test under high system load

### 10.5 Release Preparation
- [ ] Final code review
- [ ] Update version numbers
- [ ] Prepare release notes
- [ ] Create distribution packages
- [ ] MVP Release

---

## Future Phases (Post-MVP)
*These align with "Future requirements" from README*

### Future: Snapshot Morphing & Scenes
- [ ] Design scene snapshot system
- [ ] Implement save/recall all controls
- [ ] Implement scene morphing over time
- [ ] Create scene management UI

### Future: Setlists & Cues
- [ ] Design setlist data structure
- [ ] Implement per-cue overrides
- [ ] Add DAW transport sync
- [ ] Add MIDI cue triggering

### Future: OSC/Web Remote
- [ ] Design OSC protocol
- [ ] Implement OSC server
- [ ] Create web-based remote UI
- [ ] Support mobile control

### Future: Advanced Features
- [ ] Offline high-resolution render mode
- [ ] Preset tagging system
- [ ] Preset rating system
- [ ] Smart shuffle algorithm

---

## Notes for AI Agents

**Chunk Sizing Philosophy:**
- Each task should be completable in a single focused session
- Tasks are atomic: can be implemented, tested, and committed independently
- Dependencies flow top-to-bottom within phases
- Cross-phase work should complete earlier phases first

**Testing Expectations:**
- Write tests alongside implementation
- Integration tests at end of each phase
- Don't move to next phase until current phase tests pass

**When to Ask for Help:**
- Licensing questions (Phase 0.2)
- UX design decisions (Phase 4)
- Performance targets unclear (Phase 7)
- Platform-specific issues
- End of every phase (provide developer with a clear summary of what should be manually tested)