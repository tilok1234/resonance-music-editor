#pragma once

#include <JuceHeader.h>

#include "song_project.h"

#include <vector>

namespace resonance
{
// A named opaque plug-in state kept outside any song project, so sound work
// survives New/Open and can be reused on either track. This is the same
// host-owned snapshot idea ADR-0004 accepted for the sound A/B workflow; it is
// not a factory-preset index and does not interpret vendor .fxp files.
struct SoundShelfEntry
{
    juce::String name;
    juce::String pluginIdentifier;
    juce::String pluginName;
    juce::String vendor;
    juce::String version;
    juce::MemoryBlock state;
    juce::String stateSha256;
};

class SoundShelf
{
public:
    static constexpr int supportedSchemaVersion = 1;
    static constexpr std::size_t maximumEntries = 32;
    static constexpr int maximumNameLength = 80;

    // A missing file is an empty shelf, not an error. A present but invalid file
    // fails closed and leaves the in-memory shelf untouched.
    juce::Result loadFrom (const juce::File& file);
    juce::Result saveTo (const juce::File& file) const;

    const std::vector<SoundShelfEntry>& getEntries() const noexcept { return entries; }
    int getEntryCount() const noexcept { return static_cast<int> (entries.size()); }
    const SoundShelfEntry* find (const juce::String& name) const;

    // Names are unique ignoring case, so the shelf cannot grow two entries a user
    // would read as the same sound.
    juce::Result add (SoundShelfEntry entry);
    juce::Result remove (const juce::String& name);
    void clear();

private:
    std::vector<SoundShelfEntry> entries;
};
} // namespace resonance
