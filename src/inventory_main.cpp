#include <JuceHeader.h>

#include "plugin_bundle_identity.h"

#include <iostream>
#include <stdexcept>

namespace
{
struct ScopedDelete
{
    explicit ScopedDelete (juce::File fileToDelete) : file (std::move (fileToDelete)) {}
    ~ScopedDelete() { file.deleteFile(); }

    juce::File file;
};

juce::String getArgumentValue (const juce::StringArray& args, const juce::String& flag)
{
    const auto index = args.indexOf (flag);
    return index >= 0 && index + 1 < args.size() ? args[index + 1] : juce::String {};
}

void printUsage()
{
    std::cout
        << "Resonance Plugin Inventory 0.1.0\n"
        << "Usage:\n"
        << "  ResonancePluginInventory --scanner <scanner.exe> --plugin <bundle.vst3>\n"
        << "      --inventory <plugin-inventory.json> --quarantine <plugin-quarantine.json>\n"
        << "      [--timeout-ms 20000]\n";
}

juce::String nowIso8601()
{
    return juce::Time::getCurrentTime().toISO8601 (true);
}

juce::Array<juce::var> loadEntries (const juce::File& file, const juce::Identifier& property)
{
    juce::Array<juce::var> entries;

    if (! file.existsAsFile())
        return entries;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    const auto* object = parsed.getDynamicObject();

    if (object == nullptr)
        throw std::runtime_error ("Existing collection is not a JSON object");

    if (static_cast<int> (object->getProperty ("schemaVersion")) != 1)
        throw std::runtime_error ("Existing collection has an unsupported schema version");

    const auto value = object->getProperty (property);
    if (const auto* existing = value.getArray())
        entries = *existing;
    else
        throw std::runtime_error ("Existing collection has an invalid entries property");

    return entries;
}

void removeBundleEntries (juce::Array<juce::var>& entries, const juce::String& bundlePath)
{
    for (int index = entries.size(); --index >= 0;)
    {
        const auto* object = entries[index].getDynamicObject();
        if (object != nullptr && object->getProperty ("bundlePath").toString().equalsIgnoreCase (bundlePath))
            entries.remove (index);
    }
}

void removeAcceptedPluginEntries (juce::Array<juce::var>& entries,
                                  const juce::String& bundlePath,
                                  const juce::DynamicObject& replacement)
{
    const auto replacementFormat = replacement.getProperty ("format").toString();
    const auto replacementName = replacement.getProperty ("name").toString();
    const auto replacementManufacturer = replacement.getProperty ("manufacturer").toString();
    const auto replacementUniqueId = static_cast<int> (replacement.getProperty ("uniqueId"));

    for (int index = entries.size(); --index >= 0;)
    {
        const auto* object = entries[index].getDynamicObject();
        if (object == nullptr)
            continue;

        const auto sameBundle = object->getProperty ("bundlePath").toString().equalsIgnoreCase (bundlePath);
        const auto samePluginIdentity = object->getProperty ("format").toString().equalsIgnoreCase (replacementFormat)
            && object->getProperty ("name").toString().equalsIgnoreCase (replacementName)
            && object->getProperty ("manufacturer").toString().equalsIgnoreCase (replacementManufacturer)
            && static_cast<int> (object->getProperty ("uniqueId")) == replacementUniqueId;

        if (sameBundle || samePluginIdentity)
            entries.remove (index);
    }
}

bool writeCollection (const juce::File& file,
                      const juce::Identifier& property,
                      const juce::Array<juce::var>& entries)
{
    auto* rootObject = new juce::DynamicObject();
    juce::var root (rootObject);
    rootObject->setProperty ("schemaVersion", 1);
    rootObject->setProperty ("updatedAt", nowIso8601());
    rootObject->setProperty (property, entries);

    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (root, false));
}

int quarantine (const juce::File& inventoryFile,
                const juce::File& quarantineFile,
                const juce::String& bundlePath,
                const resonance::BundleIdentity& bundleIdentity,
                const juce::File& scannerFile,
                const juce::String& failureKind,
                const juce::String& detail,
                int timeoutMs,
                juce::int64 exitCode,
                int returnCode)
{
    auto inventoryEntries = loadEntries (inventoryFile, "plugins");
    removeBundleEntries (inventoryEntries, bundlePath);

    if (! writeCollection (inventoryFile, "plugins", inventoryEntries))
        throw std::runtime_error ("Could not evict the failed bundle from the plug-in inventory");

    auto entries = loadEntries (quarantineFile, "entries");
    removeBundleEntries (entries, bundlePath);

    auto* entryObject = new juce::DynamicObject();
    juce::var entry (entryObject);
    entryObject->setProperty ("bundlePath", bundlePath);
    entryObject->setProperty ("bundleFingerprintSha256", bundleIdentity.fingerprint);
    entryObject->setProperty ("bundleFileCount", bundleIdentity.fileCount);
    entryObject->setProperty ("bundleBytes", bundleIdentity.totalBytes);
    entryObject->setProperty ("scannerPath", scannerFile.getFullPathName());
    entryObject->setProperty ("quarantinedAt", nowIso8601());
    entryObject->setProperty ("failureKind", failureKind);
    entryObject->setProperty ("detail", detail.substring (0, 4000));
    entryObject->setProperty ("timeoutMs", timeoutMs);
    entryObject->setProperty ("exitCode", exitCode);
    entries.add (entry);

    if (! writeCollection (quarantineFile, "entries", entries))
        throw std::runtime_error ("Could not atomically update the quarantine file");

    auto* summaryObject = new juce::DynamicObject();
    juce::var summary (summaryObject);
    summaryObject->setProperty ("status", "quarantined");
    summaryObject->setProperty ("failureKind", failureKind);
    summaryObject->setProperty ("bundlePath", bundlePath);
    summaryObject->setProperty ("quarantinePath", quarantineFile.getFullPathName());
    summaryObject->setProperty ("exitCode", exitCode);
    std::cout << juce::JSON::toString (summary, true) << std::endl;
    return returnCode;
}

int runInventory (const juce::File& scannerFile,
                  const juce::File& pluginBundle,
                  const juce::File& inventoryFile,
                  const juce::File& quarantineFile,
                  int timeoutMs)
{
    if (! scannerFile.existsAsFile())
        throw std::runtime_error ("The scanner executable does not exist");

    if (! pluginBundle.exists())
        throw std::runtime_error ("The requested VST3 bundle does not exist");

    const auto bundlePath = pluginBundle.getFullPathName();
    const auto bundleIdentity = resonance::identifyBundle (pluginBundle);
    const auto reportFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getNonexistentChildFile ("resonance-vst-scan-", ".json", false);
    ScopedDelete reportCleanup (reportFile);

    juce::StringArray command;
    command.add (scannerFile.getFullPathName());
    command.add ("--plugin");
    command.add (bundlePath);
    command.add ("--report");
    command.add (reportFile.getFullPathName());

    juce::ChildProcess child;
    const auto started = juce::Time::getMillisecondCounterHiRes();

    // Do not capture arbitrary plug-in output: an unbounded child can fill a pipe and
    // prevent the parent from reaching its timeout. Structured failures use reportFile.
    if (! child.start (command, 0))
        return quarantine (inventoryFile,
                           quarantineFile,
                           bundlePath,
                           bundleIdentity,
                           scannerFile,
                           "launch-failed",
                           "The scanner helper could not be launched",
                           timeoutMs,
                           -1,
                           20);

    if (! child.waitForProcessToFinish (timeoutMs))
    {
        const auto killed = child.kill();
        return quarantine (inventoryFile,
                           quarantineFile,
                           bundlePath,
                           bundleIdentity,
                           scannerFile,
                           "timeout",
                           killed ? "Scanner exceeded its deadline and was terminated"
                                  : "Scanner exceeded its deadline; termination could not be confirmed",
                           timeoutMs,
                           -1,
                           21);
    }

    const auto exitCode = static_cast<juce::int64> (child.getExitCode());

    if (exitCode != 0)
    {
        juce::String detail ("Scanner returned a non-zero exit code");

        if (reportFile.existsAsFile())
        {
            const auto failureReport = juce::JSON::parse (reportFile.loadFileAsString());
            if (const auto* failureObject = failureReport.getDynamicObject())
            {
                const auto structuredError = failureObject->getProperty ("error").toString();
                if (structuredError.isNotEmpty())
                    detail = structuredError;
            }
        }

        return quarantine (inventoryFile,
                           quarantineFile,
                           bundlePath,
                           bundleIdentity,
                           scannerFile,
                           "scanner-exit",
                           detail,
                           timeoutMs,
                           exitCode,
                           22);
    }

    if (! reportFile.existsAsFile())
        return quarantine (inventoryFile,
                           quarantineFile,
                           bundlePath,
                           bundleIdentity,
                           scannerFile,
                           "missing-report",
                           "Scanner exited successfully without producing its report",
                           timeoutMs,
                           exitCode,
                           23);

    auto report = juce::JSON::parse (reportFile.loadFileAsString());
    auto* reportObject = report.getDynamicObject();

    if (reportObject == nullptr || ! static_cast<bool> (reportObject->getProperty ("passed")))
        return quarantine (inventoryFile,
                           quarantineFile,
                           bundlePath,
                           bundleIdentity,
                           scannerFile,
                           "invalid-report",
                           "Scanner report was malformed or did not pass",
                           timeoutMs,
                           exitCode,
                           24);

    auto pluginRecord = reportObject->getProperty ("plugin");
    auto* pluginObject = pluginRecord.getDynamicObject();

    if (pluginObject == nullptr)
        return quarantine (inventoryFile,
                           quarantineFile,
                           bundlePath,
                           bundleIdentity,
                           scannerFile,
                           "invalid-plugin-record",
                           "Scanner report did not contain a plug-in object",
                           timeoutMs,
                           exitCode,
                           25);

    pluginObject->setProperty ("bundlePath", bundlePath);
    pluginObject->setProperty ("bundleFingerprintSha256", bundleIdentity.fingerprint);
    pluginObject->setProperty ("bundleFileCount", bundleIdentity.fileCount);
    pluginObject->setProperty ("bundleBytes", bundleIdentity.totalBytes);
    pluginObject->setProperty ("lastScannedAt", reportObject->getProperty ("scannedAt"));
    pluginObject->setProperty ("scanDurationMs", reportObject->getProperty ("scanDurationMs"));
    pluginObject->setProperty ("scannerVersion", reportObject->getProperty ("scannerVersion"));

    auto inventoryEntries = loadEntries (inventoryFile, "plugins");
    removeAcceptedPluginEntries (inventoryEntries, bundlePath, *pluginObject);
    inventoryEntries.add (pluginRecord);

    if (! writeCollection (inventoryFile, "plugins", inventoryEntries))
        throw std::runtime_error ("Could not atomically update the plug-in inventory");

    auto quarantineEntries = loadEntries (quarantineFile, "entries");
    removeBundleEntries (quarantineEntries, bundlePath);

    if (! writeCollection (quarantineFile, "entries", quarantineEntries))
        throw std::runtime_error ("Could not atomically clear the plug-in quarantine entry");

    auto* summaryObject = new juce::DynamicObject();
    juce::var summary (summaryObject);
    summaryObject->setProperty ("status", "accepted");
    summaryObject->setProperty ("bundlePath", bundlePath);
    summaryObject->setProperty ("identifier", pluginObject->getProperty ("identifier"));
    summaryObject->setProperty ("inventoryPath", inventoryFile.getFullPathName());
    summaryObject->setProperty ("quarantinePath", quarantineFile.getFullPathName());
    summaryObject->setProperty ("elapsedMs", juce::Time::getMillisecondCounterHiRes() - started);
    std::cout << juce::JSON::toString (summary, true) << std::endl;
    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args (argv + 1, argc - 1);

    if (args.contains ("--help") || args.contains ("-h"))
    {
        printUsage();
        return 0;
    }

    const auto scanner = getArgumentValue (args, "--scanner");
    const auto plugin = getArgumentValue (args, "--plugin");
    const auto inventory = getArgumentValue (args, "--inventory");
    const auto quarantineFile = getArgumentValue (args, "--quarantine");
    const auto timeoutText = getArgumentValue (args, "--timeout-ms");

    if (scanner.isEmpty() || plugin.isEmpty() || inventory.isEmpty() || quarantineFile.isEmpty())
    {
        printUsage();
        return 64;
    }

    const auto timeoutMs = juce::jlimit (50, 120000, timeoutText.isNotEmpty() ? timeoutText.getIntValue() : 20000);

    try
    {
        return runInventory (juce::File (scanner),
                             juce::File (plugin),
                             juce::File (inventory),
                             juce::File (quarantineFile),
                             timeoutMs);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Inventory controller failed: " << error.what() << std::endl;
        return 1;
    }
}
