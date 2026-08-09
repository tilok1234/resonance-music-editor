#include <JuceHeader.h>

#include "../src/loop_scheduler.h"
#include "../src/mixer_snapshot.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
struct TestContext
{
    int assertions = 0;

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

        reportObject->setProperty ("assertions", context.assertions);
        reportObject->setProperty ("loopLengthBeats", resonance::loopLengthBeats);
        reportObject->setProperty ("noteCount", static_cast<int> (resonance::starterLoopNotes.size()));
        reportObject->setProperty ("maxMixerTracks", static_cast<int> (resonance::maxMixerTracks));
        reportObject->setProperty ("mixerContractPassed", true);
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
