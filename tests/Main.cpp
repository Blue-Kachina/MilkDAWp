#include <juce_core/juce_core.h>

int main (int, char**)
{
    juce::UnitTestRunner runner;
    // Run all registered tests; report to stdout
    runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        if (auto* r = runner.getResult(i))
            totalFailures += r->failures;
    }
    return totalFailures == 0 ? 0 : 1;
}
