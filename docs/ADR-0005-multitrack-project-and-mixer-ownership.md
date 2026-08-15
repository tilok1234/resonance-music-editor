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

The message thread owns the mutable project, plug-in-instance creation and destruction, topology edits, and conversion to immutable render data. Each instance and its preallocated MIDI/audio scratch storage lives in one of eight stable message-thread-created runtime slots. Instances are installed before engine preparation, and prepared topology changes fail closed. The audio callback reads a double-buffered fixed-capacity snapshot, renders already-prepared slots against one playhead, applies per-track gain and pan, accumulates into the preallocated master bus, and publishes bounded meter and safety values. It does not create or destroy plug-ins, resize a container or buffer, perform file I/O, or wait for state access.

The indexed state API captures and restores one selected slot under the same non-blocking callback access boundary. The original unindexed API remains a slot-zero compatibility path for the accepted M4 sound workflow.

## Implementation status

- Slice 1 implements schema-version-2 migration, stable identities, persisted mixer/MIDI state, and the pure eight-lane snapshot contract.
- Slice 2 replaces the one-instance engine shape with the eight stable runtime slots and proves two distinct real Surge instances through the production render/mix path.
- Slice 3 implements editor 0.5.0 and schema version 3. It reads versions 1, 2, and 3; writes one or two ordered tracks; preserves version-2 mixer/MIDI and exact state without source rewrite; and rejects duplicate project IDs, different per-track loop lengths, and a third track.
- Normal authoring preloads two distinct instances of the same accepted inventory record before the device callback is prepared. Persisted order maps to runtime slots zero and one. Track selection, duplicate, remove, reorder, gain, pan, mute, solo, active-track meters, Save/Open, and topology Undo/Redo are exposed in the editor.
- Active selection and pending A/B candidates remain session-only. Sound and note proposals are bound to the selected track, and track context cannot change while a candidate or uncaptured live Surge edit could cross that boundary.

## Consequences

- Version-1 projects remain usable without an in-place rewrite.
- Stable track and clip identities can survive future add, remove, reorder, command, and recovery operations.
- Per-track settings have a persistence home before controls or processing depend on them.
- The fixed eight-track ceiling makes memory, CPU, and failure tests bounded for the first ensemble implementation.
- At Slice 1, schema version 2 honestly retained exactly one track until the runtime and UI could safely support more.
- Mixer and MIDI output settings affect both visible runtime slots. Gain, pan, mute, solo, and active-track meters are exposed, while MIDI routing remains persisted but has no dedicated control in this slice.
- Schema version 3 deliberately caps normal authoring at two tracks even though the audio contract retains eight lanes; widening persistence remains a separate bounded change.
- Both visible tracks currently instantiate the same accepted Surge inventory record. Different plug-in assignment and user-facing missing-plug-in recovery remain follow-up work.
- The slice-3 automated gate is technical evidence only; it does not approve the doubled loop, sound choices, balance, or stereo mix by ear.

## Rejected alternatives

- **Widen the track array and instantiate multiple plug-ins in one change.** This combines persistence, topology, realtime lifetime, UI, and recovery failures without a proven migration boundary.
- **Keep hard-coded track and clip IDs.** Commands and future reorder/recovery behavior would target positions rather than durable entities.
- **Make mixer fields optional in version 1.** That silently changes the meaning of an existing public schema and leaves defaults dependent on reader implementation.
- **Use a dynamic vector or graph directly in the audio callback.** Topology changes could allocate, invalidate references, or require a blocking lock.
- **Persist meters.** Meters are transient render observations, not accepted song state.

## Follow-up gate

Run the explicit two-track listening and interaction pass on the packaged editor, then add user-facing missing-plug-in recovery before declaring M6 complete. Different instrument products, more than two persisted tracks, buses, arrangement, and automation remain later bounded changes.

## Amendment, 2026-08-13

The decision stands; two of its bounded numbers have since moved with evidence.

- The persisted track ceiling rose from two to four in song-project schema version 4. ADR-0005 deliberately kept the project below the runtime's eight lanes until multi-instance rendering was proven; that proof arrived with the M6 two-track runtime slice, so the gap stopped serving a purpose. Four rather than eight because preloading is eager — prepared plug-in topology never changes at runtime — so the ceiling is also the number of Surge instances created at startup. Measured idle process CPU went from about 1,300 ms at two instances to 1,797 ms at four against a 3,000 ms ceiling; eight extrapolates to roughly 2,800 ms, which is inside the limit but with almost no margin.
- The clip ceiling rose from 8 to 64 bars in schema version 5, and notes per clip from 512 to 1,024.

The eight-lane runtime and mixer contract itself is unchanged. Raising the project ceiling to eight remains available once someone pays for it with a measurement rather than an extrapolation.

One consequence worth recording: `MixerSnapshot` embeds a fixed-capacity sequence per lane, so growing note capacity grows it quadratically against lane count. At 2,048 notes an engine became roughly 1.2 MB and overflowed a default thread stack. Fixed-capacity flat storage is the wrong axis to scale indefinitely; reusable clip instances (roadmap M7.3) are the intended answer for long songs.
