#include "realtime_engine.h"

#include <cmath>

namespace resonance
{
namespace
{
constexpr int midiScratchBytes = 65536;
constexpr int maximumRoutedInputEvents = 2048;

MixerSnapshot makeInitialMixerSnapshot()
{
    MixerSnapshot result;
    result.trackCount = 1;
    result.tracks[0].enabled = true;
    result.tracks[0].sequence = makeStarterSequence();
    return result;
}

MixerSnapshot sanitiseMixerSnapshot (const MixerSnapshot& source)
{
    auto result = source;
    result.trackCount = juce::jmin (result.trackCount, maxMixerTracks);
    const auto maximumTrackGain = juce::Decibels::decibelsToGain (12.0f);

    for (std::size_t index = 0; index < maxMixerTracks; ++index)
    {
        auto& track = result.tracks[index];
        track.sequence.noteCount = juce::jmin (track.sequence.noteCount, maxSequenceNotes);
        track.sequence.loopBeats = std::isfinite (track.sequence.loopBeats)
                                       ? juce::jlimit (minimumLoopBeats, maximumLoopBeats, track.sequence.loopBeats)
                                       : loopLengthBeats;
        track.gainLinear = std::isfinite (track.gainLinear)
                               ? juce::jlimit (0.0f, maximumTrackGain, track.gainLinear)
                               : 0.0f;
        track.pan = std::isfinite (track.pan)
                        ? juce::jlimit (-1.0f, 1.0f, track.pan)
                        : 0.0f;
        track.midiInputChannel = juce::jlimit (0, 16, track.midiInputChannel);
        track.midiOutputChannel = juce::jlimit (1, 16, track.midiOutputChannel);

        if (index >= result.trackCount)
            track.enabled = false;
    }

    return result;
}
} // namespace

RealtimeEngine::RealtimeEngine()
{
    midiCollector.ensureStorageAllocated (midiScratchBytes);
    hardwareInputMidi.ensureSize (midiScratchBytes);
    for (auto& slot : runtimeSlots)
        slot.midi.ensureSize (midiScratchBytes);

    outputGain.setCurrentAndTargetValue (requestedGain.load());
    pendingMixer = makeInitialMixerSnapshot();
    mixerSlots[0].snapshot = pendingMixer;
    mixerSlots[1].snapshot = pendingMixer;
}

RealtimeEngine::~RealtimeEngine()
{
    shutdown();
}

void RealtimeEngine::configurePlugin (juce::AudioPluginInstance& instance)
{
    for (int bus = 0; bus < instance.getBusCount (false); ++bus)
        if (auto* outputBus = instance.getBus (false, bus))
            outputBus->enable (bus == 0);

    instance.setNonRealtime (false);
    instance.setPlayHead (&playHead);
}

void RealtimeEngine::setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin)
{
    shutdown();
    const auto result = setPluginForTrack (0, std::move (newPlugin));
    jassert (result.wasOk());
    juce::ignoreUnused (result);
}

juce::Result RealtimeEngine::setPluginForTrack (
    std::size_t trackIndex,
    std::unique_ptr<juce::AudioPluginInstance> newPlugin)
{
    if (trackIndex >= maxMixerTracks)
        return juce::Result::fail ("Track plug-in slot is outside the fixed mixer capacity");

    if (prepared.load (std::memory_order_acquire))
        return juce::Result::fail ("Stop the prepared runtime before changing plug-in topology");

    const juce::ScopedLock lock (pluginAccess);
    if (prepared.load (std::memory_order_acquire))
        return juce::Result::fail ("Stop the prepared runtime before changing plug-in topology");

    auto& slot = runtimeSlots[trackIndex];
    if (slot.resourcesPrepared && slot.plugin != nullptr)
        slot.plugin->releaseResources();
    slot.resourcesPrepared = false;

    if (slot.plugin != nullptr)
        slot.plugin->setPlayHead (nullptr);

    slot.plugin = std::move (newPlugin);
    slot.leftPeak.store (0.0f);
    slot.rightPeak.store (0.0f);
    slot.processedBlocks.store (0);

    if (slot.plugin != nullptr)
        configurePlugin (*slot.plugin);

    return juce::Result::ok();
}

juce::AudioPluginInstance* RealtimeEngine::getPluginForTrack (std::size_t trackIndex) const noexcept
{
    return trackIndex < maxMixerTracks ? runtimeSlots[trackIndex].plugin.get() : nullptr;
}

std::size_t RealtimeEngine::getActivePluginCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& slot : runtimeSlots)
        count += slot.plugin != nullptr ? 1u : 0u;
    return count;
}

void RealtimeEngine::releaseRenderResourcesLocked()
{
    prepared.store (false, std::memory_order_release);

    for (auto& slot : runtimeSlots)
    {
        if (slot.resourcesPrepared && slot.plugin != nullptr)
            slot.plugin->releaseResources();
        slot.resourcesPrepared = false;
        slot.leftPeak.store (0.0f);
        slot.rightPeak.store (0.0f);
    }
}

void RealtimeEngine::shutdown()
{
    playing.store (false);
    const juce::ScopedLock lock (pluginAccess);
    releaseRenderResourcesLocked();

    for (auto& slot : runtimeSlots)
    {
        if (slot.plugin != nullptr)
            slot.plugin->setPlayHead (nullptr);
        slot.plugin.reset();
        slot.processedBlocks.store (0);
    }
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
    const juce::ScopedLock lock (mixerPublishLock);
    if (pendingMixer.trackCount == 0)
    {
        pendingMixer.trackCount = 1;
        pendingMixer.tracks[0].enabled = true;
    }

    pendingMixer.tracks[0].sequence = sequence;
    pendingMixer = sanitiseMixerSnapshot (pendingMixer);
    hasPendingMixer = true;
    tryPublishPendingMixerLocked();
}

void RealtimeEngine::setMixerSnapshot (const MixerSnapshot& snapshot)
{
    const juce::ScopedLock lock (mixerPublishLock);
    pendingMixer = sanitiseMixerSnapshot (snapshot);
    hasPendingMixer = true;
    tryPublishPendingMixerLocked();
}

void RealtimeEngine::flushPendingSequence()
{
    const juce::ScopedLock lock (mixerPublishLock);
    tryPublishPendingMixerLocked();
}

void RealtimeEngine::tryPublishPendingMixerLocked()
{
    if (! hasPendingMixer)
        return;

    const auto active = activeMixerSlot.load (std::memory_order_acquire);
    const auto inactive = 1 - active;
    auto expected = static_cast<juce::uint32> (0);
    if (! mixerSlots[static_cast<std::size_t> (inactive)].accessState.compare_exchange_strong (
            expected, mixerWriterBit, std::memory_order_acq_rel))
        return;

    mixerSlots[static_cast<std::size_t> (inactive)].snapshot = pendingMixer;
    mixerSlots[static_cast<std::size_t> (inactive)].accessState.store (0, std::memory_order_release);
    const auto loopBeats = pendingMixer.trackCount > 0
                               ? pendingMixer.tracks[0].sequence.loopBeats
                               : loopLengthBeats;
    publishedLoopLength.store (loopBeats, std::memory_order_release);
    activeMixerSlot.store (inactive, std::memory_order_release);
    hasPendingMixer = false;
}

juce::Result RealtimeEngine::capturePluginState (juce::MemoryBlock& destination)
{
    return capturePluginStateForTrack (0, destination);
}

juce::Result RealtimeEngine::capturePluginStateForTrack (std::size_t trackIndex,
                                                         juce::MemoryBlock& destination)
{
    if (trackIndex >= maxMixerTracks)
        return juce::Result::fail ("Track plug-in slot is outside the fixed mixer capacity");

    const juce::ScopedLock lock (pluginAccess);
    auto* plugin = runtimeSlots[trackIndex].plugin.get();
    if (plugin == nullptr)
        return juce::Result::fail (trackIndex == 0 ? "Surge XT is not loaded"
                                                   : "Track plug-in is not loaded");

    destination.reset();
    plugin->getStateInformation (destination);
    return destination.getSize() > 0
               ? juce::Result::ok()
               : juce::Result::fail ("The track plug-in returned an empty state block");
}

juce::Result RealtimeEngine::restorePluginState (const juce::MemoryBlock& state,
                                                 juce::MemoryBlock* liveStateAfterRestore)
{
    return restorePluginStateForTrack (0, state, liveStateAfterRestore);
}

juce::Result RealtimeEngine::restorePluginStateForTrack (
    std::size_t trackIndex,
    const juce::MemoryBlock& state,
    juce::MemoryBlock* liveStateAfterRestore)
{
    if (trackIndex >= maxMixerTracks)
        return juce::Result::fail ("Track plug-in slot is outside the fixed mixer capacity");

    if (state.getSize() == 0)
        return juce::Result::fail ("The project contains an empty track plug-in state block");

    panicRequested.store (true);
    juce::uint64 processedAtRestore = 0;
    {
        const juce::ScopedLock lock (pluginAccess);
        auto* plugin = runtimeSlots[trackIndex].plugin.get();
        if (plugin == nullptr)
            return juce::Result::fail (trackIndex == 0 ? "Surge XT is not loaded"
                                                       : "Track plug-in is not loaded");

        plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        if (runtimeSlots[trackIndex].resourcesPrepared)
            plugin->reset();

        processedAtRestore = processedBlockCount.load (std::memory_order_acquire);
    }

    if (liveStateAfterRestore != nullptr)
    {
        if (prepared.load (std::memory_order_acquire))
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 250.0;
            while (processedBlockCount.load (std::memory_order_acquire) < processedAtRestore + 2
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep (1);
        }

        const auto capture = capturePluginStateForTrack (trackIndex, *liveStateAfterRestore);
        if (capture.failed())
            return capture;
    }

    return juce::Result::ok();
}

juce::Result RealtimeEngine::prepareRenderLocked (double sampleRate, int maximumBlockSize)
{
    releaseRenderResourcesLocked();

    if (sampleRate <= 0.0 || maximumBlockSize <= 0)
        return juce::Result::fail ("The render sample rate and block size must be positive");

    if (getActivePluginCount() == 0)
        return juce::Result::fail ("No accepted instrument is installed in the runtime");

    processCapacity = juce::jmax (4096, maximumBlockSize * 2);
    mixBuffer.setSize (2, processCapacity, false, false, true);
    mixBuffer.clear();
    hardwareInputMidi.ensureSize (midiScratchBytes);
    midiCollector.reset (sampleRate);
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (requestedGain.load());

    currentSampleRate.store (sampleRate);
    currentBlockSize.store (maximumBlockSize);
    absoluteTransportBeat = 0.0;
    transportSamples = 0;
    displayBeat.store (0.0);
    rewindRequested.store (false);
    panicRequested.store (false);
    clippedSamples.store (0);
    invalidSamples.store (0);
    oversizedBlocks.store (0);
    processorExceptions.store (0);
    processedBlockCount.store (0);
    lastCallbackLoad.store (0.0f);
    maximumCallbackLoad.store (0.0f);

    try
    {
        for (auto& slot : runtimeSlots)
        {
            slot.leftPeak.store (0.0f);
            slot.rightPeak.store (0.0f);
            slot.processedBlocks.store (0);
            if (slot.plugin == nullptr)
                continue;

            const auto processingChannels = juce::jmax (2,
                                                        slot.plugin->getTotalNumInputChannels(),
                                                        slot.plugin->getTotalNumOutputChannels());
            slot.processBuffer.setSize (processingChannels, processCapacity, false, false, true);
            slot.processBuffer.clear();
            slot.midi.ensureSize (midiScratchBytes);
            slot.plugin->setRateAndBufferSizeDetails (sampleRate, maximumBlockSize);
            slot.resourcesPrepared = true;
            slot.plugin->prepareToPlay (sampleRate, maximumBlockSize);
            slot.plugin->reset();
        }
    }
    catch (...)
    {
        releaseRenderResourcesLocked();
        currentSampleRate.store (0.0);
        currentBlockSize.store (0);
        return juce::Result::fail ("A track plug-in threw while preparing the fixed runtime");
    }

    prepared.store (true, std::memory_order_release);
    return juce::Result::ok();
}

juce::Result RealtimeEngine::prepareForOfflineRender (double sampleRate, int maximumBlockSize)
{
    const juce::ScopedLock lock (pluginAccess);
    return prepareRenderLocked (sampleRate, maximumBlockSize);
}

void RealtimeEngine::releaseOfflineRender()
{
    playing.store (false);
    panicRequested.store (false);
    const juce::ScopedLock lock (pluginAccess);
    releaseRenderResourcesLocked();
    currentSampleRate.store (0.0);
    currentBlockSize.store (0);
    leftPeak.store (0.0f);
    rightPeak.store (0.0f);
}

float RealtimeEngine::getTrackLeftPeak (std::size_t trackIndex) const noexcept
{
    return trackIndex < maxMixerTracks ? runtimeSlots[trackIndex].leftPeak.load() : 0.0f;
}

float RealtimeEngine::getTrackRightPeak (std::size_t trackIndex) const noexcept
{
    return trackIndex < maxMixerTracks ? runtimeSlots[trackIndex].rightPeak.load() : 0.0f;
}

juce::uint64 RealtimeEngine::getTrackProcessedBlockCount (std::size_t trackIndex) const noexcept
{
    return trackIndex < maxMixerTracks ? runtimeSlots[trackIndex].processedBlocks.load() : 0;
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

void RealtimeEngine::recordCallbackLoad (juce::int64 startTicks,
                                         int numSamples,
                                         double sampleRate) noexcept
{
    if (numSamples <= 0 || sampleRate <= 0.0)
        return;

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - startTicks);
    const auto blockDuration = static_cast<double> (numSamples) / sampleRate;
    const auto load = static_cast<float> (elapsed / blockDuration);
    lastCallbackLoad.store (load);

    auto previousMaximum = maximumCallbackLoad.load();
    while (load > previousMaximum
           && ! maximumCallbackLoad.compare_exchange_weak (previousMaximum, load))
    {
    }
}

void RealtimeEngine::audioDeviceIOCallbackWithContext (
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    silenceOutputs (outputChannelData, numOutputChannels, numSamples);

    if (! prepared.load (std::memory_order_acquire) || numSamples <= 0)
        return;

    const auto callbackStart = juce::Time::getHighResolutionTicks();
    const auto rate = currentSampleRate.load();
    if (rate <= 0.0)
        return;

    if (numSamples > processCapacity)
    {
        ++oversizedBlocks;
        recordCallbackLoad (callbackStart, numSamples, rate);
        return;
    }

    const MixerSnapshot* mixer = nullptr;
    int acquiredMixerSlot = -1;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto candidate = activeMixerSlot.load (std::memory_order_acquire);
        auto& state = mixerSlots[static_cast<std::size_t> (candidate)].accessState;
        const auto previous = state.fetch_add (1, std::memory_order_acquire);

        if ((previous & mixerWriterBit) != 0
            || candidate != activeMixerSlot.load (std::memory_order_acquire))
        {
            state.fetch_sub (1, std::memory_order_release);
            continue;
        }

        acquiredMixerSlot = candidate;
        mixer = &mixerSlots[static_cast<std::size_t> (candidate)].snapshot;
        break;
    }

    if (mixer == nullptr)
    {
        recordCallbackLoad (callbackStart, numSamples, rate);
        return;
    }

    juce::AudioBuffer<float> mixBlock (mixBuffer.getArrayOfWritePointers(),
                                       mixBuffer.getNumChannels(),
                                       numSamples);
    mixBlock.clear();
    hardwareInputMidi.clear();
    midiCollector.removeNextBlockOfMessages (hardwareInputMidi, numSamples);

    const auto panicForBlock = panicRequested.exchange (false);
    const auto rewindForBlock = rewindRequested.exchange (false);
    if (rewindForBlock)
    {
        absoluteTransportBeat = 0.0;
        transportSamples = 0;
    }

    const auto blockIsPlaying = playing.load();
    const auto tempo = bpm.load();
    auto blockLoopLength = mixer->trackCount > 0
                               ? mixer->tracks[0].sequence.loopBeats
                               : publishedLoopLength.load (std::memory_order_acquire);

    playHead.update (transportSamples,
                     absoluteTransportBeat,
                     rate,
                     tempo,
                     blockLoopLength,
                     blockIsPlaying);

    juce::int64 blockInvalid = 0;
    for (auto& slot : runtimeSlots)
    {
        slot.leftPeak.store (0.0f);
        slot.rightPeak.store (0.0f);
    }

    {
        const juce::ScopedTryLock pluginLock (pluginAccess);
        if (pluginLock.isLocked())
        {
            const auto trackCount = juce::jmin (mixer->trackCount, maxMixerTracks);
            for (std::size_t trackIndex = 0; trackIndex < trackCount; ++trackIndex)
            {
                const auto& track = mixer->tracks[trackIndex];
                auto& slot = runtimeSlots[trackIndex];
                if (! track.enabled || slot.plugin == nullptr || ! slot.resourcesPrepared)
                    continue;

                juce::AudioBuffer<float> processingBlock (slot.processBuffer.getArrayOfWritePointers(),
                                                           slot.processBuffer.getNumChannels(),
                                                           numSamples);
                processingBlock.clear();
                const auto inputsToCopy = juce::jmin (numInputChannels,
                                                      processingBlock.getNumChannels());
                for (int channel = 0; channel < inputsToCopy; ++channel)
                    if (inputChannelData[channel] != nullptr)
                        processingBlock.copyFrom (channel, 0, inputChannelData[channel], numSamples);

                slot.midi.clear();
                int routedInputEvents = 0;
                for (const auto metadata : hardwareInputMidi)
                {
                    if (routedInputEvents >= maximumRoutedInputEvents)
                        break;

                    auto message = metadata.getMessage();
                    const auto sourceChannel = message.getChannel();
                    if (track.midiInputChannel != 0 && sourceChannel != 0
                        && sourceChannel != track.midiInputChannel)
                        continue;

                    if (sourceChannel > 0)
                        message.setChannel (track.midiOutputChannel);
                    slot.midi.addEvent (message, metadata.samplePosition);
                    ++routedInputEvents;
                }

                if (panicForBlock)
                {
                    slot.midi.addEvent (juce::MidiMessage::allNotesOff (track.midiOutputChannel), 0);
                    slot.midi.addEvent (juce::MidiMessage::allSoundOff (track.midiOutputChannel), 0);
                }
                if (rewindForBlock)
                    slot.midi.addEvent (juce::MidiMessage::allNotesOff (track.midiOutputChannel), 0);

                if (blockIsPlaying)
                    LoopScheduler::addBlock (slot.midi,
                                             absoluteTransportBeat,
                                             tempo,
                                             rate,
                                             numSamples,
                                             track.sequence,
                                             track.midiOutputChannel);

                try
                {
                    const juce::ScopedNoDenormals noDenormals;
                    slot.plugin->processBlock (processingBlock, slot.midi);
                    slot.processedBlocks.fetch_add (1, std::memory_order_release);
                }
                catch (...)
                {
                    processingBlock.clear();
                    ++processorExceptions;
                }

                const auto trackGain = resolveStereoTrackGain (*mixer, trackIndex);
                float trackLeftPeak = 0.0f;
                float trackRightPeak = 0.0f;
                const auto rightSourceChannel = slot.plugin->getTotalNumOutputChannels() > 1 ? 1 : 0;

                for (int sample = 0; sample < numSamples; ++sample)
                {
                    auto left = processingBlock.getSample (0, sample) * trackGain.left;
                    auto right = processingBlock.getSample (rightSourceChannel, sample) * trackGain.right;
                    if (! std::isfinite (left))
                    {
                        left = 0.0f;
                        ++blockInvalid;
                    }
                    if (! std::isfinite (right))
                    {
                        right = 0.0f;
                        ++blockInvalid;
                    }

                    mixBlock.addSample (0, sample, left);
                    mixBlock.addSample (1, sample, right);
                    trackLeftPeak = juce::jmax (trackLeftPeak, std::abs (left));
                    trackRightPeak = juce::jmax (trackRightPeak, std::abs (right));
                }

                slot.leftPeak.store (juce::jmin (1.0f, trackLeftPeak));
                slot.rightPeak.store (juce::jmin (1.0f, trackRightPeak));
            }

            processedBlockCount.fetch_add (1, std::memory_order_release);
        }
        else
        {
            if (panicForBlock)
                panicRequested.store (true);
            if (rewindForBlock)
                rewindRequested.store (true);
        }
    }

    mixerSlots[static_cast<std::size_t> (acquiredMixerSlot)].accessState.fetch_sub (
        1, std::memory_order_release);

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
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = outputGain.getNextValue();
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] == nullptr)
                continue;

            const auto sourceChannel = juce::jmin (channel, 1);
            auto value = mixBlock.getSample (sourceChannel, sample) * gain;
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

    leftPeak.store (juce::jmin (1.0f, peaks[0]));
    rightPeak.store (juce::jmin (1.0f, numOutputChannels > 1 ? peaks[1] : peaks[0]));
    clippedSamples.fetch_add (blockClips);
    invalidSamples.fetch_add (blockInvalid);
    recordCallbackLoad (callbackStart, numSamples, rate);
}

void RealtimeEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    juce::Result result = juce::Result::fail ("No audio device was provided");
    if (device != nullptr)
    {
        const juce::ScopedLock lock (pluginAccess);
        result = prepareRenderLocked (device->getCurrentSampleRate(),
                                      device->getCurrentBufferSizeSamples());
    }

    const juce::ScopedLock deviceLock (deviceErrorLock);
    lastDeviceError = result.failed() ? result.getErrorMessage() : juce::String {};
}

void RealtimeEngine::audioDeviceStopped()
{
    releaseOfflineRender();
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
