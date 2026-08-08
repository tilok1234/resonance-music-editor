#pragma once

#include <JuceHeader.h>

#include "loop_scheduler.h"

#include <array>
#include <atomic>
#include <memory>

namespace resonance
{
class RealtimeEngine final : public juce::AudioIODeviceCallback
{
public:
    RealtimeEngine();
    ~RealtimeEngine() override;

    void setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin);
    void shutdown();

    juce::AudioPluginInstance* getPlugin() const noexcept { return plugin.get(); }
    juce::MidiMessageCollector& getMidiCollector() noexcept { return midiCollector; }

    void setPlaying (bool shouldPlay) noexcept;
    void stopAndRewind() noexcept;
    void panic() noexcept;
    bool isPlaying() const noexcept { return playing.load(); }

    void setBpm (double newBpm) noexcept;
    double getBpm() const noexcept { return bpm.load(); }
    void setMasterGainDecibels (float decibels) noexcept;
    void setSequence (const SequenceSnapshot& sequence);
    void flushPendingSequence();

    juce::Result capturePluginState (juce::MemoryBlock& destination);
    juce::Result restorePluginState (const juce::MemoryBlock& state,
                                     juce::MemoryBlock* liveStateAfterRestore = nullptr);

    bool isPrepared() const noexcept { return prepared.load(); }
    double getDisplayBeat() const noexcept { return displayBeat.load(); }
    double getSampleRate() const noexcept { return currentSampleRate.load(); }
    int getBlockSize() const noexcept { return currentBlockSize.load(); }
    float getLeftPeak() const noexcept { return leftPeak.load(); }
    float getRightPeak() const noexcept { return rightPeak.load(); }
    juce::int64 getClippedSampleCount() const noexcept { return clippedSamples.load(); }
    juce::int64 getInvalidSampleCount() const noexcept { return invalidSamples.load(); }
    int getOversizedBlockCount() const noexcept { return oversizedBlocks.load(); }
    int getProcessorExceptionCount() const noexcept { return processorExceptions.load(); }
    juce::String getLastDeviceError() const;

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

private:
    class RealtimePlayHead final : public juce::AudioPlayHead
    {
    public:
        void update (juce::int64 samples,
                     double absoluteBeat,
                     double sampleRate,
                     double bpm,
                     double loopBeats,
                     bool isPlaying) noexcept;
        juce::Optional<PositionInfo> getPosition() const override;

    private:
        std::atomic<juce::int64> timeInSamples { 0 };
        std::atomic<double> absolutePpq { 0.0 };
        std::atomic<double> rate { 48000.0 };
        std::atomic<double> tempo { 120.0 };
        std::atomic<double> loopDuration { loopLengthBeats };
        std::atomic<bool> transportPlaying { false };
    };

    void silenceOutputs (float* const* outputChannelData,
                         int numOutputChannels,
                         int numSamples) const noexcept;
    void tryPublishPendingSequenceLocked();

    struct SequenceSlot
    {
        SequenceSnapshot sequence;
        std::atomic<juce::uint32> accessState { 0 };
    };

    static constexpr juce::uint32 sequenceWriterBit = 0x80000000u;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    RealtimePlayHead playHead;
    juce::AudioBuffer<float> processBuffer;
    juce::MidiBuffer blockMidi;
    juce::MidiMessageCollector midiCollector;
    juce::SmoothedValue<float> outputGain;

    std::atomic<bool> playing { false };
    std::atomic<bool> rewindRequested { true };
    std::atomic<bool> panicRequested { false };
    std::atomic<bool> prepared { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> requestedGain { 0.25118864f };
    std::atomic<double> displayBeat { 0.0 };
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<float> leftPeak { 0.0f };
    std::atomic<float> rightPeak { 0.0f };
    std::atomic<juce::int64> clippedSamples { 0 };
    std::atomic<juce::int64> invalidSamples { 0 };
    std::atomic<int> oversizedBlocks { 0 };
    std::atomic<int> processorExceptions { 0 };
    std::atomic<juce::uint64> processedBlockCount { 0 };

    double absoluteTransportBeat = 0.0;
    juce::int64 transportSamples = 0;
    int processCapacity = 0;
    std::array<SequenceSlot, 2> sequenceSlots;
    std::atomic<int> activeSequenceSlot { 0 };
    std::atomic<double> publishedLoopLength { loopLengthBeats };
    juce::CriticalSection sequencePublishLock;
    SequenceSnapshot pendingSequence;
    bool hasPendingSequence = false;
    juce::CriticalSection pluginAccess;
    mutable juce::CriticalSection deviceErrorLock;
    juce::String lastDeviceError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RealtimeEngine)
};
} // namespace resonance
