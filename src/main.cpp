#include <JuceHeader.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr double probeSampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr double renderSeconds = 4.0;

class OfflinePlayHead final : public juce::AudioPlayHead
{
public:
    void setSamplePosition (juce::int64 newPosition) noexcept
    {
        samplePosition = newPosition;
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSamples (samplePosition);
        info.setTimeInSeconds (static_cast<double> (samplePosition) / probeSampleRate);
        info.setBpm (120.0);
        info.setPpqPosition ((static_cast<double> (samplePosition) / probeSampleRate) * 2.0);
        info.setPpqPositionOfLastBarStart (std::floor (*info.getPpqPosition() / 4.0) * 4.0);
        info.setIsPlaying (true);
        return info;
    }

private:
    juce::int64 samplePosition = 0;
};

struct RenderMetrics
{
    float peak = 0.0f;
    double rms = 0.0;
    juce::int64 nonFiniteSamples = 0;
    juce::int64 renderedSamples = 0;
    int outputChannels = 0;
    juce::MemoryBlock stateBeforeRender;
    juce::MemoryBlock stateAfterRender;
    juce::MemoryBlock stateAfterReset;
    juce::MemoryBlock stateAfterPreparedRestore;
    juce::MemoryBlock stateAfterRepeatedPreparedRestore;
    juce::String parameterSha256BeforeRender;
    juce::String parameterSha256AfterRender;
    int changedParametersAfterRender = 0;
};

std::vector<float> captureParameterValues (juce::AudioPluginInstance& plugin)
{
    std::vector<float> values;
    values.reserve (static_cast<std::size_t> (plugin.getParameters().size()));

    for (auto* parameter : plugin.getParameters())
        values.push_back (parameter != nullptr ? parameter->getValue() : 0.0f);

    return values;
}

juce::String parameterSnapshotHash (const std::vector<float>& values)
{
    return juce::SHA256 (values.data(), values.size() * sizeof (float)).toHexString();
}

int countChangedParameters (const std::vector<float>& before, const std::vector<float>& after)
{
    const auto compared = juce::jmin (before.size(), after.size());
    auto changed = static_cast<int> (before.size() > after.size()
                                         ? before.size() - after.size()
                                         : after.size() - before.size());

    for (std::size_t index = 0; index < compared; ++index)
        if (before[index] != after[index])
            ++changed;

    return changed;
}

juce::int64 countDifferingStateBytes (const juce::MemoryBlock& before,
                                      const juce::MemoryBlock& after)
{
    const auto compared = juce::jmin (before.getSize(), after.getSize());
    const auto* beforeBytes = static_cast<const juce::uint8*> (before.getData());
    const auto* afterBytes = static_cast<const juce::uint8*> (after.getData());
    auto changed = static_cast<juce::int64> (before.getSize() > after.getSize()
                                                 ? before.getSize() - after.getSize()
                                                 : after.getSize() - before.getSize());

    for (std::size_t index = 0; index < compared; ++index)
        if (beforeBytes[index] != afterBytes[index])
            ++changed;

    return changed;
}

juce::int64 countCommonPrefixBytes (const juce::MemoryBlock& first,
                                    const juce::MemoryBlock& second)
{
    const auto compared = juce::jmin (first.getSize(), second.getSize());
    const auto* firstBytes = static_cast<const juce::uint8*> (first.getData());
    const auto* secondBytes = static_cast<const juce::uint8*> (second.getData());
    std::size_t index = 0;
    while (index < compared && firstBytes[index] == secondBytes[index])
        ++index;
    return static_cast<juce::int64> (index);
}

juce::String stateTailHex (const juce::MemoryBlock& state)
{
    constexpr std::size_t tailBytes = 16;
    const auto start = state.getSize() > tailBytes ? state.getSize() - tailBytes : 0;
    juce::MemoryBlock tail (static_cast<const juce::uint8*> (state.getData()) + start,
                            state.getSize() - start);
    return juce::String::toHexString (tail.getData(), static_cast<int> (tail.getSize()), 1);
}

juce::MemoryBlock loadProjectPluginState (const juce::File& projectFile,
                                          juce::String& soundName,
                                          juce::String& savedHash)
{
    const auto parsed = juce::JSON::parse (projectFile.loadFileAsString());
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        throw std::runtime_error ("State-project root is not a JSON object");

    const auto* tracks = root->getProperty ("tracks").getArray();
    if (tracks == nullptr || tracks->isEmpty())
        throw std::runtime_error ("State project contains no track");

    auto* track = tracks->getReference (0).getDynamicObject();
    auto* instrument = track != nullptr ? track->getProperty ("instrument").getDynamicObject() : nullptr;
    if (instrument == nullptr)
        throw std::runtime_error ("State project contains no instrument object");

    soundName = instrument->getProperty ("soundName").toString();
    savedHash = instrument->getProperty ("stateSha256").toString();
    juce::MemoryBlock state;
    if (! state.fromBase64Encoding (instrument->getProperty ("state").toString()) || state.isEmpty())
        throw std::runtime_error ("State project contains invalid plug-in state data");
    if (! savedHash.equalsIgnoreCase (juce::SHA256 (state).toHexString()))
        throw std::runtime_error ("State project plug-in state failed its SHA-256 check");

    return state;
}

juce::String getArgumentValue (const juce::StringArray& args, const juce::String& flag)
{
    const auto index = args.indexOf (flag);

    if (index >= 0 && index + 1 < args.size())
        return args[index + 1];

    return {};
}

void printUsage()
{
    std::cout
        << "Resonance Host Probe 0.1.0\n"
        << "Usage:\n"
        << "  ResonanceHostProbe --plugin <bundle.vst3> --wav <output.wav> --report <report.json>"
           " [--state-project <song.resonance.json>]\n";
}

juce::var describePlugin (const juce::PluginDescription& description)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("name", description.name);
    object->setProperty ("descriptiveName", description.descriptiveName);
    object->setProperty ("manufacturer", description.manufacturerName);
    object->setProperty ("version", description.version);
    object->setProperty ("category", description.category);
    object->setProperty ("format", description.pluginFormatName);
    object->setProperty ("identifier", description.createIdentifierString());
    object->setProperty ("fileOrIdentifier", description.fileOrIdentifier);
    object->setProperty ("uniqueId", description.uniqueId);
    object->setProperty ("isInstrument", description.isInstrument);
    object->setProperty ("inputChannels", description.numInputChannels);
    object->setProperty ("outputChannels", description.numOutputChannels);
    return object;
}

juce::AudioPluginFormat* findVst3Format (juce::AudioPluginFormatManager& manager)
{
    for (int i = 0; i < manager.getNumFormats(); ++i)
        if (auto* format = manager.getFormat (i); format != nullptr && format->getName() == "VST3")
            return format;

    return nullptr;
}

std::unique_ptr<juce::AudioPluginInstance> createInstance (
    const juce::AudioPluginFormatManager& manager,
    const juce::PluginDescription& description,
    juce::String& error)
{
    auto instance = manager.createPluginInstance (description, probeSampleRate, blockSize, error);

    if (instance != nullptr)
    {
        instance->enableAllBuses();
        instance->setNonRealtime (true);
    }

    return instance;
}

void addMidiEventIfInsideBlock (juce::MidiBuffer& midi,
                                juce::int64 blockStart,
                                int samplesThisBlock,
                                juce::int64 eventSample,
                                const juce::MidiMessage& message)
{
    if (eventSample >= blockStart && eventSample < blockStart + samplesThisBlock)
        midi.addEvent (message, static_cast<int> (eventSample - blockStart));
}

bool writeWaveFile (const juce::File& outputFile, const juce::AudioBuffer<float>& audio)
{
    outputFile.getParentDirectory().createDirectory();
    outputFile.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = outputFile.createOutputStream();

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (probeSampleRate)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return false;

    const auto wrote = writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    writer->flush();
    return wrote;
}

RenderMetrics renderProbe (juce::AudioPluginInstance& plugin, const juce::File& outputFile)
{
    const auto totalSamples = static_cast<int> (std::llround (probeSampleRate * renderSeconds));
    const auto processingChannels = juce::jmax (2,
                                                plugin.getTotalNumInputChannels(),
                                                plugin.getTotalNumOutputChannels());
    juce::AudioBuffer<float> rendered (2, totalSamples);
    juce::AudioBuffer<float> block (processingChannels, blockSize);
    RenderMetrics metrics;
    metrics.outputChannels = plugin.getTotalNumOutputChannels();
    metrics.renderedSamples = totalSamples;
    rendered.clear();

    OfflinePlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.setRateAndBufferSizeDetails (probeSampleRate, blockSize);
    plugin.prepareToPlay (probeSampleRate, blockSize);
    plugin.reset();
    plugin.getStateInformation (metrics.stateBeforeRender);
    const auto parameterValuesBeforeRender = captureParameterValues (plugin);
    metrics.parameterSha256BeforeRender = parameterSnapshotHash (parameterValuesBeforeRender);

    const auto noteAt = [] (double seconds) {
        return static_cast<juce::int64> (std::llround (seconds * probeSampleRate));
    };

    double sumSquares = 0.0;

    for (int blockStart = 0; blockStart < totalSamples; blockStart += blockSize)
    {
        const auto samplesThisBlock = juce::jmin (blockSize, totalSamples - blockStart);
        block.clear();

        juce::MidiBuffer midi;
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (0.00), juce::MidiMessage::noteOn (1, 48, 0.78f));
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (0.00), juce::MidiMessage::noteOn (1, 55, 0.72f));
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (0.00), juce::MidiMessage::noteOn (1, 60, 0.70f));
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (1.50), juce::MidiMessage::noteOff (1, 48));
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (1.50), juce::MidiMessage::noteOff (1, 55));
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (1.50), juce::MidiMessage::noteOff (1, 60));
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (2.00), juce::MidiMessage::noteOn (1, 67, 0.82f));
        addMidiEventIfInsideBlock (midi, blockStart, samplesThisBlock, noteAt (3.00), juce::MidiMessage::noteOff (1, 67));

        playHead.setSamplePosition (blockStart);

        {
            juce::ScopedNoDenormals noDenormals;
            plugin.processBlock (block, midi);
        }

        for (int destinationChannel = 0; destinationChannel < rendered.getNumChannels(); ++destinationChannel)
        {
            const auto sourceChannel = juce::jmin (destinationChannel, block.getNumChannels() - 1);
            rendered.copyFrom (destinationChannel, blockStart, block, sourceChannel, 0, samplesThisBlock);

            const auto* samples = block.getReadPointer (sourceChannel);

            for (int sample = 0; sample < samplesThisBlock; ++sample)
            {
                const auto value = samples[sample];

                if (! std::isfinite (value))
                {
                    ++metrics.nonFiniteSamples;
                    continue;
                }

                metrics.peak = juce::jmax (metrics.peak, std::abs (value));
                sumSquares += static_cast<double> (value) * static_cast<double> (value);
            }
        }
    }

    plugin.getStateInformation (metrics.stateAfterRender);
    const auto parameterValuesAfterRender = captureParameterValues (plugin);
    metrics.parameterSha256AfterRender = parameterSnapshotHash (parameterValuesAfterRender);
    metrics.changedParametersAfterRender = countChangedParameters (parameterValuesBeforeRender,
                                                                   parameterValuesAfterRender);
    plugin.reset();
    plugin.getStateInformation (metrics.stateAfterReset);
    plugin.setStateInformation (metrics.stateBeforeRender.getData(),
                                static_cast<int> (metrics.stateBeforeRender.getSize()));
    plugin.reset();
    plugin.getStateInformation (metrics.stateAfterPreparedRestore);
    plugin.setStateInformation (metrics.stateAfterPreparedRestore.getData(),
                                static_cast<int> (metrics.stateAfterPreparedRestore.getSize()));
    plugin.reset();
    plugin.getStateInformation (metrics.stateAfterRepeatedPreparedRestore);
    plugin.releaseResources();
    plugin.setPlayHead (nullptr);
    metrics.rms = std::sqrt (sumSquares / static_cast<double> (totalSamples * rendered.getNumChannels()));

    if (! writeWaveFile (outputFile, rendered))
        throw std::runtime_error ("Could not write the WAV output");

    return metrics;
}

int runProbe (const juce::File& pluginPath,
              const juce::File& wavPath,
              const juce::File& reportPath,
              const juce::File& stateProjectPath)
{
    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("probeVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
    reportObject->setProperty ("pluginPath", pluginPath.getFullPathName());
    reportObject->setProperty ("sampleRate", probeSampleRate);
    reportObject->setProperty ("blockSize", blockSize);
    reportObject->setProperty ("renderSeconds", renderSeconds);

    juce::AudioPluginFormatManager manager;
    manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    auto* vst3 = findVst3Format (manager);

    if (vst3 == nullptr)
        throw std::runtime_error ("VST3 hosting was not enabled in this build");

    juce::OwnedArray<juce::PluginDescription> discovered;
    vst3->findAllTypesForFile (discovered, pluginPath.getFullPathName());

    juce::Array<juce::var> discoveredJson;
    for (const auto* description : discovered)
        discoveredJson.add (describePlugin (*description));

    reportObject->setProperty ("discoveredTypes", discoveredJson);

    if (discovered.isEmpty())
        throw std::runtime_error ("No VST3 types were discovered in the supplied bundle");

    auto* selected = discovered[0];
    for (auto* candidate : discovered)
        if (candidate->isInstrument)
            selected = candidate;

    reportObject->setProperty ("selectedPlugin", describePlugin (*selected));

    juce::String error;
    auto firstInstance = createInstance (manager, *selected, error);
    if (firstInstance == nullptr)
        throw std::runtime_error (("Initial plug-in load failed: " + error).toStdString());

    juce::MemoryBlock requestedState;
    juce::String requestedSoundName;
    juce::String requestedStateHash;
    if (stateProjectPath != juce::File())
    {
        if (! stateProjectPath.existsAsFile())
            throw std::runtime_error ("The requested state project does not exist");

        requestedState = loadProjectPluginState (stateProjectPath,
                                                 requestedSoundName,
                                                 requestedStateHash);
        firstInstance->setStateInformation (requestedState.getData(),
                                            static_cast<int> (requestedState.getSize()));
        reportObject->setProperty ("stateProjectPath", stateProjectPath.getFullPathName());
        reportObject->setProperty ("stateProjectSoundName", requestedSoundName);
        reportObject->setProperty ("stateProjectSha256", requestedStateHash);
    }

    juce::MemoryBlock originalState;
    firstInstance->getStateInformation (originalState);
    const auto originalStateHash = juce::SHA256 (originalState.getData(), originalState.getSize()).toHexString();
    firstInstance.reset();

    auto headlessPreparedInstance = createInstance (manager, *selected, error);
    if (headlessPreparedInstance == nullptr)
        throw std::runtime_error (("Headless prepared plug-in load failed: " + error).toStdString());
    headlessPreparedInstance->setNonRealtime (false);
    headlessPreparedInstance->setRateAndBufferSizeDetails (probeSampleRate, blockSize);
    headlessPreparedInstance->prepareToPlay (probeSampleRate, blockSize);
    const auto& headlessInputState = requestedState.isEmpty() ? originalState : requestedState;
    headlessPreparedInstance->setStateInformation (headlessInputState.getData(),
                                                   static_cast<int> (headlessInputState.getSize()));
    headlessPreparedInstance->reset();
    juce::MemoryBlock stateAfterHeadlessPreparedRestore;
    headlessPreparedInstance->getStateInformation (stateAfterHeadlessPreparedRestore);
    headlessPreparedInstance->setStateInformation (stateAfterHeadlessPreparedRestore.getData(),
                                                   static_cast<int> (stateAfterHeadlessPreparedRestore.getSize()));
    headlessPreparedInstance->reset();
    juce::MemoryBlock stateAfterRepeatedHeadlessPreparedRestore;
    headlessPreparedInstance->getStateInformation (stateAfterRepeatedHeadlessPreparedRestore);
    headlessPreparedInstance->releaseResources();
    headlessPreparedInstance.reset();
    const auto headlessPreparedRestoreIdempotent = stateAfterHeadlessPreparedRestore
                                                   == stateAfterRepeatedHeadlessPreparedRestore;

    auto restoredInstance = createInstance (manager, *selected, error);
    if (restoredInstance == nullptr)
        throw std::runtime_error (("State-reload plug-in load failed: " + error).toStdString());

    if (! originalState.isEmpty())
        restoredInstance->setStateInformation (originalState.getData(), static_cast<int> (originalState.getSize()));

    juce::MemoryBlock restoredState;
    restoredInstance->getStateInformation (restoredState);
    const auto restoredStateHash = juce::SHA256 (restoredState.getData(), restoredState.getSize()).toHexString();

    const auto advertisesEditor = restoredInstance->hasEditor();
    auto editorCreated = false;
    auto editorWidth = 0;
    auto editorHeight = 0;

    if (advertisesEditor)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (restoredInstance->createEditorAndMakeActive());
        editorCreated = editor != nullptr;

        if (editor != nullptr)
        {
            editorWidth = editor->getWidth();
            editorHeight = editor->getHeight();
        }
    }

    juce::MemoryBlock stateAfterEditorCreation;
    restoredInstance->getStateInformation (stateAfterEditorCreation);
    const auto stateAfterEditorHash = juce::SHA256 (stateAfterEditorCreation.getData(), stateAfterEditorCreation.getSize()).toHexString();
    const auto parameterCount = restoredInstance->getParameters().size();
    const auto programCount = restoredInstance->getNumPrograms();

    restoredInstance->setStateInformation (originalState.getData(),
                                           static_cast<int> (originalState.getSize()));
    const auto metrics = renderProbe (*restoredInstance, wavPath);
    restoredInstance.reset();

    const auto nonSilent = metrics.peak > 1.0e-5f && metrics.rms > 1.0e-7;
    const auto noInvalidSamples = metrics.nonFiniteSamples == 0;
    const auto stateCaptured = ! originalState.isEmpty();
    const auto waveWritten = wavPath.existsAsFile() && wavPath.getSize() > 44;
    const auto stateStableThroughRender = metrics.stateBeforeRender == metrics.stateAfterRender
                                          && metrics.stateBeforeRender == metrics.stateAfterReset;
    const auto parametersStableThroughRender = metrics.changedParametersAfterRender == 0;
    const auto preparedRestoreIdempotent = metrics.stateAfterPreparedRestore
                                           == metrics.stateAfterRepeatedPreparedRestore;
    const auto passed = nonSilent && noInvalidSamples && stateCaptured && waveWritten
                        && stateStableThroughRender && parametersStableThroughRender
                        && preparedRestoreIdempotent && headlessPreparedRestoreIdempotent;

    reportObject->setProperty ("stateBytes", static_cast<juce::int64> (originalState.getSize()));
    reportObject->setProperty ("restoredStateBytes", static_cast<juce::int64> (restoredState.getSize()));
    reportObject->setProperty ("stateSha256", originalStateHash);
    reportObject->setProperty ("restoredStateSha256", restoredStateHash);
    reportObject->setProperty ("stateByteExactAfterReload", originalStateHash == restoredStateHash);
    reportObject->setProperty ("stateSha256AfterHeadlessPreparedRestore",
                               juce::SHA256 (stateAfterHeadlessPreparedRestore).toHexString());
    reportObject->setProperty ("stateSha256AfterRepeatedHeadlessPreparedRestore",
                               juce::SHA256 (stateAfterRepeatedHeadlessPreparedRestore).toHexString());
    reportObject->setProperty ("headlessPreparedRestoreIdempotent",
                               headlessPreparedRestoreIdempotent);
    reportObject->setProperty ("stateBytesAfterEditorCreation", static_cast<juce::int64> (stateAfterEditorCreation.getSize()));
    reportObject->setProperty ("stateSha256AfterEditorCreation", stateAfterEditorHash);
    reportObject->setProperty ("editorCreationChangedStateBytes", restoredStateHash != stateAfterEditorHash);
    if (! requestedState.isEmpty())
    {
        reportObject->setProperty ("stateProjectRestoredByteExact", requestedState == originalState);
        reportObject->setProperty ("stateProjectBytes", static_cast<juce::int64> (requestedState.getSize()));
        reportObject->setProperty ("stateProjectRestoreDifferingBytes",
                                   countDifferingStateBytes (requestedState, originalState));
        reportObject->setProperty ("stateProjectRestoreCommonPrefixBytes",
                                   countCommonPrefixBytes (requestedState, originalState));
        reportObject->setProperty ("stateProjectTailHex", stateTailHex (requestedState));
        reportObject->setProperty ("stateAfterRestoreTailHex", stateTailHex (originalState));
    }
    reportObject->setProperty ("stateBytesBeforeRender",
                               static_cast<juce::int64> (metrics.stateBeforeRender.getSize()));
    reportObject->setProperty ("stateSha256BeforeRender", juce::SHA256 (metrics.stateBeforeRender).toHexString());
    reportObject->setProperty ("stateBytesAfterRender",
                               static_cast<juce::int64> (metrics.stateAfterRender.getSize()));
    reportObject->setProperty ("stateSha256AfterRender", juce::SHA256 (metrics.stateAfterRender).toHexString());
    reportObject->setProperty ("stateByteExactAfterRender",
                               metrics.stateBeforeRender == metrics.stateAfterRender);
    reportObject->setProperty ("differingStateBytesAfterRender",
                               countDifferingStateBytes (metrics.stateBeforeRender,
                                                        metrics.stateAfterRender));
    reportObject->setProperty ("stateBytesAfterReset",
                               static_cast<juce::int64> (metrics.stateAfterReset.getSize()));
    reportObject->setProperty ("stateSha256AfterReset", juce::SHA256 (metrics.stateAfterReset).toHexString());
    reportObject->setProperty ("stateByteExactAfterReset",
                               metrics.stateBeforeRender == metrics.stateAfterReset);
    reportObject->setProperty ("stateSha256AfterPreparedRestore",
                               juce::SHA256 (metrics.stateAfterPreparedRestore).toHexString());
    reportObject->setProperty ("stateSha256AfterRepeatedPreparedRestore",
                               juce::SHA256 (metrics.stateAfterRepeatedPreparedRestore).toHexString());
    reportObject->setProperty ("preparedRestoreIdempotent", preparedRestoreIdempotent);
    reportObject->setProperty ("parameterSha256BeforeRender", metrics.parameterSha256BeforeRender);
    reportObject->setProperty ("parameterSha256AfterRender", metrics.parameterSha256AfterRender);
    reportObject->setProperty ("changedParametersAfterRender", metrics.changedParametersAfterRender);
    reportObject->setProperty ("stateStableThroughRender", stateStableThroughRender);
    reportObject->setProperty ("parametersStableThroughRender", parametersStableThroughRender);
    reportObject->setProperty ("parameterCount", parameterCount);
    reportObject->setProperty ("programCount", programCount);
    reportObject->setProperty ("advertisesEditor", advertisesEditor);
    reportObject->setProperty ("editorCreated", editorCreated);
    reportObject->setProperty ("editorWidth", editorWidth);
    reportObject->setProperty ("editorHeight", editorHeight);
    reportObject->setProperty ("renderedSamples", metrics.renderedSamples);
    reportObject->setProperty ("reportedOutputChannels", metrics.outputChannels);
    reportObject->setProperty ("peak", metrics.peak);
    reportObject->setProperty ("rms", metrics.rms);
    reportObject->setProperty ("nonFiniteSamples", metrics.nonFiniteSamples);
    reportObject->setProperty ("nonSilent", nonSilent);
    reportObject->setProperty ("wavPath", wavPath.getFullPathName());
    reportObject->setProperty ("wavBytes", wavPath.getSize());
    reportObject->setProperty ("passed", passed);

    reportPath.getParentDirectory().createDirectory();
    if (! reportPath.replaceWithText (juce::JSON::toString (report, true)))
        throw std::runtime_error ("Could not write the JSON report");

    std::cout << juce::JSON::toString (report, true) << std::endl;
    return passed ? 0 : 2;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::StringArray args (argv + 1, argc - 1);

    if (args.contains ("--help") || args.contains ("-h"))
    {
        printUsage();
        return 0;
    }

    const auto plugin = getArgumentValue (args, "--plugin");
    const auto wav = getArgumentValue (args, "--wav");
    const auto report = getArgumentValue (args, "--report");
    const auto stateProject = getArgumentValue (args, "--state-project");

    if (plugin.isEmpty() || wav.isEmpty() || report.isEmpty())
    {
        printUsage();
        return 64;
    }

    try
    {
        const juce::File pluginFile (plugin);
        if (! pluginFile.exists())
            throw std::runtime_error ("The requested VST3 bundle does not exist");

        return runProbe (pluginFile,
                         juce::File (wav),
                         juce::File (report),
                         stateProject.isNotEmpty() ? juce::File (stateProject) : juce::File());
    }
    catch (const std::exception& error)
    {
        std::cerr << "Host probe failed: " << error.what() << std::endl;
        return 1;
    }
}
