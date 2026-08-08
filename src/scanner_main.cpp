#include <JuceHeader.h>

#include <iostream>
#include <stdexcept>

namespace
{
constexpr double probeSampleRate = 48000.0;
constexpr int probeBlockSize = 512;

juce::String getArgumentValue (const juce::StringArray& args, const juce::String& flag)
{
    const auto index = args.indexOf (flag);
    return index >= 0 && index + 1 < args.size() ? args[index + 1] : juce::String {};
}

void printUsage()
{
    std::cout
        << "Resonance Plugin Scanner 0.1.0\n"
        << "Usage:\n"
        << "  ResonancePluginScanner --plugin <bundle.vst3> --report <scan-report.json>\n";
}

juce::var descriptionToVar (const juce::PluginDescription& description)
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

bool writeJson (const juce::File& file, const juce::var& value)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (value, false));
}

int scanPlugin (const juce::File& pluginBundle, const juce::File& reportFile)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();

    juce::AudioPluginFormatManager manager;
    manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    auto* format = manager.getFormat (0);

    if (format == nullptr || format->getName() != "VST3")
        throw std::runtime_error ("VST3 format registration failed");

    juce::OwnedArray<juce::PluginDescription> discovered;
    format->findAllTypesForFile (discovered, pluginBundle.getFullPathName());

    if (discovered.isEmpty())
        throw std::runtime_error ("No VST3 types were discovered in the supplied bundle");

    auto* selected = discovered[0];
    for (auto* candidate : discovered)
        if (candidate->isInstrument)
            selected = candidate;

    juce::String loadError;
    auto instance = manager.createPluginInstance (*selected,
                                                  probeSampleRate,
                                                  probeBlockSize,
                                                  loadError);

    if (instance == nullptr)
        throw std::runtime_error (("VST3 instantiation failed: " + loadError).toStdString());

    instance->enableAllBuses();
    instance->setNonRealtime (true);

    juce::MemoryBlock state;
    instance->getStateInformation (state);
    const auto stateHash = state.isEmpty()
        ? juce::String {}
        : juce::SHA256 (state.getData(), state.getSize()).toHexString();

    juce::Array<juce::var> discoveredTypes;
    for (const auto* description : discovered)
        discoveredTypes.add (descriptionToVar (*description));

    auto selectedPlugin = descriptionToVar (*selected);
    auto* selectedObject = selectedPlugin.getDynamicObject();
    selectedObject->setProperty ("bundlePath", pluginBundle.getFullPathName());
    selectedObject->setProperty ("acceptsMidi", instance->acceptsMidi());
    selectedObject->setProperty ("producesMidi", instance->producesMidi());
    selectedObject->setProperty ("hasEditor", instance->hasEditor());
    selectedObject->setProperty ("parameterCount", instance->getParameters().size());
    selectedObject->setProperty ("programCount", instance->getNumPrograms());
    selectedObject->setProperty ("latencySamples", instance->getLatencySamples());
    selectedObject->setProperty ("tailLengthSeconds", instance->getTailLengthSeconds());
    selectedObject->setProperty ("inputBusCount", instance->getBusCount (true));
    selectedObject->setProperty ("outputBusCount", instance->getBusCount (false));
    selectedObject->setProperty ("enabledInputChannels", instance->getTotalNumInputChannels());
    selectedObject->setProperty ("enabledOutputChannels", instance->getTotalNumOutputChannels());
    selectedObject->setProperty ("supportsDoublePrecision", instance->supportsDoublePrecisionProcessing());
    selectedObject->setProperty ("stateBytes", static_cast<juce::int64> (state.getSize()));
    selectedObject->setProperty ("stateSha256", stateHash);

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("scannerVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
    reportObject->setProperty ("scannedAt", juce::Time::getCurrentTime().toISO8601 (true));
    reportObject->setProperty ("scanDurationMs", juce::Time::getMillisecondCounterHiRes() - started);
    reportObject->setProperty ("bundlePath", pluginBundle.getFullPathName());
    reportObject->setProperty ("discoveredTypes", discoveredTypes);
    reportObject->setProperty ("plugin", selectedPlugin);
    reportObject->setProperty ("passed", true);

    instance.reset();

    if (! writeJson (reportFile, report))
        throw std::runtime_error ("Could not atomically write the scan report");

    std::cout << juce::JSON::toString (report, true) << std::endl;
    return 0;
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

    const auto pluginPath = getArgumentValue (args, "--plugin");
    const auto reportPath = getArgumentValue (args, "--report");

    if (pluginPath.isEmpty() || reportPath.isEmpty())
    {
        printUsage();
        return 64;
    }

    try
    {
        const juce::File pluginBundle (pluginPath);
        if (! pluginBundle.exists())
            throw std::runtime_error ("The requested VST3 bundle does not exist");

        return scanPlugin (pluginBundle, juce::File (reportPath));
    }
    catch (const std::exception& error)
    {
        auto* errorObject = new juce::DynamicObject();
        juce::var errorReport (errorObject);
        errorObject->setProperty ("schemaVersion", 1);
        errorObject->setProperty ("scannerVersion", JUCE_APPLICATION_VERSION_STRING);
        errorObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());
        errorObject->setProperty ("scannedAt", juce::Time::getCurrentTime().toISO8601 (true));
        errorObject->setProperty ("bundlePath", pluginPath);
        errorObject->setProperty ("passed", false);
        errorObject->setProperty ("error", juce::String (error.what()).substring (0, 4000));

        if (reportPath.isNotEmpty())
            writeJson (juce::File (reportPath), errorReport);

        std::cerr << "Scanner failed: " << error.what() << std::endl;
        return 1;
    }
}
