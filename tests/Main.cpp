#include <JuceHeader.h>

int main (int, char**)
{
    juce::UnitTestRunner runner;
    // Run all registered tests; report to stdout
    const bool ok = runner.runAllTests();
    return ok ? 0 : 1;
}
