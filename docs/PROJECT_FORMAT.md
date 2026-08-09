# Resonance song project format

Status: implemented schema version 3 with version-1 and version-2 migration

## Overview

Resonance songs use the `.resonance.json` suffix and a versioned JSON document. The canonical writer contract is [`schema/song-project.schema.json`](../schema/song-project.schema.json). Editor 0.5.0 reads schema versions 1, 2, and 3 and writes schema version 3. The archived input contracts are [`schema/song-project-v1.schema.json`](../schema/song-project-v1.schema.json) and [`schema/song-project-v2.schema.json`](../schema/song-project-v2.schema.json).

The file stores symbolic music and enough VST3 identity and state to reopen one or two implemented instrument tracks. It never embeds the VST3 binary, factory content, sample library, or a machine's plug-in inventory. Active-track selection is editor session state and is not serialized.

## Document shape

```text
project
|-- schemaVersion
|-- editorVersion
|-- title
|-- sampleRate
|-- ppq
|-- tempoMap[]
|-- meterMap[]
|-- editor
|   `-- snapBeats
`-- tracks[1..2]
    |-- id, name, role
    |-- mixer
    |   `-- gainDb, pan, mute, solo
    |-- midi
    |   `-- inputChannel, outputChannel
    |-- instrument
    |   |-- format and plug-in identity
    |   `-- Base64 state and SHA-256
    `-- clips[1]
        |-- id, startTick, lengthTicks, loopEnabled
        `-- notes[]
            `-- id, startTick, lengthTicks, midiNote, velocity
```

## Root fields

| Field | Version 3 meaning |
| --- | --- |
| `schemaVersion` | integer `3`; selects the persistence contract |
| `editorVersion` | application version that wrote the file; currently `0.5.0` |
| `title` | non-empty song title |
| `sampleRate` | one of 44,100, 48,000, 88,200, or 96,000 Hz |
| `ppq` | fixed at 960 ticks per quarter note |
| `tempoMap` | schema allows one or more entries; the current editor writes and uses the first entry at tick 0 |
| `meterMap` | schema allows meter entries; the current editor writes one 4/4 entry at tick 0 |
| `editor.snapBeats` | one of 0.125, 0.25, 0.5, or 1.0 beats |
| `tracks` | one or two ordered instrument tracks in the current bounded slice |

The current loader validates the implemented subset needed by the editor. The standalone JSON-schema check enforces the full public shape, including `additionalProperties: false` where declared.

## Instrument track

The current version-3 writer requires one or two tracks with `role: "instrument"`. Every track owns one mixer object, one MIDI-routing object, one VST3 instrument object, and one loop clip. Track, clip, and note IDs are stable model data rather than array-derived identities; all three ID classes must be unique across the complete project. Array order is the persisted track order.

| Mixer field | Constraint and meaning |
| --- | --- |
| `gainDb` | number from `-60` through `12`; accepted per-track gain |
| `pan` | number from `-1` (left) through `1` (right), with `0` centred |
| `mute` | boolean accepted mute state |
| `solo` | boolean accepted solo state |

| MIDI field | Constraint and meaning |
| --- | --- |
| `inputChannel` | integer `0` through `16`; `0` means omni |
| `outputChannel` | integer `1` through `16` |

The production editor maps persisted track order to stable runtime slots zero and one and exposes active-track gain, pan, mute, solo, meter, selection, add, remove, and reorder controls. New and migrated version-1 tracks default to `0 dB`, centre, unmuted, unsoloed, omni input, and output channel 1. Duplicating a track copies its accepted instrument state and notes exactly, creates new track/clip/note IDs, selects the duplicate, and assigns MIDI output channel 2. A third track fails closed in this slice.

| Instrument field | Meaning |
| --- | --- |
| `format` | exactly `VST3` |
| `pluginIdentifier` | JUCE identifier recorded when the song was saved |
| `pluginName` | expected plug-in name |
| `vendor` | plug-in manufacturer string |
| `version` | plug-in version string |
| `soundName` | host-owned name for the currently accepted opaque sound snapshot |
| `stateEncoding` | exactly `base64` |
| `state` | opaque non-empty VST3 state bytes encoded as Base64 |
| `stateSha256` | SHA-256 of the decoded state bytes |

The state hash is an integrity check, not a signature, semantic sound fingerprint, or proof that a plug-in is safe. Surge can return different opaque bytes for the same restored sound at different lifecycle stages. Resonance therefore preserves this exact saved hash in the document and keeps any post-restore live-equivalent hash in session memory only. `soundName` remains optional for backward compatibility; a file without it loads as `Project sound`, while new saves always write it. Project opening checks every track's saved identifier and name against the currently accepted instrument. An exact identifier matches directly; VST3 identifiers from a relocated compatible bundle may match by the immutable JUCE UID suffix defined in `src/plugin_identity.h`. This slice preloads two distinct instances of the same accepted inventory record; assigning different plug-in products and interactive missing-plug-in recovery remain future work. Version-1 projects written by editor 0.2.0 or 0.3.0 and version-2 projects written by editor 0.4.0 remain valid inputs to editor 0.5.0.

## Clip and notes

Each current clip starts at tick 0, loops, and uses the shared project loop length of 4 through 32 beats. At 960 PPQ this is 3,840 through 30,720 ticks. Different per-track loop lengths are rejected.

| Note field | Constraint |
| --- | --- |
| `id` | non-empty and unique within the project |
| `startTick` | integer at or after 0 and before the loop end |
| `lengthTicks` | positive integer; the note must end within the loop |
| `midiNote` | integer 0 through 127 |
| `velocity` | integer 1 through 127 |

The current schemas accept at most 512 notes per track. Beats are converted to integer ticks when saved:

```text
tick = round(beat * 960)
beat = tick / 960
```

Using integer ticks makes normal edits deterministic across JSON round trips and avoids serializing accumulated floating-point timing error.

## Illustrative skeleton

This shortened example explains the shape but is not loadable because the placeholder state and hash are not real:

```json
{
  "schemaVersion": 3,
  "editorVersion": "0.5.0",
  "title": "Untitled",
  "sampleRate": 48000,
  "ppq": 960,
  "tempoMap": [{ "tick": 0, "bpm": 120 }],
  "meterMap": [{ "tick": 0, "numerator": 4, "denominator": 4 }],
  "editor": { "snapBeats": 0.25 },
  "tracks": [{
    "id": "track-1",
    "name": "Surge XT",
    "role": "instrument",
    "mixer": {
      "gainDb": 0,
      "pan": 0,
      "mute": false,
      "solo": false
    },
    "midi": {
      "inputChannel": 0,
      "outputChannel": 1
    },
    "instrument": {
      "format": "VST3",
      "pluginIdentifier": "VST3-Surge XT-<path-derived-part>-190e4fbd",
      "pluginName": "Surge XT",
      "vendor": "Surge Synth Team",
      "version": "1.3.4",
      "soundName": "Warm pluck",
      "stateEncoding": "base64",
      "state": "<base64 VST3 state>",
      "stateSha256": "<SHA-256 of decoded state>"
    },
    "clips": [{
      "id": "loop-1",
      "startTick": 0,
      "lengthTicks": 7680,
      "loopEnabled": true,
      "notes": [{
        "id": "note-1",
        "startTick": 0,
        "lengthTicks": 480,
        "midiNote": 60,
        "velocity": 96
      }]
    }]
  }]
}
```

Use the versioned fixtures under `artifacts/` for actual loadable examples.

## Save behavior

Saving validates every accepted track snapshot, records current plug-in metadata for every track, updates a supported live sample rate, serializes the entire document, and writes the target through JUCE's `replaceWithText`. The project is marked clean only after the write succeeds.

Changes made inside the native Surge editor remain live preview state until **Capture B**. Capturing creates an ephemeral named candidate; **Apply B** stores its name, state, and exact hash as one dirty Undo transaction. Save writes only the accepted A snapshot, so arbitrary native changes and an unapplied B are not silently substituted into the song. New, Open, and Close capture the live state once and warn when it matches neither post-restore live-equivalent A nor B. These ephemeral comparison hashes are never serialized into the project.

## Transactional open behavior

Opening is intentionally fail-closed:

1. parse JSON into a separate candidate model;
2. require schema version 1, 2, or 3, supported timing values, one or two tracks, one clip per track, stable unique IDs, a shared loop length, bounded notes, and valid ranges;
3. Base64-decode every track state and verify every SHA-256;
4. compare every saved VST3 identity and name with the accepted inventory record;
5. stop and rewind transport, close the native plug-in view, and capture the current state of both runtime slots;
6. restore every candidate track into its corresponding preloaded runtime slot, rolling restored slots back if a later restore fails;
7. replace the active project only after every restoration succeeds;
8. publish the two-track mixer snapshot and mark the project clean.

Version-1 and version-2 candidates become schema version 3 in memory, but Open does not rewrite their source files. A failed parse, migration, identity check, state check, or restore leaves the active project model and source file in place. Transport is stopped before restore for lifecycle safety. Unknown future schema versions fail closed.

## External edit commands

M5 edit commands are proposal documents, not fields inside `.resonance.json`. Their independent version-1 contract is `schema/edit-command.schema.json`; command and song-project versions are unrelated. A command carries the SHA-256 of the complete active project's canonical material JSON, where only `editorVersion` is omitted from hashing. The hash covers ordered tracks, stable track/clip/note IDs, version-3 mixer and MIDI state, notes, timing, metadata, and every accepted opaque instrument state.

Preview parses and validates the command against a separate candidate project and requires its target IDs to match the selected track and clip. Reject discards that candidate. Apply rechecks the complete-project hash and reproduces the preview through ordinary `SongProject` note operations as one Undo transaction. Commands, active-track selection, and pending previews are intentionally not saved in song-project schema version 3.

The editor may audition a pending candidate through an immutable realtime sequence, but Save still serializes only the active accepted project. A pitch- or velocity-only update may preserve an existing note's legacy non-tick-exact timing byte-semantically; any start or length changed by a command must resolve to an integer tick at 960 PPQ. This compatibility exception prevents old accepted articulation such as `0.82` beats from being silently quantized by an unrelated edit.

The dynamics target, maximum-delta, and seed controls are also outside the song-project schema. They are session-only proposal inputs that resolve whole-loop or selected-note IDs into an ordinary concrete version-1 `editNotes` command before preview. The command stores the seed as provenance, but its concrete velocity values are authoritative: Apply does not rerun the pseudo-random resolver. Pending B and its inputs are not persisted. This keeps Save/Open unchanged and makes a reviewed B independent of future resolver implementation changes.

## Compatibility and migrations

Migration to version 3 is explicit and deterministic:

1. validate and parse the complete version-1 document into a separate candidate;
2. retain track name, track ID, clip ID, notes, timing, plug-in identity, sound name, exact opaque state, and its SHA-256;
3. add the documented neutral mixer and MIDI defaults;
4. install a one-track schema-version-3 in-memory model only after the candidate succeeds;
5. leave the original version-1 file byte-identical until the user explicitly saves;
6. write the complete version-3 contract on that later Save.

`tests/fixtures/song-project-v1-migration.resonance.json` uses non-default track and clip IDs so migration and command tests cannot pass by relying on `track-1` or `loop-1`. Version 1 receives neutral mixer/MIDI defaults. `tests/fixtures/song-project-v2-migration.resonance.json` proves that version-2 stable identity, non-default mixer/MIDI data, notes, and exact state survive without source rewrite. Version 3 additionally rejects duplicate track or clip IDs, duplicate note IDs across tracks, mismatched loop lengths, and a third track. The two archived schemas remain authoritative for historical input validation.

Do not reinterpret either version in place. A future change that widens the production track count or adds arrangement sections, automation, effects, or game-transition metadata must:

1. define a new schema version or a rigorously backward-compatible optional field policy;
2. add a migration function with deterministic fixtures;
3. retain the original file until migration and validation succeed;
4. preserve unknown future versions by refusing to overwrite them;
5. add round-trip, failure-path, and schema tests;
6. update this document, the JSON schema, the handoff, and the roadmap together.

## Manual editing warning

Human-readable JSON is useful for diagnosis, but hand editing plug-in state, identifiers, hashes, or note timing can make a song unloadable. Back up the file, validate it against the schema, and reopen it in the editor before treating an external edit as accepted.
