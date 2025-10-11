#include <juce_core/juce_core.h>
#include "../src/AudioAnalysisQueue.h"
#include "../src/VisualizationThread.h"

using namespace milkdawp;

class VisualizationThreadTest : public juce::UnitTest {
public:
    VisualizationThreadTest() : juce::UnitTest("VisualizationThread Phase 1.2", "Core") {}

    void runTest() override {
        beginTest("Consumes snapshots independently of producer");
        AudioAnalysisQueue<64> q;
        VisualizationThread viz(q);
        viz.start();

        // Produce a handful of snapshots quickly
        AudioAnalysisSnapshot s{};
        s.shortTimeEnergy = 1.0f;
        bool anyPushed = false;
        for (int i = 0; i < 20; ++i) {
            s.samplePosition = (uint64_t)i * AudioAnalysisSnapshot::fftSize;
            anyPushed |= q.tryPush(s);
        }
        expect(anyPushed, "Producer should push at least one snapshot");

        // Give the viz thread time to consume
        juce::Thread::sleep(50);
        const auto consumed1 = viz.getFramesConsumed();
        expectGreaterThan((int)consumed1, 0, "Viz should have consumed frames");

        // Push more, verify it keeps consuming over time
        for (int i = 20; i < 40; ++i) {
            s.samplePosition = (uint64_t)i * AudioAnalysisSnapshot::fftSize;
            q.tryPush(s);
        }
        juce::Thread::sleep(50);
        const auto consumed2 = viz.getFramesConsumed();
        expect(consumed2 > consumed1, "Viz should continue consuming frames over time");

        viz.stop();
    }
};

static VisualizationThreadTest vizThreadTestInstance;
