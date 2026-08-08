#include <JuceHeader.h>

#include <cmath>
#include <iostream>
#include <limits>

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
};

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
        << "  ResonanceHostProbe --plugin <bundle.vst3> --wav <output.wav> --report <report.json>\n";
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
    rendered.clear();

    OfflinePlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.setRateAndBufferSizeDetails (probeSampleRate, blockSize);
    plugin.prepareToPlay (probeSampleRate, blockSize);
    plugin.reset();

    const auto noteAt = [] (double seconds) {
        return static_cast<juce::int64> (std::llround (seconds * probeSampleRate));
    };

    double sumSquares = 0.0;
    RenderMetrics metrics;
    metrics.outputChannels = plugin.getTotalNumOutputChannels();
    metrics.renderedSamples = totalSamples;

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

    plugin.releaseResources();
    plugin.setPlayHead (nullptr);
    metrics.rms = std::sqrt (sumSquares / static_cast<double> (totalSamples * rendered.getNumChannels()));

    if (! writeWaveFile (outputFile, rendered))
        throw std::runtime_error ("Could not write the WAV output");

    return metrics;
}

int runProbe (const juce::File& pluginPath, const juce::File& wavPath, const juce::File& reportPath)
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

    juce::MemoryBlock originalState;
    firstInstance->getStateInformation (originalState);
    const auto originalStateHash = juce::SHA256 (originalState.getData(), originalState.getSize()).toHexString();
    firstInstance.reset();

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

    const auto metrics = renderProbe (*restoredInstance, wavPath);
    restoredInstance.reset();

    const auto nonSilent = metrics.peak > 1.0e-5f && metrics.rms > 1.0e-7;
    const auto noInvalidSamples = metrics.nonFiniteSamples == 0;
    const auto stateCaptured = ! originalState.isEmpty();
    const auto waveWritten = wavPath.existsAsFile() && wavPath.getSize() > 44;
    const auto passed = nonSilent && noInvalidSamples && stateCaptured && waveWritten;

    reportObject->setProperty ("stateBytes", static_cast<juce::int64> (originalState.getSize()));
    reportObject->setProperty ("restoredStateBytes", static_cast<juce::int64> (restoredState.getSize()));
    reportObject->setProperty ("stateSha256", originalStateHash);
    reportObject->setProperty ("restoredStateSha256", restoredStateHash);
    reportObject->setProperty ("stateByteExactAfterReload", originalStateHash == restoredStateHash);
    reportObject->setProperty ("stateBytesAfterEditorCreation", static_cast<juce::int64> (stateAfterEditorCreation.getSize()));
    reportObject->setProperty ("stateSha256AfterEditorCreation", stateAfterEditorHash);
    reportObject->setProperty ("editorCreationChangedStateBytes", restoredStateHash != stateAfterEditorHash);
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

        return runProbe (pluginFile, juce::File (wav), juce::File (report));
    }
    catch (const std::exception& error)
    {
        std::cerr << "Host probe failed: " << error.what() << std::endl;
        return 1;
    }
}
