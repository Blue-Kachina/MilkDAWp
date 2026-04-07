#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main (int, char**)
{
    // JUCE infrastructure (MessageManager, timers, etc.) must be initialised
    // before any JUCE objects that depend on them (e.g. AudioProcessorValueTreeState).
    juce::ScopedJuceInitialiser_NonGUI juceInit;

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
