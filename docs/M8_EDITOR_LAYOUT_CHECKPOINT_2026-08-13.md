# M8 editor layout checkpoint

Date: 2026-08-13

Status: implementation evidence only. No listening approval.

## Why this slice exists

The user looked at the editor and said it had been built for testing rather than for
use. Measuring the layout confirmed it, and the numbers were worse than the impression:

| Region | Share of the default 1280x860 window, by area |
| --- | --- |
| Audio + MIDI device panel | 19.7% |
| Piano roll | 22.5% |
| Mixer card | 17.9% |
| On-screen keyboard | 12.5% |

A device chooser that is set once a year held as much of the window as the music. The
other half of that right-hand column held the M5 resolver inputs — seed and maximum
delta — which are useful when reproducing a dynamics proposal and never otherwise.

The mixer was the worse problem, and it was functional rather than spatial: it showed
**one strip, for the selected track**. On a four-track song there was no way to see a
balance. Setting one required selecting a track, reading its gain, selecting the next,
and holding the numbers in your head. The two meters in that card were labelled as
though they were the master; they were the selected track's.

## What changed

Five changes, all in `src/editor_component.{h,cpp}`.

### The device chooser moved into a dialog

`MainEditorComponent::SettingsWindow` is a `DocumentWindow` opened by the **Audio**
button in the transport bar. It reparents the existing `AudioDeviceSelectorComponent`
and `deviceSummaryLabel` rather than making second copies, so there is still one device
selector and one summary, and the 30 Hz refresh keeps updating it while the dialog is
open. Closing hides the window; it is destroyed with the editor.

### The mixer shows every track

`TrackStrip` is a fixed array of `SongProject::maxProjectTracks` control groups — a
name button, gain, pan, mute, solo. Strips past the project's track count are hidden,
not disabled, so the mixer shows exactly the tracks that exist.

Each strip writes through `setTrackMixerSettingsForTrack`, which already existed, so no
model change was needed. The strip's name button replaces the track selector combo, and
carries the selection highlight.

Every strip now has its own meter, fed from `RealtimeEngine::getTrackLeftPeak` /
`getTrackRightPeak`, which the audio probe already used but the editor did not show.
The pair of meters beside the strips now shows the true master. Nothing in the window
showed the master level before this.

### The keyboard and the resolver inputs became optional

**Keys** and **Advanced** toggle them. Both start hidden. The keyboard yields to the
piano roll when the window is short: if less than 64 px is available after reserving
320 px for the roll and the proposal bar, it stays hidden rather than crushing them.

### The proposal panel became a full-width bar

It was a card inside the right-hand column. It is now a bar under the piano roll with
its eight buttons in two rows of four, which let the right column go away entirely.
The piano roll took the whole width.

### The diagnostic line moved to a footer

`CPU / XRUN / CLIPS / INVALID`, and every startup and device error, used to sit at the
bottom of the device column. It is now a full-width footer, so the primary error
surface is not inside a panel that can be scrolled or covered.

## Result

Shares are of the default 1280x860 window, computed from the rectangles `resized()`
assigns.

| Region | Before | After |
| --- | --- | --- |
| Piano roll | 22.5% | 33.9% |
| Device panel | 19.7% | 0%, in a dialog |
| Mixer | 1 track visible | all 4 visible, each with a meter |
| Master level | not shown anywhere | two meters beside the strips |

The piano roll also went from 894 px wide to the full 1232, which matters more than the
area figure for a 32-bar song.

## Evidence

The gate is green: 124 scheduler assertions, 282 project assertions, 26 schema
validations, all packaged self-tests, UI idle 1,937.5 ms of CPU against the 3,000 ms
ceiling.

`--audio-probe` on the 32-bar four-track song passes with all four tracks audible, no
track overloaded, and a master peak of 0.557.

Both layout branches were confirmed by screenshot rather than by reading the code:

1. Four-track project, keyboard and advanced hidden — the default.
2. Starter project with **Keys** and **Advanced** on.

`prepareM5PreviewForSnapshot` now opens both optional panels, so `--ui-snapshot` keeps
documenting the whole surface instead of only the parts that happen to be on.

The first screenshot caught a real defect that compiling did not. The mixer card's
height was computed from a 20 px header while the layout consumed 40, so the strips
were 20 px short and every Mute and Solo control was clipped out of the card. This is
the third time this session that paint or layout code compiled, ran, and was wrong;
screenshots are the only gate that has ever caught it.

## Two stale strings fixed in passing

The GUI still refused a fifth track with "THIS M6 SLICE SUPPORTS TWO INSTRUMENT TRACKS"
and announced any added track as "TRACK 2 ADDED", both from the two-track slice. They
now derive from `SongProject::maxProjectTracks` and the actual track count. The
equivalent message in the edit-command layer was corrected earlier, on 2026-08-13; this
was the same error in the other path.

## What this does not do

- No listening approval. Nothing since M5 has any.
- The mixer strips cap at four because the project ceiling does. If the ceiling moves,
  the strip row will need to scroll or shrink; at four it does not.
- The sound shelf controls and the sound A/B row still sit in the track card, which is
  now the only crowded region left.
