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
- [x] Implement JUCE message thread for UI updates
- [x] Create parameter change notification system
- [x] Wire up thread-safe communication: Message↔Audio
- [x] Wire up thread-safe communication: Message↔Visualization
- [x] Add basic thread safety tests

---

## Phase 2: Parameter System & State Management
**Goal:** All controls automatable, MIDI-assignable, and state-persistent

### 2.1 Parameter Definitions
- [x] Define Beat Sensitivity parameter (0.0 - 2.0)
- [x] Define Transition Duration parameter (0.1 - 30.0 seconds)
- [x] Define Shuffle toggle parameter
- [x] Define Lock Current Preset toggle parameter
- [x] Define Preset Index parameter (0-128)
- [x] Mark all parameters as automatable

### 2.2 State Management
- [x] Implement plugin state save/restore
- [x] Save/restore current preset path
- [x] Save/restore current playlist folder path
- [x] Save/restore preset index in playlist
- [x] Save/restore all control values
- [x] Test deterministic state restoration

### 2.3 Parameter→projectM Wiring
- [x] Wire Beat Sensitivity to libprojectM
- [x] Wire Transition Duration to libprojectM
- [x] Wire shuffle/lock state to playlist manager
- [x] Ensure parameters update visualization thread safely
- [x] Test parameter automation in test DAW

---

## Phase 3: ProjectM Preset Management
**Goal:** Load single presets and preset playlists

### 3.1 Single Preset Loading
- [x] Implement file picker UI component (initially basic)
- [x] Load single .milk preset into libprojectM
- [x] Display current preset name in UI
- [x] Handle preset load errors gracefully
- [x] Store current preset in state

### 3.2 Playlist System
- [x] Implement folder picker UI component
- [x] Scan folder for .milk files and build playlist
- [x] Implement playlist data structure with shuffle support
- [x] Implement Next/Prev navigation logic
- [x] Show playlist transport panel conditionally

### 3.3 Playlist Transport Logic
- [x] Wire Lock toggle (prevents auto-transitions)
- [x] Wire Shuffle toggle (randomizes order)
- [x] Implement timed auto-transitions based on duration
- [x] Wire Prev/Next buttons
- [x] Handle playlist index automation parameter

### 3.4 Transition Styles
- [x] Research libprojectM transition options
- [x] Implement transition style picker UI
- [x] Wire transition style to libprojectM
- [x] Store transition style in state

---

## Phase 4: Basic UI & Embedded Visualization
**Goal:** Main plugin window with embedded visualization

### 4.1 Main Window Layout
- [x] Create 1200x650 default window size
- [x] Design layout: controls displayed across the top, visualization area below
- [x] Add branded logo display
- [x] Create professional hardware-style control appearance

### 4.2 Control Widgets
- [x] Implement Beat Sensitivity knob (hardware style)
- [x] Implement Transition Duration knob
- [x] Implement Lock/Shuffle toggle buttons
- [x] Implement Prev/Next transport buttons
- [x] Implement file/folder picker widgets
- [x] Style all controls per README spec

### 4.3 Embedded Visualization Display
This step replaces the current placeholder visualization with a real embedded projectM renderer. It must build on prior work already completed in this ROADMAP:
- Use the existing threading model from Phase 1 (audio thread capture → thread-safe queue → dedicated visualization render thread) and the message-thread wiring from Phase 2.
- Honor the parameter/control wiring from Phases 2–3 so that UI controls immediately affect projectM.

- [x] Replace placeholder with an embedded JUCE OpenGL/GPU canvas hosting projectM
- [x] Initialize/own the projectM instance on the visualization thread (reuse Visualization Thread from 1.2)
- [x] Render projectM frames into the embedded canvas (no external window)
- [x] Respect allocated bounds in the main window layout and maintain chosen aspect policy
- [x] Handle JUCE resize() by resizing/recreating the GL framebuffer and notifying projectM
- [x] Verify controls drive the visualization via existing wiring:
  - [x] Beat Sensitivity affects analysis/projectM inputs
  - [x] Transition Duration drives auto-transitions
  - [x] Lock/Shuffle, Prev/Next control playlist behavior
  - [x] File/Folder pickers update the active preset/playlist
- [x] On plugin load (with restored state), the visualization appears and animates within the embedded canvas

### 4.4 Audio Feed Into projectM (new)
To make projectM respond directly to the plugin’s audio input (beyond the CPU fallback), feed PCM into projectM on the GL thread and map parameters appropriately.
- [x] Feed stereo PCM frames from the audio thread to the GL thread (lock-free queue) in a format expected by libprojectM (e.g., float/16-bit)
- [x] Call projectM’s audio input API each frame with the latest PCM window
- [x] Map Beat Sensitivity to input scaling/thresholds
- [x] Verify responsiveness with various buffer sizes and sample rates
- [x] Gracefully handle missing audio (silence) and plugin bypass

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
- [x] Implement fullscreen toggle button
- [x] Detect which monitor contains visualization window
- [x] Enter fullscreen on correct monitor
- [x] Auto-detach when entering fullscreen
- [x] Auto-exit fullscreen when docking
- [x] Implement F11 keyboard shortcut

### 5.3 Visualization Window Features
- [x] Add transparent hover button (bottom-right corner)
- [x] Make hover button toggle fullscreen
- [x] Support window transparency (for OBS)
- [x] Set window title/class for easy OBS identification
- [ ] Test all window state transitions

### 5.4 Window Communication
- [x] Implement real-time parameter updates: Main→External
- [x] Implement real-time parameter updates: External→Main
- [ ] Test parameter changes propagate instantly
- [x] Handle edge cases (window closed during update, etc.)

---

## Phase 6: UI Refinement & Professional Polish
**Goal:** Modernize UI with logo, compact controls, and improved UX

### 6.1 Logo Integration
- [x] Add logo image asset to project resources
- [x] Create image component to display logo
- [x] Replace "MilkDAWp" text with logo image
- [x] Scale logo to appropriate size (~50% of original)
- [x] Position logo in main window layout

### 6.2 Preset Selection Combobox
- [x] Replace "Load Preset..." button with JUCE ComboBox
- [x] Populate combobox with current playlist presets (indexed)
- [x] Display empty state when no active preset
- [x] Display active preset name when preset loaded
- [x] Disable combobox when no active playlist
- [x] Wire combobox selection to preset index parameter
- [x] Update combobox when preset changes externally

### 6.3 Compact Playlist Picker
- [x] Create icon-only button for playlist/preset selection
- [x] Position button to right of preset combobox
- [x] Use appropriate file/folder icon
- [x] Implement preset file picker dialog
- [x] Auto-detect parent folder as playlist
- [x] Set selected preset as active and update index
- [x] Handle preset blacklist/filtering
- [x] Remove old "Load Preset..." and "Load Playlist Folder..." buttons

### 6.4 Icon-Based Toggle Controls
- [x] Replace Lock checkbox with dual-state icon button
- [x] Replace Shuffle checkbox with dual-state icon button
- [x] Select appropriate icons for each toggle state
- [x] Position Lock button next to playlist picker
- [x] Position Shuffle button next to Lock button
- [x] Maintain parameter binding for all toggles
- [x] Update button state when parameters change

### 6.5 Transition Style Popover
- [x] Create icon-only transition button
- [x] Implement popover menu for transition styles
- [x] Populate popover with text-based style options
- [x] Position button next to Shuffle button
- [x] Wire popover selection to transition parameter
- [x] Show current selection in popover

### 6.6 Compact Control Layout
- [x] Arrange controls in horizontal row: [Logo][Combobox][Playlist Picker][Lock][Shuffle][Transition Popover][Duration Knob][Beat Sensitivity Knob][Pop-out][Fullscreen]
- [x] Ensure all controls use consistent sizing/spacing
- [x] Verify responsive behavior on window resize
- [x] Test all control interactions
- [x] Remove any obsolete UI elements

### 6.7 Detached Window Overlay Controls
- [x] Convert detached window button row to overlay
- [x] Position overlay at bottom of visualization (not top)
- [x] Implement hover detection for overlay visibility
- [x] Show overlay only on mouse hover
- [x] Hide overlay when mouse leaves area
- [x] Ensure overlay doesn't block visualization when hidden
- [x] Test overlay behavior in both windowed and fullscreen modes

### 6.8 True Fullscreen & Resizable Window
- [x] Implement true fullscreen mode (not windowed fullscreen)
- [x] Ensure fullscreen occupies entire screen with no borders
- [x] Make non-fullscreen detached window properly resizable
- [x] Maintain aspect ratio or fill strategy on resize
- [x] Test fullscreen on multi-monitor setups
- [x] Verify smooth transitions between window states

---

## Phase 7: Multi-Instance & Resource Optimization
**Goal:** Efficient resource sharing between plugin instances

### 7.1 Asset Caching
- [ ] Implement shared preset cache across instances
- [ ] Implement shared texture/resource pool
- [ ] Add reference counting for shared resources
- [ ] Test load time improvements with multiple instances

### 7.2 Memory Management
- [ ] Profile memory usage per instance
- [ ] Optimize projectM memory footprint
- [ ] Implement lazy loading where appropriate
- [ ] Test stability with 8+ simultaneous instances

---

## Phase 8: Performance & Adaptive Quality
**Goal:** Maintain performance under heavy CPU/GPU load

### 8.1 Performance Monitoring
- [ ] Implement FPS counter
- [ ] Monitor CPU usage per thread
- [ ] Monitor GPU usage/frame time
- [ ] Add performance metrics logging

### 8.2 Adaptive Quality System
- [ ] Define quality levels (resolution, particle count, etc.)
- [ ] Implement automatic quality reduction under load
- [ ] Implement automatic quality increase when headroom available
- [ ] Add manual quality override option
- [ ] Test under various load conditions

---

## Phase 9: Diagnostics & First-Run Experience
**Goal:** Help users troubleshoot and optimize on first launch

### 9.1 Diagnostic System
- [ ] Implement GPU capability detection
- [ ] Implement system resource detection
- [ ] Create diagnostic report generator
- [ ] Add troubleshooting hints in UI

### 9.2 First-Run Benchmark
- [ ] Create automated benchmark on first launch
- [ ] Test various quality settings
- [ ] Auto-select optimal default quality
- [ ] Store benchmark results for future diagnostics
- [ ] Make benchmark re-runnable from menu

---

## Phase 10: Bundled Content & Packaging
**Goal:** Ship with presets and create installers

### 10.1 Bundled Presets
- [ ] Curate/license preset collection
- [ ] Bundle presets with plugin
- [ ] Set default preset for first launch
- [ ] Test preset loading from bundle location

### 10.2 Installer Creation
- [ ] Create Windows installer (self-contained)
- [ ] Create macOS installer (self-contained)
- [ ] Create Linux installer (self-contained)
- [ ] Include all required DLLs/dylibs per README spec
- [ ] Test installation on clean systems

### 10.3 Documentation
- [ ] Create user manual
- [ ] Create quick-start guide
- [ ] Document MIDI assignment workflow
- [ ] Document OBS integration workflow

---

## Phase 11: Testing & MVP Release
**Goal:** Meet all acceptance criteria and release MVP

### 11.1 Acceptance Criteria Verification
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

### 11.2 Integration Testing
- [ ] Test in multiple DAWs (Ableton, FL Studio, Reaper, Logic)
- [ ] Test with various audio interfaces
- [ ] Test multi-monitor setups
- [ ] Test OBS capture workflow
- [ ] Test MIDI controller assignment

### 11.3 Edge Case Testing
- [ ] Test plugin reload/state persistence
- [ ] Test rapid parameter changes
- [ ] Test window close/reopen scenarios
- [ ] Test invalid preset file handling
- [ ] Test empty playlist folder handling
- [ ] Test system sleep/wake
- [ ] Test multiple instances simultaneously

### 11.4 Performance Testing
- [ ] Benchmark CPU usage vs visualization quality
- [ ] Test long-duration sessions (4+ hours)
- [ ] Profile memory leaks
- [ ] Test under high system load

### 11.5 Release Preparation
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