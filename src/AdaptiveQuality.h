// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include "Logging.h"

#ifndef MDW_ENABLE_ADAPTIVE_QUALITY
#define MDW_ENABLE_ADAPTIVE_QUALITY 1
#endif

#ifndef MDW_VERBOSE_ADAPTIVE_QUALITY
#define MDW_VERBOSE_ADAPTIVE_QUALITY 0
#endif

namespace milkdawp {

// Phase 8.2: Adaptive Quality skeleton — disabled by default
// Defines quality profiles and a controller that suggests a resolution scale
// based on FPS EMA and viz-thread CPU%. No side effects unless enabled later.
struct QualityProfile
{
    // Render resolution scale (applied to back buffer size), clamped [0.5, 1.0]
    double resolutionScale = 1.0;

    // Future toggles/placeholders (effect density/complexity)
    bool highDetailEffects = true;
    bool particlesEnabled = true;
};

enum class QualityMode {
    Auto = 0,    // Adaptive based on performance
    Low = 1,     // 0.5x resolution
    Medium = 2,  // 0.75x resolution
    High = 3     // 1.0x resolution
};

class AdaptiveQualityController
{
public:
    void setTargetFps(double fps) { targetFps.store(juce::jlimit(1.0, 240.0, fps)); }

    // Configure thresholds and hysteresis
    void setFpsThresholds(double lowFps, double highFps)
    {
        fpsLow.store(lowFps);
        fpsHigh.store(juce::jmax(highFps, lowFps + 1.0));
    }
    void setCpuThresholds(double highPct, double relaxPct)
    {
        cpuHigh.store(juce::jlimit(0.0, 100.0, highPct));
        cpuRelax.store(juce::jlimit(0.0, 100.0, relaxPct));
    }

    // Set manual quality override (Auto = 0, Low = 1, Medium = 2, High = 3)
    void setQualityMode(QualityMode mode)
    {
        manualMode.store(static_cast<int>(mode));
    }

    QualityMode getQualityMode() const
    {
        return static_cast<QualityMode>(manualMode.load());
    }

    struct Decision {
        double suggestedScale = 1.0;  // [0.5, 1.0]
        QualityProfile profile;       // mirrors suggestedScale
        juce::String reason;          // human-readable rationale
    };

    // Compute a suggested resolution scale based on the latest metrics.
    // This method is cheap and can be called each perf interval.
    Decision evaluate(double fpsEma, double frameMsEma, double cpuPct)
    {
        juce::ignoreUnused(frameMsEma);
        Decision d;

        // Check for manual override first
        const QualityMode mode = getQualityMode();
        if (mode != QualityMode::Auto)
        {
            // Manual override: use fixed scale
            switch (mode)
            {
                case QualityMode::Low:
                    d.suggestedScale = 0.5;
                    d.reason = "manual override: Low";
                    break;
                case QualityMode::Medium:
                    d.suggestedScale = 0.75;
                    d.reason = "manual override: Medium";
                    break;
                case QualityMode::High:
                    d.suggestedScale = 1.0;
                    d.reason = "manual override: High";
                    break;
                default:
                    d.suggestedScale = 1.0;
                    d.reason = "manual override: unknown";
                    break;
            }
            currentScale = d.suggestedScale;
            d.profile.resolutionScale = d.suggestedScale;
            d.profile.highDetailEffects = (d.suggestedScale >= 0.9);
            d.profile.particlesEnabled = (d.suggestedScale >= 0.6);
            return d;
        }

        // Auto mode: adaptive quality based on performance
        d.suggestedScale = currentScale; // start from last (for hysteresis)

        const double tfps = targetFps.load();
        const double low = fpsLow.load();
        const double high = fpsHigh.load();
        const double cpuH = cpuHigh.load();
        const double cpuR = cpuRelax.load();

        // Basic strategy with hysteresis:
        // - If FPS is well below (low) or CPU above (cpuHigh), step down scale.
        // - If FPS is above (high) and CPU below (cpuRelax), step up.
        bool stepDown = (fpsEma > 0.0 && fpsEma < juce::jmin(low, 0.85 * tfps)) || (cpuPct >= cpuH);
        bool stepUp   = (fpsEma >= juce::jmax(high, 0.95 * tfps)) && (cpuPct <= cpuR);

        if (stepDown) {
            d.suggestedScale = juce::jmax(0.5, currentScale - 0.1);
            d.reason = "auto: low FPS or high CPU";
        } else if (stepUp) {
            d.suggestedScale = juce::jmin(1.0, currentScale + 0.1);
            d.reason = "auto: good FPS and relaxed CPU";
        } else {
            d.reason = "auto: hold (within hysteresis)";
        }

        currentScale = d.suggestedScale; // keep internal state for next call
        d.profile.resolutionScale = d.suggestedScale;
        d.profile.highDetailEffects = (d.suggestedScale >= 0.9);
        d.profile.particlesEnabled = (d.suggestedScale >= 0.6);
        return d;
    }

    double getCurrentScale() const { return currentScale; }

private:
    std::atomic<double> targetFps{ 60.0 };
    std::atomic<double> fpsLow{ 45.0 };   // scale down if < 45
    std::atomic<double> fpsHigh{ 58.0 };  // scale up if >= 58
    std::atomic<double> cpuHigh{ 80.0 };  // scale down if >= 80%
    std::atomic<double> cpuRelax{ 50.0 }; // allow scale up if <= 50%
    std::atomic<int> manualMode{ 0 };     // QualityMode: 0=Auto, 1=Low, 2=Medium, 3=High

    double currentScale { 1.0 };
};

} // namespace milkdawp
