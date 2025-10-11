#include <juce_core/juce_core.h>
#include "../src/MessageThreadBridge.h"

using namespace milkdawp;

class MessageThreadBridgeTests : public juce::UnitTest {
public:
    MessageThreadBridgeTests() : juce::UnitTest("MessageThread Phase 1.3", "Core") {}

    void runTest() override {
        beginTest("Audio→Message→Visualization routing and ordering");
        {
            MessageThreadBridge bridge;
            juce::Array<ParameterChange> messageEvents;
            juce::Array<ParameterChange> vizEvents;

            bridge.setMessageListener([&](const ParameterChange& pc){ messageEvents.add(pc); });
            bridge.setVisualizationListener([&](const ParameterChange& pc){ vizEvents.add(pc); });

            // Simulate audio thread posting several changes
            expect(bridge.postFromAudioToMessage("beatSensitivity", 1.25f));
            expect(bridge.postFromAudioToMessage("transitionDuration", 5.0f));
            expect(bridge.postFromAudioToMessage("shuffle", 1.0f));

            // Simulate message thread tick
            bridge.drainOnMessageThread();

            expectEquals(messageEvents.size(), 3);
            expectEquals(vizEvents.size(), 3);
            if (messageEvents.size() == 3)
            {
                expectEquals(messageEvents[0].paramID, juce::String("beatSensitivity"));
                expectEquals(messageEvents[1].paramID, juce::String("transitionDuration"));
                expectEquals(messageEvents[2].paramID, juce::String("shuffle"));

                // Sequence should be monotonically increasing
                expect(messageEvents[0].sequence < messageEvents[1].sequence);
                expect(messageEvents[1].sequence < messageEvents[2].sequence);
            }
        }
    }
};

static MessageThreadBridgeTests messageThreadBridgeTests;
