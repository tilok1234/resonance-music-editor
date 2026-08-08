#include <JuceHeader.h>

#include "../src/edit_command.h"
#include "../src/plugin_identity.h"
#include "../src/song_project.h"

#include <array>
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

    auto legacyJson = juce::JSON::parse (file.loadFileAsString());
    auto* legacyRoot = legacyJson.getDynamicObject();
    auto* legacyTracks = legacyRoot != nullptr ? legacyRoot->getProperty ("tracks").getArray() : nullptr;
    auto* legacyTrack = legacyTracks != nullptr && ! legacyTracks->isEmpty()
                            ? legacyTracks->getReference (0).getDynamicObject()
                            : nullptr;
    auto* legacyInstrument = legacyTrack != nullptr
                                 ? legacyTrack->getProperty ("instrument").getDynamicObject()
                                 : nullptr;
    context.expect (legacyInstrument != nullptr, "The round-trip fixture must expose an instrument object");
    legacyInstrument->removeProperty ("soundName");
    const auto legacyFile = file.getSiblingFile (file.getFileNameWithoutExtension()
                                                  + "-legacy.resonance.json");
    context.expect (legacyFile.replaceWithText (juce::JSON::toString (legacyJson, true)),
                    "A legacy version-1 fixture without soundName must be writable");
    resonance::SongProject legacyLoaded;
    context.expect (legacyLoaded.loadFromFile (legacyFile).wasOk(),
                    "An older version-1 project without soundName must still load");
    context.expect (legacyLoaded.getPluginSoundName() == "Project sound",
                    "An older project must receive the documented fallback sound name");
    context.expect (legacyFile.deleteFile(), "The temporary legacy project must be removable");
    context.expect (file.deleteFile(), "The temporary round-trip project must be removable");
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args (argv + 1, argc - 1);
    const auto reportPath = argumentValue (args, "--report");
    const auto editCommandFixturePath = argumentValue (args, "--edit-command-fixture");
    TestContext context;
    int savedBytes = 0;
    juce::String stateSha;
    juce::String editCommandCandidateSha;

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
    reportObject->setProperty ("editCommandVersion", resonance::EditCommand::supportedVersion);
    reportObject->setProperty ("editCommandFixture", juce::File (editCommandFixturePath).getFileName());

    try
    {
        testEditingAndUndo (context);
        testSequenceSnapshot (context);
        testRelocatedPluginIdentity (context);
        testSoundSnapshotAndUndo (context);
        testEditCommandFoundation (context,
                                   juce::File (editCommandFixturePath),
                                   editCommandCandidateSha);
        testRoundTrip (context, savedBytes, stateSha);
        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("roundTripBytes", savedBytes);
        reportObject->setProperty ("stateSha256", stateSha);
        reportObject->setProperty ("editCommandCandidateSha256", editCommandCandidateSha);
        reportObject->setProperty ("passed", true);
    }
    catch (const std::exception& error)
    {
        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("roundTripBytes", savedBytes);
        reportObject->setProperty ("stateSha256", stateSha);
        reportObject->setProperty ("editCommandCandidateSha256", editCommandCandidateSha);
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
