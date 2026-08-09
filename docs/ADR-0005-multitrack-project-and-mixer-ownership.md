# ADR-0005: Migrate projects before publishing a fixed-capacity mixer

Status: Accepted for M6 implementation

Date: 2026-08-09

## Context

M4 and M5 established trusted single-instrument sound and note-proposal workflows. Their persistence model still serialised hard-coded `track-1` and `loop-1` identities, and the realtime engine owned one plug-in and one immutable note sequence. Adding a second plug-in directly to that shape would make identity, migration, Undo, missing-plug-in recovery, and realtime ownership ambiguous at the same time.

M6 needs a project boundary that old songs can enter safely and an audio-side shape that cannot grow or allocate in the callback. It also needs to avoid claiming that a schema with an array is already a working multi-track engine.

## Decision

### Project schema and identity

Editor 0.4.0 writes song-project schema version 2. It continues to persist exactly one instrument track and one loop clip in the first M6 slice, but their IDs are authoritative model data rather than serializer constants.

Each version-2 track additionally owns:

- mixer state: `gainDb` from `-60` through `12`, `pan` from `-1` through `1`, `mute`, and `solo`;
- MIDI routing: input channel `0` through `16`, where `0` means omni, and output channel `1` through `16`.

New tracks begin at `0 dB`, centre pan, unmuted, unsoloed, omni input, and output channel 1. Edit commands resolve and validate against the active project's stored track and clip IDs.

The editor accepts schema versions 1 and 2. A valid version-1 document is parsed into a separate candidate, retains its track ID, track name, clip ID, notes, exact opaque plug-in state, and state hash, and receives the neutral mixer and MIDI defaults above. Loading does not rewrite the source file. The in-memory candidate becomes version 2, and a later explicit Save writes version 2. Unknown future versions and incomplete or out-of-range version-2 mixer/routing data fail closed.

The archived version-1 contract remains `schema/song-project-v1.schema.json`. The canonical writer contract is `schema/song-project.schema.json`.

### Realtime mixer ownership

The first audio-side M6 contract is `MixerSnapshot` in `src/mixer_snapshot.h`:

- capacity is fixed at eight tracks;
- every lane contains a fixed-capacity `SequenceSnapshot` plus render-time gain, pan, mute, solo, enabled state, and MIDI-routing values;
- the snapshot is trivially copyable and contains no owning pointer or growable container;
- track count is clamped to capacity before any read;
- mute and inactive lanes produce silence;
- any enabled solo lane gates enabled non-solo lanes;
- pan uses stereo balance semantics: centre feeds both channels, hard left silences right, and hard right silences left;
- invalid negative linear gain is clamped to silence.

The message thread owns the mutable project, plug-in-instance creation and destruction, topology edits, and conversion to immutable render data. When multiple instances are added, each instance and its preallocated MIDI/audio scratch storage will live in a stable message-thread-created runtime slot. Instances must be prepared before publication and retired only after the audio callback can no longer reference their topology. The audio callback will read a published fixed-capacity snapshot/topology, render already-prepared slots, apply per-track gain and pan, accumulate into the master bus, and publish bounded meter values. It must not create or destroy plug-ins, resize a container or buffer, perform file I/O, or wait for the message thread.

This ADR defines that ownership boundary; the first slice does not yet replace the one-instance production engine with the multi-instance renderer.

## Consequences

- Version-1 projects remain usable without an in-place rewrite.
- Stable track and clip identities can survive future add, remove, reorder, command, and recovery operations.
- Per-track settings have a persistence home before controls or processing depend on them.
- The fixed eight-track ceiling makes memory, CPU, and failure tests bounded for the first ensemble implementation.
- Schema version 2 is honest about current production behavior by retaining exactly one track until the runtime and UI can safely support more.
- Mixer settings currently round-trip but do not yet change audible output; no listening approval is implied by this foundation.

## Rejected alternatives

- **Widen the track array and instantiate multiple plug-ins in one change.** This combines persistence, topology, realtime lifetime, UI, and recovery failures without a proven migration boundary.
- **Keep hard-coded track and clip IDs.** Commands and future reorder/recovery behavior would target positions rather than durable entities.
- **Make mixer fields optional in version 1.** That silently changes the meaning of an existing public schema and leaves defaults dependent on reader implementation.
- **Use a dynamic vector or graph directly in the audio callback.** Topology changes could allocate, invalidate references, or require a blocking lock.
- **Persist meters.** Meters are transient render observations, not accepted song state.

## Follow-up gate

Add a second accepted instrument through stable runtime slots and the fixed-capacity publication boundary, then prove two-track scheduling, gain/pan/mute/solo behavior, meters, CPU/clipping limits, shutdown, missing-plug-in preservation, and complete state round trips before widening the production schema or adding broad mixer UI.
