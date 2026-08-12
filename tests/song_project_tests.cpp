#include <JuceHeader.h>

#include "../src/edit_command.h"
#include "../src/plugin_identity.h"
#include "../src/song_project.h"
#include "../src/sound_shelf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
struct TestContext
{
    int assertions = 0;

    void expect (bool condition, const juce::String& message)
    {
        ++assertions;
        if (! condition)
            throw std::runtime_error (message.toStdString());
    }
};

juce::String argumentValue (const juce::StringArray& args, const juce::String& flag)
{
    const auto index = args.indexOf (flag);
    return index >= 0 && index + 1 < args.size() ? args[index + 1] : juce::String {};
}

void testEditingAndUndo (TestContext& context)
{
    resonance::SongProject project;
    context.expect (project.getSchemaVersion() == resonance::SongProject::currentSchemaVersion,
                    "A new song must use the current production schema");
    context.expect (project.getTrackId() == "track-1" && project.getClipId() == "loop-1",
                    "A new song must expose stable starter track and clip identities");
    const auto starterMixer = project.getTrackMixerSettings();
    const auto starterMidi = project.getTrackMidiRouting();
    context.expect (starterMixer.gainDecibels == 0.0 && starterMixer.pan == 0.0
                        && ! starterMixer.muted && ! starterMixer.solo,
                    "A new track must start at unity, centre, unmuted, and unsoloed");
    context.expect (starterMidi.inputChannel == 0 && starterMidi.outputChannel == 1,
                    "A new track must start with omni input and MIDI output channel one");

    project.beginUndoTransaction ("Change track mix and MIDI routing");
    context.expect (project.setTrackMixerSettings ({ -7.5, -0.25, true, true }).wasOk()
                        && project.setTrackMidiRouting ({ 4, 12 }).wasOk(),
                    "Bounded track mixer and MIDI settings must be editable");
    const auto editedMixer = project.getTrackMixerSettings();
    const auto editedMidi = project.getTrackMidiRouting();
    context.expect (editedMixer.gainDecibels == -7.5 && editedMixer.pan == -0.25
                        && editedMixer.muted && editedMixer.solo
                        && editedMidi.inputChannel == 4 && editedMidi.outputChannel == 12,
                    "Edited track settings must enter the authoritative model");
    context.expect (project.undo(), "One Undo must restore a grouped track-settings edit");
    const auto restoredMixer = project.getTrackMixerSettings();
    const auto restoredMidi = project.getTrackMidiRouting();
    context.expect (restoredMixer.gainDecibels == 0.0 && restoredMixer.pan == 0.0
                        && ! restoredMixer.muted && ! restoredMixer.solo
                        && restoredMidi.inputChannel == 0 && restoredMidi.outputChannel == 1,
                    "Track-settings Undo must restore every mixer and MIDI field");
    context.expect (project.setTrackMixerSettings ({ 13.0, 0.0, false, false }).failed()
                        && project.setTrackMidiRouting ({ 0, 0 }).failed(),
                    "Out-of-range track mixer and MIDI edits must fail closed");
    context.expect (project.getNotes().size() == 8, "A new song must contain the eight starter notes");
    context.expect (project.getLoopLengthBeats() == 8.0, "A new song must be two bars long");
    context.expect (project.getTempoBpm() == 120.0, "A new song must start at 120 BPM");

    project.beginUndoTransaction ("Add note");
    const auto id = project.addNote (1.5, 0.5, 67, 111);
    context.expect (id.isNotEmpty(), "Adding a valid note must return an id");
    context.expect (project.getNotes().size() == 9, "Adding a note must change the model");
    context.expect (project.undo(), "The add-note transaction must be undoable");
    context.expect (project.getNotes().size() == 8, "Undo must remove the added note");
    context.expect (project.redo(), "The add-note transaction must be redoable");
    context.expect (project.getNotes().size() == 9, "Redo must restore the added note");

    auto edited = project.findNote (id);
    context.expect (edited.has_value(), "The redone note must keep its stable id");
    const auto originalBeat = edited->beat;
    project.beginUndoTransaction ("Move note");
    edited->beat = 2.25;
    edited->midiNote = 70;
    edited->lengthBeats = 1.25;
    edited->velocity = 77;
    context.expect (project.updateNote (*edited), "Updating an existing note must succeed");
    auto moved = project.findNote (id);
    context.expect (moved->beat == 2.25 && moved->midiNote == 70 && moved->velocity == 77,
                    "Move, resize, pitch, and velocity must update together");
    context.expect (project.undo(), "The note edit must be one undo transaction");
    context.expect (project.findNote (id)->beat == originalBeat,
                    "Undo must restore the note's original position");

    project.beginUndoTransaction ("Delete note");
    context.expect (project.removeNote (id), "Deleting the selected note must succeed");
    context.expect (! project.findNote (id).has_value(), "Deleted note must leave the model");
    context.expect (project.undo(), "Delete must be undoable");
    context.expect (project.findNote (id).has_value(), "Undo must restore a deleted note");
}

void testLegacyProjectMigration (TestContext& context,
                                 const juce::File& fixtureFile,
                                 int& migratedBytes,
                                 juce::String& sourceSha256,
                                 juce::String& stableTrackId,
                                 juce::String& stableClipId)
{
    context.expect (fixtureFile.existsAsFile(), "The version-1 migration fixture must exist");

    juce::MemoryBlock sourceBytes;
    context.expect (fixtureFile.loadFileAsData (sourceBytes),
                    "The version-1 fixture bytes must be readable");
    sourceSha256 = juce::SHA256 (sourceBytes).toHexString();

    resonance::SongProject migrated;
    const auto migrationResult = migrated.loadFromFile (fixtureFile);
    context.expect (migrationResult.wasOk(),
                    "A valid version-1 project must migrate in memory: "
                        + migrationResult.getErrorMessage());
    context.expect (migrated.getSchemaVersion() == resonance::SongProject::currentSchemaVersion,
                    "A migrated project must use the current in-memory schema");
    stableTrackId = migrated.getTrackId();
    stableClipId = migrated.getClipId();
    context.expect (stableTrackId == "track-migrated" && stableClipId == "clip-migrated",
                    "Migration must preserve stable track and clip identities");
    context.expect (migrated.getTrackName() == "Legacy Lead",
                    "Migration must preserve the user-facing track name");

    const auto defaultMixer = migrated.getTrackMixerSettings();
    const auto defaultMidi = migrated.getTrackMidiRouting();
    context.expect (defaultMixer.gainDecibels == 0.0 && defaultMixer.pan == 0.0
                        && ! defaultMixer.muted && ! defaultMixer.solo,
                    "Version-1 tracks must receive the documented neutral mixer defaults");
    context.expect (defaultMidi.inputChannel == 0 && defaultMidi.outputChannel == 1,
                    "Version-1 tracks must receive omni-in/channel-one MIDI defaults");
    context.expect (migrated.getTitle() == "Legacy Migration Fixture"
                        && migrated.getTempoBpm() == 132.0
                        && migrated.findNote ("note-migrated").has_value(),
                    "Migration must preserve project, tempo, and note data");

    juce::MemoryBlock migratedState;
    context.expect (migrated.getPluginState (migratedState).wasOk()
                        && migratedState.getSize() == 4
                        && migrated.getPluginStateSha256()
                               == "054edec1d0211f624fed0cbca9d4f9400b0e491c43742af2c5b0abebf0c990d8",
                    "Migration must preserve the exact plug-in state payload and hash");

    juce::MemoryBlock sourceBytesAfterLoad;
    context.expect (fixtureFile.loadFileAsData (sourceBytesAfterLoad)
                        && sourceBytesAfterLoad == sourceBytes,
                    "Loading a version-1 project must not rewrite the source file");

    resonance::SeededVelocityVariation variation;
    variation.noteIds = { "note-migrated" };
    variation.seed = 61002;
    variation.maximumDelta = 4;
    resonance::EditCommand command;
    context.expect (resonance::resolveSeededVelocityVariation (migrated,
                                                               variation,
                                                               command).wasOk()
                        && command.trackId == stableTrackId
                        && command.clipId == stableClipId,
                    "Resolved edit commands must target migrated stable identities");
    resonance::EditCommandPreview preview;
    context.expect (resonance::createEditCommandPreview (command, migrated, preview).wasOk(),
                    "A command targeting migrated identities must preview successfully");
    command.trackId = "track-1";
    context.expect (resonance::createEditCommandPreview (command, migrated, preview).failed(),
                    "A default-id command must not target a differently identified project");

    const auto migratedFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("resonance-v1-migrated",
                                                            ".resonance.json",
                                                            false);
    context.expect (migrated.saveToFile (migratedFile).wasOk(),
                    "A migrated project must save as the current schema");
    migratedBytes = static_cast<int> (migratedFile.getSize());
    const auto migratedJsonText = migratedFile.loadFileAsString();
    auto migratedJson = juce::JSON::parse (migratedJsonText);
    auto* migratedRoot = migratedJson.getDynamicObject();
    auto* migratedTracks = migratedRoot != nullptr
                               ? migratedRoot->getProperty ("tracks").getArray()
                               : nullptr;
    auto* migratedTrack = migratedTracks != nullptr && migratedTracks->size() == 1
                              ? migratedTracks->getReference (0).getDynamicObject()
                              : nullptr;
    auto* migratedMixer = migratedTrack != nullptr
                              ? migratedTrack->getProperty ("mixer").getDynamicObject()
                              : nullptr;
    auto* migratedMidi = migratedTrack != nullptr
                             ? migratedTrack->getProperty ("midi").getDynamicObject()
                             : nullptr;
    context.expect (migratedRoot != nullptr
                        && static_cast<int> (migratedRoot->getProperty ("schemaVersion"))
                               == resonance::SongProject::currentSchemaVersion
                        && migratedTrack != nullptr && migratedMixer != nullptr
                        && migratedMidi != nullptr,
                    "A migrated save must materialise the complete current track contract");
    context.expect (migratedTrack->getProperty ("id").toString() == stableTrackId,
                    "A current-schema save must retain the migrated track id");

    resonance::SongProject reopened;
    context.expect (reopened.loadFromFile (migratedFile).wasOk()
                        && reopened.getTrackId() == stableTrackId
                        && reopened.getClipId() == stableClipId,
                    "The migrated current-schema file must reopen with stable identities");

    migratedMixer->setProperty ("gainDb", -6.0);
    migratedMixer->setProperty ("pan", 0.25);
    migratedMixer->setProperty ("mute", true);
    migratedMixer->setProperty ("solo", true);
    migratedMidi->setProperty ("inputChannel", 2);
    migratedMidi->setProperty ("outputChannel", 10);
    context.expect (migratedFile.replaceWithText (juce::JSON::toString (migratedJson, true)),
                    "A valid non-default current-schema mixer fixture must be writable");
    resonance::SongProject nonDefault;
    context.expect (nonDefault.loadFromFile (migratedFile).wasOk(),
                    "Bounded non-default mixer and MIDI values must load");
    const auto nonDefaultMixer = nonDefault.getTrackMixerSettings();
    const auto nonDefaultMidi = nonDefault.getTrackMidiRouting();
    context.expect (nonDefaultMixer.gainDecibels == -6.0 && nonDefaultMixer.pan == 0.25
                        && nonDefaultMixer.muted && nonDefaultMixer.solo
                        && nonDefaultMidi.inputChannel == 2
                        && nonDefaultMidi.outputChannel == 10,
                    "Current-schema mixer and MIDI values must round-trip exactly");

    const auto invalidFile = migratedFile.getSiblingFile (
        migratedFile.getFileNameWithoutExtension() + "-invalid.resonance.json");
    auto writeInvalidAndReject = [&] (juce::var invalidJson,
                                     const juce::String& message)
    {
        context.expect (invalidFile.replaceWithText (juce::JSON::toString (invalidJson, true)),
                        "An invalid migration fixture must be writable for rejection testing");
        resonance::SongProject rejected;
        context.expect (rejected.loadFromFile (invalidFile).failed(), message);
    };

    auto futureVersion = juce::JSON::parse (migratedJsonText);
    futureVersion.getDynamicObject()->setProperty (
        "schemaVersion", resonance::SongProject::currentSchemaVersion + 1);
    writeInvalidAndReject (futureVersion, "An unknown future project schema must be rejected");

    auto missingMixer = juce::JSON::parse (migratedJsonText);
    missingMixer.getDynamicObject()->getProperty ("tracks").getArray()->getReference (0)
        .getDynamicObject()->removeProperty ("mixer");
    writeInvalidAndReject (missingMixer, "A current-schema track without mixer state must be rejected");

    auto invalidPan = juce::JSON::parse (migratedJsonText);
    invalidPan.getDynamicObject()->getProperty ("tracks").getArray()->getReference (0)
        .getDynamicObject()->getProperty ("mixer").getDynamicObject()->setProperty ("pan", 1.5);
    writeInvalidAndReject (invalidPan, "An out-of-range current-schema pan value must be rejected");

    auto invalidMidi = juce::JSON::parse (migratedJsonText);
    invalidMidi.getDynamicObject()->getProperty ("tracks").getArray()->getReference (0)
        .getDynamicObject()->getProperty ("midi").getDynamicObject()->setProperty ("outputChannel", 0);
    writeInvalidAndReject (invalidMidi, "An invalid current-schema MIDI output channel must be rejected");

    context.expect (invalidFile.deleteFile(), "The invalid migration fixture must be removable");
    context.expect (migratedFile.deleteFile(), "The migrated current-schema fixture must be removable");
}

void testPreviousProjectMigration (TestContext& context,
                                   const juce::File& fixtureFile,
                                   juce::String& sourceSha256)
{
    context.expect (fixtureFile.existsAsFile(), "The version-2 migration fixture must exist");

    juce::MemoryBlock sourceBytes;
    context.expect (fixtureFile.loadFileAsData (sourceBytes),
                    "The version-2 fixture bytes must be readable");
    sourceSha256 = juce::SHA256 (sourceBytes).toHexString();

    resonance::SongProject migrated;
    const auto loadResult = migrated.loadFromFile (fixtureFile);
    context.expect (loadResult.wasOk(),
                    "A valid version-2 project must migrate in memory: "
                        + loadResult.getErrorMessage());
    context.expect (migrated.getSchemaVersion() == resonance::SongProject::currentSchemaVersion
                        && migrated.getTrackCount() == 1,
                    "Version-2 input must become a one-track current-schema model");
    context.expect (migrated.getTrackId() == "track-v2"
                        && migrated.getTrackName() == "Version 2 Keys"
                        && migrated.getClipId() == "clip-v2",
                    "Version-2 migration must preserve stable identity and name");

    const auto mixer = migrated.getTrackMixerSettings();
    const auto midi = migrated.getTrackMidiRouting();
    context.expect (mixer.gainDecibels == -4.5 && mixer.pan == 0.3
                        && ! mixer.muted && mixer.solo
                        && midi.inputChannel == 3 && midi.outputChannel == 7,
                    "Version-2 migration must preserve non-default mixer and MIDI state");
    context.expect (migrated.getTempoBpm() == 108.0
                        && migrated.getLoopLengthBeats() == 4.0
                        && migrated.findNote ("note-v2").has_value(),
                    "Version-2 migration must preserve timing and notes");

    juce::MemoryBlock state;
    context.expect (migrated.getPluginState (state).wasOk() && state.getSize() == 4
                        && migrated.getPluginStateSha256()
                               == "054edec1d0211f624fed0cbca9d4f9400b0e491c43742af2c5b0abebf0c990d8",
                    "Version-2 migration must preserve exact opaque state");

    juce::MemoryBlock sourceBytesAfterLoad;
    context.expect (fixtureFile.loadFileAsData (sourceBytesAfterLoad)
                        && sourceBytesAfterLoad == sourceBytes,
                    "Loading a version-2 project must not rewrite its source file");

    const auto migratedFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("resonance-v2-migrated",
                                                            ".resonance.json",
                                                            false);
    context.expect (migrated.saveToFile (migratedFile).wasOk(),
                    "A migrated version-2 project must save as the current schema");
    const auto saved = juce::JSON::parse (migratedFile.loadFileAsString());
    context.expect (saved.getDynamicObject() != nullptr
                        && static_cast<int> (
                               saved.getDynamicObject()->getProperty ("schemaVersion"))
                               == resonance::SongProject::currentSchemaVersion,
                    "An explicit save must materialise the current schema version");

    resonance::SongProject reopened;
    context.expect (reopened.loadFromFile (migratedFile).wasOk()
                        && reopened.getTrackId() == "track-v2"
                        && reopened.getTrackMixerSettings().gainDecibels == -4.5,
                    "The migrated schema-v3 project must reopen without drift");
    context.expect (migratedFile.deleteFile(),
                    "The temporary version-2 migration output must be removable");
}

void testTwoTrackTopology (TestContext& context)
{
    resonance::SongProject project;
    const std::array<unsigned char, 4> stateBytes { 4, 3, 2, 1 };
    const juce::MemoryBlock state (stateBytes.data(), stateBytes.size());
    project.setPluginMetadata ("VST3-Surge XT-test-190e4fbd",
                               "Surge XT",
                               "Surge Synth Team",
                               "1.3.4");
    project.setPluginState (state);

    const auto firstTrackId = project.getTrackId();
    const auto firstClipId = project.getClipId();
    const auto firstNotes = project.getNotes();
    juce::String secondTrackId;
    context.expect (project.duplicateActiveTrack (&secondTrackId).wasOk(),
                    "Duplicating the active instrument must add a second track");
    context.expect (project.getTrackCount() == 2 && project.getActiveTrackIndex() == 1
                        && secondTrackId.isNotEmpty() && secondTrackId != firstTrackId,
                    "A duplicated track must receive a stable unique id and become active");
    context.expect (project.getClipId (0) == firstClipId
                        && project.getClipId (1) != firstClipId
                        && project.getTrackMidiRouting (1).outputChannel == 2,
                    "A duplicated track must receive a unique clip id and next MIDI channel");

    const auto secondNotes = project.getNotes();
    bool noteIdsAreUnique = secondNotes.size() == firstNotes.size();
    for (const auto& second : secondNotes)
        noteIdsAreUnique = noteIdsAreUnique
                           && std::none_of (firstNotes.begin(),
                                            firstNotes.end(),
                                            [&second] (const resonance::SongNote& first)
                                            {
                                                return first.id == second.id;
                                            });
    context.expect (noteIdsAreUnique,
                    "Duplicated musical content must receive project-unique note ids");

    // The piano roll draws inactive-track notes as ghosts, so notes must be readable
    // by index without disturbing the active-track selection.
    const auto notesByIndexZero = project.getNotes (0);
    const auto notesByIndexOne = project.getNotes (1);
    context.expect (notesByIndexZero.size() == firstNotes.size()
                        && notesByIndexOne.size() == secondNotes.size()
                        && project.getActiveTrackIndex() == 1,
                    "Indexed note access must read either track without changing selection");
    bool indexedNotesMatchFirstTrack = notesByIndexZero.size() == firstNotes.size();
    for (std::size_t index = 0; index < notesByIndexZero.size(); ++index)
        indexedNotesMatchFirstTrack = indexedNotesMatchFirstTrack
                                      && notesByIndexZero[index].id == firstNotes[index].id
                                      && notesByIndexZero[index].midiNote == firstNotes[index].midiNote;
    context.expect (indexedNotesMatchFirstTrack,
                    "Indexed note access must return that track's exact notes");
    context.expect (project.getNotes (-1).empty() && project.getNotes (2).empty(),
                    "Indexed note access must return nothing for an out-of-range track");

    juce::MemoryBlock firstState;
    juce::MemoryBlock secondState;
    context.expect (project.getPluginStateForTrack (0, firstState).wasOk()
                        && project.getPluginStateForTrack (1, secondState).wasOk()
                        && firstState == state && secondState == state,
                    "Both tracks must own exact independent accepted state snapshots");

    project.beginUndoTransaction ("Set independent track mixes");
    context.expect (project.setTrackMixerSettingsForTrack (0, { -3.0, -0.5, false, false }).wasOk()
                        && project.setTrackMixerSettingsForTrack (1, { -9.0, 0.5, true, false }).wasOk(),
                    "Each track must accept bounded independent mixer state");
    context.expect (project.getTrackMixerSettings (0).gainDecibels == -3.0
                        && project.getTrackMixerSettings (1).gainDecibels == -9.0
                        && project.getTrackMixerSettings (1).muted,
                    "Track mixer edits must not alias across identities");
    // A third track is allowed from schema version 4 onward; testFourTrackCeiling owns
    // the capacity contract. Remove it again so the reorder cases below stay two-track.
    juce::String thirdTrackId;
    context.expect (project.duplicateActiveTrack (&thirdTrackId).wasOk()
                        && project.getTrackCount() == 3
                        && thirdTrackId != firstTrackId && thirdTrackId != secondTrackId,
                    "A third project track must be accepted with unique identity");
    context.expect (project.removeTrack (thirdTrackId).wasOk() && project.getTrackCount() == 2,
                    "Removing the third track must restore the two-track topology");
    context.expect (project.setActiveTrackIndex (1),
                    "The second track must be selectable after the third is removed");

    context.expect (project.moveTrack (secondTrackId, 0).wasOk()
                        && project.getTrackId (0) == secondTrackId,
                    "Track reorder must use stable identity rather than position");
    context.expect (project.undo() && project.getTrackId (0) == firstTrackId,
                    "One Undo must restore the prior track order");
    context.expect (project.redo() && project.getTrackId (0) == secondTrackId,
                    "One Redo must restore the reordered track");
    context.expect (project.undo() && project.getTrackId (0) == firstTrackId,
                    "The test must restore canonical order before persistence");

    project.beginUndoTransaction ("Shorten both loops");
    project.setLoopLengthBeats (4.0);
    auto tracksFitSharedLoop = true;
    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
    {
        const auto snapshot = project.createSequenceSnapshotForTrack (trackIndex);
        tracksFitSharedLoop = tracksFitSharedLoop && snapshot.loopBeats == 4.0;
        for (std::size_t noteIndex = 0; noteIndex < snapshot.noteCount; ++noteIndex)
            tracksFitSharedLoop = tracksFitSharedLoop
                                  && snapshot.notes[noteIndex].beat
                                         + snapshot.notes[noteIndex].lengthBeats <= 4.0 + 1.0e-9;
    }
    context.expect (tracksFitSharedLoop,
                    "Changing the shared loop must clamp notes on every track");

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("resonance-two-track",
                                                    ".resonance.json",
                                                    false);
    context.expect (project.saveToFile (file).wasOk(),
                    "A valid two-track project must save as the current schema version");
    const auto savedText = file.loadFileAsString();
    auto saved = juce::JSON::parse (savedText);
    auto* savedRoot = saved.getDynamicObject();
    auto* savedTracks = savedRoot != nullptr
                            ? savedRoot->getProperty ("tracks").getArray()
                            : nullptr;
    context.expect (savedRoot != nullptr && savedTracks != nullptr && savedTracks->size() == 2
                        && static_cast<int> (savedRoot->getProperty ("schemaVersion"))
                               == resonance::SongProject::currentSchemaVersion,
                    "The canonical writer must persist both tracks in the current schema version");

    resonance::SongProject reopened;
    context.expect (reopened.loadFromFile (file).wasOk() && reopened.getTrackCount() == 2
                        && reopened.getTrackId (0) == firstTrackId
                        && reopened.getTrackId (1) == secondTrackId,
                    "Two-track Save/Open must preserve count, identity, and order");
    context.expect (reopened.getTrackMixerSettings (0).gainDecibels == -3.0
                        && reopened.getTrackMixerSettings (1).gainDecibels == -9.0
                        && reopened.getTrackMixerSettings (1).muted,
                    "Two-track Save/Open must preserve independent mixer state");

    const auto invalidFile = file.getSiblingFile (
        file.getFileNameWithoutExtension() + "-invalid.resonance.json");
    auto writeInvalidAndReject = [&] (juce::var invalid, const juce::String& message)
    {
        context.expect (invalidFile.replaceWithText (juce::JSON::toString (invalid, true)),
                        "An invalid two-track fixture must be writable");
        resonance::SongProject rejected;
        context.expect (rejected.loadFromFile (invalidFile).failed(), message);
    };

    auto duplicateTrackId = juce::JSON::parse (savedText);
    auto* duplicateTrackArray = duplicateTrackId.getDynamicObject()->getProperty ("tracks").getArray();
    duplicateTrackArray->getReference (1).getDynamicObject()->setProperty (
        "id", duplicateTrackArray->getReference (0).getDynamicObject()->getProperty ("id"));
    writeInvalidAndReject (duplicateTrackId, "Duplicate track ids must be rejected");

    auto duplicateClipId = juce::JSON::parse (savedText);
    auto* duplicateClipTracks = duplicateClipId.getDynamicObject()->getProperty ("tracks").getArray();
    const auto firstSavedClipId = duplicateClipTracks->getReference (0).getDynamicObject()
                                      ->getProperty ("clips").getArray()->getReference (0)
                                      .getDynamicObject()->getProperty ("id");
    duplicateClipTracks->getReference (1).getDynamicObject()->getProperty ("clips")
        .getArray()->getReference (0).getDynamicObject()->setProperty ("id", firstSavedClipId);
    writeInvalidAndReject (duplicateClipId, "Duplicate clip ids must be rejected");

    auto duplicateNoteId = juce::JSON::parse (savedText);
    auto* duplicateNoteTracks = duplicateNoteId.getDynamicObject()->getProperty ("tracks").getArray();
    const auto firstSavedNoteId = duplicateNoteTracks->getReference (0).getDynamicObject()
                                      ->getProperty ("clips").getArray()->getReference (0)
                                      .getDynamicObject()->getProperty ("notes").getArray()
                                      ->getReference (0).getDynamicObject()->getProperty ("id");
    duplicateNoteTracks->getReference (1).getDynamicObject()->getProperty ("clips")
        .getArray()->getReference (0).getDynamicObject()->getProperty ("notes")
        .getArray()->getReference (0).getDynamicObject()->setProperty ("id", firstSavedNoteId);
    writeInvalidAndReject (duplicateNoteId, "Duplicate cross-track note ids must be rejected");

    auto mismatchedLoop = juce::JSON::parse (savedText);
    auto* mismatchTracks = mismatchedLoop.getDynamicObject()->getProperty ("tracks").getArray();
    mismatchTracks->getReference (1).getDynamicObject()->getProperty ("clips")
        .getArray()->getReference (0).getDynamicObject()->setProperty ("lengthTicks", 7680);
    writeInvalidAndReject (mismatchedLoop, "Different per-track loop lengths must be rejected");

    auto thirdTrack = juce::JSON::parse (savedText);
    auto* thirdTracks = thirdTrack.getDynamicObject()->getProperty ("tracks").getArray();
    thirdTracks->add (thirdTracks->getReference (1));
    writeInvalidAndReject (thirdTrack, "A third persisted track must be rejected");

    context.expect (project.removeTrack (firstTrackId).wasOk() && project.getTrackCount() == 1
                        && project.getTrackId() == secondTrackId,
                    "Removing one track must preserve the other stable identity");
    context.expect (project.undo() && project.getTrackCount() == 2,
                    "One Undo must restore a removed track");
    context.expect (project.redo() && project.getTrackCount() == 1,
                    "One Redo must remove the same track again");
    context.expect (project.removeTrack (secondTrackId).failed() && project.getTrackCount() == 1,
                    "Removing the final project track must fail closed");

    resonance::SongProject addUndo;
    addUndo.setPluginState (state);
    context.expect (addUndo.duplicateActiveTrack().wasOk() && addUndo.getTrackCount() == 2
                        && addUndo.undo() && addUndo.getTrackCount() == 1
                        && addUndo.redo() && addUndo.getTrackCount() == 2,
                    "Track add must be one reversible Undo/Redo transaction");

    context.expect (invalidFile.deleteFile(), "The invalid two-track fixture must be removable");
    context.expect (file.deleteFile(), "The valid two-track fixture must be removable");
}

void testSequenceSnapshot (TestContext& context)
{
    resonance::SongProject project;
    project.beginUndoTransaction ("Longer loop");
    project.setLoopLengthBeats (16.0);
    const auto snapshot = project.createSequenceSnapshot();
    context.expect (snapshot.loopBeats == 16.0, "Sequence snapshot must carry the editable loop length");
    context.expect (snapshot.noteCount == project.getNotes().size(), "Snapshot and model note counts must match");
    context.expect (snapshot.notes[0].midiNote == project.getNotes()[0].midiNote,
                    "Snapshot must preserve MIDI pitches");
    context.expect (snapshot.notes[0].velocity > 0.0f && snapshot.notes[0].velocity <= 1.0f,
                    "Snapshot velocity must be normalised for MIDI output");
}

void testRelocatedPluginIdentity (TestContext& context)
{
    constexpr int surgeVst3Uid = 420368317;
    const juce::String original { "VST3-Surge XT-bf38ca69-190e4fbd" };
    const juce::String relocated { "VST3-Surge XT-b793f78b-190e4fbd" };

    context.expect (resonance::vst3IdentifiersAreCompatible (original, original, surgeVst3Uid),
                    "An unchanged VST3 identifier must remain compatible");
    context.expect (resonance::vst3IdentifiersAreCompatible (original, relocated, surgeVst3Uid),
                    "Moving a VST3 bundle must not invalidate projects with the same immutable UID");
    context.expect (! resonance::vst3IdentifiersAreCompatible (
                        "VST3-Surge XT-bf38ca69-190e4fbe", relocated, surgeVst3Uid),
                    "A different VST3 UID must remain incompatible");
    context.expect (! resonance::vst3IdentifiersAreCompatible (
                        "AU-Surge XT-bf38ca69-190e4fbd", relocated, surgeVst3Uid),
                    "A non-VST3 identity must not pass VST3 relocation matching");
}

void testSoundSnapshotAndUndo (TestContext& context)
{
    resonance::SongProject project;
    const std::array<juce::uint8, 8> stateABytes { 1, 3, 5, 7, 9, 11, 13, 15 };
    const std::array<juce::uint8, 8> stateBBytes { 2, 4, 6, 8, 10, 12, 14, 16 };
    const juce::MemoryBlock stateA (stateABytes.data(), stateABytes.size());
    const juce::MemoryBlock stateB (stateBBytes.data(), stateBBytes.size());

    project.setPluginState (stateA);
    project.markClean();
    const auto stateAHash = project.getPluginStateSha256();
    context.expect (! project.isDirty(), "The accepted A snapshot can be marked clean");

    const auto apply = project.applyPluginSound ("Bright pluck", stateB);
    context.expect (apply.wasOk(), "A non-empty named sound B must apply successfully");
    context.expect (project.isDirty(), "Applying sound B must mark the song dirty");
    context.expect (project.getPluginSoundName() == "Bright pluck", "The applied sound name must enter the model");
    context.expect (project.getPluginStateSha256() != stateAHash, "Sound B must carry its own state hash");

    resonance::PluginSoundSnapshot applied;
    context.expect (project.getPluginSoundSnapshot (applied).wasOk(),
                    "The applied sound snapshot must pass its integrity check");
    context.expect (applied.name == "Bright pluck" && applied.state == stateB,
                    "The host-owned sound snapshot must return its exact name and bytes");

    context.expect (project.undo(), "Applying sound B must be one undoable transaction");
    resonance::PluginSoundSnapshot undone;
    context.expect (project.getPluginSoundSnapshot (undone).wasOk(), "Undo must leave a valid sound snapshot");
    context.expect (undone.state == stateA && undone.stateSha256 == stateAHash,
                    "Undo must restore the exact accepted A state");

    context.expect (project.redo(), "The sound transaction must be redoable");
    resonance::PluginSoundSnapshot redone;
    context.expect (project.getPluginSoundSnapshot (redone).wasOk(), "Redo must leave a valid sound snapshot");
    context.expect (redone.name == "Bright pluck" && redone.state == stateB,
                    "Redo must restore the exact named B state");

    context.expect (project.applyPluginSound (juce::String {}, stateB).failed(),
                    "An unnamed sound must be rejected");
    context.expect (project.applyPluginSound ("Empty", juce::MemoryBlock {}).failed(),
                    "An empty sound state must be rejected");
}

void testEditCommandFoundation (TestContext& context,
                                const juce::File& fixtureFile,
                                juce::String& candidateSha256)
{
    context.expect (fixtureFile.existsAsFile(), "The portable edit-command fixture must exist");

    resonance::SongProject project;
    const auto beforeHash = project.getContentSha256();
    context.expect (beforeHash.length() == 64, "A project content precondition must be a SHA-256 hash");
    context.expect (! project.isDirty(), "Creating a command precondition must not dirty the project");

    constexpr auto placeholderHash =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const auto commandJson = fixtureFile.loadFileAsString().replace (placeholderHash, beforeHash);
    resonance::EditCommand command;
    context.expect (resonance::parseEditCommand (commandJson, command).wasOk(),
                    "The version-1 note-patch fixture must parse");
    context.expect (command.commandVersion == 1 && command.operation == "editNotes",
                    "The parsed command must preserve its version and operation");
    context.expect (command.seed.has_value() && *command.seed == 18421,
                    "The deterministic seed must survive command parsing");
    context.expect (command.changes.size() == 3,
                    "The fixture must contain one update, one remove, and one add");

    resonance::EditCommand roundTrippedCommand;
    context.expect (resonance::parseEditCommand (resonance::serialiseEditCommand (command),
                                                 roundTrippedCommand).wasOk(),
                    "A parsed edit command must round-trip through canonical JSON");
    context.expect (roundTrippedCommand.projectContentSha256 == beforeHash
                        && roundTrippedCommand.seed == command.seed
                        && roundTrippedCommand.changes.size() == command.changes.size(),
                    "The command round trip must preserve its precondition, seed, and changes");

    resonance::EditCommandPreview preview;
    context.expect (resonance::createEditCommandPreview (command, project, preview).wasOk(),
                    "A valid command must create a candidate preview");
    context.expect (preview.isPending() && preview.getCandidateProject() != nullptr,
                    "A new preview must expose a pending candidate project");
    context.expect (project.getContentSha256() == beforeHash && ! project.isDirty(),
                    "Preview creation must not mutate or dirty the active project");
    context.expect (preview.beforeContentSha256 == beforeHash
                        && preview.afterContentSha256 != beforeHash,
                    "A preview must carry distinct before and after content hashes");
    context.expect (preview.noteDiffs.size() == 3,
                    "The candidate preview must expose one concrete diff per change");

    const auto* candidate = preview.getCandidateProject();
    context.expect (candidate->getNotes().size() == project.getNotes().size(),
                    "One removal plus one addition must preserve the note count");
    context.expect (! candidate->findNote ("note-2").has_value(),
                    "The candidate must contain the resolved removal");
    const auto added = candidate->findNote ("note-m5-fixture-1");
    context.expect (added.has_value() && added->midiNote == 67 && added->velocity == 88,
                    "The candidate must contain the exact resolved addition");
    const auto updated = candidate->findNote ("note-1");
    context.expect (updated.has_value() && updated->midiNote == 50
                        && updated->velocity == 100 && updated->lengthBeats == 0.75,
                    "The candidate must contain the exact resolved update");

    const auto expectedAfterHash = preview.afterContentSha256;
    candidateSha256 = expectedAfterHash;
    context.expect (preview.applyTo (project).wasOk(),
                    "Applying a current preview must succeed");
    context.expect (! preview.isPending() && preview.getCandidateProject() == nullptr,
                    "Apply must consume the preview exactly once");
    context.expect (project.getContentSha256() == expectedAfterHash && project.isDirty(),
                    "Apply must publish the exact previewed project and mark it dirty");
    context.expect (project.getUndoDescription() == "Apply edit: Tighten the opening motif",
                    "Apply must create one named Undo transaction");
    context.expect (preview.applyTo (project).failed(),
                    "An applied preview must reject a second Apply");
    context.expect (project.undo(), "The accepted command must be undoable in one action");
    context.expect (project.getContentSha256() == beforeHash,
                    "One Undo must restore the complete pre-command project");
    context.expect (project.redo(), "The accepted command must be redoable in one action");
    context.expect (project.getContentSha256() == expectedAfterHash,
                    "One Redo must restore the exact previewed candidate");

    resonance::SongProject rejectProject;
    auto rejectCommand = command;
    rejectCommand.projectContentSha256 = rejectProject.getContentSha256();
    resonance::EditCommandPreview rejectedPreview;
    context.expect (resonance::createEditCommandPreview (rejectCommand,
                                                         rejectProject,
                                                         rejectedPreview).wasOk(),
                    "A fresh project must accept the portable command fixture");
    const auto rejectHash = rejectProject.getContentSha256();
    context.expect (rejectedPreview.reject().wasOk(), "Reject must consume a pending preview");
    context.expect (rejectProject.getContentSha256() == rejectHash && ! rejectProject.isDirty(),
                    "Reject must leave the active project byte-semantically unchanged");
    context.expect (rejectedPreview.reject().failed()
                        && rejectedPreview.applyTo (rejectProject).failed(),
                    "A rejected preview must reject every later decision");

    resonance::SongProject staleProject;
    auto staleCommand = command;
    staleCommand.projectContentSha256 = staleProject.getContentSha256();
    staleProject.beginUndoTransaction ("Change tempo before preview");
    staleProject.setTempoBpm (121.0);
    resonance::EditCommandPreview stalePreview;
    context.expect (resonance::createEditCommandPreview (staleCommand,
                                                         staleProject,
                                                         stalePreview).failed(),
                    "A stale command must be rejected before preview");

    resonance::SongProject staleApplyProject;
    auto staleApplyCommand = command;
    staleApplyCommand.projectContentSha256 = staleApplyProject.getContentSha256();
    resonance::EditCommandPreview staleApplyPreview;
    context.expect (resonance::createEditCommandPreview (staleApplyCommand,
                                                         staleApplyProject,
                                                         staleApplyPreview).wasOk(),
                    "A current command must preview before an intervening edit");
    staleApplyProject.beginUndoTransaction ("Intervening edit");
    staleApplyProject.setTempoBpm (122.0);
    context.expect (staleApplyPreview.applyTo (staleApplyProject).failed(),
                    "An intervening project change must make Apply stale");
    context.expect (staleApplyPreview.isPending() && staleApplyPreview.reject().wasOk(),
                    "A stale Apply failure must leave the preview available to reject");

    resonance::SongProject legacyTimingProject;
    auto legacyTimingNote = legacyTimingProject.findNote ("note-1");
    context.expect (legacyTimingNote.has_value()
                        && std::abs (legacyTimingNote->lengthBeats - 0.82) < 1.0e-9,
                    "The accepted starter loop must retain its legacy 0.82-beat articulation");
    resonance::EditCommand legacyTimingCommand;
    legacyTimingCommand.projectContentSha256 = legacyTimingProject.getContentSha256();
    legacyTimingCommand.trackId = legacyTimingProject.getTrackId();
    legacyTimingCommand.clipId = legacyTimingProject.getClipId();
    legacyTimingCommand.summary = "Transpose without changing legacy timing";
    ++legacyTimingNote->midiNote;
    legacyTimingCommand.changes.push_back ({ resonance::NoteEditAction::update,
                                             legacyTimingNote->id,
                                             *legacyTimingNote });
    resonance::EditCommandPreview legacyTimingPreview;
    context.expect (resonance::createEditCommandPreview (legacyTimingCommand,
                                                         legacyTimingProject,
                                                         legacyTimingPreview).wasOk(),
                    "An update may preserve accepted legacy timing exactly");
    const auto preservedTimingNote = legacyTimingPreview.getCandidateProject()->findNote ("note-1");
    context.expect (preservedTimingNote.has_value()
                        && std::abs (preservedTimingNote->lengthBeats - 0.82) < 1.0e-9,
                    "A pitch-only proposal must not quantize the accepted note articulation");

    auto invalidLegacyTimingCommand = legacyTimingCommand;
    invalidLegacyTimingCommand.changes.front().note->lengthBeats = 0.821;
    context.expect (resonance::createEditCommandPreview (invalidLegacyTimingCommand,
                                                         legacyTimingProject,
                                                         legacyTimingPreview).failed(),
                    "Changed timing must still resolve to an integer tick at PPQ 960");

    resonance::SongProject invalidProject;
    auto unknownTarget = command;
    unknownTarget.projectContentSha256 = invalidProject.getContentSha256();
    unknownTarget.clipId = "missing-clip";
    resonance::EditCommandPreview invalidPreview;
    context.expect (resonance::createEditCommandPreview (unknownTarget,
                                                         invalidProject,
                                                         invalidPreview).failed(),
                    "An unknown clip target must be rejected");

    auto unknownNote = command;
    unknownNote.projectContentSha256 = invalidProject.getContentSha256();
    unknownNote.changes[0].noteId = "missing-note";
    unknownNote.changes[0].note->id = "missing-note";
    context.expect (resonance::createEditCommandPreview (unknownNote,
                                                         invalidProject,
                                                         invalidPreview).failed(),
                    "An unknown update note id must be rejected");

    auto invalidBounds = command;
    invalidBounds.projectContentSha256 = invalidProject.getContentSha256();
    invalidBounds.changes[0].note->lengthBeats = 1.0 / 960.0;
    context.expect (resonance::createEditCommandPreview (invalidBounds,
                                                         invalidProject,
                                                         invalidPreview).failed(),
                    "A command shorter than the active snap length must be rejected");

    auto duplicateTarget = command;
    duplicateTarget.projectContentSha256 = invalidProject.getContentSha256();
    duplicateTarget.changes.push_back (duplicateTarget.changes.front());
    context.expect (resonance::createEditCommandPreview (duplicateTarget,
                                                         invalidProject,
                                                         invalidPreview).failed(),
                    "A command that changes one note twice must be rejected");

    resonance::EditCommand invalidVersion;
    context.expect (resonance::parseEditCommand (
                        commandJson.replace ("\"commandVersion\": 1", "\"commandVersion\": 2"),
                        invalidVersion).failed(),
                    "An unsupported command version must be rejected during parsing");
}

void testSeededVelocityVariation (TestContext& context,
                                  juce::String& commandSha256,
                                  juce::String& candidateSha256)
{
    resonance::SongProject project;
    const auto beforeHash = project.getContentSha256();
    const auto notes = project.getNotes();
    std::vector<juce::String> reverseIds;
    for (auto iterator = notes.rbegin(); iterator != notes.rend(); ++iterator)
        reverseIds.push_back (iterator->id);

    resonance::SeededVelocityVariation variation;
    variation.noteIds = reverseIds;
    variation.seed = 18421;
    variation.maximumDelta = 8;

    resonance::EditCommand first;
    context.expect (resonance::resolveSeededVelocityVariation (project,
                                                               variation,
                                                               first).wasOk(),
                    "A bounded seeded velocity variation must resolve");
    context.expect (first.seed == variation.seed
                        && first.changes.size() == notes.size()
                        && first.projectContentSha256 == beforeHash,
                    "The resolved variation must retain its seed, targets, and project precondition");

    auto forwardVariation = variation;
    std::reverse (forwardVariation.noteIds.begin(), forwardVariation.noteIds.end());
    resonance::EditCommand reordered;
    context.expect (resonance::resolveSeededVelocityVariation (project,
                                                               forwardVariation,
                                                               reordered).wasOk()
                        && resonance::serialiseEditCommand (first)
                               == resonance::serialiseEditCommand (reordered),
                    "Target ordering must not change the resolved seeded command");

    const auto commandJson = resonance::serialiseEditCommand (first);
    const juce::MemoryBlock commandBytes (commandJson.toRawUTF8(),
                                           commandJson.getNumBytesAsUTF8());
    commandSha256 = juce::SHA256 (commandBytes).toHexString();

    resonance::EditCommandPreview firstPreview;
    resonance::EditCommandPreview repeatedPreview;
    context.expect (resonance::createEditCommandPreview (first,
                                                         project,
                                                         firstPreview).wasOk(),
                    "The resolved velocity command must create a candidate");
    context.expect (resonance::createEditCommandPreview (reordered,
                                                         project,
                                                         repeatedPreview).wasOk(),
                    "The canonically reordered command must create a candidate");
    candidateSha256 = firstPreview.afterContentSha256;
    context.expect (firstPreview.afterContentSha256 == repeatedPreview.afterContentSha256
                        && project.getContentSha256() == beforeHash
                        && ! project.isDirty(),
                    "The same seed must create the same isolated candidate without mutating A");

    auto everyChangeIsBounded = firstPreview.noteDiffs.size() == notes.size();
    for (const auto& diff : firstPreview.noteDiffs)
    {
        if (! diff.before.has_value() || ! diff.after.has_value())
        {
            everyChangeIsBounded = false;
            break;
        }

        const auto velocityDelta = std::abs (diff.after->velocity - diff.before->velocity);
        everyChangeIsBounded = everyChangeIsBounded
                               && diff.action == resonance::NoteEditAction::update
                               && velocityDelta >= 1
                               && velocityDelta <= variation.maximumDelta
                               && diff.after->beat == diff.before->beat
                               && diff.after->lengthBeats == diff.before->lengthBeats
                               && diff.after->midiNote == diff.before->midiNote;
    }
    context.expect (everyChangeIsBounded,
                    "The seeded transform must change only velocity within the declared bound");

    auto differentSeed = variation;
    differentSeed.seed = 18422;
    resonance::EditCommand differentCommand;
    resonance::EditCommandPreview differentPreview;
    context.expect (resonance::resolveSeededVelocityVariation (project,
                                                               differentSeed,
                                                               differentCommand).wasOk()
                        && resonance::createEditCommandPreview (differentCommand,
                                                               project,
                                                               differentPreview).wasOk()
                        && differentPreview.afterContentSha256 != candidateSha256,
                    "A different seed must resolve a different velocity candidate");

    auto invalid = variation;
    invalid.noteIds.clear();
    context.expect (resonance::resolveSeededVelocityVariation (project, invalid, first).failed(),
                    "A velocity transform without targets must be rejected");
    invalid = variation;
    invalid.seed = -1;
    context.expect (resonance::resolveSeededVelocityVariation (project, invalid, first).failed(),
                    "A negative velocity-transform seed must be rejected");
    invalid = variation;
    invalid.maximumDelta = 33;
    context.expect (resonance::resolveSeededVelocityVariation (project, invalid, first).failed(),
                    "An unbounded velocity delta must be rejected");
    invalid = variation;
    invalid.noteIds.push_back (invalid.noteIds.front());
    context.expect (resonance::resolveSeededVelocityVariation (project, invalid, first).failed(),
                    "Duplicate velocity-transform targets must be rejected");
    invalid = variation;
    invalid.noteIds.front() = "missing-note";
    context.expect (resonance::resolveSeededVelocityVariation (project, invalid, first).failed(),
                    "An unknown velocity-transform target must be rejected");

    context.expect (firstPreview.applyTo (project).wasOk()
                        && project.getContentSha256() == candidateSha256,
                    "The multi-note velocity candidate must apply as its exact preview");
    context.expect (project.undo() && project.getContentSha256() == beforeHash,
                    "One Undo must restore every velocity changed by the transform");
}

void testPriorProjectMigration (TestContext& context,
                                const juce::File& fixtureFile,
                                juce::String& sourceSha256)
{
    context.expect (fixtureFile.existsAsFile(), "The version-3 migration fixture must exist");

    juce::MemoryBlock sourceBytes;
    context.expect (fixtureFile.loadFileAsData (sourceBytes),
                    "The version-3 fixture bytes must be readable");
    sourceSha256 = juce::SHA256 (sourceBytes).toHexString();

    resonance::SongProject migrated;
    context.expect (migrated.loadFromFile (fixtureFile).wasOk(),
                    "A version-3 two-track project must load");
    context.expect (migrated.getSchemaVersion() == resonance::SongProject::currentSchemaVersion,
                    "A loaded version-3 project must become the current schema version in memory");
    context.expect (migrated.getTrackCount() == 2,
                    "Version-3 migration must preserve both persisted tracks");
    context.expect (migrated.getTrackId (0) == "track-v3-alpha"
                        && migrated.getTrackId (1) == "track-v3-beta"
                        && migrated.getClipId (0) == "clip-v3-alpha"
                        && migrated.getClipId (1) == "clip-v3-beta",
                    "Version-3 migration must preserve stable track and clip identity");

    // Non-default mixer and MIDI data must survive rather than resetting to neutral.
    const auto firstMixer = migrated.getTrackMixerSettings (0);
    const auto secondMixer = migrated.getTrackMixerSettings (1);
    context.expect (firstMixer.gainDecibels == -5.5 && firstMixer.pan == -0.4 && firstMixer.solo
                        && secondMixer.gainDecibels == -11.25 && secondMixer.pan == 0.7
                        && secondMixer.muted,
                    "Version-3 migration must preserve non-default mixer state");
    context.expect (migrated.getTrackMidiRouting (0).inputChannel == 3
                        && migrated.getTrackMidiRouting (0).outputChannel == 5
                        && migrated.getTrackMidiRouting (1).outputChannel == 9,
                    "Version-3 migration must preserve non-default MIDI routing");

    juce::MemoryBlock sourceAfterLoad;
    context.expect (fixtureFile.loadFileAsData (sourceAfterLoad)
                        && juce::SHA256 (sourceAfterLoad).toHexString() == sourceSha256,
                    "Loading a version-3 project must leave its source file byte-identical");
}

void testFourTrackCeiling (TestContext& context, bool& ceilingPassed)
{
    const auto before = context.assertions;
    resonance::SongProject project;
    juce::MemoryBlock state;
    const juce::String payload ("four-track-ceiling-state");
    state.append (payload.toRawUTF8(), payload.getNumBytesAsUTF8());
    project.setPluginMetadata ("VST3-Surge XT-b793f78b-190e4fbd", "Surge XT", "Surge Synth Team", "1.3.4");
    project.setPluginState (state);
    context.expect (project.getPluginStateSha256().isNotEmpty(),
                    "A ceiling fixture must accept accepted plug-in state");

    context.expect (resonance::SongProject::maxProjectTracks == 4,
                    "This editor version must publish a four-track ceiling");

    auto grown = true;
    for (int expected = 2; expected <= resonance::SongProject::maxProjectTracks; ++expected)
        grown = grown && project.duplicateActiveTrack().wasOk()
                && project.getTrackCount() == expected;
    context.expect (grown, "Tracks must be addable up to the published ceiling");

    context.expect (project.duplicateActiveTrack().failed()
                        && project.getTrackCount() == resonance::SongProject::maxProjectTracks,
                    "A track beyond the ceiling must fail closed");

    // Every identity class must stay unique across all four tracks.
    std::set<juce::String> trackIds;
    std::set<juce::String> clipIds;
    std::set<juce::String> noteIds;
    auto identitiesUnique = true;
    for (int index = 0; index < project.getTrackCount(); ++index)
    {
        identitiesUnique = identitiesUnique && trackIds.insert (project.getTrackId (index)).second
                           && clipIds.insert (project.getClipId (index)).second;
        for (const auto& note : project.getNotes (index))
            identitiesUnique = identitiesUnique && noteIds.insert (note.id).second;
    }
    context.expect (identitiesUnique,
                    "Four-track projects must keep track, clip, and note ids unique");

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("resonance-four-track", ".resonance.json", false);
    context.expect (project.saveToFile (file).wasOk(), "A four-track project must save");

    resonance::SongProject reopened;
    context.expect (reopened.loadFromFile (file).wasOk() && reopened.getTrackCount() == 4
                        && reopened.getSchemaVersion()
                               == resonance::SongProject::currentSchemaVersion,
                    "A four-track project must reopen as four current-schema tracks");
    context.expect (reopened.getContentSha256() == project.getContentSha256(),
                    "A four-track round trip must preserve exact project content");

    // A fifth persisted track must be refused at the loader, not only in the UI.
    const auto overfullText = file.loadFileAsString()
                                  .replace ("\"tracks\": [", "\"tracks\": [", false);
    auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* parsedRoot = parsed.getDynamicObject();
    auto* parsedTracks = parsedRoot != nullptr ? parsedRoot->getProperty ("tracks").getArray()
                                               : nullptr;
    context.expect (parsedTracks != nullptr && parsedTracks->size() == 4,
                    "The saved four-track document must contain four tracks");
    if (parsedTracks != nullptr)
    {
        auto extra = parsedTracks->getReference (0).clone();
        if (auto* extraObject = extra.getDynamicObject())
        {
            extraObject->setProperty ("id", "track-overflow");
            if (auto* extraClips = extraObject->getProperty ("clips").getArray())
                if (auto* extraClip = extraClips->getReference (0).getDynamicObject())
                    extraClip->setProperty ("id", "clip-overflow");
        }
        parsedTracks->add (extra);
    }
    const auto overfull = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("resonance-five-track", ".resonance.json", false);
    context.expect (overfull.replaceWithText (juce::JSON::toString (parsed, false)),
                    "An over-capacity fixture must be writable");
    resonance::SongProject refused;
    context.expect (refused.loadFromFile (overfull).failed(),
                    "A five-track document must fail closed at the loader");

    context.expect (file.deleteFile() && overfull.deleteFile(),
                    "Four-track fixtures must be removable");
    juce::ignoreUnused (overfullText);
    ceilingPassed = context.assertions > before;
}

resonance::SoundShelfEntry makeShelfEntry (const juce::String& name, const juce::String& payload)
{
    resonance::SoundShelfEntry entry;
    entry.name = name;
    entry.pluginIdentifier = "VST3-Surge XT-b793f78b-190e4fbd";
    entry.pluginName = "Surge XT";
    entry.vendor = "Surge Synth Team";
    entry.version = "1.3.4";
    entry.state.append (payload.toRawUTF8(), payload.getNumBytesAsUTF8());
    entry.stateSha256 = juce::SHA256 (entry.state).toHexString();
    return entry;
}

void testSoundShelf (TestContext& context, bool& shelfPassed, int& shelfBytes)
{
    const auto before = context.assertions;
    resonance::SoundShelf shelf;

    const auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getNonexistentChildFile ("resonance-shelf-absent", ".json", false);
    context.expect (shelf.loadFrom (missing).wasOk() && shelf.getEntryCount() == 0,
                    "A missing shelf file must load as an empty shelf rather than fail");

    context.expect (shelf.add (makeShelfEntry ("Warm pluck", "state-one")).wasOk()
                        && shelf.add (makeShelfEntry ("Deep kick", "state-two")).wasOk()
                        && shelf.getEntryCount() == 2,
                    "Distinct named sounds must be accepted");
    context.expect (shelf.add (makeShelfEntry ("  warm PLUCK  ", "state-three")).failed()
                        && shelf.getEntryCount() == 2,
                    "Shelf names must be unique ignoring case and surrounding space");
    context.expect (shelf.add (makeShelfEntry ("", "state-four")).failed(),
                    "A shelf sound must be named");
    context.expect (shelf.add (makeShelfEntry (juce::String::repeatedString ("x", 81),
                                               "state-five"))
                        .failed(),
                    "A shelf name longer than 80 characters must be refused");

    auto corrupt = makeShelfEntry ("Corrupt", "state-six");
    corrupt.stateSha256 = juce::String::repeatedString ("0", 64);
    context.expect (shelf.add (corrupt).failed() && shelf.getEntryCount() == 2,
                    "A shelf sound whose hash does not match its state must be refused");

    const auto* found = shelf.find ("deep KICK");
    context.expect (found != nullptr && found->name == "Deep kick",
                    "Shelf lookup must ignore case");

    const auto shelfFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getNonexistentChildFile ("resonance-shelf", ".json", false);
    context.expect (shelf.saveTo (shelfFile).wasOk() && shelfFile.existsAsFile(),
                    "A shelf must be writable");
    shelfBytes = static_cast<int> (shelfFile.getSize());

    resonance::SoundShelf reloaded;
    context.expect (reloaded.loadFrom (shelfFile).wasOk() && reloaded.getEntryCount() == 2,
                    "A saved shelf must reload");
    const auto* reloadedEntry = reloaded.find ("Warm pluck");
    const auto* originalEntry = shelf.find ("Warm pluck");
    context.expect (reloadedEntry != nullptr && originalEntry != nullptr
                        && reloadedEntry->state == originalEntry->state
                        && reloadedEntry->stateSha256.equalsIgnoreCase (originalEntry->stateSha256)
                        && reloadedEntry->pluginIdentifier == originalEntry->pluginIdentifier,
                    "Reloaded shelf sounds must carry exact state, hash, and identity");

    // A shelf that fails validation must not replace entries already in memory.
    const auto corruptFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getNonexistentChildFile ("resonance-shelf-bad", ".json", false);
    context.expect (corruptFile.replaceWithText ("{ \"schemaVersion\": 99, \"sounds\": [] }"),
                    "An unsupported shelf fixture must be writable");
    context.expect (reloaded.loadFrom (corruptFile).failed() && reloaded.getEntryCount() == 2,
                    "An unsupported shelf version must fail closed and preserve loaded sounds");
    context.expect (corruptFile.replaceWithText ("{ \"schemaVersion\": 1 }"),
                    "A shelf fixture without sounds must be writable");
    context.expect (reloaded.loadFrom (corruptFile).failed() && reloaded.getEntryCount() == 2,
                    "A shelf without a sounds array must fail closed");

    context.expect (shelf.remove ("DEEP kick").wasOk() && shelf.getEntryCount() == 1
                        && shelf.find ("Deep kick") == nullptr,
                    "Removing a shelf sound must ignore case and drop exactly one entry");
    context.expect (shelf.remove ("Deep kick").failed(),
                    "Removing an absent shelf sound must fail");

    resonance::SoundShelf full;
    auto capacityHeld = true;
    for (std::size_t index = 0; index < resonance::SoundShelf::maximumEntries; ++index)
        capacityHeld = capacityHeld
                       && full.add (makeShelfEntry ("Sound " + juce::String (static_cast<int> (index)),
                                                    "payload " + juce::String (static_cast<int> (index))))
                              .wasOk();
    context.expect (capacityHeld
                        && full.add (makeShelfEntry ("One too many", "overflow")).failed()
                        && full.getEntryCount()
                               == static_cast<int> (resonance::SoundShelf::maximumEntries),
                    "The shelf must fail closed at its capacity");

    context.expect (shelfFile.deleteFile() && corruptFile.deleteFile(),
                    "Shelf fixtures must be removable");

    shelfPassed = context.assertions > before;
}

void testRoundTrip (TestContext& context, int& savedBytes, juce::String& stateSha)
{
    resonance::SongProject project;
    project.setTitle ("Round Trip Theme");
    project.setTempoBpm (146.0);
    project.setLoopLengthBeats (16.0);
    project.setSnapBeats (0.125);
    project.setSampleRate (44100);
    project.setPluginMetadata ("VST3-Surge XT-bf38ca69-190e4fbd",
                               "Surge XT",
                               "Surge Synth Team",
                               "1.3.4");

    const std::array<juce::uint8, 12> stateBytes { 0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144 };
    juce::MemoryBlock state (stateBytes.data(), stateBytes.size());
    context.expect (project.applyPluginSound ("Round Trip Pluck", state).wasOk(),
                    "A named sound must be accepted before round trip");
    stateSha = project.getPluginStateSha256();

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("resonance-song-project-test", ".resonance.json", false);
    const auto saveResult = project.saveToFile (file);
    context.expect (saveResult.wasOk(), "A complete song project must save successfully");
    context.expect (file.existsAsFile(), "Saving must create the selected project file");
    savedBytes = static_cast<int> (file.getSize());
    context.expect (savedBytes > 500, "Saved JSON must contain the complete project contract");

    resonance::SongProject loaded;
    const auto loadResult = loaded.loadFromFile (file);
    context.expect (loadResult.wasOk(), "The saved song must reopen successfully");
    context.expect (loaded.getTitle() == "Round Trip Theme", "Title must survive save and reopen");
    context.expect (loaded.getTempoBpm() == 146.0, "Tempo must survive save and reopen");
    context.expect (loaded.getLoopLengthBeats() == 16.0, "Loop length must survive save and reopen");
    context.expect (loaded.getSnapBeats() == 0.125, "Grid snap must survive save and reopen");
    context.expect (loaded.getSampleRate() == 44100, "Sample rate must survive save and reopen");
    context.expect (loaded.getPluginIdentifier() == "VST3-Surge XT-bf38ca69-190e4fbd",
                    "The saved plugin identifier must survive save and reopen");
    context.expect (loaded.getPluginSoundName() == "Round Trip Pluck",
                    "The host-owned sound name must survive save and reopen");
    context.expect (loaded.getNotes().size() == project.getNotes().size(),
                    "All notes must survive save and reopen");

    juce::MemoryBlock restoredState;
    const auto stateResult = loaded.getPluginState (restoredState);
    context.expect (stateResult.wasOk(), "Reopened plugin state must pass its integrity check");
    context.expect (restoredState == state, "Reopened plugin state must be byte-exact");
    context.expect (loaded.getPluginStateSha256() == stateSha, "Saved state hash must round-trip exactly");

    auto compatibilityJson = juce::JSON::parse (file.loadFileAsString());
    auto* compatibilityRoot = compatibilityJson.getDynamicObject();
    auto* compatibilityTracks = compatibilityRoot != nullptr
                                    ? compatibilityRoot->getProperty ("tracks").getArray()
                                    : nullptr;
    auto* compatibilityTrack = compatibilityTracks != nullptr && ! compatibilityTracks->isEmpty()
                            ? compatibilityTracks->getReference (0).getDynamicObject()
                            : nullptr;
    auto* compatibilityInstrument = compatibilityTrack != nullptr
                                 ? compatibilityTrack->getProperty ("instrument").getDynamicObject()
                                 : nullptr;
    context.expect (compatibilityInstrument != nullptr,
                    "The round-trip fixture must expose an instrument object");
    compatibilityInstrument->removeProperty ("soundName");
    const auto compatibilityFile = file.getSiblingFile (file.getFileNameWithoutExtension()
                                                  + "-legacy.resonance.json");
    context.expect (compatibilityFile.replaceWithText (
                        juce::JSON::toString (compatibilityJson, true)),
                    "A project without the optional soundName must be writable");
    resonance::SongProject compatibilityLoaded;
    context.expect (compatibilityLoaded.loadFromFile (compatibilityFile).wasOk(),
                    "A project without soundName must still load");
    context.expect (compatibilityLoaded.getPluginSoundName() == "Project sound",
                    "A project without soundName must receive the documented fallback name");
    context.expect (compatibilityFile.deleteFile(),
                    "The temporary sound-name compatibility project must be removable");
    context.expect (file.deleteFile(), "The temporary round-trip project must be removable");
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args (argv + 1, argc - 1);
    const auto reportPath = argumentValue (args, "--report");
    const auto editCommandFixturePath = argumentValue (args, "--edit-command-fixture");
    const auto legacyProjectFixturePath = argumentValue (args, "--legacy-project-fixture");
    const auto previousProjectFixturePath = argumentValue (args, "--previous-project-fixture");
    const auto priorProjectFixturePath = argumentValue (args, "--prior-project-fixture");
    TestContext context;
    int savedBytes = 0;
    juce::String stateSha;
    juce::String editCommandCandidateSha;
    juce::String seededVelocityCommandSha;
    juce::String seededVelocityCandidateSha;
    int migratedBytes = 0;
    juce::String legacySourceSha;
    juce::String stableTrackId;
    juce::String stableClipId;
    bool legacyMigrationPassed = false;
    juce::String previousSourceSha;
    bool previousMigrationPassed = false;
    bool twoTrackTopologyPassed = false;
    bool soundShelfPassed = false;
    juce::String priorSourceSha;
    bool priorMigrationPassed = false;
    bool fourTrackCeilingPassed = false;
    int soundShelfBytes = 0;

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
    reportObject->setProperty ("editCommandVersion", resonance::EditCommand::supportedVersion);
    reportObject->setProperty ("editCommandFixture", juce::File (editCommandFixturePath).getFileName());
    reportObject->setProperty ("projectSchemaVersion", resonance::SongProject::currentSchemaVersion);
    reportObject->setProperty ("legacySchemaVersion", resonance::SongProject::legacySchemaVersion);
    reportObject->setProperty ("previousSchemaVersion", resonance::SongProject::previousSchemaVersion);
    reportObject->setProperty ("priorSchemaVersion", resonance::SongProject::priorSchemaVersion);
    reportObject->setProperty ("priorMigrationFixture",
                               juce::File (priorProjectFixturePath).getFileName());
    reportObject->setProperty ("legacyMigrationFixture",
                               juce::File (legacyProjectFixturePath).getFileName());
    reportObject->setProperty ("previousMigrationFixture",
                               juce::File (previousProjectFixturePath).getFileName());
    reportObject->setProperty ("maxProjectTracks", resonance::SongProject::maxProjectTracks);

    try
    {
        testEditingAndUndo (context);
        testSequenceSnapshot (context);
        testRelocatedPluginIdentity (context);
        testSoundSnapshotAndUndo (context);
        testSoundShelf (context, soundShelfPassed, soundShelfBytes);
        testLegacyProjectMigration (context,
                                    juce::File (legacyProjectFixturePath),
                                    migratedBytes,
                                    legacySourceSha,
                                    stableTrackId,
                                    stableClipId);
        legacyMigrationPassed = true;
        testPreviousProjectMigration (context,
                                      juce::File (previousProjectFixturePath),
                                      previousSourceSha);
        previousMigrationPassed = true;
        testPriorProjectMigration (context,
                                   juce::File (priorProjectFixturePath),
                                   priorSourceSha);
        priorMigrationPassed = true;
        testTwoTrackTopology (context);
        twoTrackTopologyPassed = true;
        testFourTrackCeiling (context, fourTrackCeilingPassed);
        testEditCommandFoundation (context,
                                   juce::File (editCommandFixturePath),
                                   editCommandCandidateSha);
        testSeededVelocityVariation (context,
                                     seededVelocityCommandSha,
                                     seededVelocityCandidateSha);
        testRoundTrip (context, savedBytes, stateSha);
        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("roundTripBytes", savedBytes);
        reportObject->setProperty ("stateSha256", stateSha);
        reportObject->setProperty ("editCommandCandidateSha256", editCommandCandidateSha);
        reportObject->setProperty ("seededVelocitySeed", 18421);
        reportObject->setProperty ("seededVelocityMaximumDelta", 8);
        reportObject->setProperty ("seededVelocityCommandSha256", seededVelocityCommandSha);
        reportObject->setProperty ("seededVelocityCandidateSha256", seededVelocityCandidateSha);
        reportObject->setProperty ("legacyMigrationPassed", legacyMigrationPassed);
        reportObject->setProperty ("legacySourceSha256", legacySourceSha);
        reportObject->setProperty ("previousMigrationPassed", previousMigrationPassed);
        reportObject->setProperty ("previousSourceSha256", previousSourceSha);
        reportObject->setProperty ("twoTrackTopologyPassed", twoTrackTopologyPassed);
        reportObject->setProperty ("priorMigrationPassed", priorMigrationPassed);
        reportObject->setProperty ("priorSourceSha256", priorSourceSha);
        reportObject->setProperty ("fourTrackCeilingPassed", fourTrackCeilingPassed);
        reportObject->setProperty ("soundShelfPassed", soundShelfPassed);
        reportObject->setProperty ("soundShelfBytes", soundShelfBytes);
        reportObject->setProperty ("migratedRoundTripBytes", migratedBytes);
        reportObject->setProperty ("stableTrackId", stableTrackId);
        reportObject->setProperty ("stableClipId", stableClipId);
        reportObject->setProperty ("passed", true);
    }
    catch (const std::exception& error)
    {
        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("roundTripBytes", savedBytes);
        reportObject->setProperty ("stateSha256", stateSha);
        reportObject->setProperty ("editCommandCandidateSha256", editCommandCandidateSha);
        reportObject->setProperty ("seededVelocitySeed", 18421);
        reportObject->setProperty ("seededVelocityMaximumDelta", 8);
        reportObject->setProperty ("seededVelocityCommandSha256", seededVelocityCommandSha);
        reportObject->setProperty ("seededVelocityCandidateSha256", seededVelocityCandidateSha);
        reportObject->setProperty ("legacyMigrationPassed", legacyMigrationPassed);
        reportObject->setProperty ("legacySourceSha256", legacySourceSha);
        reportObject->setProperty ("previousMigrationPassed", previousMigrationPassed);
        reportObject->setProperty ("previousSourceSha256", previousSourceSha);
        reportObject->setProperty ("twoTrackTopologyPassed", twoTrackTopologyPassed);
        reportObject->setProperty ("priorMigrationPassed", priorMigrationPassed);
        reportObject->setProperty ("priorSourceSha256", priorSourceSha);
        reportObject->setProperty ("fourTrackCeilingPassed", fourTrackCeilingPassed);
        reportObject->setProperty ("soundShelfPassed", soundShelfPassed);
        reportObject->setProperty ("soundShelfBytes", soundShelfBytes);
        reportObject->setProperty ("migratedRoundTripBytes", migratedBytes);
        reportObject->setProperty ("stableTrackId", stableTrackId);
        reportObject->setProperty ("stableClipId", stableClipId);
        reportObject->setProperty ("passed", false);
        reportObject->setProperty ("error", error.what());
    }

    if (reportPath.isNotEmpty())
    {
        const juce::File reportFile (reportPath);
        reportFile.getParentDirectory().createDirectory();
        if (! reportFile.replaceWithText (juce::JSON::toString (report, true)))
            return 2;
    }

    std::cout << juce::JSON::toString (report, true) << std::endl;
    return static_cast<bool> (reportObject->getProperty ("passed")) ? 0 : 1;
}
