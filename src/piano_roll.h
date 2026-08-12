#pragma once

#include <JuceHeader.h>

#include "edit_command.h"

#include <functional>
#include <optional>
#include <vector>

namespace resonance
{
class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll (SongProject& projectToEdit);

    void setPlayheadBeat (double beat);
    void frameAllTracks();
    void setSelectedNote (const juce::String& id);
    // Ids that name no live note are dropped; the last surviving id becomes primary.
    void setSelectedNotes (std::vector<juce::String> ids);
    void setEditPreview (const std::vector<NoteEditDiff>& diffs,
                         bool auditioningCandidate);
    void setEditPreviewAudition (bool auditioningCandidate);
    void clearEditPreview();
    // The primary selection is the most recently added note. It drives the single-note
    // controls that predate multiple selection; getSelectedNotes is the full set.
    const juce::String& getSelectedNote() const noexcept { return primarySelectedNoteId; }
    const std::vector<juce::String>& getSelectedNotes() const noexcept { return selectedNoteIds; }
    void setSelectionChangedCallback (std::function<void (const juce::String&)> callback);
    void setStatusMessageCallback (std::function<void (const juce::String&)> callback);
    // Drops ids whose notes no longer exist, for example after Undo or a track change.
    void pruneSelection();

    void copySelection();
    juce::Result pasteAtInsertBeat();
    juce::Result duplicateSelection();
    void nudgeSelection (double beatDelta);
    void transposeSelection (int semitones);
    bool hasClipboardContent() const noexcept { return ! clipboard.empty(); }

    void paint (juce::Graphics& graphics) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    enum class DragMode
    {
        none,
        move,
        resize,
        marquee
    };

    void adjustVerticalZoom (int rowDelta);
    juce::Rectangle<float> gridBounds() const;
    juce::Rectangle<float> boundsForNote (const SongNote& note) const;
    std::optional<SongNote> noteAt (juce::Point<float> point) const;
    double beatAtX (float x) const;
    int noteAtY (float y) const;
    double snapBeat (double beat) const;
    bool isBlackKey (int midiNote) const;
    bool isSelected (const juce::String& id) const;
    void select (const juce::String& id);
    void setSelection (std::vector<juce::String> ids);
    void toggleSelection (const juce::String& id);
    void selectAll();
    void notifySelectionChanged();
    void removeSelected();
    void beginMoveDrag (const SongNote& anchor, const juce::MouseEvent& event);
    juce::Result insertCopies (const std::vector<SongNote>& source,
                               double targetBeat,
                               const juce::String& transactionName);
    void reportStatus (const juce::String& message) const;

    SongProject& project;
    std::function<void (const juce::String&)> selectionChanged;
    std::function<void (const juce::String&)> statusChanged;
    // Session-only note clipboard, normalised so the earliest note starts at beat 0.
    std::vector<SongNote> clipboard;
    // Where Paste places the clipboard: the last grid position pressed.
    double insertBeat = 0.0;
    // Ordered; the back entry is the primary selection.
    std::vector<juce::String> selectedNoteIds;
    juce::String primarySelectedNoteId;
    double playheadBeat = 0.0;
    int lowestVisibleNote = 40;
    // Session-only view state; deliberately not persisted in the song project.
    int visibleNoteRows = 29;
    static constexpr int minimumVisibleRows = 12;
    static constexpr int maximumVisibleRows = 72;
    DragMode dragMode = DragMode::none;
    std::optional<SongNote> dragOrigin;
    // Pre-drag state of every selected note, so a move applies to the whole selection.
    std::vector<SongNote> dragOrigins;
    std::vector<NoteEditDiff> editPreviewDiffs;
    double dragBeatOffset = 0.0;
    int dragPitchOffset = 0;
    juce::Point<float> marqueeAnchor;
    juce::Rectangle<float> marqueeBounds;
    std::vector<juce::String> marqueeBaseSelection;
    // An empty-space press becomes a note only if it never became a drag.
    bool pendingAddOnRelease = false;
    bool auditioningEditCandidate = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
} // namespace resonance
