# Resonance song project format

Status: implemented schema version 1

## Overview

Resonance songs use the `.resonance.json` suffix and a versioned JSON document. The canonical machine-readable contract is [`schema/song-project.schema.json`](../schema/song-project.schema.json). The current editor reads and writes schema version 1 only.

The file stores symbolic music and enough VST3 identity and state to reopen the one implemented instrument track. It never embeds the VST3 binary, factory content, sample library, or a machine's plug-in inventory.

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
`-- tracks[1]
    |-- id, name, role
    |-- instrument
    |   |-- format and plug-in identity
    |   `-- Base64 state and SHA-256
    `-- clips[1]
        |-- id, startTick, lengthTicks, loopEnabled
        `-- notes[]
            `-- id, startTick, lengthTicks, midiNote, velocity
```

## Root fields

| Field | Version 1 meaning |
| --- | --- |
| `schemaVersion` | integer `1`; selects the persistence contract |
| `editorVersion` | application version that wrote the file; currently `0.3.0` |
| `title` | non-empty song title |
| `sampleRate` | one of 44,100, 48,000, 88,200, or 96,000 Hz |
| `ppq` | fixed at 960 ticks per quarter note |
| `tempoMap` | schema allows one or more entries; the current editor writes and uses the first entry at tick 0 |
| `meterMap` | schema allows meter entries; the current editor writes one 4/4 entry at tick 0 |
| `editor.snapBeats` | one of 0.125, 0.25, 0.5, or 1.0 beats |
| `tracks` | exactly one instrument track in version 1 |

The current loader validates the implemented subset needed by the editor. The standalone JSON-schema check enforces the full public shape, including `additionalProperties: false` where declared.

## Instrument track

Version 1 requires exactly one track with `role: "instrument"`, one VST3 instrument object, and one loop clip.

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

The state hash is an integrity check, not a signature, semantic sound fingerprint, or proof that a plug-in is safe. Surge can return different opaque bytes for the same restored sound at different lifecycle stages. Resonance therefore preserves this exact saved hash in the document and keeps any post-restore live-equivalent hash in session memory only. `soundName` is optional for backward compatibility within schema version 1; new saves always write it, while an older version-1 file loads as `Project sound`. Project opening also checks that the saved identifier and name match the currently accepted instrument. An exact identifier matches directly; VST3 identifiers from a relocated compatible bundle may match by the immutable JUCE UID suffix defined in `src/plugin_identity.h`. The editor-version bump from 0.2.0 to 0.3.0 does not change schema version 1, and 0.2.0 projects remain valid inputs.

## Clip and notes

The current clip starts at tick 0, loops, and is 4 through 32 beats long. At 960 PPQ this is 3,840 through 30,720 ticks.

| Note field | Constraint |
| --- | --- |
| `id` | non-empty and unique within the project |
| `startTick` | integer at or after 0 and before the loop end |
| `lengthTicks` | positive integer; the note must end within the loop |
| `midiNote` | integer 0 through 127 |
| `velocity` | integer 1 through 127 |

Version 1 accepts at most 512 notes. Beats are converted to integer ticks when saved:

```text
tick = round(beat * 960)
beat = tick / 960
```

Using integer ticks makes normal edits deterministic across JSON round trips and avoids serializing accumulated floating-point timing error.

## Illustrative skeleton

This shortened example explains the shape but is not loadable because the placeholder state and hash are not real:

```json
{
  "schemaVersion": 1,
  "editorVersion": "0.3.0",
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

Saving validates the accepted project snapshot, records current plug-in metadata, updates a supported live sample rate, serializes the entire document, and writes the target through JUCE's `replaceWithText`. The project is marked clean only after the write succeeds.

Changes made inside the native Surge editor remain live preview state until **Capture B**. Capturing creates an ephemeral named candidate; **Apply B** stores its name, state, and exact hash as one dirty Undo transaction. Save writes only the accepted A snapshot, so arbitrary native changes and an unapplied B are not silently substituted into the song. New, Open, and Close capture the live state once and warn when it matches neither post-restore live-equivalent A nor B. These ephemeral comparison hashes are never serialized into the project.

## Transactional open behavior

Opening is intentionally fail-closed:

1. parse JSON into a separate candidate model;
2. require schema version 1, supported timing values, exactly one track and clip, bounded notes, unique IDs, and valid ranges;
3. Base64-decode state and verify its SHA-256;
4. compare saved and active VST3 identity and name;
5. stop and rewind transport, close the native plug-in view, and restore state;
6. replace the active project only after restoration succeeds;
7. publish its sequence and mark it clean.

A failed parse, identity check, state check, or restore leaves the active project model in place. Transport is stopped before restore for lifecycle safety.

## External edit commands

M5 edit commands are proposal documents, not fields inside `.resonance.json`. Their independent version-1 contract is `schema/edit-command.schema.json`. A command carries the SHA-256 of the active project's canonical material JSON, where only `editorVersion` is omitted from hashing. The hash therefore covers notes, timing, metadata, and the accepted opaque instrument state without forcing a song-project schema migration.

Preview parses and validates the command against a separate candidate project. Reject discards that candidate. Apply rechecks the hash and reproduces the preview through ordinary `SongProject` note operations as one Undo transaction. Commands and pending previews are intentionally not saved in song-project schema version 1.

The editor may audition a pending candidate through an immutable realtime sequence, but Save still serializes only the active accepted project. A pitch-only update may preserve an existing note's legacy non-tick-exact timing byte-semantically; any start or length changed by a command must resolve to an integer tick at 960 PPQ. This compatibility exception prevents old accepted articulation such as `0.82` beats from being silently quantized by an unrelated pitch edit.

## Compatibility and migrations

Do not reinterpret version 1 fields in place. A future change that adds multiple tracks, arrangement sections, automation, effects, or game-transition metadata must:

1. define a new schema version or a rigorously backward-compatible optional field policy;
2. add a migration function with deterministic fixtures;
3. retain the original file until migration and validation succeed;
4. preserve unknown future versions by refusing to overwrite them;
5. add round-trip, failure-path, and schema tests;
6. update this document, the JSON schema, the handoff, and the roadmap together.

## Manual editing warning

Human-readable JSON is useful for diagnosis, but hand editing plug-in state, identifiers, hashes, or note timing can make a song unloadable. Back up the file, validate it against the schema, and reopen it in the editor before treating an external edit as accepted.
