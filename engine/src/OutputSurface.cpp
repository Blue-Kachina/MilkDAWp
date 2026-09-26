// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/OutputSurface.h"

#include <cmath>

namespace milkdawp::engine {

OutputSurface::OutputSurface(RenderEngine& engine) : engine_(engine) {
  engine_.glContext().setRenderer(this);
  engine_.glContext().setContinuousRepainting(true);
  engine_.glContext().attachTo(*this);
}

OutputSurface::~OutputSurface() { engine_.glContext().detach(); }

void OutputSurface::newOpenGLContextCreated() {
  engine_.notifyGlContextCreated();
  engine_.ensureInstanceCreated();
}

void OutputSurface::renderOpenGL() {
  using namespace ::juce::gl;

  const double seconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
  const auto phase = static_cast<float>(std::fmod(seconds, juce::MathConstants<double>::twoPi));
  const float r = 0.5f + 0.5f * std::sin(phase);
  const float g = 0.5f + 0.5f * std::sin(phase + 2.0943951f); // +120 degrees
  const float b = 0.5f + 0.5f * std::sin(phase + 4.1887902f); // +240 degrees

  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

void OutputSurface::openGLContextClosing() { engine_.releaseInstance(); }

} // namespace milkdawp::engine
