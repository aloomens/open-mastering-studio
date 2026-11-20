#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace lab
{
struct Results
{
    float cleanThd = -300.0f;                                   // worst THD of the clean path (dB)
    std::vector<std::pair<juce::String, float>> aliasing;       // worst single alias product per configuration (dBc)
    float worstTruePeak = -100.0f;                              // realistic material, True Peak on, ceiling -1 dBTP (exact reconstruction)
    float stressTruePeak = -100.0f;                             // flat-to-Nyquist noise stress tests
    int latencyError = 0;                                       // samples between reported and measured latency
    float cpu48 = 0.0f;                                         // whole plugin, % of one core at 48 kHz
};

// Runs all measurements, appends a Markdown report to md.
Results run (juce::String& md, bool verbose);

// Prints how accurately the true-peak meters read sines of known amplitude across the band.
void calibrate();

// Aliasing sweep only, for the configurations whose name contains `filter` (quick check while tuning).
void aliasProbe (const juce::String& filter);
}
