#include "realtime_engine.h"

#include <cmath>

namespace resonance
{
RealtimeEngine::RealtimeEngine()
{
    midiCollector.ensureStorageAllocated (4096);
    blockMidi.ensureSize (4096);
    outputGain.setCurrentAndTargetValue (requestedGain.load());
    sequenceSlots[0].sequence = makeStarterSequence();
    sequenceSlots[1].sequence = sequenceSlots[0].sequence;
    pendingSequence = sequenceSlots[0].sequence;
}

RealtimeEngine::~RealtimeEngine()
{
    shutdown();
}

void RealtimeEngine::setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin)
{
    jassert (! prepared.load());
    shutdown();
    const juce::ScopedLock lock (pluginAccess);
    plugin = std::move (newPlugin);

    if (plugin == nullptr)
        return;

    for (int bus = 0; bus < plugin->getBusCount (false); ++bus)
        if (auto* outputBus = plugin->getBus (false, bus))
            outputBus->enable (bus == 0);

    plugin->setNonRealtime (false);
    plugin->setPlayHead (&playHead);
}

void RealtimeEngine::shutdown()
{
    playing.store (false);
    const juce::ScopedLock lock (pluginAccess);

    if (plugin != nullptr)
    {
        if (prepared.exchange (false))
            plugin->releaseResources();

        plugin->setPlayHead (nullptr);
    }

    plugin.reset();
}

void RealtimeEngine::setPlaying (bool shouldPlay) noexcept
{
    if (! shouldPlay)
        panicRequested.store (true);

    playing.store (shouldPlay);
}

void RealtimeEngine::stopAndRewind() noexcept
{
    playing.store (false);
    panicRequested.store (true);
    rewindRequested.store (true);
}

void RealtimeEngine::panic() noexcept
{
    panicRequested.store (true);
}

void RealtimeEngine::setBpm (double newBpm) noexcept
{
    bpm.store (juce::jlimit (40.0, 240.0, newBpm));
}

void RealtimeEngine::setMasterGainDecibels (float decibels) noexcept
{
    requestedGain.store (juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 0.0f, decibels)));
}

void RealtimeEngine::setSequence (const SequenceSnapshot& sequence)
{
    const juce::ScopedLock lock (sequencePublishLock);
    pendingSequence = sequence;
    pendingSequence.noteCount = juce::jmin (pendingSequence.noteCount, maxSequenceNotes);
    pendingSequence.loopBeats = juce::jlimit (4.0, 32.0, pendingSequence.loopBeats);
    hasPendingSequence = true;
    tryPublishPendingSequenceLocked();
}

void RealtimeEngine::flushPendingSequence()
{
    const juce::ScopedLock lock (sequencePublishLock);
    tryPublishPendingSequenceLocked();
}

void RealtimeEngine::tryPublishPendingSequenceLocked()
{
    if (! hasPendingSequence)
        return;

    const auto active = activeSequenceSlot.load (std::memory_order_acquire);
    const auto inactive = 1 - active;
    auto expected = static_cast<juce::uint32> (0);
    if (! sequenceSlots[static_cast<std::size_t> (inactive)].accessState.compare_exchange_strong (
            expected, sequenceWriterBit, std::memory_order_acq_rel))
        return;

    sequenceSlots[static_cast<std::size_t> (inactive)].sequence = pendingSequence;
    sequenceSlots[static_cast<std::size_t> (inactive)].accessState.store (0, std::memory_order_release);
    publishedLoopLength.store (pendingSequence.loopBeats, std::memory_order_release);
    activeSequenceSlot.store (inactive, std::memory_order_release);
    hasPendingSequence = false;
}

juce::Result RealtimeEngine::capturePluginState (juce::MemoryBlock& destination)
{
    const juce::ScopedLock lock (pluginAccess);
    if (plugin == nullptr)
        return juce::Result::fail ("Surge XT is not loaded");

    destination.reset();
    plugin->getStateInformation (destination);
    return destination.getSize() > 0
               ? juce::Result::ok()
               : juce::Result::fail ("Surge XT returned an empty state block");
}

juce::Result RealtimeEngine::restorePluginState (const juce::MemoryBlock& state)
{
    if (state.getSize() == 0)
        return juce::Result::fail ("The project contains an empty Surge XT state block");

    panicRequested.store (true);
    const juce::ScopedLock lock (pluginAccess);
    if (plugin == nullptr)
        return juce::Result::fail ("Surge XT is not loaded");

    plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    if (prepared.load())
        plugin->reset();
    return juce::Result::ok();
}

juce::String RealtimeEngine::getLastDeviceError() const
{
    const juce::ScopedLock lock (deviceErrorLock);
    return lastDeviceError;
}

void RealtimeEngine::silenceOutputs (float* const* outputChannelData,
                                     int numOutputChannels,
                                     int numSamples) const noexcept
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
}

void RealtimeEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                       int numInputChannels,
                                                       float* const* outputChannelData,
                                                       int numOutputChannels,
                                                       int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    silenceOutputs (outputChannelData, numOutputChannels, numSamples);

    if (! prepared.load() || plugin == nullptr || numSamples <= 0)
        return;

    if (numSamples > processCapacity)
    {
        ++oversizedBlocks;
        return;
    }

    juce::AudioBuffer<float> processingBlock (processBuffer.getArrayOfWritePointers(),
                                              processBuffer.getNumChannels(),
                                              numSamples);
    processingBlock.clear();

    const auto inputsToCopy = juce::jmin (numInputChannels, processingBlock.getNumChannels());
    for (int channel = 0; channel < inputsToCopy; ++channel)
        if (inputChannelData[channel] != nullptr)
            processingBlock.copyFrom (channel, 0, inputChannelData[channel], numSamples);

    blockMidi.clear();
    midiCollector.removeNextBlockOfMessages (blockMidi, numSamples);

    if (panicRequested.exchange (false))
    {
        blockMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        blockMidi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
    }

    if (rewindRequested.exchange (false))
    {
        absoluteTransportBeat = 0.0;
        transportSamples = 0;
        blockMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    }

    const auto blockIsPlaying = playing.load();
    const auto rate = currentSampleRate.load();
    const auto tempo = bpm.load();

    auto blockLoopLength = publishedLoopLength.load (std::memory_order_acquire);
    if (blockIsPlaying)
    {
        const SequenceSnapshot* sequence = nullptr;
        int acquiredSlot = -1;

        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto candidate = activeSequenceSlot.load (std::memory_order_acquire);
            auto& state = sequenceSlots[static_cast<std::size_t> (candidate)].accessState;
            const auto previous = state.fetch_add (1, std::memory_order_acquire);

            if ((previous & sequenceWriterBit) != 0
                || candidate != activeSequenceSlot.load (std::memory_order_acquire))
            {
                state.fetch_sub (1, std::memory_order_release);
                continue;
            }

            acquiredSlot = candidate;
            sequence = &sequenceSlots[static_cast<std::size_t> (candidate)].sequence;
            break;
        }

        if (sequence != nullptr)
        {
            blockLoopLength = sequence->loopBeats;
            LoopScheduler::addBlock (blockMidi, absoluteTransportBeat, tempo, rate, numSamples, *sequence);
            sequenceSlots[static_cast<std::size_t> (acquiredSlot)].accessState.fetch_sub (1,
                                                                                         std::memory_order_release);
        }
    }

    playHead.update (transportSamples,
                     absoluteTransportBeat,
                     rate,
                     tempo,
                     blockLoopLength,
                     blockIsPlaying);

    const juce::ScopedTryLock pluginLock (pluginAccess);
    if (pluginLock.isLocked())
    {
        try
        {
            const juce::ScopedNoDenormals noDenormals;
            plugin->processBlock (processingBlock, blockMidi);
        }
        catch (...)
        {
            processingBlock.clear();
            ++processorExceptions;
        }
    }

    if (blockIsPlaying)
    {
        absoluteTransportBeat += tempo * static_cast<double> (numSamples) / (60.0 * rate);
        transportSamples += numSamples;
    }

    displayBeat.store (std::fmod (absoluteTransportBeat, blockLoopLength));

    const auto targetGain = requestedGain.load();
    if (std::abs (targetGain - outputGain.getTargetValue()) > 1.0e-6f)
        outputGain.setTargetValue (targetGain);

    float peaks[2] { 0.0f, 0.0f };
    juce::int64 blockClips = 0;
    juce::int64 blockInvalid = 0;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = outputGain.getNextValue();

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] == nullptr)
                continue;

            const auto sourceChannel = juce::jmin (channel, processingBlock.getNumChannels() - 1);
            auto value = processingBlock.getSample (sourceChannel, sample) * gain;

            if (! std::isfinite (value))
            {
                value = 0.0f;
                ++blockInvalid;
            }

            if (std::abs (value) > 1.0f)
                ++blockClips;

            if (channel < 2)
                peaks[channel] = juce::jmax (peaks[channel], std::abs (value));

            outputChannelData[channel][sample] = juce::jlimit (-1.0f, 1.0f, value);
        }
    }

    leftPeak.store (peaks[0]);
    rightPeak.store (numOutputChannels > 1 ? peaks[1] : peaks[0]);
    clippedSamples.fetch_add (blockClips);
    invalidSamples.fetch_add (blockInvalid);
}

void RealtimeEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock lock (pluginAccess);
    if (plugin == nullptr || device == nullptr)
        return;

    if (prepared.exchange (false))
        plugin->releaseResources();

    const auto rate = device->getCurrentSampleRate();
    const auto blockSize = device->getCurrentBufferSizeSamples();
    const auto processingChannels = juce::jmax (2,
                                                plugin->getTotalNumInputChannels(),
                                                plugin->getTotalNumOutputChannels());

    processCapacity = juce::jmax (4096, blockSize * 2);
    processBuffer.setSize (processingChannels, processCapacity, false, false, true);
    processBuffer.clear();
    blockMidi.ensureSize (4096);
    midiCollector.reset (rate);
    outputGain.reset (rate, 0.02);
    outputGain.setCurrentAndTargetValue (requestedGain.load());

    currentSampleRate.store (rate);
    currentBlockSize.store (blockSize);
    absoluteTransportBeat = 0.0;
    transportSamples = 0;
    displayBeat.store (0.0);
    rewindRequested.store (false);

    plugin->setRateAndBufferSizeDetails (rate, blockSize);
    plugin->prepareToPlay (rate, blockSize);
    plugin->reset();
    prepared.store (true);

    const juce::ScopedLock deviceLock (deviceErrorLock);
    lastDeviceError.clear();
}

void RealtimeEngine::audioDeviceStopped()
{
    playing.store (false);
    panicRequested.store (false);

    const juce::ScopedLock lock (pluginAccess);

    if (plugin != nullptr && prepared.exchange (false))
        plugin->releaseResources();

    currentSampleRate.store (0.0);
    currentBlockSize.store (0);
    leftPeak.store (0.0f);
    rightPeak.store (0.0f);
}

void RealtimeEngine::audioDeviceError (const juce::String& errorMessage)
{
    const juce::ScopedLock lock (deviceErrorLock);
    lastDeviceError = errorMessage;
}

void RealtimeEngine::RealtimePlayHead::update (juce::int64 samples,
                                               double newAbsoluteBeat,
                                               double sampleRate,
                                               double bpmValue,
                                               double loopBeats,
                                               bool isPlaying) noexcept
{
    timeInSamples.store (samples);
    absolutePpq.store (newAbsoluteBeat);
    rate.store (sampleRate);
    tempo.store (bpmValue);
    loopDuration.store (loopBeats);
    transportPlaying.store (isPlaying);
}

juce::Optional<juce::AudioPlayHead::PositionInfo>
RealtimeEngine::RealtimePlayHead::getPosition() const
{
    const auto samples = timeInSamples.load();
    const auto sampleRate = rate.load();
    const auto absoluteBeat = absolutePpq.load();
    const auto loopBeats = loopDuration.load();
    const auto loopBeat = std::fmod (absoluteBeat, loopBeats);

    PositionInfo position;
    position.setTimeInSamples (samples);
    position.setTimeInSeconds (sampleRate > 0.0 ? static_cast<double> (samples) / sampleRate : 0.0);
    position.setBpm (tempo.load());
    position.setTimeSignature (TimeSignature { 4, 4 });
    position.setPpqPosition (loopBeat);
    position.setPpqPositionOfLastBarStart (std::floor (loopBeat / 4.0) * 4.0);
    position.setLoopPoints (LoopPoints { 0.0, loopBeats });
    position.setIsLooping (true);
    position.setIsPlaying (transportPlaying.load());
    return position;
}
} // namespace resonance
