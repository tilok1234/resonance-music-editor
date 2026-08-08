#pragma once

#include <JuceHeader.h>

namespace resonance
{
struct KnownPluginRecord
{
    juce::PluginDescription description;
    juce::File bundlePath;
    juce::String identifier;
    juce::String bundleFingerprint;
    bool hasEditor = false;
    int expectedParameterCount = 0;
    int bundleFileCount = 0;
    juce::int64 bundleBytes = 0;
};

juce::Result loadFirstAcceptedInstrument (const juce::File& inventoryFile,
                                          const juce::File& quarantineFile,
                                          KnownPluginRecord& destination);
} // namespace resonance
