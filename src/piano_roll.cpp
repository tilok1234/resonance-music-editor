#include "piano_roll.h"

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
    setDescription ("Click empty space to add a note. Drag notes to move them, drag the right edge to resize, and press Delete to remove the selection.");
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

juce::Rectangle<float> PianoRoll::boundsForNote (const SongNote& note) const
{
    const auto grid = gridBounds();
    const auto loop = project.getLoopLengthBeats();
    const auto topNote = lowestVisibleNote + visibleNoteRows - 1;
    if (note.midiNote < lowestVisibleNote || note.midiNote > topNote)
        return {};

    const auto rowHeight = grid.getHeight() / static_cast<float> (visibleNoteRows);
    const auto x = grid.getX() + grid.getWidth() * static_cast<float> (note.beat / loop);
    const auto width = grid.getWidth() * static_cast<float> (note.lengthBeats / loop);
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
    return juce::jlimit (0.0,
                         project.getLoopLengthBeats(),
                         static_cast<double> ((x - grid.getX()) / grid.getWidth())
                             * project.getLoopLengthBeats());
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

        if (midiNote % 12 == 0)
        {
            graphics.setColour (textMuted);
            graphics.drawText (juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3),
                               juce::Rectangle<float> (5.0f, y, 40.0f, rowHeight).toNearestInt(),
                               juce::Justification::centredRight);
        }
    }

    const auto snap = project.getSnapBeats();
    const auto steps = juce::roundToInt (loop / snap);
    for (int step = 0; step <= steps; ++step)
    {
        const auto beat = static_cast<double> (step) * snap;
        const auto x = grid.getX() + grid.getWidth() * static_cast<float> (beat / loop);
        const auto roundedBeat = juce::roundToInt (beat);
        const auto onBeat = std::abs (beat - roundedBeat) < 1.0e-8;
        const auto onBar = onBeat && roundedBeat % 4 == 0;
        graphics.setColour ((onBar ? primary : gridLine).withAlpha (onBar ? 0.60f : onBeat ? 0.42f : 0.20f));
        graphics.drawVerticalLine (juce::roundToInt (x), grid.getY(), grid.getBottom());

        if (onBar && beat < loop)
        {
            graphics.setColour (textMuted);
            graphics.drawText ("BAR " + juce::String (roundedBeat / 4 + 1),
                               juce::Rectangle<float> (x + 5.0f, 5.0f, 62.0f, 15.0f).toNearestInt(),
                               juce::Justification::centredLeft);
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

        if (note.id == selectedNoteId)
        {
            graphics.setColour (juce::Colours::white.withAlpha (0.96f));
            graphics.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.5f);
            graphics.fillRect (bounds.getRight() - 4.0f, bounds.getY() + 1.0f, 2.0f, bounds.getHeight() - 2.0f);
        }
    }

    const auto playheadX = grid.getX() + grid.getWidth()
                                         * static_cast<float> (playheadBeat / juce::jmax (1.0, loop));
    graphics.setColour (juce::Colours::white.withAlpha (0.88f));
    graphics.fillRect (playheadX - 1.0f, grid.getY(), 2.0f, grid.getHeight());
    graphics.setColour (primary);
    graphics.fillEllipse (playheadX - 4.0f, grid.getY() - 4.0f, 8.0f, 8.0f);

    graphics.setColour (gridLine.withAlpha (0.8f));
    graphics.drawRoundedRectangle (outer, 9.0f, 1.0f);
}

void PianoRoll::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto hit = noteAt (event.position);

    if (hit.has_value())
    {
        select (hit->id);
        if (event.mods.isPopupMenu())
        {
            project.beginUndoTransaction ("Delete note");
            removeSelected();
            return;
        }

        dragOrigin = hit;
        const auto noteBounds = boundsForNote (*hit);
        dragMode = event.position.x >= noteBounds.getRight() - 7.0f ? DragMode::resize : DragMode::move;
        dragBeatOffset = beatAtX (event.position.x) - hit->beat;
        dragPitchOffset = hit->midiNote - noteAtY (event.position.y);
        project.beginUndoTransaction (dragMode == DragMode::resize ? "Resize note" : "Move note");
        return;
    }

    if (! gridBounds().contains (event.position) || event.mods.isPopupMenu())
    {
        select ({});
        return;
    }

    project.beginUndoTransaction ("Add note");
    const auto snap = project.getSnapBeats();
    const auto beat = juce::jlimit (0.0,
                                    project.getLoopLengthBeats() - snap,
                                    std::floor (beatAtX (event.position.x) / snap) * snap);
    const auto id = project.addNote (beat,
                                     juce::jmax (0.5, snap),
                                     noteAtY (event.position.y),
                                     96);
    select (id);
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragOrigin.has_value() || dragMode == DragMode::none)
        return;

    auto edited = *dragOrigin;
    const auto loop = project.getLoopLengthBeats();
    const auto snap = project.getSnapBeats();

    if (dragMode == DragMode::move)
    {
        edited.beat = juce::jlimit (0.0,
                                    loop - edited.lengthBeats,
                                    snapBeat (beatAtX (event.position.x) - dragBeatOffset));
        edited.midiNote = juce::jlimit (0, 127, noteAtY (event.position.y) + dragPitchOffset);
    }
    else
    {
        const auto snappedEnd = snapBeat (beatAtX (event.position.x));
        edited.lengthBeats = juce::jlimit (snap, loop - edited.beat, snappedEnd - edited.beat);
    }

    project.updateNote (edited);
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    dragMode = DragMode::none;
    dragOrigin.reset();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto direction = wheel.deltaY > 0.0f ? 2 : wheel.deltaY < 0.0f ? -2 : 0;
    lowestVisibleNote = juce::jlimit (0, 128 - visibleNoteRows, lowestVisibleNote + direction);
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        project.beginUndoTransaction ("Delete note");
        removeSelected();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        select ({});
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

void PianoRoll::select (const juce::String& id)
{
    if (selectedNoteId == id)
        return;

    selectedNoteId = id;
    repaint();
    if (selectionChanged)
        selectionChanged (selectedNoteId);
}

void PianoRoll::removeSelected()
{
    if (selectedNoteId.isEmpty())
        return;

    const auto toRemove = selectedNoteId;
    select ({});
    project.removeNote (toRemove);
    repaint();
}
} // namespace resonance
