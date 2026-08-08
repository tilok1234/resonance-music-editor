#include <JuceHeader.h>

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
    project.setPluginState (state);
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
    context.expect (loaded.getNotes().size() == project.getNotes().size(),
                    "All notes must survive save and reopen");

    juce::MemoryBlock restoredState;
    const auto stateResult = loaded.getPluginState (restoredState);
    context.expect (stateResult.wasOk(), "Reopened plugin state must pass its integrity check");
    context.expect (restoredState == state, "Reopened plugin state must be byte-exact");
    context.expect (loaded.getPluginStateSha256() == stateSha, "Saved state hash must round-trip exactly");
    context.expect (file.deleteFile(), "The temporary round-trip project must be removable");
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args (argv + 1, argc - 1);
    const auto reportPath = argumentValue (args, "--report");
    TestContext context;
    int savedBytes = 0;
    juce::String stateSha;

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());

    try
    {
        testEditingAndUndo (context);
        testSequenceSnapshot (context);
        testRelocatedPluginIdentity (context);
        testRoundTrip (context, savedBytes, stateSha);
        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("roundTripBytes", savedBytes);
        reportObject->setProperty ("stateSha256", stateSha);
        reportObject->setProperty ("passed", true);
    }
    catch (const std::exception& error)
    {
        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("roundTripBytes", savedBytes);
        reportObject->setProperty ("stateSha256", stateSha);
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
