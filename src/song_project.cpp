#include "song_project.h"

#include <cmath>
#include <limits>
#include <set>

namespace resonance
{
namespace
{
constexpr auto rootType = "songProject";
constexpr auto notesType = "notes";
constexpr auto noteType = "note";
constexpr auto instrumentType = "instrument";

bool isSupportedSampleRate (int rate)
{
    return rate == 44100 || rate == 48000 || rate == 88200 || rate == 96000;
}

bool isSupportedSnap (double beats)
{
    for (const auto allowed : { 0.125, 0.25, 0.5, 1.0 })
        if (std::abs (beats - allowed) < 1.0e-9)
            return true;

    return false;
}

juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

juce::DynamicObject* requireObject (const juce::var& value)
{
    return value.isObject() ? value.getDynamicObject() : nullptr;
}

juce::String requireString (juce::DynamicObject& object, const juce::Identifier& property)
{
    return object.hasProperty (property) ? object.getProperty (property).toString() : juce::String {};
}

bool readNumber (juce::DynamicObject& object, const juce::Identifier& property, double& destination)
{
    if (! object.hasProperty (property))
        return false;

    const auto value = object.getProperty (property);
    if (! value.isInt() && ! value.isInt64() && ! value.isDouble())
        return false;

    destination = static_cast<double> (value);
    return std::isfinite (destination);
}

bool readInt (juce::DynamicObject& object, const juce::Identifier& property, int& destination)
{
    double numeric = 0.0;
    if (! readNumber (object, property, numeric) || std::floor (numeric) != numeric
        || numeric < static_cast<double> (std::numeric_limits<int>::min())
        || numeric > static_cast<double> (std::numeric_limits<int>::max()))
        return false;

    destination = static_cast<int> (numeric);
    return true;
}

juce::String stateHash (const juce::MemoryBlock& state)
{
    return juce::SHA256 (state).toHexString();
}
} // namespace

SongProject::SongProject()
{
    resetToStarter();
    markClean();
}

SongProject::~SongProject()
{
    root.removeListener (this);
}

void SongProject::resetToStarter()
{
    juce::ValueTree newRoot (rootType);
    newRoot.setProperty ("schemaVersion", 1, nullptr);
    newRoot.setProperty ("title", "Untitled", nullptr);
    newRoot.setProperty ("sampleRate", 48000, nullptr);
    newRoot.setProperty ("tempoBpm", 120.0, nullptr);
    newRoot.setProperty ("loopLengthBeats", loopLengthBeats, nullptr);
    newRoot.setProperty ("snapBeats", 0.25, nullptr);

    juce::ValueTree instrument (instrumentType);
    instrument.setProperty ("format", "VST3", nullptr);
    instrument.setProperty ("identifier", "unassigned", nullptr);
    instrument.setProperty ("name", "Surge XT", nullptr);
    instrument.setProperty ("vendor", "Surge Synth Team", nullptr);
    instrument.setProperty ("version", "unknown", nullptr);
    instrument.setProperty ("soundName", "Initial Surge state", nullptr);
    instrument.setProperty ("stateEncoding", "base64", nullptr);
    instrument.setProperty ("state", juce::String {}, nullptr);
    instrument.setProperty ("stateSha256", stateHash (juce::MemoryBlock {}), nullptr);
    newRoot.addChild (instrument, -1, nullptr);

    juce::ValueTree notes (notesType);
    int index = 0;
    for (const auto& starter : starterLoopNotes)
    {
        juce::ValueTree note (noteType);
        note.setProperty ("id", "note-" + juce::String (++index), nullptr);
        note.setProperty ("beat", starter.beat, nullptr);
        note.setProperty ("lengthBeats", starter.lengthBeats, nullptr);
        note.setProperty ("midiNote", starter.midiNote, nullptr);
        note.setProperty ("velocity", juce::jlimit (1, 127, juce::roundToInt (starter.velocity * 127.0f)), nullptr);
        notes.addChild (note, -1, nullptr);
    }
    newRoot.addChild (notes, -1, nullptr);

    installRoot (newRoot, true);
}

void SongProject::replaceWith (const SongProject& other)
{
    installRoot (other.root.createCopy(), false);
}

juce::String SongProject::getTitle() const
{
    return root.getProperty ("title").toString();
}

void SongProject::setTitle (const juce::String& title)
{
    const auto trimmed = title.trim();
    if (trimmed.isNotEmpty())
        root.setProperty ("title", trimmed, &undoManager);
}

double SongProject::getTempoBpm() const
{
    return static_cast<double> (root.getProperty ("tempoBpm", 120.0));
}

void SongProject::setTempoBpm (double bpm)
{
    root.setProperty ("tempoBpm", juce::jlimit (40.0, 240.0, bpm), &undoManager);
}

double SongProject::getLoopLengthBeats() const
{
    return static_cast<double> (root.getProperty ("loopLengthBeats", loopLengthBeats));
}

void SongProject::setLoopLengthBeats (double beats)
{
    const auto safeLength = juce::jlimit (4.0, 32.0, beats);
    root.setProperty ("loopLengthBeats", safeLength, &undoManager);

    auto notes = getNotesTree();
    const auto minimumLength = getSnapBeats();
    for (int index = 0; index < notes.getNumChildren(); ++index)
    {
        auto note = notes.getChild (index);
        auto beat = static_cast<double> (note.getProperty ("beat"));
        auto length = static_cast<double> (note.getProperty ("lengthBeats"));
        beat = juce::jlimit (0.0, juce::jmax (0.0, safeLength - minimumLength), beat);
        length = juce::jlimit (minimumLength, safeLength - beat, length);
        note.setProperty ("beat", beat, &undoManager);
        note.setProperty ("lengthBeats", length, &undoManager);
    }
}

double SongProject::getSnapBeats() const
{
    return static_cast<double> (root.getProperty ("snapBeats", 0.25));
}

void SongProject::setSnapBeats (double beats)
{
    if (isSupportedSnap (beats))
        root.setProperty ("snapBeats", beats, &undoManager);
}

int SongProject::getSampleRate() const
{
    return static_cast<int> (root.getProperty ("sampleRate", 48000));
}

void SongProject::setSampleRate (int sampleRate)
{
    if (isSupportedSampleRate (sampleRate))
        root.setProperty ("sampleRate", sampleRate, nullptr);
}

std::vector<SongNote> SongProject::getNotes() const
{
    std::vector<SongNote> result;
    auto notes = getNotesTree();
    result.reserve (static_cast<std::size_t> (notes.getNumChildren()));

    for (int index = 0; index < notes.getNumChildren(); ++index)
    {
        const auto note = notes.getChild (index);
        result.push_back ({ note.getProperty ("id").toString(),
                            static_cast<double> (note.getProperty ("beat")),
                            static_cast<double> (note.getProperty ("lengthBeats")),
                            static_cast<int> (note.getProperty ("midiNote")),
                            static_cast<int> (note.getProperty ("velocity")) });
    }

    return result;
}

std::optional<SongNote> SongProject::findNote (const juce::String& id) const
{
    const auto note = findNoteTree (id);
    if (! note.isValid())
        return std::nullopt;

    return SongNote { id,
                      static_cast<double> (note.getProperty ("beat")),
                      static_cast<double> (note.getProperty ("lengthBeats")),
                      static_cast<int> (note.getProperty ("midiNote")),
                      static_cast<int> (note.getProperty ("velocity")) };
}

juce::String SongProject::addNote (double beat, double lengthBeatsValue, int midiNote, int velocity)
{
    auto notes = getNotesTree();
    if (! notes.isValid() || notes.getNumChildren() >= static_cast<int> (maxSequenceNotes))
        return {};

    const auto loop = getLoopLengthBeats();
    const auto minimumLength = getSnapBeats();
    const auto safeBeat = juce::jlimit (0.0, juce::jmax (0.0, loop - minimumLength), beat);
    const auto safeLength = juce::jlimit (minimumLength, loop - safeBeat, lengthBeatsValue);
    const auto id = "note-" + juce::Uuid().toString();

    juce::ValueTree note (noteType);
    note.setProperty ("id", id, nullptr);
    note.setProperty ("beat", safeBeat, nullptr);
    note.setProperty ("lengthBeats", safeLength, nullptr);
    note.setProperty ("midiNote", juce::jlimit (0, 127, midiNote), nullptr);
    note.setProperty ("velocity", juce::jlimit (1, 127, velocity), nullptr);
    notes.addChild (note, -1, &undoManager);
    return id;
}

bool SongProject::updateNote (const SongNote& note)
{
    auto target = findNoteTree (note.id);
    if (! target.isValid())
        return false;

    const auto loop = getLoopLengthBeats();
    const auto minimumLength = getSnapBeats();
    const auto safeBeat = juce::jlimit (0.0, juce::jmax (0.0, loop - minimumLength), note.beat);
    const auto safeLength = juce::jlimit (minimumLength, loop - safeBeat, note.lengthBeats);
    target.setProperty ("beat", safeBeat, &undoManager);
    target.setProperty ("lengthBeats", safeLength, &undoManager);
    target.setProperty ("midiNote", juce::jlimit (0, 127, note.midiNote), &undoManager);
    target.setProperty ("velocity", juce::jlimit (1, 127, note.velocity), &undoManager);
    return true;
}

bool SongProject::removeNote (const juce::String& id)
{
    auto note = findNoteTree (id);
    if (! note.isValid())
        return false;

    getNotesTree().removeChild (note, &undoManager);
    return true;
}

void SongProject::setPluginMetadata (const juce::String& identifier,
                                     const juce::String& name,
                                     const juce::String& vendor,
                                     const juce::String& version)
{
    auto instrument = getInstrumentTree();
    instrument.setProperty ("identifier", identifier, nullptr);
    instrument.setProperty ("name", name, nullptr);
    instrument.setProperty ("vendor", vendor, nullptr);
    instrument.setProperty ("version", version, nullptr);
}

juce::String SongProject::getPluginIdentifier() const
{
    return getInstrumentTree().getProperty ("identifier").toString();
}

juce::String SongProject::getPluginName() const
{
    return getInstrumentTree().getProperty ("name").toString();
}

juce::String SongProject::getPluginSoundName() const
{
    const auto name = getInstrumentTree().getProperty ("soundName").toString().trim();
    return name.isNotEmpty() ? name : juce::String { "Project sound" };
}

void SongProject::setPluginState (const juce::MemoryBlock& state)
{
    const auto result = writePluginSoundSnapshot (getPluginSoundName(), state, nullptr);
    jassert (result.wasOk());
    juce::ignoreUnused (result);
}

juce::Result SongProject::applyPluginSound (const juce::String& soundName,
                                            const juce::MemoryBlock& state)
{
    const auto trimmed = soundName.trim().substring (0, 80);
    if (trimmed.isEmpty())
        return juce::Result::fail ("A sound snapshot name is required");
    if (state.getSize() == 0)
        return juce::Result::fail ("A sound snapshot cannot contain empty VST3 state");

    beginUndoTransaction ("Apply sound: " + trimmed);
    return writePluginSoundSnapshot (trimmed, state, &undoManager);
}

juce::Result SongProject::getPluginState (juce::MemoryBlock& state) const
{
    const auto instrument = getInstrumentTree();
    const auto encoded = instrument.getProperty ("state").toString();
    juce::MemoryBlock decoded;
    if (! decoded.fromBase64Encoding (encoded))
        return juce::Result::fail ("The saved Surge state is not valid Base64");

    const auto expected = instrument.getProperty ("stateSha256").toString();
    if (! expected.equalsIgnoreCase (stateHash (decoded)))
        return juce::Result::fail ("The saved Surge state failed its SHA-256 integrity check");

    state = std::move (decoded);
    return juce::Result::ok();
}

juce::String SongProject::getPluginStateSha256() const
{
    return getInstrumentTree().getProperty ("stateSha256").toString();
}

juce::Result SongProject::getPluginSoundSnapshot (PluginSoundSnapshot& snapshot) const
{
    juce::MemoryBlock state;
    const auto result = getPluginState (state);
    if (result.failed())
        return result;

    if (state.getSize() == 0)
        return juce::Result::fail ("The project contains an empty Surge XT sound snapshot");

    snapshot.name = getPluginSoundName();
    snapshot.state = std::move (state);
    snapshot.stateSha256 = getPluginStateSha256().toLowerCase();
    return juce::Result::ok();
}

SequenceSnapshot SongProject::createSequenceSnapshot() const
{
    SequenceSnapshot snapshot;
    snapshot.loopBeats = getLoopLengthBeats();

    const auto notes = getNotes();
    snapshot.noteCount = juce::jmin (notes.size(), maxSequenceNotes);
    for (std::size_t index = 0; index < snapshot.noteCount; ++index)
    {
        snapshot.notes[index] = { notes[index].beat,
                                  notes[index].lengthBeats,
                                  notes[index].midiNote,
                                  static_cast<float> (notes[index].velocity) / 127.0f };
    }

    return snapshot;
}

void SongProject::beginUndoTransaction (const juce::String& name)
{
    undoManager.beginNewTransaction (name);
}

bool SongProject::undo()
{
    const auto changed = undoManager.undo();
    if (changed && changeCallback)
        changeCallback();
    return changed;
}

bool SongProject::redo()
{
    const auto changed = undoManager.redo();
    if (changed && changeCallback)
        changeCallback();
    return changed;
}
bool SongProject::canUndo() const              { return undoManager.canUndo(); }
bool SongProject::canRedo() const              { return undoManager.canRedo(); }
juce::String SongProject::getUndoDescription() const { return undoManager.getUndoDescription(); }
juce::String SongProject::getRedoDescription() const { return undoManager.getRedoDescription(); }

juce::Result SongProject::saveToFile (const juce::File& file) const
{
    if (file == juce::File())
        return juce::Result::fail ("No project file was selected");

    const auto directoryResult = file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return juce::Result::fail ("The project folder could not be created: "
                                   + directoryResult.getErrorMessage());

    const auto json = juce::JSON::toString (toJsonValue(), true);
    return file.replaceWithText (json)
               ? juce::Result::ok()
               : juce::Result::fail ("The project file could not be written");
}

juce::Result SongProject::loadFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("The selected project file does not exist");

    juce::var parsed;
    const auto parseResult = juce::JSON::parse (file.loadFileAsString(), parsed);
    if (parseResult.failed())
        return juce::Result::fail ("Project JSON is invalid: " + parseResult.getErrorMessage());

    juce::ValueTree loaded;
    const auto conversion = valueTreeFromJson (parsed, loaded);
    if (conversion.failed())
        return conversion;

    installRoot (loaded, false);
    return juce::Result::ok();
}

juce::ValueTree SongProject::getNotesTree() const
{
    return root.getChildWithName (notesType);
}

juce::ValueTree SongProject::getInstrumentTree() const
{
    return root.getChildWithName (instrumentType);
}

juce::ValueTree SongProject::findNoteTree (const juce::String& id) const
{
    const auto notes = getNotesTree();
    for (int index = 0; index < notes.getNumChildren(); ++index)
    {
        auto note = notes.getChild (index);
        if (note.getProperty ("id").toString() == id)
            return note;
    }

    return {};
}

juce::var SongProject::toJsonValue() const
{
    auto result = makeObject();
    auto* object = result.getDynamicObject();
    object->setProperty ("schemaVersion", 1);
    object->setProperty ("editorVersion", JUCE_APPLICATION_VERSION_STRING);
    object->setProperty ("title", getTitle());
    object->setProperty ("sampleRate", getSampleRate());
    object->setProperty ("ppq", projectPpq);

    juce::Array<juce::var> tempoMap;
    auto tempo = makeObject();
    tempo.getDynamicObject()->setProperty ("tick", 0);
    tempo.getDynamicObject()->setProperty ("bpm", getTempoBpm());
    tempoMap.add (tempo);
    object->setProperty ("tempoMap", tempoMap);

    juce::Array<juce::var> meterMap;
    auto meter = makeObject();
    meter.getDynamicObject()->setProperty ("tick", 0);
    meter.getDynamicObject()->setProperty ("numerator", 4);
    meter.getDynamicObject()->setProperty ("denominator", 4);
    meterMap.add (meter);
    object->setProperty ("meterMap", meterMap);

    auto editor = makeObject();
    editor.getDynamicObject()->setProperty ("snapBeats", getSnapBeats());
    object->setProperty ("editor", editor);

    auto track = makeObject();
    auto* trackObject = track.getDynamicObject();
    trackObject->setProperty ("id", "track-1");
    trackObject->setProperty ("name", getPluginName());
    trackObject->setProperty ("role", "instrument");

    auto instrumentJson = makeObject();
    auto* instrumentObject = instrumentJson.getDynamicObject();
    const auto instrument = getInstrumentTree();
    instrumentObject->setProperty ("format", "VST3");
    instrumentObject->setProperty ("pluginIdentifier", getPluginIdentifier());
    instrumentObject->setProperty ("pluginName", getPluginName());
    instrumentObject->setProperty ("vendor", instrument.getProperty ("vendor"));
    instrumentObject->setProperty ("version", instrument.getProperty ("version"));
    instrumentObject->setProperty ("soundName", getPluginSoundName());
    instrumentObject->setProperty ("stateEncoding", "base64");
    instrumentObject->setProperty ("state", instrument.getProperty ("state"));
    instrumentObject->setProperty ("stateSha256", getPluginStateSha256());
    trackObject->setProperty ("instrument", instrumentJson);

    auto clip = makeObject();
    auto* clipObject = clip.getDynamicObject();
    clipObject->setProperty ("id", "loop-1");
    clipObject->setProperty ("startTick", 0);
    clipObject->setProperty ("lengthTicks", juce::roundToInt (getLoopLengthBeats() * projectPpq));
    clipObject->setProperty ("loopEnabled", true);

    juce::Array<juce::var> notesJson;
    for (const auto& note : getNotes())
    {
        auto noteJson = makeObject();
        auto* noteObject = noteJson.getDynamicObject();
        noteObject->setProperty ("id", note.id);
        noteObject->setProperty ("startTick", juce::roundToInt (note.beat * projectPpq));
        noteObject->setProperty ("lengthTicks", juce::roundToInt (note.lengthBeats * projectPpq));
        noteObject->setProperty ("midiNote", note.midiNote);
        noteObject->setProperty ("velocity", note.velocity);
        notesJson.add (noteJson);
    }
    clipObject->setProperty ("notes", notesJson);

    juce::Array<juce::var> clips;
    clips.add (clip);
    trackObject->setProperty ("clips", clips);

    juce::Array<juce::var> tracks;
    tracks.add (track);
    object->setProperty ("tracks", tracks);
    return result;
}

juce::Result SongProject::valueTreeFromJson (const juce::var& json, juce::ValueTree& destination)
{
    auto* rootObject = requireObject (json);
    if (rootObject == nullptr)
        return juce::Result::fail ("Project root must be a JSON object");

    int schemaVersion = 0;
    int sampleRate = 0;
    int ppq = 0;
    if (! readInt (*rootObject, "schemaVersion", schemaVersion) || schemaVersion != 1)
        return juce::Result::fail ("Only song-project schema version 1 is supported");
    if (! readInt (*rootObject, "sampleRate", sampleRate) || ! isSupportedSampleRate (sampleRate))
        return juce::Result::fail ("Project sampleRate is unsupported");
    if (! readInt (*rootObject, "ppq", ppq) || ppq != projectPpq)
        return juce::Result::fail ("Project ppq must be 960");

    const auto title = requireString (*rootObject, "title").trim();
    if (title.isEmpty())
        return juce::Result::fail ("Project title is required");

    const auto tempoMapValue = rootObject->getProperty ("tempoMap");
    const auto* tempoMap = tempoMapValue.getArray();
    if (tempoMap == nullptr || tempoMap->isEmpty())
        return juce::Result::fail ("Project tempoMap must contain an initial tempo");
    auto* tempoObject = requireObject (tempoMap->getReference (0));
    double tempo = 0.0;
    if (tempoObject == nullptr || ! readNumber (*tempoObject, "bpm", tempo) || tempo < 40.0 || tempo > 240.0)
        return juce::Result::fail ("Initial tempo must be between 40 and 240 BPM");

    const auto editorValue = rootObject->getProperty ("editor");
    auto* editorObject = requireObject (editorValue);
    double snap = 0.0;
    if (editorObject == nullptr || ! readNumber (*editorObject, "snapBeats", snap) || ! isSupportedSnap (snap))
        return juce::Result::fail ("Project snapBeats must be 0.125, 0.25, 0.5, or 1.0");

    const auto tracksValue = rootObject->getProperty ("tracks");
    const auto* tracks = tracksValue.getArray();
    if (tracks == nullptr || tracks->size() != 1)
        return juce::Result::fail ("This editor version requires exactly one instrument track");
    auto* trackObject = requireObject (tracks->getReference (0));
    if (trackObject == nullptr)
        return juce::Result::fail ("Track must be a JSON object");

    const auto instrumentValue = trackObject->getProperty ("instrument");
    auto* instrumentObject = requireObject (instrumentValue);
    if (instrumentObject == nullptr || requireString (*instrumentObject, "format") != "VST3")
        return juce::Result::fail ("Track instrument must be a VST3 object");
    const auto pluginIdentifier = requireString (*instrumentObject, "pluginIdentifier");
    const auto pluginName = requireString (*instrumentObject, "pluginName");
    auto soundName = requireString (*instrumentObject, "soundName").trim();
    if (soundName.isEmpty())
        soundName = "Project sound";
    if (soundName.length() > 80)
        return juce::Result::fail ("Project soundName must contain at most 80 characters");
    const auto encodedState = requireString (*instrumentObject, "state");
    const auto savedHash = requireString (*instrumentObject, "stateSha256");
    if (pluginIdentifier.isEmpty() || pluginName.isEmpty() || encodedState.isEmpty() || savedHash.isEmpty())
        return juce::Result::fail ("Project instrument identity and state are required");

    juce::MemoryBlock decodedState;
    if (! decodedState.fromBase64Encoding (encodedState) || decodedState.getSize() == 0)
        return juce::Result::fail ("Project instrument state is not valid Base64 data");
    if (! savedHash.equalsIgnoreCase (stateHash (decodedState)))
        return juce::Result::fail ("Project instrument state failed its SHA-256 integrity check");

    const auto clipsValue = trackObject->getProperty ("clips");
    const auto* clips = clipsValue.getArray();
    if (clips == nullptr || clips->size() != 1)
        return juce::Result::fail ("This editor version requires exactly one loop clip");
    auto* clipObject = requireObject (clips->getReference (0));
    int loopTicks = 0;
    if (clipObject == nullptr || ! readInt (*clipObject, "lengthTicks", loopTicks)
        || loopTicks < projectPpq * 4 || loopTicks > projectPpq * 32)
        return juce::Result::fail ("Loop length must be between 4 and 32 beats");

    const auto notesValue = clipObject->getProperty ("notes");
    const auto* notes = notesValue.getArray();
    if (notes == nullptr || notes->size() > static_cast<int> (maxSequenceNotes))
        return juce::Result::fail ("Project notes array is missing or exceeds 512 notes");

    juce::ValueTree loadedRoot (rootType);
    loadedRoot.setProperty ("schemaVersion", 1, nullptr);
    loadedRoot.setProperty ("title", title, nullptr);
    loadedRoot.setProperty ("sampleRate", sampleRate, nullptr);
    loadedRoot.setProperty ("tempoBpm", tempo, nullptr);
    loadedRoot.setProperty ("loopLengthBeats", static_cast<double> (loopTicks) / projectPpq, nullptr);
    loadedRoot.setProperty ("snapBeats", snap, nullptr);

    juce::ValueTree loadedInstrument (instrumentType);
    loadedInstrument.setProperty ("format", "VST3", nullptr);
    loadedInstrument.setProperty ("identifier", pluginIdentifier, nullptr);
    loadedInstrument.setProperty ("name", pluginName, nullptr);
    loadedInstrument.setProperty ("vendor", requireString (*instrumentObject, "vendor"), nullptr);
    loadedInstrument.setProperty ("version", requireString (*instrumentObject, "version"), nullptr);
    loadedInstrument.setProperty ("soundName", soundName, nullptr);
    loadedInstrument.setProperty ("stateEncoding", "base64", nullptr);
    loadedInstrument.setProperty ("state", encodedState, nullptr);
    loadedInstrument.setProperty ("stateSha256", savedHash.toLowerCase(), nullptr);
    loadedRoot.addChild (loadedInstrument, -1, nullptr);

    juce::ValueTree loadedNotes (notesType);
    std::set<juce::String> ids;
    for (const auto& noteValue : *notes)
    {
        auto* noteObject = requireObject (noteValue);
        if (noteObject == nullptr)
            return juce::Result::fail ("Every note must be a JSON object");

        const auto id = requireString (*noteObject, "id");
        int startTick = 0;
        int lengthTick = 0;
        int midiNote = 0;
        int velocity = 0;
        if (id.isEmpty() || ! ids.insert (id).second)
            return juce::Result::fail ("Every note id must be non-empty and unique");
        if (! readInt (*noteObject, "startTick", startTick) || startTick < 0 || startTick >= loopTicks)
            return juce::Result::fail ("Note startTick is outside the loop");
        if (! readInt (*noteObject, "lengthTicks", lengthTick) || lengthTick <= 0
            || startTick + lengthTick > loopTicks)
            return juce::Result::fail ("Note lengthTicks is outside the loop");
        if (! readInt (*noteObject, "midiNote", midiNote) || midiNote < 0 || midiNote > 127)
            return juce::Result::fail ("Note midiNote must be from 0 through 127");
        if (! readInt (*noteObject, "velocity", velocity) || velocity < 1 || velocity > 127)
            return juce::Result::fail ("Note velocity must be from 1 through 127");

        juce::ValueTree loadedNote (noteType);
        loadedNote.setProperty ("id", id, nullptr);
        loadedNote.setProperty ("beat", static_cast<double> (startTick) / projectPpq, nullptr);
        loadedNote.setProperty ("lengthBeats", static_cast<double> (lengthTick) / projectPpq, nullptr);
        loadedNote.setProperty ("midiNote", midiNote, nullptr);
        loadedNote.setProperty ("velocity", velocity, nullptr);
        loadedNotes.addChild (loadedNote, -1, nullptr);
    }
    loadedRoot.addChild (loadedNotes, -1, nullptr);

    destination = loadedRoot;
    return juce::Result::ok();
}

juce::Result SongProject::writePluginSoundSnapshot (const juce::String& soundName,
                                                     const juce::MemoryBlock& state,
                                                     juce::UndoManager* undo)
{
    const auto trimmed = soundName.trim().substring (0, 80);
    if (trimmed.isEmpty())
        return juce::Result::fail ("A sound snapshot name is required");
    if (state.getSize() == 0)
        return juce::Result::fail ("A sound snapshot cannot contain empty VST3 state");

    auto instrument = getInstrumentTree();
    if (! instrument.isValid())
        return juce::Result::fail ("The project instrument record is missing");

    instrument.setProperty ("soundName", trimmed, undo);
    instrument.setProperty ("state", state.toBase64Encoding(), undo);
    instrument.setProperty ("stateSha256", stateHash (state), undo);
    return juce::Result::ok();
}

void SongProject::installRoot (juce::ValueTree newRoot, bool shouldBeDirty)
{
    suppressChanges = true;
    root.removeListener (this);
    root = std::move (newRoot);
    root.addListener (this);
    undoManager.clearUndoHistory();
    suppressChanges = false;
    dirty = shouldBeDirty;

    if (changeCallback)
        changeCallback();
}

void SongProject::projectChanged()
{
    if (suppressChanges)
        return;

    dirty = true;
    if (changeCallback)
        changeCallback();
}

void SongProject::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) { projectChanged(); }
void SongProject::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)               { projectChanged(); }
void SongProject::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)       { projectChanged(); }
void SongProject::valueTreeChildOrderChanged (juce::ValueTree&, int, int)               { projectChanged(); }
void SongProject::valueTreeParentChanged (juce::ValueTree&)                             { projectChanged(); }
} // namespace resonance
