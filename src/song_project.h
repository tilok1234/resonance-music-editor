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

struct TrackMixerSettings
{
    double gainDecibels = 0.0;
    double pan = 0.0;
    bool muted = false;
    bool solo = false;
};

struct TrackMidiRouting
{
    int inputChannel = 0;
    int outputChannel = 1;
};

class SongProject final : private juce::ValueTree::Listener
{
public:
    static constexpr int legacySchemaVersion = 1;
    static constexpr int previousSchemaVersion = 2;
    // Versions 1 and 2 hold exactly one track; 3 was the first to persist more.
    static constexpr int multiTrackSchemaVersion = 3;
    static constexpr int priorSchemaVersion = 3;
    // Version 4 is the newest archived input contract; version 5 widened the clip
    // ceiling from 8 to 64 bars of 4/4.
    static constexpr int lastArchivedSchemaVersion = 4;
    static constexpr int currentSchemaVersion = 5;
    static constexpr int maxProjectTracks = 4;

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

    int getSchemaVersion() const;
    int getTrackCount() const noexcept;
    int getActiveTrackIndex() const noexcept;
    bool setActiveTrackIndex (int trackIndex);
    juce::String getTrackId() const;
    juce::String getTrackId (int trackIndex) const;
    juce::String getTrackName() const;
    juce::String getTrackName (int trackIndex) const;
    juce::String getClipId() const;
    juce::String getClipId (int trackIndex) const;
    TrackMixerSettings getTrackMixerSettings() const;
    TrackMixerSettings getTrackMixerSettings (int trackIndex) const;
    juce::Result setTrackMixerSettings (const TrackMixerSettings& settings);
    juce::Result setTrackMixerSettingsForTrack (int trackIndex,
                                                const TrackMixerSettings& settings);
    TrackMidiRouting getTrackMidiRouting() const;
    TrackMidiRouting getTrackMidiRouting (int trackIndex) const;
    juce::Result setTrackMidiRouting (const TrackMidiRouting& routing);
    juce::Result setTrackMidiRoutingForTrack (int trackIndex,
                                              const TrackMidiRouting& routing);
    juce::Result duplicateActiveTrack (juce::String* createdTrackId = nullptr);
    juce::Result removeTrack (const juce::String& trackId);
    juce::Result moveTrack (const juce::String& trackId, int newIndex);

    std::vector<SongNote> getNotes() const;
    std::vector<SongNote> getNotes (int trackIndex) const;
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
    void setPluginMetadataForTrack (int trackIndex,
                                    const juce::String& identifier,
                                    const juce::String& name,
                                    const juce::String& vendor,
                                    const juce::String& version);
    juce::String getPluginIdentifier() const;
    juce::String getPluginIdentifier (int trackIndex) const;
    juce::String getPluginName() const;
    juce::String getPluginName (int trackIndex) const;
    juce::String getPluginSoundName() const;
    juce::String getPluginSoundName (int trackIndex) const;
    void setPluginState (const juce::MemoryBlock& state);
    juce::Result applyPluginSound (const juce::String& soundName,
                                   const juce::MemoryBlock& state);
    juce::Result getPluginState (juce::MemoryBlock& state) const;
    juce::Result getPluginStateForTrack (int trackIndex, juce::MemoryBlock& state) const;
    juce::Result getPluginSoundSnapshot (PluginSoundSnapshot& snapshot) const;
    juce::Result getPluginSoundSnapshotForTrack (int trackIndex,
                                                 PluginSoundSnapshot& snapshot) const;
    juce::String getPluginStateSha256() const;
    juce::String getPluginStateSha256 (int trackIndex) const;

    SequenceSnapshot createSequenceSnapshot() const;
    SequenceSnapshot createSequenceSnapshotForTrack (int trackIndex) const;

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

    juce::ValueTree getTrackTree (int trackIndex) const;
    juce::ValueTree findTrackTree (const juce::String& trackId) const;
    juce::ValueTree getActiveTrackTree() const;
    juce::ValueTree getNotesTree() const;
    juce::ValueTree getNotesTree (int trackIndex) const;
    juce::ValueTree getInstrumentTree() const;
    juce::ValueTree getInstrumentTree (int trackIndex) const;
    juce::ValueTree findNoteTree (const juce::String& id) const;
    juce::var toJsonValue() const;
    static juce::Result valueTreeFromJson (const juce::var& json, juce::ValueTree& destination);
    juce::Result writePluginSoundSnapshot (const juce::String& soundName,
                                           const juce::MemoryBlock& state,
                                           juce::UndoManager* undo);
    void installRoot (juce::ValueTree newRoot,
                      bool shouldBeDirty,
                      const juce::String& preferredActiveTrackId = {});
    void ensureActiveTrack();
    void projectChanged();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;

    juce::ValueTree root;
    juce::UndoManager undoManager { 2000, 16 * 1024 * 1024 };
    std::function<void()> changeCallback;
    juce::String activeTrackId;
    bool dirty = false;
    bool suppressChanges = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongProject)
};
} // namespace resonance
