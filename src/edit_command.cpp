#include "edit_command.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace resonance
{
namespace
{
constexpr int projectPpq = 960;
constexpr int maximumCommandChanges = 128;

juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

juce::DynamicObject* requireObject (const juce::var& value)
{
    return value.isObject() ? value.getDynamicObject() : nullptr;
}

bool hasOnlyProperties (const juce::DynamicObject& object,
                        std::initializer_list<const char*> allowed)
{
    const auto& properties = object.getProperties();
    for (int index = 0; index < properties.size(); ++index)
    {
        const auto name = properties.getName (index).toString();
        const auto found = std::any_of (allowed.begin(), allowed.end(),
                                       [&name] (const char* candidate)
                                       {
                                           return name == candidate;
                                       });
        if (! found)
            return false;
    }

    return true;
}

juce::String requireString (juce::DynamicObject& object, const juce::Identifier& property)
{
    if (! object.hasProperty (property))
        return {};

    const auto value = object.getProperty (property);
    return value.isString() ? value.toString() : juce::String {};
}

bool readInt (juce::DynamicObject& object, const juce::Identifier& property, int& destination)
{
    if (! object.hasProperty (property))
        return false;

    const auto value = object.getProperty (property);
    double numeric = 0.0;
    if (value.isInt() || value.isInt64() || value.isDouble())
        numeric = static_cast<double> (value);
    else
        return false;

    if (! std::isfinite (numeric) || std::floor (numeric) != numeric
        || numeric < static_cast<double> (std::numeric_limits<int>::min())
        || numeric > static_cast<double> (std::numeric_limits<int>::max()))
        return false;

    destination = static_cast<int> (numeric);
    return true;
}

bool readSeed (juce::DynamicObject& object, std::int64_t& destination)
{
    const auto value = object.getProperty ("seed");
    if (value.isInt())
        destination = static_cast<int> (value);
    else if (value.isInt64())
        destination = static_cast<juce::int64> (value);
    else
        return false;

    return destination >= 0 && destination <= std::numeric_limits<std::int32_t>::max();
}

bool isSha256 (const juce::String& value)
{
    return value.length() == 64
        && value.containsOnly ("0123456789abcdefABCDEF");
}

juce::Result parseNote (const juce::var& value, SongNote& destination)
{
    auto* object = requireObject (value);
    if (object == nullptr
        || ! hasOnlyProperties (*object, { "id", "startTick", "lengthTicks", "midiNote", "velocity" }))
        return juce::Result::fail ("A command note must contain only the version-1 note fields");

    const auto id = requireString (*object, "id");
    int startTick = 0;
    int lengthTicks = 0;
    int midiNote = 0;
    int velocity = 0;
    if (id.isEmpty())
        return juce::Result::fail ("A command note id is required");
    if (! readInt (*object, "startTick", startTick) || startTick < 0 || startTick >= 32 * projectPpq)
        return juce::Result::fail ("Command note startTick must be from 0 through 30719");
    if (! readInt (*object, "lengthTicks", lengthTicks) || lengthTicks < 1 || lengthTicks > 32 * projectPpq)
        return juce::Result::fail ("Command note lengthTicks must be from 1 through 30720");
    if (! readInt (*object, "midiNote", midiNote) || midiNote < 0 || midiNote > 127)
        return juce::Result::fail ("Command note midiNote must be from 0 through 127");
    if (! readInt (*object, "velocity", velocity) || velocity < 1 || velocity > 127)
        return juce::Result::fail ("Command note velocity must be from 1 through 127");

    destination = { id,
                    static_cast<double> (startTick) / projectPpq,
                    static_cast<double> (lengthTicks) / projectPpq,
                    midiNote,
                    velocity };
    return juce::Result::ok();
}

juce::var noteToJson (const SongNote& note)
{
    auto result = makeObject();
    auto* object = result.getDynamicObject();
    object->setProperty ("id", note.id);
    object->setProperty ("startTick", juce::roundToInt (note.beat * projectPpq));
    object->setProperty ("lengthTicks", juce::roundToInt (note.lengthBeats * projectPpq));
    object->setProperty ("midiNote", note.midiNote);
    object->setProperty ("velocity", note.velocity);
    return result;
}

juce::String actionName (NoteEditAction action)
{
    switch (action)
    {
        case NoteEditAction::add: return "add";
        case NoteEditAction::update: return "update";
        case NoteEditAction::remove: return "remove";
    }

    return {};
}

bool notesMatch (const SongNote& first, const SongNote& second)
{
    return first.id == second.id
        && std::abs (first.beat - second.beat) < 1.0e-9
        && std::abs (first.lengthBeats - second.lengthBeats) < 1.0e-9
        && first.midiNote == second.midiNote
        && first.velocity == second.velocity;
}

juce::Result validateResolvedNote (const SongNote& note, const SongProject& project)
{
    if (note.id.isEmpty())
        return juce::Result::fail ("A resolved note id is required");
    if (! std::isfinite (note.beat) || ! std::isfinite (note.lengthBeats))
        return juce::Result::fail ("Resolved note timing must be finite");

    const auto startTick = juce::roundToInt (note.beat * projectPpq);
    const auto lengthTicks = juce::roundToInt (note.lengthBeats * projectPpq);
    if (std::abs (note.beat - static_cast<double> (startTick) / projectPpq) > 1.0e-9
        || std::abs (note.lengthBeats - static_cast<double> (lengthTicks) / projectPpq) > 1.0e-9)
        return juce::Result::fail ("Resolved command timing must use integer ticks at PPQ 960");

    const auto loopTicks = juce::roundToInt (project.getLoopLengthBeats() * projectPpq);
    const auto minimumLengthTicks = juce::roundToInt (project.getSnapBeats() * projectPpq);
    if (startTick < 0 || startTick >= loopTicks)
        return juce::Result::fail ("Resolved note start is outside the target loop");
    if (lengthTicks < minimumLengthTicks || startTick + lengthTicks > loopTicks)
        return juce::Result::fail ("Resolved note length is outside the target loop or below the active snap length");
    if (note.midiNote < 0 || note.midiNote > 127)
        return juce::Result::fail ("Resolved note MIDI pitch must be from 0 through 127");
    if (note.velocity < 1 || note.velocity > 127)
        return juce::Result::fail ("Resolved note velocity must be from 1 through 127");
    return juce::Result::ok();
}

juce::Result applyChange (const NoteEditChange& change, SongProject& project)
{
    switch (change.action)
    {
        case NoteEditAction::add:
            return change.note.has_value()
                       ? project.insertNote (*change.note)
                       : juce::Result::fail ("An add change is missing its resolved note");

        case NoteEditAction::update:
            if (! change.note.has_value())
                return juce::Result::fail ("An update change is missing its resolved note");
            return project.updateNote (*change.note)
                       ? juce::Result::ok()
                       : juce::Result::fail ("The update target no longer exists: " + change.noteId);

        case NoteEditAction::remove:
            return project.removeNote (change.noteId)
                       ? juce::Result::ok()
                       : juce::Result::fail ("The remove target no longer exists: " + change.noteId);
    }

    return juce::Result::fail ("The note edit action is unsupported");
}
} // namespace

juce::Result parseEditCommand (const juce::String& json, EditCommand& destination)
{
    juce::var parsed;
    const auto parseResult = juce::JSON::parse (json, parsed);
    if (parseResult.failed())
        return juce::Result::fail ("Edit command JSON is invalid: " + parseResult.getErrorMessage());

    auto* root = requireObject (parsed);
    if (root == nullptr
        || ! hasOnlyProperties (*root, { "commandVersion", "projectContentSha256", "operation",
                                        "target", "summary", "seed", "changes" }))
        return juce::Result::fail ("Edit command root contains unsupported fields");

    EditCommand command;
    if (! readInt (*root, "commandVersion", command.commandVersion)
        || command.commandVersion != EditCommand::supportedVersion)
        return juce::Result::fail ("Only edit-command version 1 is supported");

    command.projectContentSha256 = requireString (*root, "projectContentSha256").toLowerCase();
    if (! isSha256 (command.projectContentSha256))
        return juce::Result::fail ("Edit command projectContentSha256 must contain 64 hexadecimal characters");

    command.operation = requireString (*root, "operation");
    if (command.operation != "editNotes")
        return juce::Result::fail ("Only the editNotes operation is supported in command version 1");

    auto* target = requireObject (root->getProperty ("target"));
    if (target == nullptr || ! hasOnlyProperties (*target, { "trackId", "clipId" }))
        return juce::Result::fail ("Edit command target must contain only trackId and clipId");
    command.trackId = requireString (*target, "trackId");
    command.clipId = requireString (*target, "clipId");
    if (command.trackId.isEmpty() || command.clipId.isEmpty())
        return juce::Result::fail ("Edit command target IDs are required");

    command.summary = requireString (*root, "summary").trim();
    if (command.summary.isEmpty() || command.summary.length() > 160)
        return juce::Result::fail ("Edit command summary must contain from 1 through 160 characters");

    if (root->hasProperty ("seed"))
    {
        std::int64_t seed = 0;
        if (! readSeed (*root, seed))
            return juce::Result::fail ("Edit command seed must be an integer from 0 through 2147483647");
        command.seed = seed;
    }

    const auto* changes = root->getProperty ("changes").getArray();
    if (changes == nullptr || changes->isEmpty() || changes->size() > maximumCommandChanges)
        return juce::Result::fail ("Edit command changes must contain from 1 through 128 entries");

    std::set<juce::String> touchedIds;
    for (const auto& value : *changes)
    {
        auto* changeObject = requireObject (value);
        if (changeObject == nullptr)
            return juce::Result::fail ("Every note change must be a JSON object");

        NoteEditChange change;
        const auto action = requireString (*changeObject, "action");
        if (action == "add" || action == "update")
        {
            if (! hasOnlyProperties (*changeObject, { "action", "note" }))
                return juce::Result::fail ("Add and update changes must contain only action and note");

            SongNote note;
            const auto noteResult = parseNote (changeObject->getProperty ("note"), note);
            if (noteResult.failed())
                return noteResult;
            change.action = action == "add" ? NoteEditAction::add : NoteEditAction::update;
            change.noteId = note.id;
            change.note = note;
        }
        else if (action == "remove")
        {
            if (! hasOnlyProperties (*changeObject, { "action", "noteId" }))
                return juce::Result::fail ("Remove changes must contain only action and noteId");
            change.action = NoteEditAction::remove;
            change.noteId = requireString (*changeObject, "noteId");
            if (change.noteId.isEmpty())
                return juce::Result::fail ("A remove change noteId is required");
        }
        else
        {
            return juce::Result::fail ("A note change action must be add, update, or remove");
        }

        if (! touchedIds.insert (change.noteId).second)
            return juce::Result::fail ("A command cannot change the same note more than once: " + change.noteId);
        command.changes.push_back (std::move (change));
    }

    destination = std::move (command);
    return juce::Result::ok();
}

juce::String serialiseEditCommand (const EditCommand& command)
{
    auto root = makeObject();
    auto* object = root.getDynamicObject();
    object->setProperty ("commandVersion", command.commandVersion);
    object->setProperty ("projectContentSha256", command.projectContentSha256.toLowerCase());
    object->setProperty ("operation", command.operation);

    auto target = makeObject();
    target.getDynamicObject()->setProperty ("trackId", command.trackId);
    target.getDynamicObject()->setProperty ("clipId", command.clipId);
    object->setProperty ("target", target);
    object->setProperty ("summary", command.summary);
    if (command.seed.has_value())
        object->setProperty ("seed", static_cast<juce::int64> (*command.seed));

    juce::Array<juce::var> changes;
    for (const auto& change : command.changes)
    {
        auto value = makeObject();
        auto* changeObject = value.getDynamicObject();
        changeObject->setProperty ("action", actionName (change.action));
        if (change.action == NoteEditAction::remove)
            changeObject->setProperty ("noteId", change.noteId);
        else if (change.note.has_value())
            changeObject->setProperty ("note", noteToJson (*change.note));
        changes.add (value);
    }
    object->setProperty ("changes", changes);
    return juce::JSON::toString (root, true);
}

juce::Result createEditCommandPreview (const EditCommand& command,
                                       const SongProject& activeProject,
                                       EditCommandPreview& destination)
{
    if (command.commandVersion != EditCommand::supportedVersion)
        return juce::Result::fail ("Only edit-command version 1 is supported");
    if (! isSha256 (command.projectContentSha256))
        return juce::Result::fail ("The edit command content-hash precondition is invalid");
    if (command.operation != "editNotes")
        return juce::Result::fail ("Only the editNotes operation is supported");
    if (command.trackId != "track-1" || command.clipId != "loop-1")
        return juce::Result::fail ("The edit command targets an unknown track or clip");
    if (command.summary.trim().isEmpty() || command.summary.length() > 160)
        return juce::Result::fail ("The edit command summary is invalid");
    if (command.changes.empty() || command.changes.size() > maximumCommandChanges)
        return juce::Result::fail ("The edit command must contain from 1 through 128 changes");

    const auto beforeHash = activeProject.getContentSha256();
    if (! command.projectContentSha256.equalsIgnoreCase (beforeHash))
        return juce::Result::fail ("The edit command is stale: the active project content has changed");

    auto candidate = std::make_unique<SongProject>();
    candidate->replaceWith (activeProject);
    candidate->beginUndoTransaction ("Preview edit: " + command.summary);

    std::set<juce::String> touchedIds;
    std::vector<NoteEditDiff> diffs;
    diffs.reserve (command.changes.size());
    auto candidateNoteCount = candidate->getNotes().size();

    for (const auto& change : command.changes)
    {
        if (change.noteId.isEmpty() || ! touchedIds.insert (change.noteId).second)
            return juce::Result::fail ("A command cannot change the same note more than once");

        NoteEditDiff diff;
        diff.action = change.action;
        diff.noteId = change.noteId;

        if (change.action == NoteEditAction::add)
        {
            if (! change.note.has_value() || change.note->id != change.noteId)
                return juce::Result::fail ("An add change must contain a matching resolved note");
            if (candidate->findNote (change.noteId).has_value())
                return juce::Result::fail ("The add note id already exists: " + change.noteId);
            if (candidateNoteCount >= maxSequenceNotes)
                return juce::Result::fail ("The command would exceed the 512-note project limit");
            const auto validation = validateResolvedNote (*change.note, *candidate);
            if (validation.failed())
                return validation;
            diff.after = change.note;
            ++candidateNoteCount;
        }
        else
        {
            diff.before = candidate->findNote (change.noteId);
            if (! diff.before.has_value())
                return juce::Result::fail ("The command targets an unknown note id: " + change.noteId);

            if (change.action == NoteEditAction::update)
            {
                if (! change.note.has_value() || change.note->id != change.noteId)
                    return juce::Result::fail ("An update change must contain a matching resolved note");
                const auto validation = validateResolvedNote (*change.note, *candidate);
                if (validation.failed())
                    return validation;
                if (notesMatch (*diff.before, *change.note))
                    return juce::Result::fail ("The update does not change note " + change.noteId);
                diff.after = change.note;
            }
            else
            {
                if (change.note.has_value())
                    return juce::Result::fail ("A remove change cannot contain a replacement note");
                --candidateNoteCount;
            }
        }

        const auto applyResult = applyChange (change, *candidate);
        if (applyResult.failed())
            return applyResult;
        diffs.push_back (std::move (diff));
    }

    const auto afterHash = candidate->getContentSha256();
    if (afterHash == beforeHash)
        return juce::Result::fail ("The edit command does not change the project");

    EditCommandPreview preview;
    preview.command = command;
    preview.beforeContentSha256 = beforeHash;
    preview.afterContentSha256 = afterHash;
    preview.noteDiffs = std::move (diffs);
    preview.candidate = std::move (candidate);
    destination = std::move (preview);
    return juce::Result::ok();
}

juce::Result EditCommandPreview::applyTo (SongProject& activeProject)
{
    if (! isPending())
        return juce::Result::fail ("This edit preview has already been applied or rejected");
    if (! activeProject.getContentSha256().equalsIgnoreCase (beforeContentSha256))
        return juce::Result::fail ("The edit preview is stale: the active project content has changed");

    activeProject.beginUndoTransaction ("Apply edit: " + command.summary);
    bool mutated = false;
    for (const auto& change : command.changes)
    {
        const auto result = applyChange (change, activeProject);
        if (result.failed())
        {
            if (mutated)
                activeProject.undo();
            return juce::Result::fail ("The edit could not be applied atomically: " + result.getErrorMessage());
        }
        mutated = true;
    }

    if (! activeProject.getContentSha256().equalsIgnoreCase (afterContentSha256))
    {
        if (mutated)
            activeProject.undo();
        return juce::Result::fail ("The applied edit did not match its preview and was rolled back");
    }

    consumed = true;
    candidate.reset();
    return juce::Result::ok();
}

juce::Result EditCommandPreview::reject()
{
    if (! isPending())
        return juce::Result::fail ("This edit preview has already been applied or rejected");

    consumed = true;
    candidate.reset();
    return juce::Result::ok();
}
} // namespace resonance
