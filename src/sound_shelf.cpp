#include "sound_shelf.h"

#include <algorithm>

namespace resonance
{
namespace
{
juce::String requireString (const juce::DynamicObject& object, const juce::Identifier& key)
{
    return object.getProperty (key).toString();
}

juce::Result validateEntry (const SoundShelfEntry& entry)
{
    const auto name = entry.name.trim();
    if (name.isEmpty())
        return juce::Result::fail ("A shelf sound needs a name");
    if (name.length() > SoundShelf::maximumNameLength)
        return juce::Result::fail ("A shelf sound name must contain at most "
                                   + juce::String (SoundShelf::maximumNameLength)
                                   + " characters");
    if (entry.pluginIdentifier.isEmpty() || entry.pluginName.isEmpty())
        return juce::Result::fail ("A shelf sound needs its plug-in identity");
    if (entry.state.getSize() == 0)
        return juce::Result::fail ("A shelf sound needs non-empty state");
    if (! entry.stateSha256.equalsIgnoreCase (juce::SHA256 (entry.state).toHexString()))
        return juce::Result::fail ("Shelf sound '" + name + "' failed its SHA-256 integrity check");

    return juce::Result::ok();
}
} // namespace

const SoundShelfEntry* SoundShelf::find (const juce::String& name) const
{
    const auto match = std::find_if (entries.begin(),
                                     entries.end(),
                                     [&name] (const SoundShelfEntry& entry)
                                     {
                                         return entry.name.equalsIgnoreCase (name.trim());
                                     });
    return match != entries.end() ? &(*match) : nullptr;
}

juce::Result SoundShelf::add (SoundShelfEntry entry)
{
    entry.name = entry.name.trim();
    const auto validation = validateEntry (entry);
    if (validation.failed())
        return validation;

    if (find (entry.name) != nullptr)
        return juce::Result::fail ("A shelf sound named '" + entry.name + "' already exists");
    if (entries.size() >= maximumEntries)
        return juce::Result::fail ("The shelf holds at most "
                                   + juce::String (static_cast<int> (maximumEntries))
                                   + " sounds");

    entries.push_back (std::move (entry));
    return juce::Result::ok();
}

juce::Result SoundShelf::remove (const juce::String& name)
{
    const auto trimmed = name.trim();
    const auto match = std::find_if (entries.begin(),
                                     entries.end(),
                                     [&trimmed] (const SoundShelfEntry& entry)
                                     {
                                         return entry.name.equalsIgnoreCase (trimmed);
                                     });
    if (match == entries.end())
        return juce::Result::fail ("No shelf sound named '" + trimmed + "'");

    entries.erase (match);
    return juce::Result::ok();
}

void SoundShelf::clear()
{
    entries.clear();
}

juce::Result SoundShelf::loadFrom (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        entries.clear();
        return juce::Result::ok();
    }

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return juce::Result::fail ("The sound shelf is not a JSON object");

    int schemaVersion = 0;
    if (! root->hasProperty ("schemaVersion")
        || ! root->getProperty ("schemaVersion").isInt()
        || (schemaVersion = static_cast<int> (root->getProperty ("schemaVersion")))
               != supportedSchemaVersion)
        return juce::Result::fail ("Unsupported sound shelf schema version");

    const auto soundsValue = root->getProperty ("sounds");
    const auto* sounds = soundsValue.getArray();
    if (sounds == nullptr)
        return juce::Result::fail ("The sound shelf is missing its sounds array");
    if (sounds->size() > static_cast<int> (maximumEntries))
        return juce::Result::fail ("The sound shelf exceeds "
                                   + juce::String (static_cast<int> (maximumEntries))
                                   + " sounds");

    // Build a candidate shelf first so an invalid file never replaces good entries.
    std::vector<SoundShelfEntry> candidate;
    for (const auto& soundValue : *sounds)
    {
        auto* soundObject = soundValue.getDynamicObject();
        if (soundObject == nullptr)
            return juce::Result::fail ("Every shelf sound must be a JSON object");

        SoundShelfEntry entry;
        entry.name = requireString (*soundObject, "name").trim();
        entry.pluginIdentifier = requireString (*soundObject, "pluginIdentifier");
        entry.pluginName = requireString (*soundObject, "pluginName");
        entry.vendor = requireString (*soundObject, "vendor");
        entry.version = requireString (*soundObject, "version");
        entry.stateSha256 = requireString (*soundObject, "stateSha256");

        if (requireString (*soundObject, "stateEncoding") != "base64")
            return juce::Result::fail ("Shelf sound '" + entry.name
                                       + "' must use base64 state encoding");
        if (! entry.state.fromBase64Encoding (requireString (*soundObject, "state")))
            return juce::Result::fail ("Shelf sound '" + entry.name
                                       + "' state is not valid Base64 data");

        const auto validation = validateEntry (entry);
        if (validation.failed())
            return validation;

        const auto duplicate = std::any_of (candidate.begin(),
                                            candidate.end(),
                                            [&entry] (const SoundShelfEntry& existing)
                                            {
                                                return existing.name.equalsIgnoreCase (entry.name);
                                            });
        if (duplicate)
            return juce::Result::fail ("The sound shelf contains duplicate name '"
                                       + entry.name + "'");

        candidate.push_back (std::move (entry));
    }

    entries = std::move (candidate);
    return juce::Result::ok();
}

juce::Result SoundShelf::saveTo (const juce::File& file) const
{
    auto* root = new juce::DynamicObject();
    juce::var document (root);
    root->setProperty ("schemaVersion", supportedSchemaVersion);
    root->setProperty ("editorVersion", JUCE_APPLICATION_VERSION_STRING);

    juce::Array<juce::var> sounds;
    for (const auto& entry : entries)
    {
        auto* soundObject = new juce::DynamicObject();
        juce::var sound (soundObject);
        soundObject->setProperty ("name", entry.name);
        soundObject->setProperty ("pluginIdentifier", entry.pluginIdentifier);
        soundObject->setProperty ("pluginName", entry.pluginName);
        soundObject->setProperty ("vendor", entry.vendor);
        soundObject->setProperty ("version", entry.version);
        soundObject->setProperty ("stateEncoding", "base64");
        soundObject->setProperty ("state", entry.state.toBase64Encoding());
        soundObject->setProperty ("stateSha256", entry.stateSha256.toLowerCase());
        sounds.add (sound);
    }
    root->setProperty ("sounds", sounds);

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Could not create the sound shelf directory");
    if (! file.replaceWithText (juce::JSON::toString (document, false)))
        return juce::Result::fail ("Could not write the sound shelf file");

    return juce::Result::ok();
}
} // namespace resonance
