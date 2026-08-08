#pragma once

#include <JuceHeader.h>

#include "edit_command.h"

#include <functional>
#include <optional>

namespace resonance
{
class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll (SongProject& projectToEdit);

    void setPlayheadBeat (double beat);
    void setSelectedNote (const juce::String& id);
    void setEditPreview (const std::vector<NoteEditDiff>& diffs,
                         bool auditioningCandidate);
    void setEditPreviewAudition (bool auditioningCandidate);
    void clearEditPreview();
    const juce::String& getSelectedNote() const noexcept { return selectedNoteId; }
    void setSelectionChangedCallback (std::function<void (const juce::String&)> callback);

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
        resize
    };

    juce::Rectangle<float> gridBounds() const;
    juce::Rectangle<float> boundsForNote (const SongNote& note) const;
    std::optional<SongNote> noteAt (juce::Point<float> point) const;
    double beatAtX (float x) const;
    int noteAtY (float y) const;
    double snapBeat (double beat) const;
    bool isBlackKey (int midiNote) const;
    void select (const juce::String& id);
    void removeSelected();

    SongProject& project;
    std::function<void (const juce::String&)> selectionChanged;
    juce::String selectedNoteId;
    double playheadBeat = 0.0;
    int lowestVisibleNote = 40;
    static constexpr int visibleNoteRows = 29;
    DragMode dragMode = DragMode::none;
    std::optional<SongNote> dragOrigin;
    std::vector<NoteEditDiff> editPreviewDiffs;
    double dragBeatOffset = 0.0;
    int dragPitchOffset = 0;
    bool auditioningEditCandidate = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
} // namespace resonance
