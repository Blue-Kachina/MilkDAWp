#include <juce_core/juce_core.h>
#include "../src/Version.h"
#include "../src/Logging.h"

class BasicSanityTests : public juce::UnitTest {
public:
    BasicSanityTests() : juce::UnitTest("BasicSanityTests", "core") {}

    void runTest() override {
        beginTest("Version string is defined and not empty");
        {
            expect(juce::String(MILKDAWP_VERSION_STRING).isNotEmpty());
        }

        beginTest("Logging can initialize without throwing");
        {
            // Should be safe to call multiple times, init() is guarded.
            milkdawp::Logging::init("MilkDAWp", MILKDAWP_VERSION_STRING);
            expect(true);
        }
    }
};

static BasicSanityTests basicSanityTests;