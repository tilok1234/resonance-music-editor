#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace resonance
{
struct LoopNote
{
    double beat = 0.0;
    double lengthBeats = 0.0;
    int midiNote = 60;
    float velocity = 0.5f;
};

inline constexpr double loopLengthBeats = 8.0;
inline constexpr std::size_t maxSequenceNotes = 512;

inline constexpr std::array<LoopNote, 8> starterLoopNotes {{
    { 0.0, 0.82, 48, 0.52f },
    { 1.0, 0.82, 55, 0.48f },
    { 2.0, 0.82, 60, 0.50f },
    { 3.0, 0.82, 64, 0.46f },
    { 4.0, 0.82, 45, 0.52f },
    { 5.0, 0.82, 52, 0.48f },
    { 6.0, 0.82, 57, 0.50f },
    { 7.0, 0.82, 60, 0.46f },
}};

struct SequenceSnapshot
{
    std::array<LoopNote, maxSequenceNotes> notes {};
    std::size_t noteCount = 0;
    double loopBeats = loopLengthBeats;
};

inline SequenceSnapshot makeStarterSequence()
{
    SequenceSnapshot result;
    result.noteCount = starterLoopNotes.size();
    std::copy (starterLoopNotes.begin(), starterLoopNotes.end(), result.notes.begin());
    return result;
}

class LoopScheduler
{
public:
    static void addBlock (juce::MidiBuffer& destination,
                          double absoluteStartBeat,
                          double bpm,
                          double sampleRate,
                          int numSamples)
    {
        addBlock (destination,
                  absoluteStartBeat,
                  bpm,
                  sampleRate,
                  numSamples,
                  starterLoopNotes.data(),
                  starterLoopNotes.size(),
                  loopLengthBeats);
    }

    static void addBlock (juce::MidiBuffer& destination,
                          double absoluteStartBeat,
                          double bpm,
                          double sampleRate,
                          int numSamples,
                          const SequenceSnapshot& sequence)
    {
        addBlock (destination,
                  absoluteStartBeat,
                  bpm,
                  sampleRate,
                  numSamples,
                  sequence.notes.data(),
                  sequence.noteCount,
                  sequence.loopBeats);
    }

    static void addBlock (juce::MidiBuffer& destination,
                          double absoluteStartBeat,
                          double bpm,
                          double sampleRate,
                          int numSamples,
                          const LoopNote* notes,
                          std::size_t noteCount,
                          double loopBeats)
    {
        if (absoluteStartBeat < 0.0 || bpm <= 0.0 || sampleRate <= 0.0
            || numSamples <= 0 || notes == nullptr || loopBeats <= 0.0)
            return;

        const auto beatsPerSample = bpm / (60.0 * sampleRate);
        const auto absoluteEndBeat = absoluteStartBeat + beatsPerSample * static_cast<double> (numSamples);
        const auto safeCount = juce::jmin (noteCount, maxSequenceNotes);

        for (std::size_t index = 0; index < safeCount; ++index)
        {
            const auto& note = notes[index];
            if (note.beat < 0.0 || note.beat >= loopBeats || note.lengthBeats <= 0.0
                || ! juce::isPositiveAndBelow (note.midiNote, 128))
                continue;

            addRecurringEvent (destination,
                               absoluteStartBeat,
                               absoluteEndBeat,
                               beatsPerSample,
                               numSamples,
                               loopBeats,
                               note.beat,
                               juce::MidiMessage::noteOn (1,
                                                          note.midiNote,
                                                          juce::jlimit (0.0f, 1.0f, note.velocity)));
            addRecurringEvent (destination,
                               absoluteStartBeat,
                               absoluteEndBeat,
                               beatsPerSample,
                               numSamples,
                               loopBeats,
                               note.beat + note.lengthBeats,
                               juce::MidiMessage::noteOff (1, note.midiNote));
        }
    }

private:
    static void addRecurringEvent (juce::MidiBuffer& destination,
                                   double absoluteStartBeat,
                                   double absoluteEndBeat,
                                   double beatsPerSample,
                                   int numSamples,
                                   double loopBeats,
                                   double eventBeat,
                                   const juce::MidiMessage& message)
    {
        auto eventBeatInLoop = std::fmod (eventBeat, loopBeats);
        if (eventBeatInLoop < 0.0)
            eventBeatInLoop += loopBeats;

        // Assign each event to the block containing its nearest sample centre.
        // Without the half-sample window, an event at the exact block end can
        // round to numSamples, be rejected, and then look infinitesimally late
        // in the next block after accumulated floating-point transport drift.
        const auto halfSampleBeats = beatsPerSample * 0.5;
        const auto schedulingStartBeat = absoluteStartBeat - halfSampleBeats;
        const auto schedulingEndBeat = absoluteEndBeat - halfSampleBeats;
        auto cycle = std::floor (schedulingStartBeat / loopBeats);
        auto occurrence = cycle * loopBeats + eventBeatInLoop;

        if (occurrence < schedulingStartBeat)
            occurrence += loopBeats;

        while (occurrence < schedulingEndBeat)
        {
            const auto sampleOffset = static_cast<int> (std::floor (
                (occurrence - absoluteStartBeat) / beatsPerSample + 0.5));

            if (sampleOffset >= 0 && sampleOffset < numSamples)
                destination.addEvent (message, sampleOffset);

            occurrence += loopBeats;
        }
    }
};
} // namespace resonance
