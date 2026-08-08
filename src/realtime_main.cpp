#include <JuceHeader.h>

#include "editor_component.h"
#include "known_plugin.h"

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
        song.setPluginState (livePluginState);
        song.beginUndoTransaction ("Self-test note");
        song.addNote (10.5, 0.75, 67, 109);

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
        plugin->setStateInformation (reopenedPluginState.getData(),
                                     static_cast<int> (reopenedPluginState.getSize()));
        juce::MemoryBlock recapturedPluginState;
        plugin->getStateInformation (recapturedPluginState);
        const auto pluginRestoreExact = recapturedPluginState == livePluginState;

        auto* songObject = new juce::DynamicObject();
        juce::var songReport (songObject);
        songObject->setProperty ("schemaVersion", 1);
        songObject->setProperty ("fileBytes", songProjectFile.getSize());
        songObject->setProperty ("stateBytes", static_cast<juce::int64> (livePluginState.getSize()));
        songObject->setProperty ("stateSha256", song.getPluginStateSha256());
        songObject->setProperty ("noteCount", static_cast<int> (reopenedSong.getNotes().size()));
        songObject->setProperty ("tempoBpm", reopenedSong.getTempoBpm());
        songObject->setProperty ("loopLengthBeats", reopenedSong.getLoopLengthBeats());
        songObject->setProperty ("savedPayloadExact", savedPayloadExact);
        songObject->setProperty ("pluginRestoreExact", pluginRestoreExact);
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
                                       && savedPayloadExact && pluginRestoreExact);
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
        window = std::make_unique<MainWindow> (inventory,
                                               quarantine,
                                               properties.getUserSettings(),
                                               ! snapshotMode && ! idleTestMode);

        if (snapshotMode)
        {
            const auto snapshotFile = resolvePathArgument (args, "--snapshot", "realtime-ui-snapshot.png");
            juce::Timer::callAfterDelay (1800, [this, snapshotFile]
            {
                auto* content = window != nullptr ? window->getContentComponent() : nullptr;
                auto passed = false;

                if (content != nullptr)
                {
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
