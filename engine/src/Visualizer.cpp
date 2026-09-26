// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/Visualizer.h"

#include <algorithm>

namespace milkdawp::engine {

namespace {
constexpr int kDefaultScratchFrames = 4096;
}

Visualizer::Visualizer(const Config& config)
    : render_(RenderEngine::create(ring_, config.render, config.bundleDirectoryHint)),
      director_(std::make_unique<Director>(ring_, *render_, config.followHostTransport)),
      interleaveScratch_(static_cast<std::size_t>(kDefaultScratchFrames) * kRingChannels, 0.0f) {
  director_->setSampleRate(config.render.sampleRate);
}

Visualizer::~Visualizer() {
  // Director first: it feeds the render engine.
  director_.reset();
  render_.reset();
}

void Visualizer::prepare(double sampleRate, int maxBlockSize) {
  interleaveScratch_.assign(static_cast<std::size_t>(std::max(maxBlockSize, kDefaultScratchFrames)) * kRingChannels,
                            0.0f);
  render_->setSampleRate(sampleRate);
  director_->setSampleRate(sampleRate);
}

void Visualizer::processAudio(const float* const* channels, int numChannels, int numSamples,
                              const core::TransportInfo* hostTransport) noexcept {
  if (hostTransport != nullptr) {
    auto info = *hostTransport;
    info.samplePos = ring_.samplePosition(); // this block's first frame, in ring numbering
    director_->publishHostTransport(info);
  }
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  const float* left = channels[0];
  const float* right = channels[numChannels > 1 ? 1 : 0];
  const auto scratchFrames = interleaveScratch_.size() / kRingChannels;
  int offset = 0;
  // Blocks larger than prepare() promised go through in chunks: never
  // allocate here.
  while (offset < numSamples) {
    const auto frames = std::min<std::size_t>(static_cast<std::size_t>(numSamples - offset), scratchFrames);
    for (std::size_t i = 0; i < frames; ++i) {
      interleaveScratch_[i * kRingChannels] = left[static_cast<std::size_t>(offset) + i];
      interleaveScratch_[i * kRingChannels + 1] = right[static_cast<std::size_t>(offset) + i];
    }
    ring_.write(interleaveScratch_.data(), frames);
    offset += static_cast<int>(frames);
  }
}

void Visualizer::setControls(const EngineControls& controls) noexcept {
  render_->setBeatSensitivity(controls.beatSensitivity);
  render_->setQualityScale(controls.qualityScale);
  director_->setControls(controls);
}

} // namespace milkdawp::engine
