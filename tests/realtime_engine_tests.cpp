#include <JuceHeader.h>

#include "../src/loop_scheduler.h"
#include "../src/mixer_snapshot.h"
#include "../src/realtime_engine.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
struct TestContext
{
    int assertions = 0;
    double twoTrackAverageCallbackLoad = 0.0;

    void expect (bool condition, const juce::String& message)
    {
        ++assertions;
        if (! condition)
            throw std::runtime_error (message.toStdString());
    }
};

struct EventMatch
{
    int sample = -1;
    int count = 0;
};

struct FakePluginStats
{
    int prepareCalls = 0;
    int releaseCalls = 0;
    int processCalls = 0;
    int noteOnCount = 0;
    int lastNote = -1;
    int lastChannel = -1;
    double preparedRate = 0.0;
    int preparedBlockSize = 0;
};

class FakeInstrument final : public juce::AudioPluginInstance
{
public:
    FakeInstrument (float outputAmplitude,
                    int initialState,
                    std::shared_ptr<FakePluginStats> sharedStats)
        : juce::AudioPluginInstance (
              BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          amplitude (outputAmplitude),
          stateValue (initialState),
          stats (std::move (sharedStats))
    {
    }

    const juce::String getName() const override { return "Deterministic M6 instrument"; }
    void prepareToPlay (double sampleRate, int maximumBlockSize) override
    {
        ++stats->prepareCalls;
        stats->preparedRate = sampleRate;
        stats->preparedBlockSize = maximumBlockSize;
    }
    void releaseResources() override { ++stats->releaseCalls; }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        ++stats->processCalls;
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                ++stats->noteOnCount;
                stats->lastNote = message.getNoteNumber();
                stats->lastChannel = message.getChannel();
            }
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, amplitude);
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Test"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destination) override
    {
        destination.reset();
        destination.append (&stateValue, sizeof (stateValue));
    }
    void setStateInformation (const void* data, int size) override
    {
        if (data != nullptr && size == static_cast<int> (sizeof (stateValue)))
            std::memcpy (&stateValue, data, sizeof (stateValue));
    }
    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = getName();
        description.pluginFormatName = "M6 test";
        description.fileOrIdentifier = "m6-fake-instrument";
        description.uniqueId = stateValue;
        description.isInstrument = true;
    }

private:
    float amplitude = 0.0f;
    int stateValue = 0;
    std::shared_ptr<FakePluginStats> stats;
};

juce::MemoryBlock stateBlock (int value)
{
    juce::MemoryBlock result;
    result.append (&value, sizeof (value));
    return result;
}

void renderBlock (resonance::RealtimeEngine& engine,
                  juce::AudioBuffer<float>& output,
                  int numSamples = 512)
{
    output.setSize (2, numSamples, false, false, true);
    output.clear();
    auto* outputs = output.getArrayOfWritePointers();
    const juce::AudioIODeviceCallbackContext context;
    engine.audioDeviceIOCallbackWithContext (nullptr, 0, outputs, 2, numSamples, context);
}

EventMatch findNoteEvent (const juce::MidiBuffer& midi,
                          int note,
                          bool noteOn)
{
    EventMatch result;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto matches = noteOn ? message.isNoteOn() : message.isNoteOff();

        if (matches && message.getNoteNumber() == note)
        {
            result.sample = metadata.samplePosition;
            ++result.count;
        }
    }

    return result;
}

void testLoopStart (TestContext& context)
{
    juce::MidiBuffer midi;
    resonance::LoopScheduler::addBlock (midi, 0.0, 120.0, 48000.0, 512);
    const auto note = findNoteEvent (midi, 48, true);
    context.expect (note.count == 1, "Loop start must schedule exactly one C3 note-on");
    context.expect (note.sample == 0, "Loop start note-on must be sample-accurate at zero");
}

void testNoteOffOffset (TestContext& context)
{
    juce::MidiBuffer midi;
    resonance::LoopScheduler::addBlock (midi, 0.81, 120.0, 48000.0, 480);
    const auto noteOff = findNoteEvent (midi, 48, false);
    context.expect (noteOff.count == 1, "A block crossing beat 0.82 must schedule the C3 note-off");
    context.expect (noteOff.sample == 240, "C3 note-off must land at sample 240");
}

void testLoopWrap (TestContext& context)
{
    juce::MidiBuffer midi;
    resonance::LoopScheduler::addBlock (midi, 7.99, 120.0, 48000.0, 480);
    const auto wrapped = findNoteEvent (midi, 48, true);
    context.expect (wrapped.count == 1, "A block crossing beat eight must schedule the next loop's C3");
    context.expect (wrapped.sample == 240, "Wrapped C3 note-on must land at sample 240");
}

void testTempoMapping (TestContext& context)
{
    juce::MidiBuffer midi;
    resonance::LoopScheduler::addBlock (midi, 0.80, 90.0, 48000.0, 1280);
    const auto noteOff = findNoteEvent (midi, 48, false);
    context.expect (noteOff.count == 1, "Tempo-adjusted block must contain the first note-off");
    context.expect (noteOff.sample == 640, "At 90 BPM, 0.02 beats must map to 640 samples");
}

void testFourLoopBalance (TestContext& context)
{
    constexpr double bpm = 137.0;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 257;
    const auto beatsPerSample = bpm / (60.0 * sampleRate);
    const auto totalBeats = resonance::loopLengthBeats * 4.0;
    double startBeat = 0.0;
    int noteOns = 0;
    int noteOffs = 0;

    while (startBeat < totalBeats)
    {
        const auto samplesRemaining = static_cast<int> (std::floor (
            (totalBeats - startBeat) / beatsPerSample));
        const auto samplesThisBlock = juce::jmin (blockSize, samplesRemaining);
        if (samplesThisBlock <= 0)
            break;

        juce::MidiBuffer midi;
        resonance::LoopScheduler::addBlock (midi, startBeat, bpm, sampleRate, samplesThisBlock);

        for (const auto metadata : midi)
        {
            noteOns += metadata.getMessage().isNoteOn() ? 1 : 0;
            noteOffs += metadata.getMessage().isNoteOff() ? 1 : 0;
            context.expect (metadata.samplePosition >= 0 && metadata.samplePosition < samplesThisBlock,
                            "Every scheduled event must stay inside its audio block");
        }

        startBeat += beatsPerSample * static_cast<double> (samplesThisBlock);
    }

    context.expect (noteOns == 32, "Four complete loops must contain 32 note-ons");
    context.expect (noteOffs == 32, "Four complete loops must contain 32 note-offs");
}

void testEditableSequence (TestContext& context)
{
    resonance::SequenceSnapshot sequence;
    sequence.loopBeats = 4.0;
    sequence.noteCount = 2;
    sequence.notes[0] = { 0.0, 0.5, 72, 0.25f };
    sequence.notes[1] = { 3.75, 0.5, 76, 0.90f };

    juce::MidiBuffer start;
    resonance::LoopScheduler::addBlock (start, 0.0, 120.0, 48000.0, 512, sequence);
    const auto first = findNoteEvent (start, 72, true);
    context.expect (first.count == 1 && first.sample == 0,
                    "An editable sequence must schedule its first note at sample zero");
    context.expect (findNoteEvent (start, 48, true).count == 0,
                    "Editable playback must not leak notes from the starter sequence");

    juce::MidiBuffer wrap;
    resonance::LoopScheduler::addBlock (wrap, 3.99, 120.0, 48000.0, 480, sequence);
    const auto wrapped = findNoteEvent (wrap, 72, true);
    context.expect (wrapped.count == 1 && wrapped.sample == 240,
                    "A four-beat editable loop must wrap sample-accurately");

    juce::MidiBuffer crossingOff;
    resonance::LoopScheduler::addBlock (crossingOff, 4.24, 120.0, 48000.0, 480, sequence);
    const auto noteOff = findNoteEvent (crossingOff, 76, false);
    context.expect (noteOff.count == 1 && noteOff.sample == 240,
                    "A note crossing the loop boundary must release in the following cycle");

    juce::MidiBuffer empty;
    sequence.noteCount = 0;
    resonance::LoopScheduler::addBlock (empty, 0.0, 120.0, 48000.0, 512, sequence);
    context.expect (empty.isEmpty(), "An empty edited sequence must schedule no MIDI events");
}

void testExactDeviceBlockBoundaries (TestContext& context)
{
    constexpr double bpm = 120.0;
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 441;
    constexpr int blocksPerLoop = 400;
    const auto beatsPerBlock = bpm * static_cast<double> (blockSize) / (60.0 * sampleRate);
    double startBeat = 0.0;
    int noteOns = 0;
    int noteOffs = 0;
    int firstNoteOffBlock = -1;
    int secondNoteOnBlock = -1;

    for (int block = 0; block < blocksPerLoop; ++block)
    {
        juce::MidiBuffer midi;
        resonance::LoopScheduler::addBlock (midi, startBeat, bpm, sampleRate, blockSize);

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            noteOns += message.isNoteOn() ? 1 : 0;
            noteOffs += message.isNoteOff() ? 1 : 0;

            if (message.isNoteOff() && message.getNoteNumber() == 48)
            {
                firstNoteOffBlock = block;
                context.expect (metadata.samplePosition == 0,
                                "The beat-0.82 note-off must move to sample zero of the next device block");
            }

            if (message.isNoteOn() && message.getNoteNumber() == 55)
            {
                secondNoteOnBlock = block;
                context.expect (metadata.samplePosition == 0,
                                "The beat-one note-on must move to sample zero of the next device block");
            }
        }

        startBeat += beatsPerBlock;
    }

    context.expect (noteOns == 8 && noteOffs == 8,
                    "The exact 44.1 kHz / 441-sample loop must not drop boundary note events");
    context.expect (firstNoteOffBlock == 41 && secondNoteOnBlock == 50,
                    "Boundary events must be owned by the expected following device blocks");
}

void testFixedCapacityMixerContract (TestContext& context)
{
    static_assert (resonance::maxMixerTracks == 8);

    resonance::MixerSnapshot snapshot;
    snapshot.trackCount = 3;
    snapshot.tracks[0].enabled = true;
    snapshot.tracks[1].enabled = true;
    snapshot.tracks[2].enabled = true;

    const auto centre = resonance::resolveStereoTrackGain (snapshot, 0);
    context.expect (centre.left == 1.0f && centre.right == 1.0f,
                    "A centred unity-gain track must feed both stereo channels equally");

    snapshot.tracks[0].pan = -1.0f;
    const auto hardLeft = resonance::resolveStereoTrackGain (snapshot, 0);
    context.expect (hardLeft.left == 1.0f && hardLeft.right == 0.0f,
                    "Hard-left balance must silence only the right channel");

    snapshot.tracks[0].pan = 1.0f;
    snapshot.tracks[0].gainLinear = 0.25f;
    const auto hardRight = resonance::resolveStereoTrackGain (snapshot, 0);
    context.expect (hardRight.left == 0.0f && hardRight.right == 0.25f,
                    "Hard-right balance must preserve the bounded track gain");

    snapshot.tracks[0].muted = true;
    const auto muted = resonance::resolveStereoTrackGain (snapshot, 0);
    context.expect (muted.left == 0.0f && muted.right == 0.0f,
                    "Mute must gate a track before it reaches the mix bus");

    snapshot.tracks[0].muted = false;
    snapshot.tracks[0].pan = 0.0f;
    snapshot.tracks[1].solo = true;
    const auto nonSoloed = resonance::resolveStereoTrackGain (snapshot, 0);
    const auto soloed = resonance::resolveStereoTrackGain (snapshot, 1);
    context.expect (nonSoloed.left == 0.0f && nonSoloed.right == 0.0f
                        && soloed.left == 1.0f && soloed.right == 1.0f,
                    "An active solo must gate every enabled non-solo track");

    snapshot.tracks[1].enabled = false;
    const auto inactiveSoloIgnored = resonance::resolveStereoTrackGain (snapshot, 0);
    context.expect (inactiveSoloIgnored.left == 0.25f && inactiveSoloIgnored.right == 0.25f,
                    "A disabled track must not activate the global solo gate");

    snapshot.tracks[2].gainLinear = -4.0f;
    const auto negativeGain = resonance::resolveStereoTrackGain (snapshot, 2);
    context.expect (negativeGain.left == 0.0f && negativeGain.right == 0.0f,
                    "The render contract must clamp an invalid negative linear gain to silence");

    snapshot.trackCount = resonance::maxMixerTracks + 4;
    const auto pastCapacity = resonance::resolveStereoTrackGain (snapshot,
                                                                 resonance::maxMixerTracks);
    context.expect (pastCapacity.left == 0.0f && pastCapacity.right == 0.0f,
                    "The audio-side mixer must never read beyond its fixed track capacity");

    context.expect (std::is_trivially_copyable_v<resonance::MixerSnapshot>,
                    "Mixer snapshots must remain allocation-free trivially copyable values");
}

void testTwoTrackRuntime (TestContext& context)
{
    auto firstStats = std::make_shared<FakePluginStats>();
    auto secondStats = std::make_shared<FakePluginStats>();
    resonance::RealtimeEngine engine;

    context.expect (engine.setPluginForTrack (
                              0, std::make_unique<FakeInstrument> (0.2f, 101, firstStats)).wasOk(),
                    "The first stable runtime slot must accept an instrument before preparation");
    context.expect (engine.setPluginForTrack (
                              1, std::make_unique<FakeInstrument> (0.4f, 202, secondStats)).wasOk(),
                    "The second stable runtime slot must accept an instrument before preparation");
    context.expect (engine.getActivePluginCount() == 2,
                    "The runtime must report both installed instrument instances");
    context.expect (engine.setPluginForTrack (
                              resonance::maxMixerTracks,
                              std::make_unique<FakeInstrument> (0.1f, 303,
                                                                std::make_shared<FakePluginStats>())).failed(),
                    "Topology changes beyond the fixed eight-slot capacity must fail closed");

    resonance::MixerSnapshot mixer;
    mixer.trackCount = 2;
    mixer.tracks[0].enabled = true;
    mixer.tracks[0].gainLinear = 1.0f;
    mixer.tracks[0].pan = -1.0f;
    mixer.tracks[0].midiOutputChannel = 2;
    mixer.tracks[0].sequence.loopBeats = 4.0;
    mixer.tracks[0].sequence.noteCount = 1;
    mixer.tracks[0].sequence.notes[0] = { 0.0, 0.5, 60, 0.8f };
    mixer.tracks[1].enabled = true;
    mixer.tracks[1].gainLinear = 0.5f;
    mixer.tracks[1].pan = 1.0f;
    mixer.tracks[1].midiOutputChannel = 10;
    mixer.tracks[1].sequence.loopBeats = 4.0;
    mixer.tracks[1].sequence.noteCount = 1;
    mixer.tracks[1].sequence.notes[0] = { 0.0, 0.5, 67, 0.7f };
    engine.setMixerSnapshot (mixer);
    engine.setMasterGainDecibels (0.0f);

    const auto preparation = engine.prepareForOfflineRender (48000.0, 512);
    context.expect (preparation.wasOk() && engine.isPrepared(),
                    "The fixed runtime must prepare both slots for a silent offline block");
    context.expect (firstStats->prepareCalls == 1 && secondStats->prepareCalls == 1,
                    "Every installed slot must be prepared exactly once");
    context.expect (firstStats->preparedRate == 48000.0 && secondStats->preparedBlockSize == 512,
                    "Both slots must receive the exact shared device format");
    context.expect (engine.setPluginForTrack (
                              2, std::make_unique<FakeInstrument> (0.1f, 404,
                                                                   std::make_shared<FakePluginStats>())).failed(),
                    "Prepared topology must remain immutable until the runtime is released");

    engine.setPlaying (true);
    juce::AudioBuffer<float> output;
    renderBlock (engine, output);
    context.expect (std::abs (output.getSample (0, 0) - 0.2f) < 1.0e-5f
                        && std::abs (output.getSample (1, 0) - 0.2f) < 1.0e-5f,
                    "Hard-left unity and hard-right half-gain tracks must sum deterministically");
    context.expect (firstStats->noteOnCount == 1 && firstStats->lastNote == 60
                        && firstStats->lastChannel == 2,
                    "Track one must receive its own sequence on its routed MIDI output channel");
    context.expect (secondStats->noteOnCount == 1 && secondStats->lastNote == 67
                        && secondStats->lastChannel == 10,
                    "Track two must receive its own sequence on its routed MIDI output channel");
    context.expect (std::abs (engine.getTrackLeftPeak (0) - 0.2f) < 1.0e-5f
                        && engine.getTrackRightPeak (0) == 0.0f,
                    "Track-one post-fader meters must reflect hard-left balance");
    context.expect (engine.getTrackLeftPeak (1) == 0.0f
                        && std::abs (engine.getTrackRightPeak (1) - 0.2f) < 1.0e-5f,
                    "Track-two post-fader meters must reflect gain and hard-right balance");
    context.expect (engine.getTrackProcessedBlockCount (0) == 1
                        && engine.getTrackProcessedBlockCount (1) == 1,
                    "Both prepared instruments must process the same callback block");
    context.expect (engine.getLastCallbackLoad() > 0.0f
                        && std::isfinite (engine.getMaximumCallbackLoad()),
                    "The callback must publish finite bounded-load diagnostics");

    const auto loadStart = juce::Time::getHighResolutionTicks();
    constexpr int loadBlocks = 64;
    for (int block = 0; block < loadBlocks; ++block)
        renderBlock (engine, output);
    const auto loadSeconds = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - loadStart);
    context.twoTrackAverageCallbackLoad = loadSeconds
                                          / (loadBlocks * 512.0 / 48000.0);
    context.expect (context.twoTrackAverageCallbackLoad < 0.25,
                    "The deterministic two-track renderer must remain below 25% average callback load");

    mixer.tracks[0].muted = true;
    engine.setMixerSnapshot (mixer);
    renderBlock (engine, output);
    context.expect (output.getSample (0, 0) == 0.0f
                        && std::abs (output.getSample (1, 0) - 0.2f) < 1.0e-5f,
                    "Muting track one must remove only its contribution from the master bus");

    mixer.tracks[0].muted = false;
    mixer.tracks[0].solo = true;
    engine.setMixerSnapshot (mixer);
    renderBlock (engine, output);
    context.expect (std::abs (output.getSample (0, 0) - 0.2f) < 1.0e-5f
                        && output.getSample (1, 0) == 0.0f,
                    "Soloing track one must gate the non-soloed second track");

    mixer.tracks[0].solo = false;
    mixer.tracks[0].pan = 0.0f;
    mixer.tracks[0].gainLinear = 4.0f;
    mixer.tracks[1].pan = 0.0f;
    mixer.tracks[1].gainLinear = 4.0f;
    engine.setMixerSnapshot (mixer);
    renderBlock (engine, output);
    context.expect (output.getSample (0, 0) == 1.0f && output.getSample (1, 0) == 1.0f,
                    "The master bus must clamp an intentionally overloaded two-track mix");
    context.expect (engine.getClippedSampleCount() > 0,
                    "The two-track runtime must count pre-clamp master overloads");
    context.expect (engine.getLeftPeak() <= 1.0f && engine.getRightPeak() <= 1.0f
                        && engine.getTrackLeftPeak (0) <= 1.0f,
                    "Published master and track meters must remain bounded");
    context.expect (engine.getInvalidSampleCount() == 0
                        && engine.getProcessorExceptionCount() == 0,
                    "A valid two-track render must remain finite and exception-free");

    juce::MemoryBlock firstState;
    juce::MemoryBlock secondState;
    context.expect (engine.capturePluginStateForTrack (0, firstState).wasOk()
                        && firstState == stateBlock (101),
                    "Track one must expose its complete independent state block");
    context.expect (engine.capturePluginStateForTrack (1, secondState).wasOk()
                        && secondState == stateBlock (202),
                    "Track two must expose its complete independent state block");
    const auto replacementFirstState = stateBlock (1111);
    const auto replacementSecondState = stateBlock (2222);
    context.expect (engine.restorePluginStateForTrack (0, replacementFirstState).wasOk()
                        && engine.restorePluginStateForTrack (1, replacementSecondState).wasOk(),
                    "Each prepared slot must accept an independent state restore");
    context.expect (engine.capturePluginStateForTrack (0, firstState).wasOk()
                        && engine.capturePluginStateForTrack (1, secondState).wasOk()
                        && firstState == replacementFirstState && secondState == replacementSecondState,
                    "Both restored state blocks must round-trip byte-for-byte");

    juce::AudioBuffer<float> oversized;
    renderBlock (engine, oversized, 4097);
    context.expect (engine.getOversizedBlockCount() == 1,
                    "A callback beyond preallocated capacity must fail to silence without resizing");

    engine.releaseOfflineRender();
    context.expect (! engine.isPrepared() && firstStats->releaseCalls == 1
                        && secondStats->releaseCalls == 1,
                    "Releasing the runtime must release every prepared instance exactly once");
    context.expect (engine.setPluginForTrack (1, {}).wasOk()
                        && engine.getActivePluginCount() == 1,
                    "A missing second plug-in must be represented without disturbing slot one");
    context.expect (engine.capturePluginStateForTrack (1, secondState).failed(),
                    "State access for a missing plug-in must fail closed");
    context.expect (engine.capturePluginStateForTrack (0, firstState).wasOk()
                        && firstState == replacementFirstState,
                    "Removing a missing track slot must preserve the surviving track state exactly");

    engine.shutdown();
    context.expect (engine.getPlugin() == nullptr && engine.getActivePluginCount() == 0,
                    "Shutdown must retire every stable plug-in slot");
}

juce::String getArgumentValue (const juce::StringArray& args, const juce::String& flag)
{
    const auto index = args.indexOf (flag);
    return index >= 0 && index + 1 < args.size() ? args[index + 1] : juce::String {};
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args (argv + 1, argc - 1);
    const auto reportPath = getArgumentValue (args, "--report");
    TestContext context;

    auto* reportObject = new juce::DynamicObject();
    juce::var report (reportObject);
    reportObject->setProperty ("schemaVersion", 1);
    reportObject->setProperty ("testVersion", JUCE_APPLICATION_VERSION_STRING);
    reportObject->setProperty ("juceVersion", juce::SystemStats::getJUCEVersion());

    try
    {
        testLoopStart (context);
        testNoteOffOffset (context);
        testLoopWrap (context);
        testTempoMapping (context);
        testFourLoopBalance (context);
        testEditableSequence (context);
        testExactDeviceBlockBoundaries (context);
        testFixedCapacityMixerContract (context);
        testTwoTrackRuntime (context);

        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("loopLengthBeats", resonance::loopLengthBeats);
        reportObject->setProperty ("noteCount", static_cast<int> (resonance::starterLoopNotes.size()));
        reportObject->setProperty ("maxMixerTracks", static_cast<int> (resonance::maxMixerTracks));
        reportObject->setProperty ("mixerContractPassed", true);
        reportObject->setProperty ("twoTrackRuntimePassed", true);
        reportObject->setProperty ("twoTrackAverageCallbackLoad",
                                   context.twoTrackAverageCallbackLoad);
        reportObject->setProperty ("passed", true);
    }
    catch (const std::exception& error)
    {
        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("passed", false);
        reportObject->setProperty ("error", error.what());
    }

    if (reportPath.isNotEmpty())
    {
        const juce::File reportFile (reportPath);
        reportFile.getParentDirectory().createDirectory();
        if (! reportFile.replaceWithText (juce::JSON::toString (report, true)))
            return 2;
    }

    std::cout << juce::JSON::toString (report, true) << std::endl;
    return static_cast<bool> (reportObject->getProperty ("passed")) ? 0 : 1;
}
