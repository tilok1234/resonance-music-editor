# Per-track audio probe and the four-track mix diagnosis

Date: 2026-08-13

Status: implementation evidence plus one user listening observation.

## The report

A four-track project was handed to the user to play. Their response: *"it sounds ok but i only hear 1 instrument."*

This is the first defect in this line of work found by a human ear rather than a gate, and it could not have been found any other way — every packaged test in this repository is deliberately silent, and the silent tests all passed.

## Investigation

**Expected:** playing a four-track project delivers each track's notes to its own Surge instance and sums four voices.
**Observed:** one instrument.

Three hypotheses were checked, cheapest first.

### Eliminated: only the active track is published

`publishProjectMixerSnapshot` builds a lane for every track with `createSequenceSnapshotForTrack (trackIndex)`, and that accessor reads the indexed track's notes tree. No active-track aliasing.

### Eliminated: non-default MIDI output channels are dropped

Tracks 2, 3, and 4 route to MIDI channels 2, 3, and 4. The callback stamps each track's notes with its own `midiOutputChannel` and delivers them to that track's **own** plug-in instance, so a channel filter could not silence a track.

Both were settled decisively by adding the measurement described below: all four tracks rendered 1,920 blocks each and every one produced signal.

### Eliminated: the three patches are the same sound

The three distinct real Surge states on this machine were each rendered through `ResonanceHostProbe` on an identical chord and compared numerically:

| Pair | Relative RMS difference |
| --- | --- |
| init vs candidateB | 129% |
| init vs roundtrip | 93% |
| candidateB vs roundtrip | 168% |

They are genuinely different patches, not three copies of one.

## Cause

That same render exposed it. The three patches have very different intrinsic output levels:

| Patch | Peak | RMS |
| --- | --- | --- |
| `a771b288` init | −3.0 dBFS | −20.4 dBFS |
| `390b5b0d` roundtrip | −6.0 dBFS | −21.1 dBFS |
| `ccaf99d4` candidateB | −11.5 dBFS | −22.9 dBFS |

An 8.5 dB spread between loudest and quietest. The mixer gains for the song had been chosen without knowing this, assuming comparable output. The result put the **melody on the quietest patch and then cut it a further 7 dB**, so the bass sat roughly 15 dB above it in combined patch-plus-gain terms and everything else blended underneath.

The mechanism is mundane: **a mix authored blind against patches whose levels differ by 8.5 dB.** The editor behaved correctly throughout; nothing in the engine, routing, or scheduling was at fault.

## Fix

Gains recomputed against the measured patch levels, with a deliberate hierarchy placing the melody on top, and the Pulse part lifted an octave out of the Lead's register where two similar timbres were fusing.

| Track | Peak before | Peak after |
| --- | --- | --- |
| Bass | −11.0 dBFS | −11.3 dBFS |
| Harmony | −18.1 dBFS | −15.9 dBFS |
| Lead | −16.8 dBFS | **−10.1 dBFS** |
| Pulse | −22.2 dBFS | −18.0 dBFS |

Master peak −7.3 dBFS, no clipping, no invalid samples.

Whether this now sounds like four instruments is a listening question and remains open.

## The regression that was missing

Every packaged gate in this repository is silent by contract, which is correct for the invariants they protect but means **no gate could observe that a track made no sound**. A new `--audio-probe` mode closes that:

- opens a project, publishes the real mixer snapshot, and renders the full loop through the production callback with no audio device attached;
- reports per-track peak level, processed block count, MIDI channel, gain, and mute state;
- requires every track that *should* sound to produce signal, and every muted or solo-suppressed track to stay silent;
- fails on clipping, invalid samples, or a silent master.

It is wired into the Release gate against the committed two-track authoring artifact, whose second track is muted — so the probe exercises both the audible and the correctly-silent path on every run.

This does not make listening approval unnecessary. It catches *silence*, not *badness*: a mix that is audible but wrong, as this one was, still needs a human.

## What this changes about the process

Two things worth carrying forward:

1. **Patch level is not a constant.** Any future automatic or assisted mixing must measure a patch's output rather than assume parity. An 8.5 dB spread across three patches from the same synth is enough to bury a part.
2. **Six slices shipped without a listening pass, and the first listen found a real problem.** The technical gates were never going to find it. The overdue M6 listening pass is worth more than the next feature.
