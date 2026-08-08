#pragma once

#include <JuceHeader.h>

namespace resonance
{
inline juce::String vst3UniqueIdSuffix (int uniqueId)
{
    const auto unsignedId = static_cast<juce::uint32> (uniqueId);
    return "-" + juce::String::toHexString (static_cast<juce::int64> (unsignedId)).paddedLeft ('0', 8);
}

inline bool vst3IdentifiersAreCompatible (const juce::String& savedIdentifier,
                                          const juce::String& currentIdentifier,
                                          int currentUniqueId)
{
    if (savedIdentifier == currentIdentifier)
        return true;

    const auto uidSuffix = vst3UniqueIdSuffix (currentUniqueId);
    return savedIdentifier.startsWithIgnoreCase ("VST3-")
        && currentIdentifier.startsWithIgnoreCase ("VST3-")
        && savedIdentifier.endsWithIgnoreCase (uidSuffix)
        && currentIdentifier.endsWithIgnoreCase (uidSuffix);
}
} // namespace resonance
