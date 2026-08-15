# M6 piano roll visibility checkpoint

Date: 2026-08-12

Status: implementation evidence only. This slice emits no music and carries no listening approval.

## Why this slice exists

The 2026-08-12 authoring assessment found that the piano roll could not show a two-part arrangement. Two limits caused this:

1. the roll drew a fixed 29 pitch rows starting at MIDI 40, about 2.4 octaves, with no zoom;
2. `PianoRoll` read `project.getNotes()`, which returns only the **active** track, so the other track was invisible.

A bass part around MIDI 36–47 and a melody around MIDI 69–84 span four octaves. Neither could be seen at the same time, and material below MIDI 40 sat outside the default window entirely. Writing two parts against each other was therefore guesswork.

## What changed

| Change | Detail |
| --- | --- |
| Vertical zoom | `visibleNoteRows` became session view state, adjustable from 12 to 72 rows |
| Zoom controls | `Ctrl` + mouse wheel, or `+` / `-` while the roll has focus |
| Zoom anchoring | The centre pitch is held steady so zooming never scrolls material off screen |
| Ghost notes | Inactive-track notes paint dimmed behind the active notes |
| Auto-fit | Open and New fit the view to the pitch range of **every** track, padded by two rows |
| Pitch labels | Every octave is always labelled; when rows reach 14 px, every white key is named |

Ghost notes are painted before the active notes and are never hit tested, so selection, dragging, resizing, and deletion still apply only to the selected track. Zoom and scroll are session-only view state and are deliberately not persisted in the song project, matching how active-track selection is already treated.

`SongProject::getNotes (int trackIndex)` was added to read a track's notes by index, following the existing overload convention used by `getTrackId`, `getClipId`, `getTrackMixerSettings`, and `getTrackMidiRouting`. The unindexed `getNotes()` now delegates to it. An out-of-range index returns an empty vector.

## Why auto-fit was necessary

Zoom alone did not fix the problem. A first capture of a real two-part song with the zoom feature present still showed no ghosts, because the default 29-row window starting at MIDI 40 excluded the entire melody. The feature existed but was unreachable without the user knowing to zoom out first. Fitting the view to all tracks on Open and New is what actually makes two-part writing visible by default.

## Evidence

`--ui-snapshot` now accepts an optional `--project`, so any project can be captured rather than only the starter one. This was added specifically so a two-track view could be produced as visual evidence for a visual change.

A capture of an eight-bar, two-part song with a bass part at MIDI 36–47 and a melody at MIDI 69–84 shows both parts simultaneously: active-track notes in the accent gradient, inactive-track notes as dim grey ghosts, with the view auto-fitted from C1 to C5. The committed two-track artifact `artifacts/m6-two-track-authoring.resonance.json` cannot demonstrate this, because it is a duplicated track whose two parts hold identical notes; its ghosts render exactly behind the active notes and are invisible by construction.

Paint behavior itself is not unit tested. The model change is:

| Check | Result |
| --- | --- |
| Indexed note access reads either track without changing selection | passed |
| Indexed note access returns that track's exact note ids and pitches | passed |
| Out-of-range track index returns no notes | passed |

## Full Release gate

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/topology/round-trip/command assertions | 212 passed (was 209) |
| Schema-validated artifacts and fixtures | 20 passed |
| Packaged external command load | passed |
| UI idle CPU | below the 3,000 ms ceiling |

## What remains open

- Note entry is still one mouse click at a time. Multi-note selection and a clipboard are the next slices.
- Ghost notes are read-only by design; editing the other track still requires selecting it.
- Both visible tracks still instantiate the same accepted inventory record, so percussion remains impossible.
- The M6 two-track listening and interaction pass is still not run, and neither this slice nor the command-load slice substitutes for it.
