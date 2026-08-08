#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <stdexcept>

namespace resonance
{
struct BundleIdentity
{
    juce::String fingerprint;
    int fileCount = 0;
    juce::int64 totalBytes = 0;
};

inline BundleIdentity identifyBundle (const juce::File& bundle)
{
    juce::Array<juce::File> files;

    if (bundle.existsAsFile())
        files.add (bundle);
    else
        bundle.findChildFiles (files, juce::File::findFiles, true);

    if (files.isEmpty())
        throw std::runtime_error ("The VST3 bundle does not contain any files");

    std::sort (files.begin(), files.end(), [&bundle] (const auto& left, const auto& right)
    {
        return left.getRelativePathFrom (bundle).compareIgnoreCase (
                   right.getRelativePathFrom (bundle)) < 0;
    });

    BundleIdentity identity;
    juce::String manifest;

    for (const auto& file : files)
    {
        const auto relativePath = bundle.existsAsFile()
            ? file.getFileName()
            : file.getRelativePathFrom (bundle).replaceCharacter ('\\', '/');
        const auto bytes = file.getSize();

        manifest << relativePath << "|" << bytes << "|"
                 << juce::SHA256 (file).toHexString() << "\n";
        ++identity.fileCount;
        identity.totalBytes += bytes;
    }

    identity.fingerprint = juce::SHA256 (
        manifest.toRawUTF8(),
        static_cast<size_t> (manifest.getNumBytesAsUTF8())).toHexString();
    return identity;
}
} // namespace resonance
