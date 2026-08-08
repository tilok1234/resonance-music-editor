#include "known_plugin.h"

#include "plugin_bundle_identity.h"

#include <stdexcept>

namespace resonance
{
namespace
{
const juce::Array<juce::var>& requireArray (const juce::DynamicObject& root,
                                            const juce::Identifier& property,
                                            const juce::String& label)
{
    if (const auto* values = root.getProperty (property).getArray())
        return *values;

    throw std::runtime_error ((label + " does not contain a valid " + property.toString() + " array").toStdString());
}

juce::String requiredString (const juce::DynamicObject& object,
                             const juce::Identifier& property)
{
    const auto value = object.getProperty (property).toString();
    if (value.isEmpty())
        throw std::runtime_error (("Inventory record is missing " + property.toString()).toStdString());
    return value;
}
} // namespace

juce::Result loadFirstAcceptedInstrument (const juce::File& inventoryFile,
                                          const juce::File& quarantineFile,
                                          KnownPluginRecord& destination)
{
    try
    {
        if (! inventoryFile.existsAsFile())
            throw std::runtime_error (("Plug-in inventory does not exist: "
                                       + inventoryFile.getFullPathName()).toStdString());

        const auto inventoryJson = juce::JSON::parse (inventoryFile.loadFileAsString());
        const auto* inventoryRoot = inventoryJson.getDynamicObject();
        if (inventoryRoot == nullptr || static_cast<int> (inventoryRoot->getProperty ("schemaVersion")) != 1)
            throw std::runtime_error ("Plug-in inventory has an invalid schema");

        const auto& plugins = requireArray (*inventoryRoot, "plugins", "Plug-in inventory");
        const juce::DynamicObject* selected = nullptr;

        for (const auto& pluginValue : plugins)
        {
            const auto* candidate = pluginValue.getDynamicObject();
            if (candidate != nullptr
                && static_cast<bool> (candidate->getProperty ("isInstrument"))
                && candidate->getProperty ("format").toString() == "VST3")
            {
                selected = candidate;
                break;
            }
        }

        if (selected == nullptr)
            throw std::runtime_error ("Plug-in inventory has no accepted VST3 instrument");

        KnownPluginRecord record;
        record.identifier = requiredString (*selected, "identifier");
        record.bundleFingerprint = requiredString (*selected, "bundleFingerprintSha256");
        record.bundlePath = juce::File (requiredString (*selected, "bundlePath"));
        record.hasEditor = static_cast<bool> (selected->getProperty ("hasEditor"));
        record.expectedParameterCount = static_cast<int> (selected->getProperty ("parameterCount"));
        record.bundleFileCount = static_cast<int> (selected->getProperty ("bundleFileCount"));
        record.bundleBytes = static_cast<juce::int64> (selected->getProperty ("bundleBytes"));

        auto& description = record.description;
        description.name = requiredString (*selected, "name");
        description.descriptiveName = selected->getProperty ("descriptiveName").toString();
        description.pluginFormatName = requiredString (*selected, "format");
        description.category = selected->getProperty ("category").toString();
        description.manufacturerName = selected->getProperty ("manufacturer").toString();
        description.version = selected->getProperty ("version").toString();
        description.fileOrIdentifier = requiredString (*selected, "fileOrIdentifier");
        description.uniqueId = static_cast<int> (selected->getProperty ("uniqueId"));
        description.isInstrument = static_cast<bool> (selected->getProperty ("isInstrument"));
        description.numInputChannels = static_cast<int> (selected->getProperty ("inputChannels"));
        description.numOutputChannels = static_cast<int> (selected->getProperty ("outputChannels"));

        if (description.descriptiveName.isEmpty())
            description.descriptiveName = description.name;

        if (description.createIdentifierString() != record.identifier)
            throw std::runtime_error ("Inventory fields do not reproduce the scanned-path plug-in identifier");

        if (! record.bundlePath.exists())
            throw std::runtime_error (("Accepted VST3 bundle is missing: "
                                       + record.bundlePath.getFullPathName()).toStdString());

        if (! juce::File (description.fileOrIdentifier).existsAsFile())
            throw std::runtime_error (("Accepted VST3 module is missing: "
                                       + description.fileOrIdentifier).toStdString());

        if (! quarantineFile.existsAsFile())
            throw std::runtime_error (("Plug-in quarantine does not exist: "
                                       + quarantineFile.getFullPathName()).toStdString());

        const auto quarantineJson = juce::JSON::parse (quarantineFile.loadFileAsString());
        const auto* quarantineRoot = quarantineJson.getDynamicObject();
        if (quarantineRoot == nullptr || static_cast<int> (quarantineRoot->getProperty ("schemaVersion")) != 1)
            throw std::runtime_error ("Plug-in quarantine has an invalid schema");

        const auto& quarantineEntries = requireArray (*quarantineRoot, "entries", "Plug-in quarantine");
        for (const auto& entryValue : quarantineEntries)
        {
            const auto* entry = entryValue.getDynamicObject();
            if (entry != nullptr
                && entry->getProperty ("bundlePath").toString().equalsIgnoreCase (
                    record.bundlePath.getFullPathName()))
            {
                throw std::runtime_error ("The accepted VST3 path is quarantined; rescan it before loading");
            }
        }

        const auto liveIdentity = identifyBundle (record.bundlePath);
        if (! liveIdentity.fingerprint.equalsIgnoreCase (record.bundleFingerprint)
            || liveIdentity.fileCount != record.bundleFileCount
            || liveIdentity.totalBytes != record.bundleBytes)
        {
            throw std::runtime_error ("The VST3 bundle changed after scanning; rescan is required");
        }

        destination = std::move (record);
        return juce::Result::ok();
    }
    catch (const std::exception& error)
    {
        return juce::Result::fail (error.what());
    }
}
} // namespace resonance
