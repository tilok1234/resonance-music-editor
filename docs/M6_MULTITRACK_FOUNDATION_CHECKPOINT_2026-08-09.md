# M6 multi-track foundation checkpoint

Date: 2026-08-09

Status: implemented and technically verified; M6 remains in progress

## Result

The first M6 slice establishes the persistence, identity, and realtime ownership contracts needed before a second instrument is introduced. Editor version 0.4.0 writes song-project schema version 2, reads both versions 1 and 2, preserves non-default legacy track and clip IDs, and adds bounded per-track mixer and MIDI-routing state. A fixed-capacity eight-lane mixer snapshot and its mute, solo, gain, pan, and capacity semantics are native-tested.

At this first checkpoint, the production editor still loaded and rendered exactly one Surge XT instance. This checkpoint did not claim that multiple tracks, live per-track mixing, mixer controls, meters, missing-plug-in recovery, or track topology Undo/Redo were implemented. The later [M6 two-track runtime checkpoint](M6_TWO_TRACK_RUNTIME_CHECKPOINT_2026-08-09.md) completes the bounded engine proof while retaining the one-track schema and UI.

## Persistence and migration evidence

The portable fixture `tests/fixtures/song-project-v1-migration.resonance.json` deliberately uses `track-migrated` and `clip-migrated`, one symbolic note, and a four-byte opaque state payload. Its SHA-256 is `4725dd74075981ceb6ecd605db270954deafb7743b98190d090eb42dd677c0f7`.

The native project suite proves that:

- loading leaves the fixture byte-identical;
- the in-memory model becomes schema version 2;
- track name, track ID, clip ID, note, tempo, plug-in bytes, and plug-in SHA-256 survive;
- version-1 tracks receive `0 dB`, centre, unmuted, unsoloed, omni-in, channel-one-out defaults;
- an edit command resolves against `track-migrated` and `clip-migrated`, while a hard-coded default target is rejected;
- an explicit save writes the complete version-2 mixer and MIDI objects and reopens successfully;
- bounded non-default mixer/routing values round-trip;
- schema version 3, missing version-2 mixer state, pan `1.5`, and MIDI output channel `0` are rejected.

The resulting migrated test document was 982 bytes. The production real-Surge self-test document was 91,475 bytes and validated as schema version 2.

## Realtime ownership evidence

`src/mixer_snapshot.h` fixes the first mixer capacity at eight lanes and keeps the snapshot trivially copyable. Native tests cover centre and hard-pan balance, positive gain, mute, global solo gating, disabled-solo behavior, negative-gain clamping, and out-of-capacity reads.

This is a contract test, not a claim that the one-instance `RealtimeEngine` already renders eight plug-ins. [ADR-0005](ADR-0005-multitrack-project-and-mixer-ownership.md) defines the message-thread and callback ownership required for that follow-up.

## Verification

The Release sequence completed with:

- 92 realtime scheduler and mixer-contract assertions;
- 162 project, migration, round-trip, sound, mixer/MIDI Undo, and edit-command assertions;
- 14 schema-validated current and historical artifacts/fixtures;
- real Surge XT 1.3.4 state/name round trip with 2,855 parameters;
- Windows Audio at 44.1 kHz, 441-sample blocks, and 441 samples of reported output latency;
- all accepted M5 preview, A/B, Reject, Apply, Undo/Redo, deterministic-repeat, invalid-input, and cleanup regressions passing;
- a 92,810-byte packaged UI snapshot with SHA-256 `cfe19c3b7051f28dcc20acc162782397ebfca8be353c6ac2c104f9aabbc35c57`;
- 1,140.6 ms process CPU over a 6,057 ms idle observation;
- zero music emitted by the packaged self-test and no leftover editor process.

The version-2 real-Surge project artifact has SHA-256 `6b899709ba8c56c0c652be905ae87c50ddad4f6c4bbe286e18851b93f0baf83b`. The packaged 0.4.0 editor has SHA-256 `88ba46e16061df4439a8d7a2d641127f47d78ec04f692fb0f00234896556a919`.

## Listening boundary

No audible production path changed in this slice, and no new listening approval was requested. Technical migration and mixer-contract tests do not establish mix quality.

## Next gate

This historical gate was completed by the [M6 two-track runtime checkpoint](M6_TWO_TRACK_RUNTIME_CHECKPOINT_2026-08-09.md): the second instance, state isolation, render/mix behavior, shutdown, CPU, clipping, and missing-plug-in paths now pass together while schema version 2 remains one production track.
