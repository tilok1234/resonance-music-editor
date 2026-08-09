#include <JuceHeader.h>

#include "../src/edit_command.h"
#include "../src/plugin_identity.h"
#include "../src/song_project.h"

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
                    "A migrated save must materialise the complete version-2 track contract");
    context.expect (migratedTrack->getProperty ("id").toString() == stableTrackId,
                    "A version-2 save must retain the migrated track id");

    resonance::SongProject reopened;
    context.expect (reopened.loadFromFile (migratedFile).wasOk()
                        && reopened.getTrackId() == stableTrackId
                        && reopened.getClipId() == stableClipId,
                    "The migrated version-2 file must reopen with stable identities");

    migratedMixer->setProperty ("gainDb", -6.0);
    migratedMixer->setProperty ("pan", 0.25);
    migratedMixer->setProperty ("mute", true);
    migratedMixer->setProperty ("solo", true);
    migratedMidi->setProperty ("inputChannel", 2);
    migratedMidi->setProperty ("outputChannel", 10);
    context.expect (migratedFile.replaceWithText (juce::JSON::toString (migratedJson, true)),
                    "A valid non-default version-2 mixer fixture must be writable");
    resonance::SongProject nonDefault;
    context.expect (nonDefault.loadFromFile (migratedFile).wasOk(),
                    "Bounded non-default mixer and MIDI values must load");
    const auto nonDefaultMixer = nonDefault.getTrackMixerSettings();
    const auto nonDefaultMidi = nonDefault.getTrackMidiRouting();
    context.expect (nonDefaultMixer.gainDecibels == -6.0 && nonDefaultMixer.pan == 0.25
                        && nonDefaultMixer.muted && nonDefaultMixer.solo
                        && nonDefaultMidi.inputChannel == 2
                        && nonDefaultMidi.outputChannel == 10,
                    "Version-2 mixer and MIDI values must round-trip exactly");

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
    futureVersion.getDynamicObject()->setProperty ("schemaVersion", 3);
    writeInvalidAndReject (futureVersion, "An unknown future project schema must be rejected");

    auto missingMixer = juce::JSON::parse (migratedJsonText);
    missingMixer.getDynamicObject()->getProperty ("tracks").getArray()->getReference (0)
        .getDynamicObject()->removeProperty ("mixer");
    writeInvalidAndReject (missingMixer, "A version-2 track without mixer state must be rejected");

    auto invalidPan = juce::JSON::parse (migratedJsonText);
    invalidPan.getDynamicObject()->getProperty ("tracks").getArray()->getReference (0)
        .getDynamicObject()->getProperty ("mixer").getDynamicObject()->setProperty ("pan", 1.5);
    writeInvalidAndReject (invalidPan, "An out-of-range version-2 pan value must be rejected");

    auto invalidMidi = juce::JSON::parse (migratedJsonText);
    invalidMidi.getDynamicObject()->getProperty ("tracks").getArray()->getReference (0)
        .getDynamicObject()->getProperty ("midi").getDynamicObject()->setProperty ("outputChannel", 0);
    writeInvalidAndReject (invalidMidi, "An invalid version-2 MIDI output channel must be rejected");

    context.expect (invalidFile.deleteFile(), "The invalid migration fixture must be removable");
    context.expect (migratedFile.deleteFile(), "The migrated version-2 fixture must be removable");
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

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
    reportObject->setProperty ("editCommandVersion", resonance::EditCommand::supportedVersion);
    reportObject->setProperty ("editCommandFixture", juce::File (editCommandFixturePath).getFileName());
    reportObject->setProperty ("projectSchemaVersion", resonance::SongProject::currentSchemaVersion);
    reportObject->setProperty ("legacySchemaVersion", resonance::SongProject::legacySchemaVersion);
    reportObject->setProperty ("legacyMigrationFixture",
                               juce::File (legacyProjectFixturePath).getFileName());

    try
    {
        testEditingAndUndo (context);
        testSequenceSnapshot (context);
        testRelocatedPluginIdentity (context);
        testSoundSnapshotAndUndo (context);
        testLegacyProjectMigration (context,
                                    juce::File (legacyProjectFixturePath),
                                    migratedBytes,
                                    legacySourceSha,
                                    stableTrackId,
                                    stableClipId);
        legacyMigrationPassed = true;
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
