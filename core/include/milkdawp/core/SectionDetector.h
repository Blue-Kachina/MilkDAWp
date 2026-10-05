// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <vector>

namespace milkdawp::core {

struct SectionDetectorConfig {
  /// A drop: the bass must jump at least this far above the loudest it was
  /// during the quiet stretch before it. The Energy Threshold parameter.
  float jumpDb = 10.0f;
  /// ...and come back to within this of the track's loud level, so a soft
  /// kick re-entering in a breakdown isn't a drop.
  float recoverDb = 6.0f;
  /// How long the bass must have stayed down before a drop counts: a
  /// breakdown, not the gap between two hits.
  float quietSeconds = 2.5f;
  /// The last moments before the drop are left out of the quiet stretch, so
  /// a fill, a kick roll or a riser's last beat doesn't hide it.
  float guardSeconds = 0.5f;
  /// The bass level is averaged over this long, so single kicks don't count.
  float levelSeconds = 0.5f;
  /// The loud level is this percentile of the level over `historySeconds`.
  float historySeconds = 60.0f;
  float referencePercentile = 0.8f;
  /// Bass energy below this is silence: nothing comes back from it.
  float floorEnergy = 1.0f;
};

struct SectionFrame {
  bool drop = false;
  /// The bass has stayed `jumpDb` below the loud level for `quietSeconds`:
  /// a drop can happen now.
  bool breakdown = false;
  float bassDb = -120.0f;      // this hop's bass envelope
  float levelDb = -120.0f;     // the bass averaged over `levelSeconds`
  float quietMaxDb = -120.0f;  // the loudest level in the quiet stretch
  float referenceDb = -120.0f; // the track's loud level
};

/// Section changes for the Energy transition mode (§4.4, 5.1): finds drops,
/// the moment the bass comes back in after a breakdown or build-up.
///
/// Works on the bass band because that is what a breakdown takes away and a
/// drop brings back; broadband energy also rises through a build-up, and its
/// kick-to-kick swing in a steady section looks like a jump. Comparing with
/// the *loudest* moment of the stretch before (rather than its mean) means
/// steady kicks, sparse hits and tempo changes never qualify: there is
/// always a hit as loud as this one in the 2.5 s before it.
///
/// Pure and deterministic, one call per analysis hop, like the other core
/// analysis stages (§4.3). Its buffers are sized at construction, and again
/// only when `setConfig` changes a window length.
class SectionDetector {
public:
  explicit SectionDetector(double sampleRate,
                           std::size_t hopSize = 512,
                           SectionDetectorConfig config = {});

  void setConfig(const SectionDetectorConfig& config);
  [[nodiscard]] const SectionDetectorConfig& config() const noexcept { return config_; }

  /// `bassEnergy`: `AnalysisFrame::bassEnergy`. `strongBassOnset`: the bass
  /// onset detector fired on this hop (a drop lands on one).
  SectionFrame processHop(float bassEnergy, bool strongBassOnset);

  void reset();

private:
  void resize();
  [[nodiscard]] float reference();

  double hopSeconds_;
  SectionDetectorConfig config_;

  // The last `levelHops_` bass energies (linear) and their sum.
  std::vector<float> levelRing_;
  std::size_t levelHops_ = 1;
  std::size_t levelPos_ = 0;
  std::size_t levelCount_ = 0;
  double levelSum_ = 0.0;

  // The level (dB) of the last `quietHops_ + guardHops_` hops, newest last.
  std::vector<float> recentLevels_;
  std::size_t quietHops_ = 1;
  std::size_t guardHops_ = 0;
  std::size_t recentPos_ = 0;
  std::size_t recentCount_ = 0;

  // The level (dB) every `historyStepHops_` hops over `historySeconds`.
  std::vector<float> history_;
  std::vector<float> historyScratch_;
  std::size_t historyStepHops_ = 1;
  std::size_t historyPos_ = 0;
  std::size_t historyCount_ = 0;
  std::size_t hopsSinceHistory_ = 0;
  float referenceDb_ = -120.0f;

  bool haveDropped_ = false;
  std::size_t hopsSinceDrop_ = 0;
};

} // namespace milkdawp::core
