#include "piano_roll.h"

#include <algorithm>
#include <cmath>

namespace resonance
{
namespace
{
const auto background = juce::Colour::fromRGB (11, 18, 28);
const auto lane = juce::Colour::fromRGB (16, 25, 38);
const auto laneDark = juce::Colour::fromRGB (12, 20, 31);
const auto gridLine = juce::Colour::fromRGB (46, 62, 82);
const auto primary = juce::Colour::fromRGB (93, 214, 196);
const auto secondary = juce::Colour::fromRGB (123, 151, 255);
const auto textMuted = juce::Colour::fromRGB (137, 153, 173);
const auto warning = juce::Colour::fromRGB (247, 184, 88);
const auto danger = juce::Colour::fromRGB (245, 103, 119);
const auto ghost = juce::Colour::fromRGB (108, 126, 150);

juce::Font rollFont (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions ("Segoe UI", height, style));
}
} // namespace

PianoRoll::PianoRoll (SongProject& projectToEdit)
    : project (projectToEdit)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setTitle ("Editable piano roll");
    setDescription ("Click empty space to add a note, or drag across empty space to select a group. Shift-click or Ctrl-click a note to extend the selection, and press Ctrl+A to select every note. Drag notes to move the whole selection, drag the right edge of a single note to resize, and press Delete to remove the selection. Use Ctrl+C to copy, Ctrl+V to paste at the marker set by your last click, and Ctrl+D to duplicate the selection one span later. Arrow keys nudge by the snap value and transpose by a semitone, or by an octave with Shift. Scroll to move through pitch, hold Shift while scrolling to move through time, hold Ctrl to zoom vertically or Ctrl and Shift to zoom horizontally, and press Home or End to jump to the start or end of the song. Notes on the other track are shown dimmed and cannot be edited here.");
}

void PianoRoll::frameAllTracks()
{
    auto lowest = 127;
    auto highest = 0;
    auto foundNote = false;

    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
    {
        for (const auto& note : project.getNotes (trackIndex))
        {
            lowest = juce::jmin (lowest, note.midiNote);
            highest = juce::jmax (highest, note.midiNote);
            foundNote = true;
        }
    }

    // An empty project keeps whatever the user was looking at.
    if (! foundNote)
        return;

    // Two extra rows above and below keep the outermost notes off the frame edge.
    visibleNoteRows = juce::jlimit (minimumVisibleRows,
                                    maximumVisibleRows,
                                    highest - lowest + 5);
    const auto centrePitch = (lowest + highest) / 2;
    lowestVisibleNote = juce::jlimit (0, 128 - visibleNoteRows, centrePitch - visibleNoteRows / 2);
    visibleBeats = 0.0;
    firstVisibleBeat = 0.0;
    repaint();
}

void PianoRoll::adjustVerticalZoom (int rowDelta)
{
    const auto previousRows = visibleNoteRows;
    const auto requested = juce::jlimit (minimumVisibleRows,
                                         maximumVisibleRows,
                                         previousRows + rowDelta);
    if (requested == previousRows)
        return;

    // Hold the centre pitch steady so zooming does not scroll the material off screen.
    const auto centrePitch = lowestVisibleNote + previousRows / 2;
    visibleNoteRows = requested;
    lowestVisibleNote = juce::jlimit (0, 128 - visibleNoteRows, centrePitch - visibleNoteRows / 2);
    repaint();
}

void PianoRoll::setPlayheadBeat (double beat)
{
    playheadBeat = juce::jlimit (0.0, project.getLoopLengthBeats(), beat);
    repaint();
}

void PianoRoll::setSelectedNote (const juce::String& id)
{
    if (id.isNotEmpty() && ! project.findNote (id).has_value())
        select ({});
    else
        select (id);
}

void PianoRoll::setSelectedNotes (std::vector<juce::String> ids)
{
    std::vector<juce::String> live;
    live.reserve (ids.size());
    for (const auto& id : ids)
        if (project.findNote (id).has_value()
            && std::find (live.begin(), live.end(), id) == live.end())
            live.push_back (id);

    setSelection (std::move (live));
}

bool PianoRoll::isSelected (const juce::String& id) const
{
    return std::find (selectedNoteIds.begin(), selectedNoteIds.end(), id) != selectedNoteIds.end();
}

void PianoRoll::setSelection (std::vector<juce::String> ids)
{
    if (ids == selectedNoteIds)
        return;

    selectedNoteIds = std::move (ids);
    primarySelectedNoteId = selectedNoteIds.empty() ? juce::String {} : selectedNoteIds.back();
    repaint();
    notifySelectionChanged();
}

void PianoRoll::select (const juce::String& id)
{
    if (id.isEmpty())
        setSelection ({});
    else
        setSelection ({ id });
}

void PianoRoll::toggleSelection (const juce::String& id)
{
    if (id.isEmpty())
        return;

    auto updated = selectedNoteIds;
    const auto existing = std::find (updated.begin(), updated.end(), id);
    if (existing != updated.end())
        updated.erase (existing);
    else
        updated.push_back (id);

    setSelection (std::move (updated));
}

void PianoRoll::selectAll()
{
    std::vector<juce::String> ids;
    for (const auto& note : project.getNotes())
        ids.push_back (note.id);

    setSelection (std::move (ids));
}

void PianoRoll::pruneSelection()
{
    std::vector<juce::String> surviving;
    surviving.reserve (selectedNoteIds.size());
    for (const auto& id : selectedNoteIds)
        if (project.findNote (id).has_value())
            surviving.push_back (id);

    setSelection (std::move (surviving));
}

void PianoRoll::notifySelectionChanged()
{
    if (selectionChanged)
        selectionChanged (primarySelectedNoteId);
}

void PianoRoll::setEditPreview (const std::vector<NoteEditDiff>& diffs,
                                bool auditioningCandidate)
{
    editPreviewDiffs = diffs;
    auditioningEditCandidate = auditioningCandidate;
    repaint();
}

void PianoRoll::setEditPreviewAudition (bool auditioningCandidate)
{
    auditioningEditCandidate = auditioningCandidate;
    repaint();
}

void PianoRoll::clearEditPreview()
{
    editPreviewDiffs.clear();
    auditioningEditCandidate = false;
    repaint();
}

void PianoRoll::setSelectionChangedCallback (std::function<void (const juce::String&)> callback)
{
    selectionChanged = std::move (callback);
}

juce::Rectangle<float> PianoRoll::gridBounds() const
{
    auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    bounds.removeFromLeft (48.0f);
    bounds.removeFromTop (22.0f);
    return bounds;
}

double PianoRoll::visibleBeatSpan() const
{
    const auto loop = juce::jmax (1.0, project.getLoopLengthBeats());
    return visibleBeats > 0.0 ? juce::jmin (visibleBeats, loop) : loop;
}

juce::Rectangle<float> PianoRoll::boundsForNote (const SongNote& note) const
{
    const auto grid = gridBounds();
    const auto span = visibleBeatSpan();
    const auto topNote = lowestVisibleNote + visibleNoteRows - 1;
    if (note.midiNote < lowestVisibleNote || note.midiNote > topNote)
        return {};

    // Cull anything entirely outside the visible beat window so a long song does not
    // pay for notes it cannot show.
    if (note.beat + note.lengthBeats < firstVisibleBeat || note.beat > firstVisibleBeat + span)
        return {};

    const auto rowHeight = grid.getHeight() / static_cast<float> (visibleNoteRows);
    const auto x = grid.getX()
                   + grid.getWidth() * static_cast<float> ((note.beat - firstVisibleBeat) / span);
    const auto width = grid.getWidth() * static_cast<float> (note.lengthBeats / span);
    const auto row = topNote - note.midiNote;
    const auto y = grid.getY() + rowHeight * static_cast<float> (row);
    return { x + 1.0f, y + 1.0f, juce::jmax (5.0f, width - 2.0f), juce::jmax (3.0f, rowHeight - 2.0f) };
}

std::optional<SongNote> PianoRoll::noteAt (juce::Point<float> point) const
{
    const auto notes = project.getNotes();
    for (auto iterator = notes.rbegin(); iterator != notes.rend(); ++iterator)
        if (boundsForNote (*iterator).expanded (1.5f, 1.0f).contains (point))
            return *iterator;

    return std::nullopt;
}

double PianoRoll::beatAtX (float x) const
{
    const auto grid = gridBounds();
    const auto span = visibleBeatSpan();
    return juce::jlimit (0.0,
                         project.getLoopLengthBeats(),
                         firstVisibleBeat
                             + static_cast<double> ((x - grid.getX()) / grid.getWidth()) * span);
}

void PianoRoll::clampHorizontalView()
{
    const auto loop = juce::jmax (1.0, project.getLoopLengthBeats());
    if (visibleBeats > 0.0)
        visibleBeats = juce::jlimit (minimumVisibleBeats, loop, visibleBeats);
    firstVisibleBeat = juce::jlimit (0.0, juce::jmax (0.0, loop - visibleBeatSpan()), firstVisibleBeat);
}

void PianoRoll::adjustHorizontalZoom (double factor)
{
    const auto loop = juce::jmax (1.0, project.getLoopLengthBeats());
    const auto previousSpan = visibleBeatSpan();
    const auto requested = juce::jlimit (minimumVisibleBeats, loop, previousSpan * factor);
    if (std::abs (requested - previousSpan) < 1.0e-9)
        return;

    // Hold the centre beat steady, matching how vertical zoom anchors on centre pitch.
    const auto centreBeat = firstVisibleBeat + previousSpan * 0.5;
    visibleBeats = requested;
    firstVisibleBeat = centreBeat - requested * 0.5;
    clampHorizontalView();
    repaint();
}

void PianoRoll::scrollHorizontally (double beatDelta)
{
    firstVisibleBeat += beatDelta;
    clampHorizontalView();
    repaint();
}

int PianoRoll::noteAtY (float y) const
{
    const auto grid = gridBounds();
    const auto rowHeight = grid.getHeight() / static_cast<float> (visibleNoteRows);
    const auto row = juce::jlimit (0,
                                   visibleNoteRows - 1,
                                   static_cast<int> ((y - grid.getY()) / rowHeight));
    return lowestVisibleNote + visibleNoteRows - 1 - row;
}

double PianoRoll::snapBeat (double beat) const
{
    const auto snap = project.getSnapBeats();
    return std::round (beat / snap) * snap;
}

bool PianoRoll::isBlackKey (int midiNote) const
{
    const auto pitchClass = midiNote % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

void PianoRoll::paint (juce::Graphics& graphics)
{
    auto outer = getLocalBounds().toFloat().reduced (4.0f);
    graphics.setColour (background);
    graphics.fillRoundedRectangle (outer, 9.0f);

    const auto grid = gridBounds();
    const auto loop = project.getLoopLengthBeats();
    const auto rowHeight = grid.getHeight() / static_cast<float> (visibleNoteRows);
    const auto topNote = lowestVisibleNote + visibleNoteRows - 1;

    graphics.setFont (rollFont (10.0f, juce::Font::bold));
    for (int row = 0; row < visibleNoteRows; ++row)
    {
        const auto midiNote = topNote - row;
        const auto y = grid.getY() + rowHeight * static_cast<float> (row);
        graphics.setColour (isBlackKey (midiNote) ? laneDark : lane);
        graphics.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);
        graphics.setColour (gridLine.withAlpha (0.20f));
        graphics.drawHorizontalLine (juce::roundToInt (y), grid.getX(), grid.getRight());

        // Every octave is always labelled; zoomed-in rows are tall enough to name
        // each white key without the labels colliding.
        const auto labelWhiteKeys = rowHeight >= 14.0f && ! isBlackKey (midiNote);
        if (midiNote % 12 == 0 || labelWhiteKeys)
        {
            graphics.setColour (textMuted.withAlpha (midiNote % 12 == 0 ? 1.0f : 0.55f));
            graphics.drawText (juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3),
                               juce::Rectangle<float> (5.0f, y, 40.0f, rowHeight).toNearestInt(),
                               juce::Justification::centredRight);
        }
    }

    // Grid density follows the zoom: sub-beat lines only appear once they are far
    // enough apart to read, and bar labels thin out on a long song.
    const auto snap = project.getSnapBeats();
    const auto span = visibleBeatSpan();
    const auto pixelsPerBeat = grid.getWidth() / static_cast<float> (span);
    const auto gridStep = pixelsPerBeat * static_cast<float> (snap) >= 6.0f ? snap
                          : pixelsPerBeat >= 6.0f                          ? 1.0
                                                                           : 4.0;
    const auto pixelsPerBar = pixelsPerBeat * 4.0f;
    const auto barLabelStride = pixelsPerBar >= 54.0f ? 1
                                : pixelsPerBar >= 27.0f ? 2
                                : pixelsPerBar >= 14.0f ? 4
                                                        : 8;

    const auto firstStep = static_cast<int> (std::floor (firstVisibleBeat / gridStep));
    const auto lastStep = static_cast<int> (std::ceil ((firstVisibleBeat + span) / gridStep));
    for (int step = firstStep; step <= lastStep; ++step)
    {
        const auto beat = static_cast<double> (step) * gridStep;
        if (beat < -1.0e-9 || beat > loop + 1.0e-9)
            continue;

        const auto x = grid.getX()
                       + grid.getWidth() * static_cast<float> ((beat - firstVisibleBeat) / span);
        const auto roundedBeat = juce::roundToInt (beat);
        const auto onBeat = std::abs (beat - roundedBeat) < 1.0e-8;
        const auto onBar = onBeat && roundedBeat % 4 == 0;
        graphics.setColour ((onBar ? primary : gridLine).withAlpha (onBar ? 0.60f : onBeat ? 0.42f : 0.20f));
        graphics.drawVerticalLine (juce::roundToInt (x), grid.getY(), grid.getBottom());

        if (onBar && beat < loop)
        {
            const auto barNumber = roundedBeat / 4 + 1;
            if ((barNumber - 1) % barLabelStride == 0)
            {
                graphics.setColour (textMuted);
                graphics.drawText ("BAR " + juce::String (barNumber),
                                   juce::Rectangle<float> (x + 5.0f, 5.0f, 62.0f, 15.0f).toNearestInt(),
                                   juce::Justification::centredLeft);
            }
        }
    }

    // Inactive-track notes are drawn as dim ghosts so two parts can be written against
    // each other. They are painted before the active notes and are never hit tested,
    // so selection, dragging, and deletion still apply only to the selected track.
    const auto activeTrackIndex = project.getActiveTrackIndex();
    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
    {
        if (trackIndex == activeTrackIndex)
            continue;

        for (const auto& note : project.getNotes (trackIndex))
        {
            const auto bounds = boundsForNote (note);
            if (bounds.isEmpty())
                continue;

            graphics.setColour (ghost.withAlpha (0.26f));
            graphics.fillRoundedRectangle (bounds, 3.0f);
            graphics.setColour (ghost.withAlpha (0.50f));
            graphics.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
        }
    }

    for (const auto& note : project.getNotes())
    {
        const auto bounds = boundsForNote (note);
        if (bounds.isEmpty())
            continue;

        const auto velocityAlpha = juce::jmap (static_cast<float> (note.velocity), 1.0f, 127.0f, 0.45f, 1.0f);
        juce::ColourGradient gradient (primary.withAlpha (velocityAlpha), bounds.getTopLeft(),
                                       secondary.withAlpha (velocityAlpha), bounds.getBottomRight(), false);
        graphics.setGradientFill (gradient);
        graphics.fillRoundedRectangle (bounds, 3.0f);

        if (isSelected (note.id))
        {
            const auto isPrimary = note.id == primarySelectedNoteId;
            graphics.setColour (juce::Colours::white.withAlpha (isPrimary ? 0.96f : 0.66f));
            graphics.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, isPrimary ? 1.5f : 1.1f);
            // Only the primary selection shows the resize grip, because resizing is a
            // single-note gesture.
            if (isPrimary && selectedNoteIds.size() <= 1)
                graphics.fillRect (bounds.getRight() - 4.0f, bounds.getY() + 1.0f, 2.0f, bounds.getHeight() - 2.0f);
        }
    }

    // The paste anchor is only meaningful once something has been copied, so it stays
    // hidden until then rather than adding a permanent second vertical line.
    if (! clipboard.empty())
    {
        const auto anchorX = grid.getX()
                             + grid.getWidth()
                                   * static_cast<float> ((insertBeat - firstVisibleBeat)
                                                         / visibleBeatSpan());
        graphics.setColour (warning.withAlpha (0.66f));
        for (auto y = grid.getY(); y < grid.getBottom(); y += 6.0f)
            graphics.fillRect (anchorX - 0.5f, y, 1.0f, 3.0f);
        graphics.fillEllipse (anchorX - 3.0f, grid.getY() - 3.0f, 6.0f, 6.0f);
    }

    if (dragMode == DragMode::marquee && ! marqueeBounds.isEmpty())
    {
        graphics.setColour (secondary.withAlpha (0.16f));
        graphics.fillRect (marqueeBounds);
        graphics.setColour (secondary.withAlpha (0.72f));
        graphics.drawRect (marqueeBounds, 1.0f);
    }

    for (const auto& diff : editPreviewDiffs)
    {
        if (diff.before.has_value())
        {
            const auto beforeBounds = boundsForNote (*diff.before);
            if (! beforeBounds.isEmpty())
            {
                const auto beforeColour = diff.action == NoteEditAction::remove ? danger : warning;
                graphics.setColour (beforeColour.withAlpha (auditioningEditCandidate ? 0.42f : 0.92f));
                graphics.drawRoundedRectangle (beforeBounds.reduced (1.0f), 3.0f, 2.0f);

                if (diff.action == NoteEditAction::remove)
                {
                    graphics.drawLine (beforeBounds.getX() + 4.0f,
                                       beforeBounds.getCentreY(),
                                       beforeBounds.getRight() - 4.0f,
                                       beforeBounds.getCentreY(),
                                       2.0f);
                }
            }
        }

        if (diff.after.has_value())
        {
            const auto afterBounds = boundsForNote (*diff.after);
            if (! afterBounds.isEmpty())
            {
                const auto afterAlpha = auditioningEditCandidate ? 0.92f : 0.42f;
                graphics.setColour (secondary.withAlpha (afterAlpha));
                graphics.fillRoundedRectangle (afterBounds.reduced (1.0f), 3.0f);
                graphics.setColour (juce::Colours::white.withAlpha (auditioningEditCandidate ? 0.88f : 0.42f));
                graphics.drawRoundedRectangle (afterBounds.reduced (1.0f), 3.0f, 1.4f);
            }
        }
    }

    const auto playheadX = grid.getX()
                           + grid.getWidth()
                                 * static_cast<float> ((playheadBeat - firstVisibleBeat)
                                                       / visibleBeatSpan());
    graphics.setColour (juce::Colours::white.withAlpha (0.88f));
    graphics.fillRect (playheadX - 1.0f, grid.getY(), 2.0f, grid.getHeight());
    graphics.setColour (primary);
    graphics.fillEllipse (playheadX - 4.0f, grid.getY() - 4.0f, 8.0f, 8.0f);

    graphics.setColour (gridLine.withAlpha (0.8f));
    graphics.drawRoundedRectangle (outer, 9.0f, 1.0f);
}

void PianoRoll::beginMoveDrag (const SongNote& anchor, const juce::MouseEvent& event)
{
    dragOrigin = anchor;
    dragOrigins.clear();
    for (const auto& id : selectedNoteIds)
        if (const auto note = project.findNote (id))
            dragOrigins.push_back (*note);

    dragBeatOffset = beatAtX (event.position.x) - anchor.beat;
    dragPitchOffset = anchor.midiNote - noteAtY (event.position.y);
}

void PianoRoll::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto hit = noteAt (event.position);
    const auto extending = event.mods.isShiftDown() || event.mods.isCtrlDown();

    // Any press inside the grid moves the paste anchor, so Paste lands where the user
    // last worked rather than at a hidden position.
    if (gridBounds().contains (event.position))
    {
        const auto snap = project.getSnapBeats();
        insertBeat = juce::jlimit (0.0,
                                   juce::jmax (0.0, project.getLoopLengthBeats() - snap),
                                   std::floor (beatAtX (event.position.x) / snap) * snap);
    }

    if (hit.has_value())
    {
        if (event.mods.isPopupMenu())
        {
            if (! isSelected (hit->id))
                select (hit->id);
            project.beginUndoTransaction (selectedNoteIds.size() > 1 ? "Delete notes" : "Delete note");
            removeSelected();
            return;
        }

        if (extending)
        {
            toggleSelection (hit->id);
            if (! isSelected (hit->id))
                return;
        }
        else if (! isSelected (hit->id))
        {
            select (hit->id);
        }

        const auto noteBounds = boundsForNote (*hit);
        // Resizing stays a single-note gesture; moving carries the whole selection.
        if (event.position.x >= noteBounds.getRight() - 7.0f && selectedNoteIds.size() <= 1)
        {
            dragMode = DragMode::resize;
            dragOrigin = hit;
            dragBeatOffset = beatAtX (event.position.x) - hit->beat;
            dragPitchOffset = hit->midiNote - noteAtY (event.position.y);
            project.beginUndoTransaction ("Resize note");
            return;
        }

        dragMode = DragMode::move;
        beginMoveDrag (*hit, event);
        project.beginUndoTransaction (dragOrigins.size() > 1 ? "Move notes" : "Move note");
        return;
    }

    if (! gridBounds().contains (event.position) || event.mods.isPopupMenu())
    {
        select ({});
        return;
    }

    // An empty-space press starts a marquee. It only becomes an added note if the
    // press is released without ever turning into a drag, which preserves the
    // documented click-to-add behavior.
    dragMode = DragMode::marquee;
    marqueeAnchor = event.position;
    marqueeBounds = { marqueeAnchor, marqueeAnchor };
    marqueeBaseSelection = extending ? selectedNoteIds : std::vector<juce::String> {};
    pendingAddOnRelease = ! extending;
    if (! extending)
        select ({});
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& event)
{
    if (dragMode == DragMode::marquee)
    {
        if (event.getDistanceFromDragStart() > 2)
            pendingAddOnRelease = false;

        marqueeBounds = juce::Rectangle<float>::leftTopRightBottom (
            juce::jmin (marqueeAnchor.x, event.position.x),
            juce::jmin (marqueeAnchor.y, event.position.y),
            juce::jmax (marqueeAnchor.x, event.position.x),
            juce::jmax (marqueeAnchor.y, event.position.y));

        auto updated = marqueeBaseSelection;
        for (const auto& note : project.getNotes())
        {
            const auto bounds = boundsForNote (note);
            if (bounds.isEmpty() || ! marqueeBounds.intersects (bounds))
                continue;
            if (std::find (updated.begin(), updated.end(), note.id) == updated.end())
                updated.push_back (note.id);
        }

        setSelection (std::move (updated));
        repaint();
        return;
    }

    if (! dragOrigin.has_value() || dragMode == DragMode::none)
        return;

    const auto loop = project.getLoopLengthBeats();
    const auto snap = project.getSnapBeats();

    if (dragMode == DragMode::resize)
    {
        auto edited = *dragOrigin;
        const auto snappedEnd = snapBeat (beatAtX (event.position.x));
        edited.lengthBeats = juce::jlimit (snap, loop - edited.beat, snappedEnd - edited.beat);
        project.updateNote (edited);
        repaint();
        return;
    }

    // Resolve the anchor's requested position, then shift the whole selection by the
    // same delta so relative rhythm and intervals survive the drag. Clamping is applied
    // to the delta rather than per note, so no note collapses onto the loop edge.
    const auto requestedBeat = snapBeat (beatAtX (event.position.x) - dragBeatOffset);
    const auto requestedPitch = noteAtY (event.position.y) + dragPitchOffset;
    auto beatDelta = requestedBeat - dragOrigin->beat;
    auto pitchDelta = requestedPitch - dragOrigin->midiNote;

    for (const auto& origin : dragOrigins)
    {
        beatDelta = juce::jlimit (-origin.beat,
                                  loop - origin.lengthBeats - origin.beat,
                                  beatDelta);
        pitchDelta = juce::jlimit (-origin.midiNote, 127 - origin.midiNote, pitchDelta);
    }

    for (const auto& origin : dragOrigins)
    {
        auto edited = origin;
        edited.beat = origin.beat + beatDelta;
        edited.midiNote = origin.midiNote + pitchDelta;
        project.updateNote (edited);
    }

    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent& event)
{
    if (dragMode == DragMode::marquee)
    {
        if (pendingAddOnRelease && gridBounds().contains (event.position))
        {
            project.beginUndoTransaction ("Add note");
            const auto snap = project.getSnapBeats();
            const auto beat = juce::jlimit (0.0,
                                            project.getLoopLengthBeats() - snap,
                                            std::floor (beatAtX (marqueeAnchor.x) / snap) * snap);
            const auto id = project.addNote (beat,
                                             juce::jmax (0.5, snap),
                                             noteAtY (marqueeAnchor.y),
                                             96);
            select (id);
        }

        marqueeBounds = {};
        marqueeBaseSelection.clear();
        pendingAddOnRelease = false;
    }

    dragMode = DragMode::none;
    dragOrigin.reset();
    dragOrigins.clear();
    repaint();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto direction = wheel.deltaY > 0.0f ? 2 : wheel.deltaY < 0.0f ? -2 : 0;
    if (direction == 0)
        return;

    if (event.mods.isCommandDown() && event.mods.isShiftDown())
    {
        adjustHorizontalZoom (direction > 0 ? 0.8 : 1.25);
        return;
    }

    if (event.mods.isCommandDown())
    {
        // Scrolling up shows fewer, taller rows.
        adjustVerticalZoom (-direction * 2);
        return;
    }

    if (event.mods.isShiftDown())
    {
        scrollHorizontally (-direction * visibleBeatSpan() * 0.12);
        return;
    }

    lowestVisibleNote = juce::jlimit (0, 128 - visibleNoteRows, lowestVisibleNote + direction);
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        project.beginUndoTransaction (selectedNoteIds.size() > 1 ? "Delete notes" : "Delete note");
        removeSelected();
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        selectAll();
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'C')
    {
        copySelection();
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'V')
    {
        pasteAtInsertBeat();
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D')
    {
        duplicateSelection();
        return true;
    }

    if (key == juce::KeyPress::homeKey)
    {
        firstVisibleBeat = 0.0;
        clampHorizontalView();
        repaint();
        return true;
    }

    if (key == juce::KeyPress::endKey)
    {
        firstVisibleBeat = project.getLoopLengthBeats();
        clampHorizontalView();
        repaint();
        return true;
    }

    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        const auto direction = key == juce::KeyPress::rightKey ? 1.0 : -1.0;
        nudgeSelection (direction * project.getSnapBeats());
        return true;
    }

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        const auto direction = key == juce::KeyPress::upKey ? 1 : -1;
        transposeSelection (direction * (key.getModifiers().isShiftDown() ? 12 : 1));
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        select ({});
        return true;
    }

    if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
    {
        adjustVerticalZoom (-4);
        return true;
    }

    if (key.getTextCharacter() == '-' || key.getTextCharacter() == '_')
    {
        adjustVerticalZoom (4);
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            project.redo();
        else
            project.undo();
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        project.redo();
        return true;
    }

    return false;
}

void PianoRoll::setStatusMessageCallback (std::function<void (const juce::String&)> callback)
{
    statusChanged = std::move (callback);
}

void PianoRoll::reportStatus (const juce::String& message) const
{
    if (statusChanged)
        statusChanged (message);
}

void PianoRoll::copySelection()
{
    std::vector<SongNote> copied;
    for (const auto& id : selectedNoteIds)
        if (const auto note = project.findNote (id))
            copied.push_back (*note);

    if (copied.empty())
    {
        reportStatus ("SELECT NOTES BEFORE COPYING");
        return;
    }

    std::sort (copied.begin(),
               copied.end(),
               [] (const SongNote& first, const SongNote& second)
               {
                   return first.beat < second.beat;
               });

    // Normalise so the earliest note sits at beat 0; paste then only adds an offset.
    const auto anchor = copied.front().beat;
    for (auto& note : copied)
        note.beat -= anchor;

    clipboard = std::move (copied);
    reportStatus ("COPIED " + juce::String (static_cast<int> (clipboard.size()))
                  + (clipboard.size() == 1 ? " NOTE" : " NOTES"));
}

juce::Result PianoRoll::insertCopies (const std::vector<SongNote>& source,
                                      double targetBeat,
                                      const juce::String& transactionName)
{
    if (source.empty())
        return juce::Result::fail ("Nothing has been copied yet");

    const auto loop = project.getLoopLengthBeats();
    const auto existing = static_cast<int> (project.getNotes().size());
    if (existing + static_cast<int> (source.size()) > static_cast<int> (maxSequenceNotes))
        return juce::Result::fail ("A clip holds at most "
                                   + juce::String (static_cast<int> (maxSequenceNotes))
                                   + " notes");

    // Validate every placement before touching the project, so a paste that cannot
    // fit is refused whole rather than landing partly inside the loop.
    std::vector<SongNote> placed;
    placed.reserve (source.size());
    for (const auto& note : source)
    {
        auto copy = note;
        copy.beat = targetBeat + note.beat;
        if (copy.beat < 0.0 || copy.beat + copy.lengthBeats > loop + 1.0e-9)
            return juce::Result::fail ("The notes do not fit inside the loop");
        copy.id = "note-" + juce::Uuid().toString();
        placed.push_back (copy);
    }

    project.beginUndoTransaction (transactionName);
    std::vector<juce::String> insertedIds;
    insertedIds.reserve (placed.size());
    for (const auto& note : placed)
    {
        const auto result = project.insertNote (note);
        if (result.failed())
            return result;
        insertedIds.push_back (note.id);
    }

    setSelection (std::move (insertedIds));
    repaint();
    return juce::Result::ok();
}

juce::Result PianoRoll::pasteAtInsertBeat()
{
    const auto result = insertCopies (clipboard,
                                      insertBeat,
                                      clipboard.size() > 1 ? "Paste notes" : "Paste note");
    if (result.wasOk())
        reportStatus ("PASTED " + juce::String (static_cast<int> (clipboard.size()))
                      + (clipboard.size() == 1 ? " NOTE AT BEAT " : " NOTES AT BEAT ")
                      + juce::String (insertBeat, 2));
    else
        reportStatus ("PASTE REFUSED  /  " + result.getErrorMessage().toUpperCase());

    return result;
}

juce::Result PianoRoll::duplicateSelection()
{
    std::vector<SongNote> selected;
    for (const auto& id : selectedNoteIds)
        if (const auto note = project.findNote (id))
            selected.push_back (*note);

    if (selected.empty())
    {
        reportStatus ("SELECT NOTES BEFORE DUPLICATING");
        return juce::Result::fail ("Nothing is selected");
    }

    auto earliest = selected.front().beat;
    auto latestEnd = selected.front().beat + selected.front().lengthBeats;
    for (const auto& note : selected)
    {
        earliest = juce::jmin (earliest, note.beat);
        latestEnd = juce::jmax (latestEnd, note.beat + note.lengthBeats);
    }

    // Duplicate one selection-span later, rounded up to the snap grid. Detached notes
    // end just short of the beat, so the raw span would place the copy slightly early.
    const auto snap = project.getSnapBeats();
    const auto span = latestEnd - earliest;
    const auto offset = juce::jmax (snap, std::ceil (span / snap - 1.0e-9) * snap);

    for (auto& note : selected)
        note.beat -= earliest;

    const auto result = insertCopies (selected,
                                      earliest + offset,
                                      selected.size() > 1 ? "Duplicate notes" : "Duplicate note");
    if (result.wasOk())
        reportStatus ("DUPLICATED " + juce::String (static_cast<int> (selected.size()))
                      + (selected.size() == 1 ? " NOTE  /  +" : " NOTES  /  +")
                      + juce::String (offset, 2) + " BEATS");
    else
        reportStatus ("DUPLICATE REFUSED  /  " + result.getErrorMessage().toUpperCase());

    return result;
}

void PianoRoll::nudgeSelection (double beatDelta)
{
    std::vector<SongNote> selected;
    for (const auto& id : selectedNoteIds)
        if (const auto note = project.findNote (id))
            selected.push_back (*note);

    if (selected.empty())
        return;

    // Clamp the shared delta against every note, matching how a multi-note drag works.
    const auto loop = project.getLoopLengthBeats();
    auto delta = beatDelta;
    for (const auto& note : selected)
        delta = juce::jlimit (-note.beat, loop - note.lengthBeats - note.beat, delta);

    if (std::abs (delta) < 1.0e-9)
        return;

    project.beginUndoTransaction (selected.size() > 1 ? "Nudge notes" : "Nudge note");
    for (auto note : selected)
    {
        note.beat += delta;
        project.updateNote (note);
    }
    repaint();
}

void PianoRoll::transposeSelection (int semitones)
{
    std::vector<SongNote> selected;
    for (const auto& id : selectedNoteIds)
        if (const auto note = project.findNote (id))
            selected.push_back (*note);

    if (selected.empty() || semitones == 0)
        return;

    // All or nothing: a selection that would push any note past the MIDI range is
    // refused rather than silently flattening intervals at the edge.
    for (const auto& note : selected)
    {
        if (note.midiNote + semitones < 0 || note.midiNote + semitones > 127)
        {
            reportStatus ("TRANSPOSE REFUSED  /  A SELECTED NOTE WOULD LEAVE THE MIDI RANGE");
            return;
        }
    }

    project.beginUndoTransaction (selected.size() > 1 ? "Transpose notes" : "Transpose note");
    for (auto note : selected)
    {
        note.midiNote += semitones;
        project.updateNote (note);
    }
    repaint();
}

void PianoRoll::removeSelected()
{
    if (selectedNoteIds.empty())
        return;

    const auto toRemove = selectedNoteIds;
    setSelection ({});
    for (const auto& id : toRemove)
        project.removeNote (id);
    repaint();
}
} // namespace resonance
