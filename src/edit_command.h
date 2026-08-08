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

struct EditCommand
{
    static constexpr int supportedVersion = 1;

    int commandVersion = supportedVersion;
    juce::String projectContentSha256;
    juce::String operation { "editNotes" };
    juce::String trackId { "track-1" };
    juce::String clipId { "loop-1" };
    juce::String summary;
    std::optional<std::int64_t> seed;
    std::vector<NoteEditChange> changes;
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
