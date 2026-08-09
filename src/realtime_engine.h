#pragma once

#include <JuceHeader.h>

#include "mixer_snapshot.h"

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
    juce::Result setPluginForTrack (std::size_t trackIndex,
                                    std::unique_ptr<juce::AudioPluginInstance> newPlugin);
    void shutdown();

    juce::AudioPluginInstance* getPlugin() const noexcept { return getPluginForTrack (0); }
    juce::AudioPluginInstance* getPluginForTrack (std::size_t trackIndex) const noexcept;
    std::size_t getActivePluginCount() const noexcept;
    juce::MidiMessageCollector& getMidiCollector() noexcept { return midiCollector; }

    void setPlaying (bool shouldPlay) noexcept;
    void stopAndRewind() noexcept;
    void panic() noexcept;
    bool isPlaying() const noexcept { return playing.load(); }

    void setBpm (double newBpm) noexcept;
    double getBpm() const noexcept { return bpm.load(); }
    void setMasterGainDecibels (float decibels) noexcept;
    void setSequence (const SequenceSnapshot& sequence);
    void setMixerSnapshot (const MixerSnapshot& snapshot);
    void flushPendingSequence();

    juce::Result capturePluginState (juce::MemoryBlock& destination);
    juce::Result capturePluginStateForTrack (std::size_t trackIndex,
                                             juce::MemoryBlock& destination);
    juce::Result restorePluginState (const juce::MemoryBlock& state,
                                     juce::MemoryBlock* liveStateAfterRestore = nullptr);
    juce::Result restorePluginStateForTrack (std::size_t trackIndex,
                                             const juce::MemoryBlock& state,
                                             juce::MemoryBlock* liveStateAfterRestore = nullptr);

    // Prepares the same fixed-capacity render path without attaching it to an
    // output device. This is used by silent native and packaged verification.
    juce::Result prepareForOfflineRender (double sampleRate, int maximumBlockSize);
    void releaseOfflineRender();

    bool isPrepared() const noexcept { return prepared.load(); }
    double getDisplayBeat() const noexcept { return displayBeat.load(); }
    double getSampleRate() const noexcept { return currentSampleRate.load(); }
    int getBlockSize() const noexcept { return currentBlockSize.load(); }
    float getLeftPeak() const noexcept { return leftPeak.load(); }
    float getRightPeak() const noexcept { return rightPeak.load(); }
    float getTrackLeftPeak (std::size_t trackIndex) const noexcept;
    float getTrackRightPeak (std::size_t trackIndex) const noexcept;
    juce::uint64 getTrackProcessedBlockCount (std::size_t trackIndex) const noexcept;
    juce::int64 getClippedSampleCount() const noexcept { return clippedSamples.load(); }
    juce::int64 getInvalidSampleCount() const noexcept { return invalidSamples.load(); }
    int getOversizedBlockCount() const noexcept { return oversizedBlocks.load(); }
    int getProcessorExceptionCount() const noexcept { return processorExceptions.load(); }
    float getLastCallbackLoad() const noexcept { return lastCallbackLoad.load(); }
    float getMaximumCallbackLoad() const noexcept { return maximumCallbackLoad.load(); }
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
    juce::Result prepareRenderLocked (double sampleRate, int maximumBlockSize);
    void releaseRenderResourcesLocked();
    void configurePlugin (juce::AudioPluginInstance& instance);
    void tryPublishPendingMixerLocked();
    void recordCallbackLoad (juce::int64 startTicks, int numSamples, double sampleRate) noexcept;

    struct RuntimeSlot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::AudioBuffer<float> processBuffer;
        juce::MidiBuffer midi;
        bool resourcesPrepared = false;
        std::atomic<float> leftPeak { 0.0f };
        std::atomic<float> rightPeak { 0.0f };
        std::atomic<juce::uint64> processedBlocks { 0 };
    };

    struct MixerPublicationSlot
    {
        MixerSnapshot snapshot;
        std::atomic<juce::uint32> accessState { 0 };
    };

    static constexpr juce::uint32 mixerWriterBit = 0x80000000u;

    std::array<RuntimeSlot, maxMixerTracks> runtimeSlots;
    RealtimePlayHead playHead;
    juce::AudioBuffer<float> mixBuffer;
    juce::MidiBuffer hardwareInputMidi;
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
    std::atomic<float> lastCallbackLoad { 0.0f };
    std::atomic<float> maximumCallbackLoad { 0.0f };

    double absoluteTransportBeat = 0.0;
    juce::int64 transportSamples = 0;
    int processCapacity = 0;
    std::array<MixerPublicationSlot, 2> mixerSlots;
    std::atomic<int> activeMixerSlot { 0 };
    std::atomic<double> publishedLoopLength { loopLengthBeats };
    juce::CriticalSection mixerPublishLock;
    MixerSnapshot pendingMixer;
    bool hasPendingMixer = false;
    juce::CriticalSection pluginAccess;
    mutable juce::CriticalSection deviceErrorLock;
    juce::String lastDeviceError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RealtimeEngine)
};
} // namespace resonance
