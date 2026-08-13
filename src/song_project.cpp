#include "song_project.h"

#include <cmath>
#include <limits>
#include <set>

namespace resonance
{
namespace
{
constexpr auto rootType = "songProject";
constexpr auto trackType = "track";
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

bool readBool (juce::DynamicObject& object, const juce::Identifier& property, bool& destination)
{
    if (! object.hasProperty (property))
        return false;

    const auto value = object.getProperty (property);
    if (! value.isBool())
        return false;

    destination = static_cast<bool> (value);
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
    newRoot.setProperty ("schemaVersion", currentSchemaVersion, nullptr);
    newRoot.setProperty ("title", "Untitled", nullptr);
    newRoot.setProperty ("sampleRate", 48000, nullptr);
    newRoot.setProperty ("tempoBpm", 120.0, nullptr);
    newRoot.setProperty ("loopLengthBeats", loopLengthBeats, nullptr);
    newRoot.setProperty ("snapBeats", 0.25, nullptr);
    juce::ValueTree track (trackType);
    track.setProperty ("id", "track-1", nullptr);
    track.setProperty ("name", "Surge XT", nullptr);
    track.setProperty ("clipId", "loop-1", nullptr);
    track.setProperty ("gainDecibels", 0.0, nullptr);
    track.setProperty ("pan", 0.0, nullptr);
    track.setProperty ("muted", false, nullptr);
    track.setProperty ("solo", false, nullptr);
    track.setProperty ("midiInputChannel", 0, nullptr);
    track.setProperty ("midiOutputChannel", 1, nullptr);

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
    track.addChild (instrument, -1, nullptr);

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
    track.addChild (notes, -1, nullptr);
    newRoot.addChild (track, -1, nullptr);

    installRoot (newRoot, true, "track-1");
}

void SongProject::replaceWith (const SongProject& other)
{
    installRoot (other.root.createCopy(), false, other.getTrackId());
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
    const auto safeLength = juce::jlimit (minimumLoopBeats, maximumLoopBeats, beats);
    root.setProperty ("loopLengthBeats", safeLength, &undoManager);

    const auto minimumLength = getSnapBeats();
    for (int trackIndex = 0; trackIndex < getTrackCount(); ++trackIndex)
    {
        auto notes = getNotesTree (trackIndex);
        for (int noteIndex = 0; noteIndex < notes.getNumChildren(); ++noteIndex)
        {
            auto note = notes.getChild (noteIndex);
            auto beat = static_cast<double> (note.getProperty ("beat"));
            auto length = static_cast<double> (note.getProperty ("lengthBeats"));
            beat = juce::jlimit (0.0, juce::jmax (0.0, safeLength - minimumLength), beat);
            length = juce::jlimit (minimumLength, safeLength - beat, length);
            note.setProperty ("beat", beat, &undoManager);
            note.setProperty ("lengthBeats", length, &undoManager);
        }
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

int SongProject::getSchemaVersion() const
{
    return static_cast<int> (root.getProperty ("schemaVersion", currentSchemaVersion));
}

int SongProject::getTrackCount() const noexcept
{
    return root.getNumChildren();
}

int SongProject::getActiveTrackIndex() const noexcept
{
    for (int index = 0; index < getTrackCount(); ++index)
        if (getTrackId (index) == activeTrackId)
            return index;

    return getTrackCount() > 0 ? 0 : -1;
}

bool SongProject::setActiveTrackIndex (int trackIndex)
{
    const auto track = getTrackTree (trackIndex);
    if (! track.isValid())
        return false;

    const auto selectedId = track.getProperty ("id").toString();
    if (selectedId == activeTrackId)
        return true;

    activeTrackId = selectedId;
    if (changeCallback)
        changeCallback();
    return true;
}

juce::String SongProject::getTrackId() const
{
    return getTrackId (getActiveTrackIndex());
}

juce::String SongProject::getTrackId (int trackIndex) const
{
    return getTrackTree (trackIndex).getProperty ("id").toString();
}

juce::String SongProject::getTrackName() const
{
    return getTrackName (getActiveTrackIndex());
}

juce::String SongProject::getTrackName (int trackIndex) const
{
    const auto name = getTrackTree (trackIndex).getProperty ("name").toString().trim();
    return name.isNotEmpty() ? name : getPluginName (trackIndex);
}

juce::String SongProject::getClipId() const
{
    return getClipId (getActiveTrackIndex());
}

juce::String SongProject::getClipId (int trackIndex) const
{
    return getTrackTree (trackIndex).getProperty ("clipId").toString();
}

TrackMixerSettings SongProject::getTrackMixerSettings() const
{
    return getTrackMixerSettings (getActiveTrackIndex());
}

TrackMixerSettings SongProject::getTrackMixerSettings (int trackIndex) const
{
    const auto track = getTrackTree (trackIndex);
    return { static_cast<double> (track.getProperty ("gainDecibels", 0.0)),
             static_cast<double> (track.getProperty ("pan", 0.0)),
             static_cast<bool> (track.getProperty ("muted", false)),
             static_cast<bool> (track.getProperty ("solo", false)) };
}

juce::Result SongProject::setTrackMixerSettings (const TrackMixerSettings& settings)
{
    return setTrackMixerSettingsForTrack (getActiveTrackIndex(), settings);
}

juce::Result SongProject::setTrackMixerSettingsForTrack (
    int trackIndex,
    const TrackMixerSettings& settings)
{
    if (! std::isfinite (settings.gainDecibels)
        || settings.gainDecibels < -60.0 || settings.gainDecibels > 12.0
        || ! std::isfinite (settings.pan) || settings.pan < -1.0 || settings.pan > 1.0)
        return juce::Result::fail ("Track mixer gain or pan is outside the supported range");

    auto track = getTrackTree (trackIndex);
    if (! track.isValid())
        return juce::Result::fail ("The selected track does not exist");

    track.setProperty ("gainDecibels", settings.gainDecibels, &undoManager);
    track.setProperty ("pan", settings.pan, &undoManager);
    track.setProperty ("muted", settings.muted, &undoManager);
    track.setProperty ("solo", settings.solo, &undoManager);
    return juce::Result::ok();
}

TrackMidiRouting SongProject::getTrackMidiRouting() const
{
    return getTrackMidiRouting (getActiveTrackIndex());
}

TrackMidiRouting SongProject::getTrackMidiRouting (int trackIndex) const
{
    const auto track = getTrackTree (trackIndex);
    return { static_cast<int> (track.getProperty ("midiInputChannel", 0)),
             static_cast<int> (track.getProperty ("midiOutputChannel", 1)) };
}

juce::Result SongProject::setTrackMidiRouting (const TrackMidiRouting& routing)
{
    return setTrackMidiRoutingForTrack (getActiveTrackIndex(), routing);
}

juce::Result SongProject::setTrackMidiRoutingForTrack (int trackIndex,
                                                       const TrackMidiRouting& routing)
{
    if (routing.inputChannel < 0 || routing.inputChannel > 16
        || routing.outputChannel < 1 || routing.outputChannel > 16)
        return juce::Result::fail ("Track MIDI routing is outside the supported channel range");

    auto track = getTrackTree (trackIndex);
    if (! track.isValid())
        return juce::Result::fail ("The selected track does not exist");

    track.setProperty ("midiInputChannel", routing.inputChannel, &undoManager);
    track.setProperty ("midiOutputChannel", routing.outputChannel, &undoManager);
    return juce::Result::ok();
}

juce::Result SongProject::addTrackWithIdentity (const juce::String& trackId,
                                                const juce::String& name)
{
    const auto cleanTrackId = trackId.trim();
    if (cleanTrackId.isEmpty())
        return juce::Result::fail ("A new track requires a non-empty id");
    if (getTrackCount() >= maxProjectTracks)
        return juce::Result::fail ("The current project supports at most " + juce::String (maxProjectTracks)
                                   + " instrument tracks");

    const auto clipId = cleanTrackId + "-clip";
    for (int index = 0; index < getTrackCount(); ++index)
        if (getTrackId (index) == cleanTrackId || getClipId (index) == clipId)
            return juce::Result::fail ("A track or clip already uses the id " + cleanTrackId);

    const auto source = getActiveTrackTree();
    if (! source.isValid())
        return juce::Result::fail ("The active track does not exist");

    auto copy = source.createCopy();
    copy.setProperty ("id", cleanTrackId, nullptr);
    copy.setProperty ("name", name.trim().isNotEmpty() ? name.trim() : cleanTrackId, nullptr);
    copy.setProperty ("clipId", clipId, nullptr);
    copy.setProperty ("midiOutputChannel", juce::jlimit (1, 16, getTrackCount() + 1), nullptr);

    auto notes = copy.getChildWithName (notesType);
    for (int index = 0; index < notes.getNumChildren(); ++index)
        notes.getChild (index).setProperty ("id",
                                            cleanTrackId + "-note-" + juce::String (index + 1),
                                            nullptr);

    // Deliberately does not open its own undo transaction. This is composed inside a
    // larger edit-command application, and starting one here would split that command
    // into two undo steps.
    root.addChild (copy, -1, &undoManager);
    activeTrackId = cleanTrackId;
    projectChanged();
    return juce::Result::ok();
}

juce::Result SongProject::duplicateActiveTrack (juce::String* createdTrackId)
{
    if (getTrackCount() >= maxProjectTracks)
        return juce::Result::fail ("The current project supports at most " + juce::String (maxProjectTracks)
                                   + " instrument tracks");

    const auto source = getActiveTrackTree();
    if (! source.isValid())
        return juce::Result::fail ("The active track does not exist");

    auto copy = source.createCopy();
    const auto newTrackId = "track-" + juce::Uuid().toString();
    copy.setProperty ("id", newTrackId, nullptr);
    copy.setProperty ("name", getTrackName() + " 2", nullptr);
    copy.setProperty ("clipId", "clip-" + juce::Uuid().toString(), nullptr);
    copy.setProperty ("midiOutputChannel", juce::jlimit (1, 16, getTrackCount() + 1), nullptr);

    auto notes = copy.getChildWithName (notesType);
    for (int index = 0; index < notes.getNumChildren(); ++index)
        notes.getChild (index).setProperty ("id", "note-" + juce::Uuid().toString(), nullptr);

    beginUndoTransaction ("Add instrument track");
    root.addChild (copy, -1, &undoManager);
    activeTrackId = newTrackId;
    if (createdTrackId != nullptr)
        *createdTrackId = newTrackId;
    if (changeCallback)
        changeCallback();
    return juce::Result::ok();
}

juce::Result SongProject::removeTrack (const juce::String& trackId)
{
    if (getTrackCount() <= 1)
        return juce::Result::fail ("A song must retain at least one instrument track");

    const auto target = findTrackTree (trackId);
    if (! target.isValid())
        return juce::Result::fail ("The track to remove does not exist");

    juce::String replacementId = activeTrackId;
    if (trackId == activeTrackId)
    {
        for (int index = 0; index < getTrackCount(); ++index)
            if (getTrackId (index) != trackId)
            {
                replacementId = getTrackId (index);
                break;
            }
    }

    beginUndoTransaction ("Remove instrument track");
    root.removeChild (target, &undoManager);
    activeTrackId = replacementId;
    ensureActiveTrack();
    if (changeCallback)
        changeCallback();
    return juce::Result::ok();
}

juce::Result SongProject::moveTrack (const juce::String& trackId, int newIndex)
{
    const auto track = findTrackTree (trackId);
    if (! track.isValid())
        return juce::Result::fail ("The track to move does not exist");
    if (newIndex < 0 || newIndex >= getTrackCount())
        return juce::Result::fail ("The requested track position is outside the project");

    const auto currentIndex = root.indexOf (track);
    if (currentIndex == newIndex)
        return juce::Result::ok();

    beginUndoTransaction ("Reorder instrument track");
    root.moveChild (currentIndex, newIndex, &undoManager);
    return juce::Result::ok();
}

std::vector<SongNote> SongProject::getNotes() const
{
    return getNotes (getActiveTrackIndex());
}

std::vector<SongNote> SongProject::getNotes (int trackIndex) const
{
    std::vector<SongNote> result;
    auto notes = getNotesTree (trackIndex);
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

juce::Result SongProject::insertNote (const SongNote& note)
{
    auto notes = getNotesTree();
    if (! notes.isValid())
        return juce::Result::fail ("The project notes collection is missing");
    if (notes.getNumChildren() >= static_cast<int> (maxSequenceNotes))
        return juce::Result::fail ("The project already contains the maximum number of notes");
    if (note.id.isEmpty())
        return juce::Result::fail ("A note id is required");
    if (findNoteTree (note.id).isValid())
        return juce::Result::fail ("The note id already exists: " + note.id);
    if (! std::isfinite (note.beat) || ! std::isfinite (note.lengthBeats))
        return juce::Result::fail ("Note timing must be finite");
    if (note.beat < 0.0 || note.beat >= getLoopLengthBeats())
        return juce::Result::fail ("Note start is outside the loop");
    if (note.lengthBeats <= 0.0
        || note.beat + note.lengthBeats > getLoopLengthBeats() + 1.0e-9)
        return juce::Result::fail ("Note length is outside the loop");
    if (note.midiNote < 0 || note.midiNote > 127)
        return juce::Result::fail ("Note MIDI pitch must be from 0 through 127");
    if (note.velocity < 1 || note.velocity > 127)
        return juce::Result::fail ("Note velocity must be from 1 through 127");

    juce::ValueTree added (noteType);
    added.setProperty ("id", note.id, nullptr);
    added.setProperty ("beat", note.beat, nullptr);
    added.setProperty ("lengthBeats", note.lengthBeats, nullptr);
    added.setProperty ("midiNote", note.midiNote, nullptr);
    added.setProperty ("velocity", note.velocity, nullptr);
    notes.addChild (added, -1, &undoManager);
    return juce::Result::ok();
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

juce::String SongProject::getContentSha256() const
{
    auto content = toJsonValue();
    if (auto* object = content.getDynamicObject())
        object->removeProperty ("editorVersion");

    const auto canonicalJson = juce::JSON::toString (content, false);
    const juce::MemoryBlock bytes (canonicalJson.toRawUTF8(),
                                   canonicalJson.getNumBytesAsUTF8());
    return juce::SHA256 (bytes).toHexString().toLowerCase();
}

void SongProject::setPluginMetadata (const juce::String& identifier,
                                     const juce::String& name,
                                     const juce::String& vendor,
                                     const juce::String& version)
{
    setPluginMetadataForTrack (getActiveTrackIndex(), identifier, name, vendor, version);
}

void SongProject::setPluginMetadataForTrack (int trackIndex,
                                             const juce::String& identifier,
                                             const juce::String& name,
                                             const juce::String& vendor,
                                             const juce::String& version)
{
    auto instrument = getInstrumentTree (trackIndex);
    if (! instrument.isValid())
        return;

    instrument.setProperty ("identifier", identifier, nullptr);
    instrument.setProperty ("name", name, nullptr);
    instrument.setProperty ("vendor", vendor, nullptr);
    instrument.setProperty ("version", version, nullptr);
}

juce::String SongProject::getPluginIdentifier() const
{
    return getPluginIdentifier (getActiveTrackIndex());
}

juce::String SongProject::getPluginIdentifier (int trackIndex) const
{
    return getInstrumentTree (trackIndex).getProperty ("identifier").toString();
}

juce::String SongProject::getPluginName() const
{
    return getPluginName (getActiveTrackIndex());
}

juce::String SongProject::getPluginName (int trackIndex) const
{
    return getInstrumentTree (trackIndex).getProperty ("name").toString();
}

juce::String SongProject::getPluginSoundName() const
{
    return getPluginSoundName (getActiveTrackIndex());
}

juce::String SongProject::getPluginSoundName (int trackIndex) const
{
    const auto name = getInstrumentTree (trackIndex).getProperty ("soundName").toString().trim();
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
    return getPluginStateForTrack (getActiveTrackIndex(), state);
}

juce::Result SongProject::getPluginStateForTrack (int trackIndex,
                                                  juce::MemoryBlock& state) const
{
    const auto instrument = getInstrumentTree (trackIndex);
    if (! instrument.isValid())
        return juce::Result::fail ("The selected track instrument is missing");

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
    return getPluginStateSha256 (getActiveTrackIndex());
}

juce::String SongProject::getPluginStateSha256 (int trackIndex) const
{
    return getInstrumentTree (trackIndex).getProperty ("stateSha256").toString();
}

juce::Result SongProject::getPluginSoundSnapshot (PluginSoundSnapshot& snapshot) const
{
    return getPluginSoundSnapshotForTrack (getActiveTrackIndex(), snapshot);
}

juce::Result SongProject::getPluginSoundSnapshotForTrack (
    int trackIndex,
    PluginSoundSnapshot& snapshot) const
{
    juce::MemoryBlock state;
    const auto result = getPluginStateForTrack (trackIndex, state);
    if (result.failed())
        return result;

    if (state.getSize() == 0)
        return juce::Result::fail ("The project contains an empty Surge XT sound snapshot");

    snapshot.name = getPluginSoundName (trackIndex);
    snapshot.state = std::move (state);
    snapshot.stateSha256 = getPluginStateSha256 (trackIndex).toLowerCase();
    return juce::Result::ok();
}

SequenceSnapshot SongProject::createSequenceSnapshot() const
{
    return createSequenceSnapshotForTrack (getActiveTrackIndex());
}

SequenceSnapshot SongProject::createSequenceSnapshotForTrack (int trackIndex) const
{
    SequenceSnapshot snapshot;
    snapshot.loopBeats = getLoopLengthBeats();

    const auto notesTree = getNotesTree (trackIndex);
    snapshot.noteCount = juce::jmin (static_cast<std::size_t> (notesTree.getNumChildren()),
                                    maxSequenceNotes);
    for (std::size_t index = 0; index < snapshot.noteCount; ++index)
    {
        const auto note = notesTree.getChild (static_cast<int> (index));
        snapshot.notes[index] = {
            static_cast<double> (note.getProperty ("beat")),
            static_cast<double> (note.getProperty ("lengthBeats")),
            static_cast<int> (note.getProperty ("midiNote")),
            static_cast<float> (static_cast<int> (note.getProperty ("velocity"))) / 127.0f
        };
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
    if (changed)
        ensureActiveTrack();
    if (changed && changeCallback)
        changeCallback();
    return changed;
}

bool SongProject::redo()
{
    const auto changed = undoManager.redo();
    if (changed)
        ensureActiveTrack();
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

juce::ValueTree SongProject::getTrackTree (int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= getTrackCount())
        return {};
    return root.getChild (trackIndex);
}

juce::ValueTree SongProject::findTrackTree (const juce::String& trackId) const
{
    for (int index = 0; index < getTrackCount(); ++index)
    {
        const auto track = getTrackTree (index);
        if (track.getProperty ("id").toString() == trackId)
            return track;
    }
    return {};
}

juce::ValueTree SongProject::getActiveTrackTree() const
{
    const auto selected = findTrackTree (activeTrackId);
    return selected.isValid() ? selected : getTrackTree (0);
}

juce::ValueTree SongProject::getNotesTree() const
{
    return getActiveTrackTree().getChildWithName (notesType);
}

juce::ValueTree SongProject::getNotesTree (int trackIndex) const
{
    return getTrackTree (trackIndex).getChildWithName (notesType);
}

juce::ValueTree SongProject::getInstrumentTree() const
{
    return getActiveTrackTree().getChildWithName (instrumentType);
}

juce::ValueTree SongProject::getInstrumentTree (int trackIndex) const
{
    return getTrackTree (trackIndex).getChildWithName (instrumentType);
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
    object->setProperty ("schemaVersion", currentSchemaVersion);
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

    juce::Array<juce::var> tracks;
    for (int trackIndex = 0; trackIndex < getTrackCount(); ++trackIndex)
    {
        auto track = makeObject();
        auto* trackObject = track.getDynamicObject();
        trackObject->setProperty ("id", getTrackId (trackIndex));
        trackObject->setProperty ("name", getTrackName (trackIndex));
        trackObject->setProperty ("role", "instrument");

        const auto mixerSettings = getTrackMixerSettings (trackIndex);
        auto mixer = makeObject();
        mixer.getDynamicObject()->setProperty ("gainDb", mixerSettings.gainDecibels);
        mixer.getDynamicObject()->setProperty ("pan", mixerSettings.pan);
        mixer.getDynamicObject()->setProperty ("mute", mixerSettings.muted);
        mixer.getDynamicObject()->setProperty ("solo", mixerSettings.solo);
        trackObject->setProperty ("mixer", mixer);

        const auto midiRouting = getTrackMidiRouting (trackIndex);
        auto midi = makeObject();
        midi.getDynamicObject()->setProperty ("inputChannel", midiRouting.inputChannel);
        midi.getDynamicObject()->setProperty ("outputChannel", midiRouting.outputChannel);
        trackObject->setProperty ("midi", midi);

        auto instrumentJson = makeObject();
        auto* instrumentObject = instrumentJson.getDynamicObject();
        const auto instrument = getInstrumentTree (trackIndex);
        instrumentObject->setProperty ("format", "VST3");
        instrumentObject->setProperty ("pluginIdentifier", getPluginIdentifier (trackIndex));
        instrumentObject->setProperty ("pluginName", getPluginName (trackIndex));
        instrumentObject->setProperty ("vendor", instrument.getProperty ("vendor"));
        instrumentObject->setProperty ("version", instrument.getProperty ("version"));
        instrumentObject->setProperty ("soundName", getPluginSoundName (trackIndex));
        instrumentObject->setProperty ("stateEncoding", "base64");
        instrumentObject->setProperty ("state", instrument.getProperty ("state"));
        instrumentObject->setProperty ("stateSha256", getPluginStateSha256 (trackIndex));
        trackObject->setProperty ("instrument", instrumentJson);

        auto clip = makeObject();
        auto* clipObject = clip.getDynamicObject();
        clipObject->setProperty ("id", getClipId (trackIndex));
        clipObject->setProperty ("startTick", 0);
        clipObject->setProperty ("lengthTicks",
                                 juce::roundToInt (getLoopLengthBeats() * projectPpq));
        clipObject->setProperty ("loopEnabled", true);

        juce::Array<juce::var> notesJson;
        const auto notes = getNotesTree (trackIndex);
        for (int noteIndex = 0; noteIndex < notes.getNumChildren(); ++noteIndex)
        {
            const auto note = notes.getChild (noteIndex);
            auto noteJson = makeObject();
            auto* noteObject = noteJson.getDynamicObject();
            noteObject->setProperty ("id", note.getProperty ("id"));
            noteObject->setProperty (
                "startTick",
                juce::roundToInt (static_cast<double> (note.getProperty ("beat")) * projectPpq));
            noteObject->setProperty (
                "lengthTicks",
                juce::roundToInt (static_cast<double> (note.getProperty ("lengthBeats"))
                                  * projectPpq));
            noteObject->setProperty ("midiNote", note.getProperty ("midiNote"));
            noteObject->setProperty ("velocity", note.getProperty ("velocity"));
            notesJson.add (noteJson);
        }
        clipObject->setProperty ("notes", notesJson);

        juce::Array<juce::var> clips;
        clips.add (clip);
        trackObject->setProperty ("clips", clips);
        tracks.add (track);
    }
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
    if (! readInt (*rootObject, "schemaVersion", schemaVersion)
        || schemaVersion < legacySchemaVersion
        || schemaVersion > currentSchemaVersion)
        return juce::Result::fail ("Only song-project schema versions 1 through "
                                   + juce::String (currentSchemaVersion) + " are supported");
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
    if (tracks != nullptr && tracks->size() > 1)
    {
        if (schemaVersion < multiTrackSchemaVersion)
            return juce::Result::fail ("Song-project schema versions 1 and 2 hold exactly one track");
        if (tracks->size() > maxProjectTracks)
            return juce::Result::fail ("This editor version supports at most "
                                       + juce::String (maxProjectTracks) + " instrument tracks");

        // Each track is validated in isolation through the ordinary single-track path,
        // so a multi-track project cannot relax any per-track rule.
        const auto trackCount = tracks->size();
        auto parseSingleTrack = [&json, trackCount] (int trackIndex, juce::ValueTree& parsedRoot)
        {
            juce::var copy;
            const auto copyResult = juce::JSON::parse (juce::JSON::toString (json, false), copy);
            if (copyResult.failed())
                return copyResult;

            auto* copyRoot = requireObject (copy);
            auto* copyTracks = copyRoot != nullptr
                                   ? copyRoot->getProperty ("tracks").getArray()
                                   : nullptr;
            if (copyTracks == nullptr || copyTracks->size() != trackCount)
                return juce::Result::fail ("The multi-track project could not be isolated for validation");

            const auto selectedTrack = copyTracks->getReference (trackIndex);
            juce::Array<juce::var> isolatedTracks;
            isolatedTracks.add (selectedTrack);
            copyRoot->setProperty ("tracks", isolatedTracks);
            return SongProject::valueTreeFromJson (copy, parsedRoot);
        };

        std::vector<juce::ValueTree> parsedRoots;
        parsedRoots.reserve (static_cast<std::size_t> (trackCount));
        for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            juce::ValueTree parsedRoot;
            const auto parseResult = parseSingleTrack (trackIndex, parsedRoot);
            if (parseResult.failed())
                return parseResult;
            parsedRoots.push_back (parsedRoot);
        }

        std::set<juce::String> trackIds;
        std::set<juce::String> clipIds;
        std::set<juce::String> noteIds;
        const auto sharedLoop = static_cast<double> (parsedRoots.front().getProperty ("loopLengthBeats"));
        for (const auto& parsedRoot : parsedRoots)
        {
            const auto parsedTrack = parsedRoot.getChild (0);
            if (! trackIds.insert (parsedTrack.getProperty ("id").toString()).second)
                return juce::Result::fail ("Track ids must be unique within the project");
            if (! clipIds.insert (parsedTrack.getProperty ("clipId").toString()).second)
                return juce::Result::fail ("Clip ids must be unique within the project");
            if (std::abs (static_cast<double> (parsedRoot.getProperty ("loopLengthBeats")) - sharedLoop)
                > 1.0e-9)
                return juce::Result::fail ("Every track must share the same loop length");

            const auto parsedNotes = parsedTrack.getChildWithName (notesType);
            for (int noteIndex = 0; noteIndex < parsedNotes.getNumChildren(); ++noteIndex)
            {
                const auto id = parsedNotes.getChild (noteIndex).getProperty ("id").toString();
                if (! noteIds.insert (id).second)
                    return juce::Result::fail ("Note ids must be unique across project tracks");
            }
        }

        auto combined = parsedRoots.front();
        for (std::size_t index = 1; index < parsedRoots.size(); ++index)
            combined.addChild (parsedRoots[index].getChild (0).createCopy(), -1, nullptr);

        destination = combined;
        return juce::Result::ok();
    }

    if (tracks == nullptr || tracks->size() != 1)
        return juce::Result::fail ("This editor version requires one through "
                                   + juce::String (maxProjectTracks) + " instrument tracks");
    auto* trackObject = requireObject (tracks->getReference (0));
    if (trackObject == nullptr)
        return juce::Result::fail ("Track must be a JSON object");

    const auto trackId = requireString (*trackObject, "id").trim();
    const auto trackName = requireString (*trackObject, "name").trim();
    if (trackId.isEmpty() || trackName.isEmpty()
        || requireString (*trackObject, "role") != "instrument")
        return juce::Result::fail ("Track id, name, and instrument role are required");

    TrackMixerSettings mixerSettings;
    TrackMidiRouting midiRouting;
    if (schemaVersion >= previousSchemaVersion)
    {
        auto* mixerObject = requireObject (trackObject->getProperty ("mixer"));
        if (mixerObject == nullptr
            || ! readNumber (*mixerObject, "gainDb", mixerSettings.gainDecibels)
            || mixerSettings.gainDecibels < -60.0 || mixerSettings.gainDecibels > 12.0
            || ! readNumber (*mixerObject, "pan", mixerSettings.pan)
            || mixerSettings.pan < -1.0 || mixerSettings.pan > 1.0
            || ! readBool (*mixerObject, "mute", mixerSettings.muted)
            || ! readBool (*mixerObject, "solo", mixerSettings.solo))
            return juce::Result::fail ("Track mixer must contain bounded gainDb, pan, mute, and solo values");

        auto* midiObject = requireObject (trackObject->getProperty ("midi"));
        if (midiObject == nullptr
            || ! readInt (*midiObject, "inputChannel", midiRouting.inputChannel)
            || midiRouting.inputChannel < 0 || midiRouting.inputChannel > 16
            || ! readInt (*midiObject, "outputChannel", midiRouting.outputChannel)
            || midiRouting.outputChannel < 1 || midiRouting.outputChannel > 16)
            return juce::Result::fail ("Track MIDI routing must use input channel 0 through 16 and output channel 1 through 16");
    }

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
    const auto clipId = clipObject != nullptr ? requireString (*clipObject, "id").trim()
                                              : juce::String {};
    int clipStartTick = -1;
    bool loopEnabled = false;
    int loopTicks = 0;
    if (clipObject == nullptr || clipId.isEmpty()
        || ! readInt (*clipObject, "startTick", clipStartTick) || clipStartTick != 0
        || ! readBool (*clipObject, "loopEnabled", loopEnabled) || ! loopEnabled
        || ! readInt (*clipObject, "lengthTicks", loopTicks)
        || loopTicks < projectPpq * static_cast<int> (minimumLoopBeats)
        || loopTicks > projectPpq * static_cast<int> (maximumLoopBeats))
        return juce::Result::fail ("Loop clip id, start, enabled state, and length are invalid");

    const auto notesValue = clipObject->getProperty ("notes");
    const auto* notes = notesValue.getArray();
    if (notes == nullptr || notes->size() > static_cast<int> (maxSequenceNotes))
        return juce::Result::fail ("Project notes array is missing or exceeds 512 notes");

    juce::ValueTree loadedRoot (rootType);
    loadedRoot.setProperty ("schemaVersion", currentSchemaVersion, nullptr);
    loadedRoot.setProperty ("title", title, nullptr);
    loadedRoot.setProperty ("sampleRate", sampleRate, nullptr);
    loadedRoot.setProperty ("tempoBpm", tempo, nullptr);
    loadedRoot.setProperty ("loopLengthBeats", static_cast<double> (loopTicks) / projectPpq, nullptr);
    loadedRoot.setProperty ("snapBeats", snap, nullptr);

    juce::ValueTree loadedTrack (trackType);
    loadedTrack.setProperty ("id", trackId, nullptr);
    loadedTrack.setProperty ("name", trackName, nullptr);
    loadedTrack.setProperty ("clipId", clipId, nullptr);
    loadedTrack.setProperty ("gainDecibels", mixerSettings.gainDecibels, nullptr);
    loadedTrack.setProperty ("pan", mixerSettings.pan, nullptr);
    loadedTrack.setProperty ("muted", mixerSettings.muted, nullptr);
    loadedTrack.setProperty ("solo", mixerSettings.solo, nullptr);
    loadedTrack.setProperty ("midiInputChannel", midiRouting.inputChannel, nullptr);
    loadedTrack.setProperty ("midiOutputChannel", midiRouting.outputChannel, nullptr);

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
    loadedTrack.addChild (loadedInstrument, -1, nullptr);

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
    loadedTrack.addChild (loadedNotes, -1, nullptr);
    loadedRoot.addChild (loadedTrack, -1, nullptr);

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

void SongProject::installRoot (juce::ValueTree newRoot,
                               bool shouldBeDirty,
                               const juce::String& preferredActiveTrackId)
{
    suppressChanges = true;
    root.removeListener (this);
    root = std::move (newRoot);
    root.addListener (this);
    undoManager.clearUndoHistory();
    activeTrackId = preferredActiveTrackId;
    ensureActiveTrack();
    suppressChanges = false;
    dirty = shouldBeDirty;

    if (changeCallback)
        changeCallback();
}

void SongProject::ensureActiveTrack()
{
    if (findTrackTree (activeTrackId).isValid())
        return;

    activeTrackId = getTrackCount() > 0 ? getTrackId (0) : juce::String {};
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
