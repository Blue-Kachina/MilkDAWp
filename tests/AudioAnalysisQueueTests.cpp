#include <juce_core/juce_core.h>
#include "../src/AudioAnalysisQueue.h"

class AudioAnalysisQueueTests : public juce::UnitTest {
public:
    AudioAnalysisQueueTests() : juce::UnitTest("AudioAnalysisQueueTests", "core") {}

    void runTest() override {
        beginTest("SPSC queue push/pop order and capacity");
        {
            milkdawp::AudioAnalysisQueue<4> q;
            milkdawp::AudioAnalysisSnapshot s{};

            // Push 3 items
            for (int i = 0; i < 3; ++i) {
                s.shortTimeEnergy = (float)i;
                s.samplePosition = (uint64_t)i;
                expect(q.tryPush(s));
            }
            expectEquals(q.getNumAvailable(), 3);

            // Pop 2 items
            for (int i = 0; i < 2; ++i) {
                milkdawp::AudioAnalysisSnapshot out;
                expect(q.tryPop(out));
                expectEquals((int)out.shortTimeEnergy, i);
                expectEquals((int)out.samplePosition, i);
            }
            expectEquals(q.getNumAvailable(), 1);

            // Fill to capacity
            for (int i = 3; i < 4; ++i) {
                s.shortTimeEnergy = (float)i;
                s.samplePosition = (uint64_t)i;
                expect(q.tryPush(s));
            }

            // Queue may be full now; pushing one more should fail
            expect(!q.tryPush(s));

            // Pop remaining
            milkdawp::AudioAnalysisSnapshot out;
            int count = 0;
            while (q.tryPop(out)) { ++count; }
            expectEquals(count, 2);
            expectEquals(q.getNumAvailable(), 0);
        }
    }
};

static AudioAnalysisQueueTests audioAnalysisQueueTests;
