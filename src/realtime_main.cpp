#include <JuceHeader.h>

#include "editor_component.h"
#include "known_plugin.h"
#include "plugin_identity.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace resonance
{
namespace
{
juce::String getArgumentValue (const juce::StringArray& args, const juce::String& flag)
{
    const auto index = args.indexOf (flag);
    return index >= 0 && index + 1 < args.size() ? args[index + 1] : juce::String {};
}

juce::File findArtifact (const juce::String& filename)
{
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto executableDirectory = executable.getParentDirectory();
    const auto packaged = executableDirectory.getParentDirectory().getChildFile ("artifacts").getChildFile (filename);
    const auto workingDirectory = juce::File::getCurrentWorkingDirectory();
    const auto local = workingDirectory.getChildFile ("artifacts").getChildFile (filename);
    const auto taskOutput = workingDirectory.getChildFile ("outputs")
                                            .getChildFile ("resonance-music-editor")
                                            .getChildFile ("artifacts")
                                            .getChildFile (filename);

    for (const auto& candidate : { packaged, local, taskOutput })
        if (candidate.existsAsFile())
            return candidate;

    return packaged;
}

juce::File resolvePathArgument (const juce::StringArray& args,
                                const juce::String& flag,
                                const juce::String& defaultArtifact)
{
    const auto supplied = getArgumentValue (args, flag);
    return supplied.isNotEmpty() ? juce::File (supplied) : findArtifact (defaultArtifact);
}

bool writeReport (const juce::File& reportFile, const juce::var& report)
{
    reportFile.getParentDirectory().createDirectory();
    return reportFile.replaceWithText (juce::JSON::toString (report, true));
}

// Compact, agent-facing view of a project: everything needed to reason about the music
// and author a valid edit command, with the Base64 instrument state deliberately
// omitted. A 32-bar four-track song is roughly 30 KB here against 448 KB on disk.
juce::var describeProject (const SongProject& project)
{
    auto* root = new juce::DynamicObject();
    juce::var description (root);
    root->setProperty ("schemaVersion", project.getSchemaVersion());
    root->setProperty ("title", project.getTitle());
    root->setProperty ("tempoBpm", project.getTempoBpm());
    root->setProperty ("sampleRate", project.getSampleRate());
    root->setProperty ("snapBeats", project.getSnapBeats());
    root->setProperty ("loopLengthBeats", project.getLoopLengthBeats());
    root->setProperty ("loopLengthTicks",
                       juce::roundToInt (project.getLoopLengthBeats() * 960.0));
    root->setProperty ("ppq", 960);
    // The precondition for any command against this exact state.
    root->setProperty ("projectContentSha256", project.getContentSha256());
    root->setProperty ("trackCount", project.getTrackCount());
    root->setProperty ("activeTrackIndex", project.getActiveTrackIndex());
    root->setProperty ("maxProjectTracks", SongProject::maxProjectTracks);
    root->setProperty ("maxNotesPerClip", static_cast<int> (maxSequenceNotes));
    root->setProperty ("minimumLoopBeats", minimumLoopBeats);
    root->setProperty ("maximumLoopBeats", maximumLoopBeats);

    juce::Array<juce::var> tracks;
    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
    {
        auto* trackObject = new juce::DynamicObject();
        juce::var trackVar (trackObject);
        const auto mixer = project.getTrackMixerSettings (trackIndex);
        const auto midi = project.getTrackMidiRouting (trackIndex);
        trackObject->setProperty ("index", trackIndex);
        trackObject->setProperty ("id", project.getTrackId (trackIndex));
        trackObject->setProperty ("name", project.getTrackName (trackIndex));
        trackObject->setProperty ("clipId", project.getClipId (trackIndex));
        trackObject->setProperty ("gainDb", mixer.gainDecibels);
        trackObject->setProperty ("pan", mixer.pan);
        trackObject->setProperty ("mute", mixer.muted);
        trackObject->setProperty ("solo", mixer.solo);
        trackObject->setProperty ("midiInputChannel", midi.inputChannel);
        trackObject->setProperty ("midiOutputChannel", midi.outputChannel);
        trackObject->setProperty ("soundName", project.getPluginSoundName (trackIndex));
        // Identity and integrity only; the opaque state bytes are not included.
        trackObject->setProperty ("stateSha256", project.getPluginStateSha256 (trackIndex));

        const auto notes = project.getNotes (trackIndex);
        trackObject->setProperty ("noteCount", static_cast<int> (notes.size()));

        juce::Array<juce::var> noteArray;
        auto lowestPitch = 128;
        auto highestPitch = -1;
        for (const auto& note : notes)
        {
            auto* noteObject = new juce::DynamicObject();
            juce::var noteVar (noteObject);
            noteObject->setProperty ("id", note.id);
            noteObject->setProperty ("startTick", juce::roundToInt (note.beat * 960.0));
            noteObject->setProperty ("lengthTicks", juce::roundToInt (note.lengthBeats * 960.0));
            noteObject->setProperty ("midiNote", note.midiNote);
            noteObject->setProperty ("velocity", note.velocity);
            noteArray.add (noteVar);
            lowestPitch = juce::jmin (lowestPitch, note.midiNote);
            highestPitch = juce::jmax (highestPitch, note.midiNote);
        }
        if (highestPitch >= 0)
        {
            trackObject->setProperty ("lowestMidiNote", lowestPitch);
            trackObject->setProperty ("highestMidiNote", highestPitch);
        }
        trackObject->setProperty ("notes", noteArray);
        tracks.add (trackVar);
    }
    root->setProperty ("tracks", tracks);
    return description;
}

int runDescribe (const juce::StringArray& args)
{
    const auto projectPath = getArgumentValue (args, "--project");
    if (projectPath.isEmpty())
    {
        std::cerr << "--describe requires --project <song.resonance.json>" << std::endl;
        return 30;
    }

    SongProject project;
    const auto loaded = project.loadFromFile (juce::File (projectPath));
    if (loaded.failed())
    {
        std::cerr << "Could not load project: " << loaded.getErrorMessage() << std::endl;
        return 31;
    }

    const auto description = describeProject (project);
    const auto text = juce::JSON::toString (description, false);
    const auto outPath = getArgumentValue (args, "--out");
    if (outPath.isEmpty())
    {
        std::cout << text << std::endl;
        return 0;
    }

    juce::File outFile (outPath);
    outFile.getParentDirectory().createDirectory();
    return outFile.replaceWithText (text) ? 0 : 32;
}

// Applies a command file to a project file with the same validation, hash
// precondition, and note operations the interactive path uses, and no plug-in loaded.
int runApplyCommand (const juce::StringArray& args)
{
    const auto projectPath = getArgumentValue (args, "--project");
    const auto commandPath = getArgumentValue (args, "--command");
    if (projectPath.isEmpty() || commandPath.isEmpty())
    {
        std::cerr << "--apply-command requires --project and --command" << std::endl;
        return 40;
    }

    const juce::File projectFile (projectPath);
    SongProject project;
    const auto loaded = project.loadFromFile (projectFile);
    if (loaded.failed())
    {
        std::cerr << "Could not load project: " << loaded.getErrorMessage() << std::endl;
        return 41;
    }

    const juce::File commandFile (commandPath);
    if (! commandFile.existsAsFile())
    {
        std::cerr << "Command file not found: " << commandPath << std::endl;
        return 42;
    }

    EditCommand command;
    const auto parsed = parseEditCommand (commandFile.loadFileAsString(), command);
    if (parsed.failed())
    {
        std::cerr << "Command parse failed: " << parsed.getErrorMessage() << std::endl;
        return 43;
    }

    // Command previews validate against the selected track, which is a UI concept with
    // no meaning headlessly. Here the command's own target defines the selection, so an
    // agent can address any track without a session. Selection is not serialised, so
    // this does not alter the saved project.
    auto targetTrackIndex = -1;
    for (int trackIndex = 0; trackIndex < project.getTrackCount(); ++trackIndex)
        if (project.getTrackId (trackIndex) == command.trackId)
            targetTrackIndex = trackIndex;

    if (targetTrackIndex < 0)
    {
        std::cerr << "No track with id " << command.trackId << " in this project" << std::endl;
        return 44;
    }
    project.setActiveTrackIndex (targetTrackIndex);

    const auto beforeHash = project.getContentSha256();
    EditCommandPreview preview;
    const auto previewed = createEditCommandPreview (command, project, preview);
    if (previewed.failed())
    {
        std::cerr << "Command rejected: " << previewed.getErrorMessage() << std::endl;
        return 44;
    }

    const auto diffCount = static_cast<int> (preview.noteDiffs.size());
    auto additions = 0;
    auto updates = 0;
    auto removals = 0;
    for (const auto& diff : preview.noteDiffs)
    {
        additions += diff.action == NoteEditAction::add ? 1 : 0;
        updates += diff.action == NoteEditAction::update ? 1 : 0;
        removals += diff.action == NoteEditAction::remove ? 1 : 0;
    }

    const auto applied = preview.applyTo (project);
    if (applied.failed())
    {
        std::cerr << "Apply failed: " << applied.getErrorMessage() << std::endl;
        return 45;
    }

    const auto outPath = getArgumentValue (args, "--out");
    const juce::File destination (outPath.isNotEmpty() ? outPath : projectPath);
    const auto saved = project.saveToFile (destination);
    if (saved.failed())
    {
        std::cerr << "Save failed: " << saved.getErrorMessage() << std::endl;
        return 46;
    }

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("projectPath", projectFile.getFileName());
    reportObject->setProperty ("commandPath", commandFile.getFileName());
    reportObject->setProperty ("outputPath", destination.getFileName());
    reportObject->setProperty ("summary", command.summary);
    reportObject->setProperty ("beforeContentSha256", beforeHash);
    reportObject->setProperty ("afterContentSha256", project.getContentSha256());
    reportObject->setProperty ("noteDiffCount", diffCount);
    reportObject->setProperty ("added", additions);
    reportObject->setProperty ("updated", updates);
    reportObject->setProperty ("removed", removals);
    reportObject->setProperty ("passed", true);

    const auto reportPath = getArgumentValue (args, "--report");
    const auto text = juce::JSON::toString (report, true);
    if (reportPath.isNotEmpty())
        writeReport (juce::File (reportPath), report);
    else
        std::cout << text << std::endl;

    return 0;
}

int runM6RuntimeTest (const juce::StringArray& args)
{
    const auto inventoryFile = resolvePathArgument (args, "--inventory", "plugin-inventory.json");
    const auto quarantineFile = resolvePathArgument (args, "--quarantine", "plugin-quarantine.json");
    const auto reportFile = resolvePathArgument (args, "--report", "m6-runtime-test-report.json");
    const auto alternateProjectFile = resolvePathArgument (args,
                                                           "--alternate-project",
                                                           "m4-accepted-candidate-b.resonance.json");

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("editorVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
    reportObject->setProperty ("testedAt", juce::Time::getCurrentTime().toISO8601 (true));
    reportObject->setProperty ("inventoryPath", inventoryFile.getFileName());
    reportObject->setProperty ("quarantinePath", quarantineFile.getFileName());
    reportObject->setProperty ("alternateProjectPath", alternateProjectFile.getFileName());
    reportObject->setProperty ("noRescanPerformed", true);
    reportObject->setProperty ("audioEmitted", false);
    reportObject->setProperty ("runtimeCapacity", static_cast<int> (maxMixerTracks));

    try
    {
        KnownPluginRecord record;
        const auto inventoryResult = loadFirstAcceptedInstrument (inventoryFile, quarantineFile, record);
        if (inventoryResult.failed())
            throw std::runtime_error (inventoryResult.getErrorMessage().toStdString());

        SongProject alternateProject;
        const auto alternateLoad = alternateProject.loadFromFile (alternateProjectFile);
        if (alternateLoad.failed())
            throw std::runtime_error (("Alternate Surge project load failed: "
                                      + alternateLoad.getErrorMessage()).toStdString());
        if (! vst3IdentifiersAreCompatible (alternateProject.getPluginIdentifier(),
                                             record.identifier,
                                             record.description.uniqueId))
            throw std::runtime_error ("The alternate project does not target the accepted Surge identity");

        juce::MemoryBlock alternatePluginState;
        const auto alternateStateRead = alternateProject.getPluginState (alternatePluginState);
        if (alternateStateRead.failed() || alternatePluginState.getSize() == 0)
            throw std::runtime_error ("The alternate Surge project did not contain a state block");

        juce::AudioDeviceManager deviceManager;
        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = 48000.0;
        preferred.bufferSize = 512;
        const auto deviceError = deviceManager.initialise (0, 2, nullptr, true, {}, &preferred);
        if (deviceError.isNotEmpty())
            throw std::runtime_error (("WASAPI device open failed: " + deviceError).toStdString());

        auto* device = deviceManager.getCurrentAudioDevice();
        if (device == nullptr || ! device->getTypeName().containsIgnoreCase ("Windows Audio"))
            throw std::runtime_error ("The M6 runtime gate did not open Windows Audio/WASAPI");

        juce::AudioPluginFormatManager manager;
        manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
        const auto rate = device->getCurrentSampleRate();
        const auto blockSize = device->getCurrentBufferSizeSamples();
        const auto deviceType = device->getTypeName();
        const auto deviceName = device->getName();
        juce::String firstLoadError;
        juce::String secondLoadError;
        auto firstPlugin = manager.createPluginInstance (record.description,
                                                         rate,
                                                         blockSize,
                                                         firstLoadError);
        auto secondPlugin = manager.createPluginInstance (record.description,
                                                          rate,
                                                          blockSize,
                                                          secondLoadError);
        if (firstPlugin == nullptr || secondPlugin == nullptr)
            throw std::runtime_error (("Two-instance cached Surge load failed: "
                                      + firstLoadError + " " + secondLoadError).toStdString());

        const auto firstParameterCount = firstPlugin->getParameters().size();
        const auto secondParameterCount = secondPlugin->getParameters().size();
        const auto distinctInstances = firstPlugin.get() != secondPlugin.get();

        const auto engineStorage = std::make_unique<RealtimeEngine>();
        auto& engine = *engineStorage;
        const auto firstInstall = engine.setPluginForTrack (0, std::move (firstPlugin));
        const auto secondInstall = engine.setPluginForTrack (1, std::move (secondPlugin));
        if (firstInstall.failed() || secondInstall.failed())
            throw std::runtime_error ((firstInstall.getErrorMessage() + " "
                                      + secondInstall.getErrorMessage()).toStdString());

        const auto mixerStorage = std::make_unique<MixerSnapshot>();
        auto& mixer = *mixerStorage;
        mixer.trackCount = 2;
        mixer.tracks[0].enabled = true;
        mixer.tracks[0].gainLinear = 0.02f;
        mixer.tracks[0].pan = -1.0f;
        mixer.tracks[0].midiOutputChannel = 1;
        mixer.tracks[0].sequence.loopBeats = 4.0;
        mixer.tracks[0].sequence.noteCount = 1;
        mixer.tracks[0].sequence.notes[0] = { 0.0, 0.5, 48, 0.75f };
        mixer.tracks[1].enabled = true;
        mixer.tracks[1].gainLinear = 0.02f;
        mixer.tracks[1].pan = 1.0f;
        mixer.tracks[1].midiOutputChannel = 2;
        mixer.tracks[1].sequence.loopBeats = 4.0;
        mixer.tracks[1].sequence.noteCount = 1;
        mixer.tracks[1].sequence.notes[0] = { 0.0, 0.5, 67, 0.70f };
        engine.setMixerSnapshot (mixer);
        engine.setMasterGainDecibels (0.0f);

        const auto preparation = engine.prepareForOfflineRender (rate, blockSize);
        if (preparation.failed())
            throw std::runtime_error (preparation.getErrorMessage().toStdString());

        juce::MemoryBlock firstBaselineState;
        juce::MemoryBlock secondBaselineState;
        if (engine.capturePluginStateForTrack (0, firstBaselineState).failed()
            || engine.capturePluginStateForTrack (1, secondBaselineState).failed())
            throw std::runtime_error ("The prepared Surge slots did not expose complete state blocks");

        engine.setPlaying (true);
        juce::AudioBuffer<float> output (2, blockSize);
        juce::AudioIODeviceCallbackContext callbackContext;
        const auto blockCount = juce::jmax (32,
                                            static_cast<int> (std::ceil (rate * 1.0
                                                                        / blockSize)));
        double accumulatedCallbackLoad = 0.0;
        float maximumOutputPeak = 0.0f;
        float maximumTrackOnePeak = 0.0f;
        float maximumTrackTwoPeak = 0.0f;

        for (int block = 0; block < blockCount; ++block)
        {
            output.clear();
            auto* outputs = output.getArrayOfWritePointers();
            engine.audioDeviceIOCallbackWithContext (nullptr,
                                                     0,
                                                     outputs,
                                                     2,
                                                     blockSize,
                                                     callbackContext);
            accumulatedCallbackLoad += engine.getLastCallbackLoad();
            maximumOutputPeak = juce::jmax (maximumOutputPeak,
                                            output.getMagnitude (0, blockSize));
            maximumTrackOnePeak = juce::jmax (maximumTrackOnePeak,
                                              engine.getTrackLeftPeak (0));
            maximumTrackTwoPeak = juce::jmax (maximumTrackTwoPeak,
                                              engine.getTrackRightPeak (1));
        }

        const auto averageCallbackLoad = accumulatedCallbackLoad / static_cast<double> (blockCount);
        const auto bothTracksProcessed =
            engine.getTrackProcessedBlockCount (0) == static_cast<juce::uint64> (blockCount)
            && engine.getTrackProcessedBlockCount (1) == static_cast<juce::uint64> (blockCount);

        const auto firstSlotAccessible = engine.getPluginForTrack (0) != nullptr;
        juce::MemoryBlock firstStateBeforeMutation;
        juce::MemoryBlock secondStateBeforeMutation;
        engine.capturePluginStateForTrack (0, firstStateBeforeMutation);
        engine.capturePluginStateForTrack (1, secondStateBeforeMutation);
        juce::MemoryBlock changedSecondState;
        juce::MemoryBlock unchangedFirstState;
        const auto alternateStateRestore = engine.restorePluginStateForTrack (1,
                                                                               alternatePluginState);
        constexpr int stateSettleBlocks = 4;
        engine.setPlaying (false);
        for (int block = 0; block < stateSettleBlocks; ++block)
        {
            output.clear();
            auto* outputs = output.getArrayOfWritePointers();
            engine.audioDeviceIOCallbackWithContext (nullptr,
                                                     0,
                                                     outputs,
                                                     2,
                                                     blockSize,
                                                     callbackContext);
        }
        const auto changedSecondCapture = engine.capturePluginStateForTrack (1,
                                                                              changedSecondState);
        const auto unchangedFirstCapture = engine.capturePluginStateForTrack (0,
                                                                               unchangedFirstState);
        const auto alternateStateApplied = alternateStateRestore.wasOk()
                                           && changedSecondCapture.wasOk()
                                           && changedSecondState != secondStateBeforeMutation;
        const auto alternateStatePreservedExact = changedSecondState == alternatePluginState;
        const auto independentStateMutation = alternateStateApplied
                                              && changedSecondState != secondStateBeforeMutation
                                              && unchangedFirstCapture.wasOk()
                                              && unchangedFirstState == firstStateBeforeMutation;

        const auto firstRestore = engine.restorePluginStateForTrack (0, firstBaselineState);
        const auto secondRestore = engine.restorePluginStateForTrack (1, secondBaselineState);
        for (int block = 0; block < stateSettleBlocks; ++block)
        {
            output.clear();
            auto* outputs = output.getArrayOfWritePointers();
            engine.audioDeviceIOCallbackWithContext (nullptr,
                                                     0,
                                                     outputs,
                                                     2,
                                                     blockSize,
                                                     callbackContext);
        }
        juce::MemoryBlock firstRestoredState;
        juce::MemoryBlock secondRestoredState;
        const auto firstRecapture = engine.capturePluginStateForTrack (0, firstRestoredState);
        const auto secondRecapture = engine.capturePluginStateForTrack (1, secondRestoredState);
        const auto completeStateRoundTrip = firstRestore.wasOk() && secondRestore.wasOk()
                                            && firstRecapture.wasOk() && secondRecapture.wasOk()
                                            && firstRestoredState == firstBaselineState
                                            && secondRestoredState == secondBaselineState;

        engine.releaseOfflineRender();
        juce::MemoryBlock survivingStateBeforeRemoval;
        juce::MemoryBlock survivingStateAfterRemoval;
        engine.capturePluginStateForTrack (0, survivingStateBeforeRemoval);
        const auto removeSecond = engine.setPluginForTrack (1, {});
        juce::MemoryBlock missingState;
        const auto missingCapture = engine.capturePluginStateForTrack (1, missingState);
        const auto survivingCapture = engine.capturePluginStateForTrack (0,
                                                                         survivingStateAfterRemoval);
        const auto missingPluginPreserved = removeSecond.wasOk() && missingCapture.failed()
                                            && survivingCapture.wasOk()
                                            && survivingStateAfterRemoval
                                                   == survivingStateBeforeRemoval
                                            && engine.getActivePluginCount() == 1;

        const auto invalidSamples = engine.getInvalidSampleCount();
        const auto clippedSamples = engine.getClippedSampleCount();
        const auto processorExceptions = engine.getProcessorExceptionCount();
        const auto maximumCallbackLoad = engine.getMaximumCallbackLoad();
        engine.shutdown();
        const auto shutdownComplete = ! engine.isPrepared()
                                      && engine.getActivePluginCount() == 0;
        deviceManager.closeAudioDevice();

        auto* deviceObject = new juce::DynamicObject();
        juce::var deviceReport (deviceObject);
        deviceObject->setProperty ("type", deviceType);
        deviceObject->setProperty ("name", deviceName);
        deviceObject->setProperty ("sampleRate", rate);
        deviceObject->setProperty ("blockSize", blockSize);
        reportObject->setProperty ("device", deviceReport);

        auto* pluginObject = new juce::DynamicObject();
        juce::var pluginReport (pluginObject);
        pluginObject->setProperty ("identifier", record.identifier);
        pluginObject->setProperty ("name", record.description.name);
        pluginObject->setProperty ("version", record.description.version);
        pluginObject->setProperty ("expectedParameterCount", record.expectedParameterCount);
        pluginObject->setProperty ("firstParameterCount", firstParameterCount);
        pluginObject->setProperty ("secondParameterCount", secondParameterCount);
        pluginObject->setProperty ("distinctInstances", distinctInstances);
        pluginObject->setProperty ("firstStateBytes",
                                   static_cast<juce::int64> (firstBaselineState.getSize()));
        pluginObject->setProperty ("secondStateBytes",
                                   static_cast<juce::int64> (secondBaselineState.getSize()));
        pluginObject->setProperty ("alternateStateBytes",
                                   static_cast<juce::int64> (alternatePluginState.getSize()));
        pluginObject->setProperty ("alternateStateSha256",
                                   juce::SHA256 (alternatePluginState).toHexString());
        pluginObject->setProperty ("normalisedAlternateStateSha256",
                                   juce::SHA256 (changedSecondState).toHexString());
        pluginObject->setProperty ("alternateStateApplied", alternateStateApplied);
        pluginObject->setProperty ("alternateStatePreservedExact",
                                   alternateStatePreservedExact);
        pluginObject->setProperty ("independentStateMutation", independentStateMutation);
        pluginObject->setProperty ("completeStateRoundTrip", completeStateRoundTrip);
        reportObject->setProperty ("plugin", pluginReport);

        auto* runtimeObject = new juce::DynamicObject();
        juce::var runtimeReport (runtimeObject);
        runtimeObject->setProperty ("installedInstances", 2);
        runtimeObject->setProperty ("renderedBlocks", blockCount);
        runtimeObject->setProperty ("stateSettleBlocks", stateSettleBlocks * 2);
        runtimeObject->setProperty ("bothTracksProcessed", bothTracksProcessed);
        runtimeObject->setProperty ("maximumOutputPeak", maximumOutputPeak);
        runtimeObject->setProperty ("maximumTrackOnePeak", maximumTrackOnePeak);
        runtimeObject->setProperty ("maximumTrackTwoPeak", maximumTrackTwoPeak);
        runtimeObject->setProperty ("averageCallbackLoad", averageCallbackLoad);
        runtimeObject->setProperty ("maximumCallbackLoad", maximumCallbackLoad);
        runtimeObject->setProperty ("invalidSamples", invalidSamples);
        runtimeObject->setProperty ("clippedSamples", clippedSamples);
        runtimeObject->setProperty ("processorExceptions", processorExceptions);
        runtimeObject->setProperty ("missingPluginPreserved", missingPluginPreserved);
        runtimeObject->setProperty ("shutdownComplete", shutdownComplete);
        reportObject->setProperty ("runtime", runtimeReport);

        const auto parameterCountsMatch = firstParameterCount == record.expectedParameterCount
                                          && secondParameterCount == record.expectedParameterCount;
        const auto audioObservedInMemory = maximumOutputPeak > 0.0f
                                           && maximumTrackOnePeak > 0.0f
                                           && maximumTrackTwoPeak > 0.0f;
        reportObject->setProperty ("passed",
                                   distinctInstances && parameterCountsMatch
                                       && firstSlotAccessible && bothTracksProcessed
                                       && audioObservedInMemory && averageCallbackLoad < 1.0
                                       && invalidSamples == 0 && clippedSamples == 0
                                       && processorExceptions == 0 && independentStateMutation
                                       && completeStateRoundTrip && missingPluginPreserved
                                       && shutdownComplete);
    }
    catch (const std::exception& error)
    {
        reportObject->setProperty ("passed", false);
        reportObject->setProperty ("error", error.what());
    }

    if (! writeReport (reportFile, report))
        return 2;

    std::cout << juce::JSON::toString (report, true) << std::endl;
    return static_cast<bool> (reportObject->getProperty ("passed")) ? 0 : 1;
}

int runSelfTest (const juce::StringArray& args)
{
    const auto inventoryFile = resolvePathArgument (args, "--inventory", "plugin-inventory.json");
    const auto quarantineFile = resolvePathArgument (args, "--quarantine", "plugin-quarantine.json");
    const auto reportFile = resolvePathArgument (args, "--report", "realtime-self-test.json");
    const auto songProjectFile = resolvePathArgument (args,
                                                      "--project",
                                                      "realtime-song-project.resonance.json");

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("editorVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
    reportObject->setProperty ("testedAt", juce::Time::getCurrentTime().toISO8601 (true));
    reportObject->setProperty ("inventoryPath", inventoryFile.getFullPathName());
    reportObject->setProperty ("quarantinePath", quarantineFile.getFullPathName());
    reportObject->setProperty ("songProjectPath", songProjectFile.getFullPathName());
    reportObject->setProperty ("noRescanPerformed", true);
    reportObject->setProperty ("audioEmitted", false);

    try
    {
        KnownPluginRecord record;
        const auto inventoryResult = loadFirstAcceptedInstrument (inventoryFile, quarantineFile, record);
        if (inventoryResult.failed())
            throw std::runtime_error (inventoryResult.getErrorMessage().toStdString());

        juce::AudioDeviceManager deviceManager;
        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = 48000.0;
        preferred.bufferSize = 512;
        const auto deviceError = deviceManager.initialise (0, 2, nullptr, true, {}, &preferred);
        if (deviceError.isNotEmpty())
            throw std::runtime_error (("WASAPI device open failed: " + deviceError).toStdString());

        auto* device = deviceManager.getCurrentAudioDevice();
        if (device == nullptr)
            throw std::runtime_error ("No Windows audio output device was opened");

        auto* deviceObject = new juce::DynamicObject();
        juce::var deviceReport (deviceObject);
        deviceObject->setProperty ("type", device->getTypeName());
        deviceObject->setProperty ("name", device->getName());
        deviceObject->setProperty ("sampleRate", device->getCurrentSampleRate());
        deviceObject->setProperty ("blockSize", device->getCurrentBufferSizeSamples());
        deviceObject->setProperty ("outputChannels", device->getActiveOutputChannels().countNumberOfSetBits());
        deviceObject->setProperty ("outputLatencySamples", device->getOutputLatencyInSamples());
        deviceObject->setProperty ("xRuns", device->getXRunCount());
        reportObject->setProperty ("device", deviceReport);

        if (! device->getTypeName().containsIgnoreCase ("Windows Audio"))
            throw std::runtime_error ("The opened device is not JUCE Windows Audio/WASAPI");

        juce::AudioPluginFormatManager manager;
        manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
        juce::String loadError;
        auto plugin = manager.createPluginInstance (record.description,
                                                     device->getCurrentSampleRate(),
                                                     device->getCurrentBufferSizeSamples(),
                                                     loadError);
        if (plugin == nullptr)
            throw std::runtime_error (("Cached Surge load failed: " + loadError).toStdString());

        for (int bus = 0; bus < plugin->getBusCount (false); ++bus)
            if (auto* outputBus = plugin->getBus (false, bus))
                outputBus->enable (bus == 0);

        plugin->setNonRealtime (false);
        plugin->setRateAndBufferSizeDetails (device->getCurrentSampleRate(),
                                             device->getCurrentBufferSizeSamples());
        plugin->prepareToPlay (device->getCurrentSampleRate(),
                               device->getCurrentBufferSizeSamples());

        juce::MemoryBlock livePluginState;
        plugin->getStateInformation (livePluginState);
        if (livePluginState.getSize() == 0)
            throw std::runtime_error ("Surge returned an empty state during song-project self-test");

        SongProject song;
        song.setTitle ("Realtime Surge Round Trip");
        song.setTempoBpm (137.0);
        song.setLoopLengthBeats (16.0);
        song.setSnapBeats (0.125);
        song.setSampleRate (juce::roundToInt (device->getCurrentSampleRate()));
        song.setPluginMetadata (record.identifier,
                                record.description.name,
                                record.description.manufacturerName,
                                record.description.version);
        const auto soundApplyResult = song.applyPluginSound ("Self-test Surge state", livePluginState);
        if (soundApplyResult.failed())
            throw std::runtime_error (soundApplyResult.getErrorMessage().toStdString());
        song.beginUndoTransaction ("Self-test note");
        const auto noteInsertResult = song.insertNote ({ "note-self-test-1", 10.5, 0.75, 67, 109 });
        if (noteInsertResult.failed())
            throw std::runtime_error (noteInsertResult.getErrorMessage().toStdString());

        const auto projectSaveResult = song.saveToFile (songProjectFile);
        if (projectSaveResult.failed())
            throw std::runtime_error (("Song-project save failed: "
                                       + projectSaveResult.getErrorMessage()).toStdString());

        SongProject reopenedSong;
        const auto projectLoadResult = reopenedSong.loadFromFile (songProjectFile);
        if (projectLoadResult.failed())
            throw std::runtime_error (("Song-project reopen failed: "
                                       + projectLoadResult.getErrorMessage()).toStdString());

        juce::MemoryBlock reopenedPluginState;
        const auto reopenedStateResult = reopenedSong.getPluginState (reopenedPluginState);
        if (reopenedStateResult.failed())
            throw std::runtime_error (reopenedStateResult.getErrorMessage().toStdString());

        const auto savedPayloadExact = reopenedPluginState == livePluginState;
        const auto soundNameRoundTrip = reopenedSong.getPluginSoundName() == "Self-test Surge state";
        const auto fixtureNotePresent = reopenedSong.findNote ("note-self-test-1").has_value();
        const auto mixerSettings = reopenedSong.getTrackMixerSettings();
        const auto midiRouting = reopenedSong.getTrackMidiRouting();
        const auto trackContractRoundTrip =
            reopenedSong.getSchemaVersion() == SongProject::currentSchemaVersion
            && reopenedSong.getTrackId() == "track-1"
            && reopenedSong.getClipId() == "loop-1"
            && mixerSettings.gainDecibels == 0.0 && mixerSettings.pan == 0.0
            && ! mixerSettings.muted && ! mixerSettings.solo
            && midiRouting.inputChannel == 0 && midiRouting.outputChannel == 1;
        plugin->setStateInformation (reopenedPluginState.getData(),
                                     static_cast<int> (reopenedPluginState.getSize()));
        juce::MemoryBlock recapturedPluginState;
        plugin->getStateInformation (recapturedPluginState);
        const auto pluginRestoreExact = recapturedPluginState == livePluginState;

        auto* songObject = new juce::DynamicObject();
        juce::var songReport (songObject);
        songObject->setProperty ("schemaVersion", reopenedSong.getSchemaVersion());
        songObject->setProperty ("trackId", reopenedSong.getTrackId());
        songObject->setProperty ("clipId", reopenedSong.getClipId());
        songObject->setProperty ("mixerGainDb", mixerSettings.gainDecibels);
        songObject->setProperty ("mixerPan", mixerSettings.pan);
        songObject->setProperty ("mixerMuted", mixerSettings.muted);
        songObject->setProperty ("mixerSolo", mixerSettings.solo);
        songObject->setProperty ("midiInputChannel", midiRouting.inputChannel);
        songObject->setProperty ("midiOutputChannel", midiRouting.outputChannel);
        songObject->setProperty ("fileBytes", songProjectFile.getSize());
        songObject->setProperty ("stateBytes", static_cast<juce::int64> (livePluginState.getSize()));
        songObject->setProperty ("stateSha256", song.getPluginStateSha256());
        songObject->setProperty ("soundName", reopenedSong.getPluginSoundName());
        songObject->setProperty ("noteCount", static_cast<int> (reopenedSong.getNotes().size()));
        songObject->setProperty ("fixtureNoteId", "note-self-test-1");
        songObject->setProperty ("tempoBpm", reopenedSong.getTempoBpm());
        songObject->setProperty ("loopLengthBeats", reopenedSong.getLoopLengthBeats());
        songObject->setProperty ("savedPayloadExact", savedPayloadExact);
        songObject->setProperty ("pluginRestoreExact", pluginRestoreExact);
        songObject->setProperty ("soundNameRoundTrip", soundNameRoundTrip);
        reportObject->setProperty ("songProject", songReport);

        auto* pluginObject = new juce::DynamicObject();
        juce::var pluginReport (pluginObject);
        pluginObject->setProperty ("identifier", record.identifier);
        pluginObject->setProperty ("name", record.description.name);
        pluginObject->setProperty ("version", record.description.version);
        pluginObject->setProperty ("bundleFingerprintSha256", record.bundleFingerprint);
        pluginObject->setProperty ("parameterCount", plugin->getParameters().size());
        pluginObject->setProperty ("expectedParameterCount", record.expectedParameterCount);
        pluginObject->setProperty ("mainOutputChannels", plugin->getMainBusNumOutputChannels());
        pluginObject->setProperty ("preparedForRealtime", true);
        reportObject->setProperty ("plugin", pluginReport);

        const auto parameterCountMatches = plugin->getParameters().size() == record.expectedParameterCount;
        const auto stereoOutput = plugin->getMainBusNumOutputChannels() >= 2;
        plugin->releaseResources();
        plugin.reset();
        deviceManager.closeAudioDevice();

        reportObject->setProperty ("parameterCountMatchesInventory", parameterCountMatches);
        reportObject->setProperty ("stereoMainOutput", stereoOutput);
        reportObject->setProperty ("passed",
                                   parameterCountMatches && stereoOutput
                                       && savedPayloadExact && pluginRestoreExact
                                       && soundNameRoundTrip && fixtureNotePresent
                                       && trackContractRoundTrip);
    }
    catch (const std::exception& error)
    {
        reportObject->setProperty ("passed", false);
        reportObject->setProperty ("error", error.what());
    }

    if (! writeReport (reportFile, report))
        return 2;

    std::cout << juce::JSON::toString (report, true) << std::endl;
    return static_cast<bool> (reportObject->getProperty ("passed")) ? 0 : 1;
}

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (juce::File inventoryFile,
                juce::File quarantineFile,
                juce::PropertiesFile* settings,
                bool shouldShow)
        : juce::DocumentWindow ("Resonance Music Editor",
                                juce::Colour::fromRGB (10, 15, 24),
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (1080, 720, 1800, 1200);
        editor = new MainEditorComponent (std::move (inventoryFile),
                                          std::move (quarantineFile),
                                          settings);
        setContentOwned (editor, true);
        centreWithSize (1280, 860);
        setVisible (shouldShow);
    }

    void closeButtonPressed() override
    {
        if (editor != nullptr)
            editor->requestClose ([] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }

    juce::var runM4WorkflowSelfTest (const juce::File& projectFile)
    {
        return editor != nullptr ? editor->runM4WorkflowSelfTest (projectFile) : juce::var {};
    }

    juce::var runM5WorkflowSelfTest()
    {
        return editor != nullptr ? editor->runM5WorkflowSelfTest() : juce::var {};
    }

    juce::var runM6AuthoringSelfTest (const juce::File& projectFile)
    {
        return editor != nullptr ? editor->runM6AuthoringSelfTest (projectFile) : juce::var {};
    }

    juce::var runCommandLoadSelfTest()
    {
        return editor != nullptr ? editor->runCommandLoadSelfTest() : juce::var {};
    }

    juce::var runSelectionSelfTest()
    {
        return editor != nullptr ? editor->runSelectionSelfTest() : juce::var {};
    }

    juce::var runSoundShelfSelfTest (const juce::File& alternateProjectFile)
    {
        return editor != nullptr ? editor->runSoundShelfSelfTest (alternateProjectFile)
                                 : juce::var {};
    }

    juce::var runAudioProbeSelfTest (const juce::File& projectFile)
    {
        return editor != nullptr ? editor->runAudioProbeSelfTest (projectFile) : juce::var {};
    }

    juce::var renderProjectToWav (const juce::File& projectFile,
                                  const juce::File& wavFile,
                                  int repeats,
                                  double tailSeconds)
    {
        return editor != nullptr
                   ? editor->renderProjectToWav (projectFile, wavFile, repeats, tailSeconds)
                   : juce::var {};
    }

    bool openProjectFromCommandLine (const juce::File& projectFile)
    {
        return editor != nullptr && editor->openProjectFromCommandLine (projectFile);
    }

    void prepareM5PreviewForSnapshot()
    {
        if (editor != nullptr)
            editor->prepareM5PreviewForSnapshot();
    }

private:
    MainEditorComponent* editor = nullptr;
};
} // namespace

class ResonanceApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Resonance Music Editor"; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::StringArray args;
        args.addTokens (commandLine, true);
        args.removeEmptyStrings();

        if (args.contains ("--self-test"))
        {
            setApplicationReturnValue (runSelfTest (args));
            quit();
            return;
        }

        // Agent-facing modes. Neither needs an instrument, so both exit before the
        // window is built and the four Surge instances are preloaded.
        if (args.contains ("--describe"))
        {
            setApplicationReturnValue (runDescribe (args));
            quit();
            return;
        }

        if (args.contains ("--apply-command"))
        {
            setApplicationReturnValue (runApplyCommand (args));
            quit();
            return;
        }

        if (args.contains ("--m6-runtime-test"))
        {
            setApplicationReturnValue (runM6RuntimeTest (args));
            quit();
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.folderName = "ResonanceMusicEditor";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 1500;
        properties.setStorageParameters (options);

        const auto inventory = resolvePathArgument (args, "--inventory", "plugin-inventory.json");
        const auto quarantine = resolvePathArgument (args, "--quarantine", "plugin-quarantine.json");
        const auto snapshotMode = args.contains ("--ui-snapshot");
        const auto idleTestMode = args.contains ("--ui-idle-test");
        const auto m4WorkflowTestMode = args.contains ("--m4-workflow-test");
        const auto m5WorkflowTestMode = args.contains ("--m5-workflow-test");
        const auto m6AuthoringTestMode = args.contains ("--m6-authoring-test");
        const auto commandLoadTestMode = args.contains ("--command-load-test");
        const auto selectionTestMode = args.contains ("--selection-test");
        const auto soundShelfTestMode = args.contains ("--sound-shelf-test");
        const auto audioProbeMode = args.contains ("--audio-probe");
        const auto renderMode = args.contains ("--render");
        window = std::make_unique<MainWindow> (inventory,
                                               quarantine,
                                               properties.getUserSettings(),
                                               ! snapshotMode && ! idleTestMode
                                                   && ! m4WorkflowTestMode && ! m5WorkflowTestMode
                                                   && ! m6AuthoringTestMode
                                                   && ! commandLoadTestMode
                                                   && ! selectionTestMode
                                                   && ! soundShelfTestMode
                                                   && ! audioProbeMode
                                                   && ! renderMode);

        const auto interactive = ! snapshotMode && ! idleTestMode && ! m4WorkflowTestMode
                                 && ! m5WorkflowTestMode && ! m6AuthoringTestMode
                                 && ! commandLoadTestMode && ! selectionTestMode
                                 && ! soundShelfTestMode && ! audioProbeMode && ! renderMode;

        // Opening a song from the command line so the editor starts on it, rather than
        // starting on the starter project and making the user find the file.
        if (interactive && args.contains ("--project") && window != nullptr)
        {
            const auto launchProject = resolvePathArgument (args, "--project", "");
            if (! window->openProjectFromCommandLine (launchProject))
                std::cerr << "Could not open " << launchProject.getFullPathName() << std::endl;
        }

        if (snapshotMode)
        {
            const auto snapshotFile = resolvePathArgument (args, "--snapshot", "realtime-ui-snapshot.png");
            // Optional: snapshot a specific project instead of the starter one, so
            // two-track views can be captured as visual evidence.
            const auto snapshotProject = args.contains ("--project")
                                             ? resolvePathArgument (args, "--project", "")
                                             : juce::File {};
            juce::Timer::callAfterDelay (1800, [this, snapshotFile, snapshotProject]
            {
                auto* content = window != nullptr ? window->getContentComponent() : nullptr;
                auto passed = false;

                if (content != nullptr
                    && (snapshotProject == juce::File()
                        || window->openProjectFromCommandLine (snapshotProject)))
                {
                    if (snapshotProject == juce::File())
                        window->prepareM5PreviewForSnapshot();
                    const auto image = content->createComponentSnapshot (content->getLocalBounds(), true, 1.0f);
                    juce::MemoryOutputStream encoded;
                    juce::PNGImageFormat png;

                    if (image.isValid() && png.writeImageToStream (image, encoded))
                    {
                        snapshotFile.getParentDirectory().createDirectory();
                        passed = snapshotFile.replaceWithData (encoded.getData(), encoded.getDataSize());
                    }
                }

                setApplicationReturnValue (passed ? 0 : 3);
                quit();
            });
        }
        else if (idleTestMode)
        {
            juce::Timer::callAfterDelay (4000, [this]
            {
                setApplicationReturnValue (0);
                quit();
            });
        }
        else if (m4WorkflowTestMode)
        {
            const auto projectFile = resolvePathArgument (args,
                                                          "--project",
                                                          "realtime-song-project.resonance.json");
            const auto reportFile = resolvePathArgument (args,
                                                         "--report",
                                                         "m4-workflow-self-test.json");
            juce::Timer::callAfterDelay (750, [this, projectFile, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->runM4WorkflowSelfTest (projectFile)
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 5);
                quit();
            });
        }
        else if (m5WorkflowTestMode)
        {
            const auto reportFile = resolvePathArgument (args,
                                                         "--report",
                                                         "m5-workflow-test-report.json");
            juce::Timer::callAfterDelay (750, [this, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->runM5WorkflowSelfTest()
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 6);
                quit();
            });
        }
        else if (m6AuthoringTestMode)
        {
            const auto projectFile = resolvePathArgument (
                args, "--project", "m6-two-track-authoring.resonance.json");
            const auto reportFile = resolvePathArgument (
                args, "--report", "m6-authoring-test-report.json");
            juce::Timer::callAfterDelay (750, [this, projectFile, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->runM6AuthoringSelfTest (projectFile)
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 7);
                quit();
            });
        }
        else if (commandLoadTestMode)
        {
            const auto reportFile = resolvePathArgument (
                args, "--report", "command-load-test-report.json");
            juce::Timer::callAfterDelay (750, [this, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->runCommandLoadSelfTest()
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 8);
                quit();
            });
        }
        else if (selectionTestMode)
        {
            const auto reportFile = resolvePathArgument (
                args, "--report", "selection-test-report.json");
            juce::Timer::callAfterDelay (750, [this, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->runSelectionSelfTest()
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 9);
                quit();
            });
        }
        else if (soundShelfTestMode)
        {
            const auto alternateProject = resolvePathArgument (
                args, "--alternate-project", "m4-accepted-candidate-b.resonance.json");
            const auto reportFile = resolvePathArgument (
                args, "--report", "sound-shelf-test-report.json");
            juce::Timer::callAfterDelay (750, [this, alternateProject, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->runSoundShelfSelfTest (alternateProject)
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 10);
                quit();
            });
        }
        else if (audioProbeMode)
        {
            const auto projectFile = resolvePathArgument (args, "--project", "");
            const auto reportFile = resolvePathArgument (
                args, "--report", "audio-probe-report.json");
            juce::Timer::callAfterDelay (750, [this, projectFile, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->runAudioProbeSelfTest (projectFile)
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 11);
                quit();
            });
        }
        else if (renderMode)
        {
            const auto projectFile = resolvePathArgument (args, "--project", "");
            const auto wavFile = juce::File (getArgumentValue (args, "--wav"));
            const auto repeatsText = getArgumentValue (args, "--repeats");
            const auto tailText = getArgumentValue (args, "--tail-seconds");
            const auto repeats = repeatsText.isNotEmpty() ? repeatsText.getIntValue() : 1;
            const auto tailSeconds = tailText.isNotEmpty() ? tailText.getDoubleValue() : 2.0;
            const auto reportFile = resolvePathArgument (args, "--report", "render-report.json");
            juce::Timer::callAfterDelay (750,
                                         [this, projectFile, wavFile, repeats, tailSeconds, reportFile]
            {
                const auto report = window != nullptr
                                        ? window->renderProjectToWav (projectFile, wavFile,
                                                                      repeats, tailSeconds)
                                        : juce::var {};
                const auto* object = report.getDynamicObject();
                const auto passed = object != nullptr
                                    && static_cast<bool> (object->getProperty ("passed"));
                if (object != nullptr && ! passed)
                    std::cerr << object->getProperty ("error").toString() << std::endl;
                const auto reportWritten = writeReport (reportFile, report);
                setApplicationReturnValue (passed && reportWritten ? 0 : 12);
                quit();
            });
        }
    }

    void shutdown() override
    {
        window.reset();
        properties.saveIfNeeded();
        properties.closeFiles();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr)
            window->toFront (true);
    }

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> window;
};
} // namespace resonance

START_JUCE_APPLICATION (resonance::ResonanceApplication)
