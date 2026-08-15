# M6 bounded two-track authoring checkpoint

Date: 2026-08-09

Status: technically verified; listening approval and missing-plug-in recovery remain open

## Scope

Editor 0.5.0 turns the proven two-slot runtime into a bounded normal-authoring workflow. A song now persists one or two ordered Surge XT instrument tracks. The public limit is intentionally smaller than the eight-lane realtime capacity so persistence, topology, sound state, UI, and Undo remain testable together.

This checkpoint does not claim that the duplicated starter loop is a good arrangement or mix. The packaged authoring gate keeps transport stopped and reports `audioEmitted: false`.

## Project and migration contract

- `schema/song-project.schema.json` is canonical schema version 3 with one or two tracks.
- `schema/song-project-v2.schema.json` archives the exact previous writer contract.
- Versions 1 and 2 migrate in memory without rewriting their source files; explicit Save writes version 3.
- Track and clip IDs are unique, note IDs are unique across the whole project, and both clips must use the shared project loop length.
- A third persisted track, duplicate identities, cross-track duplicate note IDs, and different loop lengths fail closed.
- Active-track selection and both A/B candidate lanes remain session-only.

The version-2 migration fixture source remained byte-identical at SHA-256 `4b15956b981e085602e3e000f94bd08992ff7ea9ba53669d67a0be917406f21b`.

## Runtime and editor behavior

Normal startup creates two distinct instances of the accepted Surge XT inventory record before the audio callback is registered. Project order maps to runtime slots zero and one; a one-track song leaves slot one prepared but disabled in the immutable mixer snapshot.

The track card now exposes:

- selected-track choice;
- duplicate, remove, move-left, and move-right actions;
- gain, pan, mute, and solo;
- the selected track's stereo meter;
- the selected track's piano roll, native Surge editor, sound A/B lane, and note A/B lane.

Duplicating copies accepted state, notes, and mixer data exactly while assigning new track, clip, and note IDs plus MIDI output channel 2. Remove, reorder, Add, and their Undo/Redo paths remap accepted project state back into the appropriate stable slot. Track context is blocked while a candidate or uncaptured live Surge edit could otherwise be applied to the wrong track.

Open validates every identity and state before replacing the active project. It captures the current runtime states, restores the candidate tracks, and rolls already-restored slots back if a later restore fails.

## Automated evidence

The Release gate passed:

- 124 real-time scheduler, mixer, safety, and runtime assertions;
- 209 project, version-1/version-2 migration, topology, round-trip, and edit-command assertions;
- two distinct real Surge XT 1.3.4 instances with 2,855 parameters each;
- hidden Add Track, independent note/mixer/state, reorder/remap, Undo, remove/Undo, and schema-v3 Save/Open checks;
- the accepted M4 and M5 packaged workflows;
- 19 JSON artifacts and fixtures against their versioned schemas;
- a 1,280 x 860 packaged UI snapshot and the four-second idle-process gate;
- zero invalid samples or processor exceptions in the authoring gate.

The standalone real-Surge runtime gate still rendered both slots in memory without attaching the engine callback to the device. Its recorded 44.1 kHz / 441-sample run processed 100 render blocks plus eight settle blocks, with average callback load `0.007391499998048`, maximum load `0.387239992618561`, and zero invalid samples, clips, or processor exceptions.

The final packaged editor SHA-256 is `46af844d523e46f82f0b0cf7adf9b491c75e44b1320706ba46b06ae45f05db82`. The generated two-track schema-v3 project SHA-256 is `8c2a9ee5c62a825bf0dd60453448f96f1506fa97e8a81c8df084026e6f81d8b7`, and the UI snapshot SHA-256 is `5341f4cdfa80f660eaf362f98fee7e0b1ba412e6df9010d2b1f9dc92e9fd152c`.

## Remaining M6 gates

- Listen to an intentional two-track project through the exact packaged editor and judge balance, pan, mute, solo, switching, and first-play behavior.
- Add user-facing preservation/recovery for a project whose required plug-in cannot be instantiated.
- Keep different plug-in products, more than two persisted tracks, buses, effects, arrangement, and automation outside this bounded slice.

Technical completion remains separate from musical approval. M4 and M5 acceptance evidence remains unchanged.
