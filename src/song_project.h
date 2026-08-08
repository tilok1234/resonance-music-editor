#pragma once

#include <JuceHeader.h>

#include "loop_scheduler.h"

#include <functional>
#include <optional>
#include <vector>

namespace resonance
{
struct SongNote
{
    juce::String id;
    double beat = 0.0;
    double lengthBeats = 0.5;
    int midiNote = 60;
    int velocity = 96;
};

struct PluginSoundSnapshot
{
    juce::String name;
    juce::MemoryBlock state;
    juce::String stateSha256;
};

class SongProject final : private juce::ValueTree::Listener
{
public:
    SongProject();
    ~SongProject() override;

    void resetToStarter();
    void replaceWith (const SongProject& other);

    juce::String getTitle() const;
    void setTitle (const juce::String& title);
    double getTempoBpm() const;
    void setTempoBpm (double bpm);
    double getLoopLengthBeats() const;
    void setLoopLengthBeats (double beats);
    double getSnapBeats() const;
    void setSnapBeats (double beats);
    int getSampleRate() const;
    void setSampleRate (int sampleRate);

    std::vector<SongNote> getNotes() const;
    std::optional<SongNote> findNote (const juce::String& id) const;
    juce::String addNote (double beat, double lengthBeats, int midiNote, int velocity);
    juce::Result insertNote (const SongNote& note);
    bool updateNote (const SongNote& note);
    bool removeNote (const juce::String& id);

    juce::String getContentSha256() const;

    void setPluginMetadata (const juce::String& identifier,
                            const juce::String& name,
                            const juce::String& vendor,
                            const juce::String& version);
    juce::String getPluginIdentifier() const;
    juce::String getPluginName() const;
    juce::String getPluginSoundName() const;
    void setPluginState (const juce::MemoryBlock& state);
    juce::Result applyPluginSound (const juce::String& soundName,
                                   const juce::MemoryBlock& state);
    juce::Result getPluginState (juce::MemoryBlock& state) const;
    juce::Result getPluginSoundSnapshot (PluginSoundSnapshot& snapshot) const;
    juce::String getPluginStateSha256() const;

    SequenceSnapshot createSequenceSnapshot() const;

    void beginUndoTransaction (const juce::String& name);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;

    juce::Result saveToFile (const juce::File& file) const;
    juce::Result loadFromFile (const juce::File& file);

    bool isDirty() const noexcept { return dirty; }
    void markClean() noexcept { dirty = false; }
    void setChangeCallback (std::function<void()> callback) { changeCallback = std::move (callback); }

private:
    static constexpr int projectPpq = 960;

    juce::ValueTree getNotesTree() const;
    juce::ValueTree getInstrumentTree() const;
    juce::ValueTree findNoteTree (const juce::String& id) const;
    juce::var toJsonValue() const;
    static juce::Result valueTreeFromJson (const juce::var& json, juce::ValueTree& destination);
    juce::Result writePluginSoundSnapshot (const juce::String& soundName,
                                           const juce::MemoryBlock& state,
                                           juce::UndoManager* undo);
    void installRoot (juce::ValueTree newRoot, bool shouldBeDirty);
    void projectChanged();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;

    juce::ValueTree root;
    juce::UndoManager undoManager { 2000, 16 * 1024 * 1024 };
    std::function<void()> changeCallback;
    bool dirty = false;
    bool suppressChanges = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongProject)
};
} // namespace resonance
