// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

// Engine-internal GL helpers shared by the passes that draw over projectM's
// output (LayerCompositor, EffectsChain). Not a public header: every function
// needs the render thread's context current and `juce::gl` loaded.

#include <cstdint>
#include <string>

namespace milkdawp::engine::gl {

/// A fullscreen triangle from gl_VertexID, so there is no vertex buffer to own.
/// Passes `vUv` (0..1 over the target) to the fragment shader.
extern const char* const kFullscreenVertexSource;

/// Compiles and links a program. 0 with `error` set (prefixed by `label`) if
/// either stage or the link fails.
[[nodiscard]] std::uint32_t buildProgram(const char* vertexSource, const char* fragmentSource, const char* label,
                                         std::string& error);

/// The GL state a pass changes, captured before it and put back after, so
/// projectM finds the context as it left it.
struct SavedState {
  int program = 0;
  int vertexArray = 0;
  int framebuffer = 0;
  int viewport[4] = {0, 0, 0, 0};
  int activeTexture = 0;
  int texture2d = 0;
  unsigned char blend = 0;
  unsigned char depthTest = 0;
  unsigned char cullFace = 0;
  unsigned char scissorTest = 0;
  int blendSrcRgb = 0;
  int blendDstRgb = 0;
  int blendSrcAlpha = 0;
  int blendDstAlpha = 0;
  unsigned char colourMask[4] = {1, 1, 1, 1};

  void capture();
  void restore() const;
};

} // namespace milkdawp::engine::gl
