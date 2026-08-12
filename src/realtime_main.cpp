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

        RealtimeEngine engine;
        const auto firstInstall = engine.setPluginForTrack (0, std::move (firstPlugin));
        const auto secondInstall = engine.setPluginForTrack (1, std::move (secondPlugin));
        if (firstInstall.failed() || secondInstall.failed())
            throw std::runtime_error ((firstInstall.getErrorMessage() + " "
                                      + secondInstall.getErrorMessage()).toStdString());

        MixerSnapshot mixer;
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
        window = std::make_unique<MainWindow> (inventory,
                                               quarantine,
                                               properties.getUserSettings(),
                                               ! snapshotMode && ! idleTestMode
                                                   && ! m4WorkflowTestMode && ! m5WorkflowTestMode
                                                   && ! m6AuthoringTestMode
                                                   && ! commandLoadTestMode);

        if (snapshotMode)
        {
            const auto snapshotFile = resolvePathArgument (args, "--snapshot", "realtime-ui-snapshot.png");
            juce::Timer::callAfterDelay (1800, [this, snapshotFile]
            {
                auto* content = window != nullptr ? window->getContentComponent() : nullptr;
                auto passed = false;

                if (content != nullptr)
                {
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
