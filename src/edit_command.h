#pragma once

#include "song_project.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace resonance
{
enum class NoteEditAction
{
    add,
    update,
    remove
};

struct NoteEditChange
{
    NoteEditAction action = NoteEditAction::update;
    juce::String noteId;
    std::optional<SongNote> note;
};

// Project-level operations, added in command version 2. They are applied before any
// note changes in the same command, so a command can lengthen the song and then write
// notes into the space it just created.
enum class ProjectOperationType
{
    setTempo,
    setSongLength,
    setSnap,
    setTitle,
    setTrackMixer,
    addTrack,
    removeTrack
};

struct ProjectOperation
{
    ProjectOperationType type = ProjectOperationType::setTempo;
    juce::String trackId;
    juce::String text;
    double numeric = 0.0;
    int lengthTicks = 0;
    // setTrackMixer changes only the fields the command actually supplied.
    std::optional<double> gainDb;
    std::optional<double> pan;
    std::optional<bool> mute;
    std::optional<bool> solo;
};

struct EditCommand
{
    static constexpr int legacyVersion = 1;
    static constexpr int supportedVersion = 2;
    static constexpr std::size_t maximumNoteChanges = 1024;
    static constexpr std::size_t maximumProjectOperations = 32;

    int commandVersion = supportedVersion;
    juce::String projectContentSha256;
    juce::String operation { "editNotes" };
    juce::String trackId;
    juce::String clipId;
    juce::String summary;
    std::optional<std::int64_t> seed;
    std::vector<NoteEditChange> changes;
    std::vector<ProjectOperation> projectOperations;

    bool hasNoteChanges() const noexcept { return ! changes.empty(); }
};

struct SeededVelocityVariation
{
    std::vector<juce::String> noteIds;
    std::int64_t seed = 18421;
    int maximumDelta = 8;
};

struct NoteEditDiff
{
    NoteEditAction action = NoteEditAction::update;
    juce::String noteId;
    std::optional<SongNote> before;
    std::optional<SongNote> after;
};

class EditCommandPreview
{
public:
    EditCommandPreview() = default;
    EditCommandPreview (EditCommandPreview&&) noexcept = default;
    EditCommandPreview& operator= (EditCommandPreview&&) noexcept = default;
    EditCommandPreview (const EditCommandPreview&) = delete;
    EditCommandPreview& operator= (const EditCommandPreview&) = delete;

    bool isPending() const noexcept { return ! consumed && candidate != nullptr; }
    const SongProject* getCandidateProject() const noexcept { return candidate.get(); }
    juce::Result applyTo (SongProject& activeProject);
    juce::Result reject();

    EditCommand command;
    juce::String beforeContentSha256;
    juce::String afterContentSha256;
    std::vector<NoteEditDiff> noteDiffs;
    // Human-readable record of each applied project operation, and any track the
    // command created, whose id the author could not have known in advance.
    std::vector<juce::String> projectOperationSummaries;
    std::vector<juce::String> createdTrackIds;

private:
    friend juce::Result createEditCommandPreview (const EditCommand&,
                                                   const SongProject&,
                                                   EditCommandPreview&);

    std::unique_ptr<SongProject> candidate;
    bool consumed = false;
};

juce::Result parseEditCommand (const juce::String& json, EditCommand& destination);
juce::String serialiseEditCommand (const EditCommand& command);
juce::Result resolveSeededVelocityVariation (const SongProject& activeProject,
                                             const SeededVelocityVariation& variation,
                                             EditCommand& destination);
juce::Result createEditCommandPreview (const EditCommand& command,
                                       const SongProject& activeProject,
                                       EditCommandPreview& destination);
} // namespace resonance
