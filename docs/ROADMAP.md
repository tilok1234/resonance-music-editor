# Roadmap

Status: ordered product and engineering plan

## Roadmap rule

Each milestone must leave a usable editor and preserve the proven safety boundaries. New capabilities should be added as vertical slices: model, UI, playback, persistence, tests, evidence, and user review together. Avoid building many disconnected controls that do not survive save/open or cannot be auditioned.

The focus is game music. General sound-effect creation is not part of this roadmap.

## Milestone summary

| Milestone | Status | Outcome |
| --- | --- | --- |
| F0 VST3 compatibility foundation | Complete | Surge can be discovered, instantiated, edited, state-round-tripped, and rendered |
| F1 Crash-isolated scanning | Complete | unknown discovery runs in a bounded child with inventory and quarantine |
| F2 First real-time editor | Complete | accepted Surge plays through explicit Windows Audio with safe transport and diagnostics |
| F3 Editable single-track song | Complete | piano roll, Undo/Redo, exact state, and `.resonance.json` save/open |
| M4 Sound and preset workflow | Complete | Accepted as version 0.3.0 after A/B, persistence, lifecycle, and listening gates passed |
| M5 Unified edit-command layer | Complete | explicitly accepted after packaged command, A/B, deterministic controls, listening, Apply/Undo, and cleanup gates |
| M6 Multi-track and mixer | In progress | schema-v2 migration and the fixed eight-slot/two-real-instance runtime are proven; visible persisted multi-track authoring remains |
| M7 Arrangement, automation, and effects | Planned | full sections, curves, buses, and dependable song structure |
| M8 AI music assistant | Planned | natural-language requests resolve to bounded command proposals |
| M9 Game-music authoring and export | Planned | variants, transitions, loops, stems, renders, and engine-facing metadata |
| M10 Release hardening | Planned | performance, recovery, packaging, licensing, compatibility, and support policy |

## Completed foundations

### F0: VST3 compatibility

Delivered the host probe, real Surge state round trip, native editor creation, parameter discovery, MIDI delivery, diagnostic offline render, and machine-readable report.

### F1: Scanner isolation

Delivered one-bundle child scanning, parent deadlines, hang termination, invalid-bundle handling, exact fingerprints, accepted inventory, quarantine, and fail-closed startup consumption.

### F2: Real-time playback

Delivered Windows Audio/WASAPI selection, sample-accurate loop scheduling, transport, Panic, mouse and hardware MIDI, meters, safety gain, sample guards, silent self-test, UI snapshot, and idle regression mode.

### F3: Editable song

Delivered the first piano roll, stable note IDs, gesture-level Undo/Redo, tempo/snap/loop controls, fixed-capacity sequence publication, exact plug-in state persistence, native Save/Open, and project schema version 1.

## M4: Sound and preset workflow

The snapshot-first implementation and its bounded packaged retest are complete. The user preferred B, the saved project preserved it, and the exact native reopen/play/unchanged-recapture/reject/close sequence passed. The user explicitly accepted M4 at 2026-08-08 23:54 +02:00, establishing editor version 0.3.0. M5 continued on a separate stacked branch and was explicitly accepted on 2026-08-09 without changing that executable version.

### Goal

Make sound design a first-class Resonance workflow instead of relying only on whatever the native Surge window happens to expose.

### Deliverables

- inspected the accepted Surge state capabilities without adding startup scanning;
- defined a host-owned named `PluginSoundSnapshot` record;
- captured explicit live state as ephemeral B while keeping project A unchanged;
- selected and auditioned A/B snapshots through a bounded host UI and the existing instance;
- marked the project dirty only when B is applied;
- restored one accepted sound change through global Undo/Redo as one state transaction;
- preserved the exact accepted name and state through `.resonance.json` save/open;
- kept opaque state out of high-frequency UI polling;
- deferred Surge `.fxp` indexing behind a later vendor-specific adapter.

### Acceptance gate

- choose two meaningfully different sounds;
- A/B audition them without a second synth instance;
- apply one, Undo, Redo, Save, close, reopen, and recapture the exact expected state;
- prove transport and note edits still pass;
- prove UI idle behavior remains under the established ceiling;
- obtain a separate user listening judgment for the chosen sounds.

Manual Capture B, A/B, Apply, dirty marker, Undo, Redo, Save/Open, and the user's preference for B passed. The packaged native retest then reopened the exact saved project, normalized A to `91ED214E`, played a loop, captured unchanged B as the same `91ED214E`, reported `STATE MATCHES A`, rejected B, preserved a clean project, and closed without a false warning. The user explicitly accepted the completed gate as M4 version 0.3.0.

### Design question to resolve first

VST3 does not guarantee one uniform factory-preset browsing experience across all instruments. [ADR-0004](ADR-0004-host-owned-sound-snapshots.md) chose named opaque state snapshots because Surge reports zero host programs while its `.fxp` library is vendor-specific. Factory-file indexing remains deferred.

## M5: Unified edit-command layer

Status: complete and explicitly accepted at 2026-08-09 11:02 +02:00. The foundation implements schema version 1, exact project-content SHA-256 preconditions, resolved note add/update/remove, non-mutating candidate projects, explicit note diffs, consume-once Apply/Reject, one-transaction Undo/Redo, strict stale/invalid rejection, seed provenance, native tests, and a portable schema-validated fixture. The editor owns one pending preview, renders before/after note overlays and counts, auditions A/B through normal immutable sequence publication, applies or rejects explicitly, preserves accepted A on Save, and invalidates stale previews. Checkpoint `2351cb6` adds deterministic bounded velocity resolution. Checkpoint `ef9710d` exposes whole-loop/selected-note target, maximum delta, and seed controls, fails closed on invalid input, and proves repeat, A/B, Reject, Apply, and one Undo through the packaged native workflow. The user found A slightly preferable but difficult to distinguish at delta `8`, and heard about the same result at delta `24` and for selected-note delta `32`; M5 acceptance therefore covers the trusted edit-command workflow without claiming that the current Surge sound made B musically superior. Additional transform families and natural-language translation remain later work.

### Goal

Give every edit a validated command and diff representation before connecting an AI model.

### Deliverables

- versioned command schema;
- project revision or content-hash precondition;
- note add/update/remove and bounded transform operations;
- candidate-project preview without mutating the active song;
- before/after visual diff and A/B audition;
- one Apply action mapped to one Undo transaction;
- stale-command and invalid-target rejection;
- deterministic seeds for randomized transforms;
- command tests and portable fixtures.

Manual UI actions do not all need to serialize as external JSON immediately, but they and external commands must converge on the same model operations and invariants.

### Acceptance evidence

- the user previewed whole-loop and selected-note dynamics through the exact packaged editor;
- delta `8` and `24` whole-loop requests plus a delta `32` selected-note request covered multiple strengths and both scopes;
- the packaged UI showed one targeted update and froze proposal inputs while B was pending;
- the user exercised A/B and Reject; the fresh hidden workflow proved deterministic repeat, Apply as one transaction, and Undo to A;
- the fresh report matched the schema-validated artifact byte-for-byte and cleanup left zero editor processes;
- the user explicitly accepted M5 at 2026-08-09 11:02 +02:00 with the subtle-velocity listening caveat recorded.

## M6: Multi-track and mixer

Status: in progress. The schema/identity foundation and the first two-instance runtime slice were implemented and technically verified on 2026-08-09 as editor 0.4.0.

### Goal

Move from one instrument loop to a small, reliable ensemble without breaking real-time behavior.

### Deliverables

- project schema migration from version 1;
- stable track and clip IDs;
- multiple accepted instrument assignments;
- per-track gain, pan, mute, solo, level meter, and MIDI routing;
- a preallocated render/mix path with explicit ownership;
- missing-plug-in preservation and user-facing recovery;
- track add/remove/reorder Undo/Redo;
- CPU, clipping, shutdown, and state round-trip tests.

Effects can begin with a limited bus or slot design, but should not be allowed to obscure the core multi-instrument acceptance gate.

### Slice 1 delivered

- schema version 2 stores authoritative track/clip IDs plus bounded gain, pan, mute, solo, and MIDI routing;
- the loader accepts version 1 and 2, migrates version 1 in memory with neutral settings, preserves exact plug-in bytes and non-default IDs, and leaves the source file untouched;
- version 2 remains honest at exactly one production track until multi-instance rendering is proven;
- edit-command producers and validation use the active project's IDs rather than hard-coded defaults;
- `MixerSnapshot` defines eight fixed, trivially copyable lanes and native-tested capacity, gain, balance, mute, and solo semantics;
- Release verification passed 92 engine/mixer assertions, 162 project/migration/command assertions, the full M4/M5 regressions, and 14 schema validations.

See [ADR-0005](ADR-0005-multitrack-project-and-mixer-ownership.md) and the [M6 foundation checkpoint](M6_MULTITRACK_FOUNDATION_CHECKPOINT_2026-08-09.md).

### Slice 2 delivered

- `RealtimeEngine` owns eight stable message-thread-created slots with preallocated audio/MIDI scratch, indexed state access, and fail-closed prepared topology;
- a double-buffered immutable `MixerSnapshot` publishes separate sequences, MIDI output channels, gain, pan, mute, and solo against one shared playhead;
- the callback renders and sums slots without resizing, publishes bounded per-track/master meters, and counts invalid samples, clips, processor exceptions, and callback load;
- deterministic fake-plug-in cases prove separate scheduling, exact summing and mix controls, meters, safety fallback, oversize handling, missing-slot preservation, state isolation, and shutdown;
- the packaged hidden gate loads two distinct accepted Surge XT instances, renders 100 in-memory blocks plus eight state-settle blocks, applies the accepted M4 B only to slot two, restores both current baseline states, and emits no device audio;
- Release verification passes 124 engine/runtime assertions, 162 project/migration/command assertions, the full packaged M4/M5 regressions, and 16 schema validations.

The production schema and UI remain exactly one track, and the silent runtime gate is not a listening approval. See the [M6 two-track runtime checkpoint](M6_TWO_TRACK_RUNTIME_CHECKPOINT_2026-08-09.md).

### Next slice

Define the next bounded song-schema revision and migrate version 2 without source rewrite, then expose a minimal second instrument track using the proven slots. Add track selection, gain/pan/mute/solo, meters, add/remove/reorder Undo, and user-facing missing-plug-in recovery. Prove Save/Open and identity/state preservation, then run an explicit two-track listening pass before calling M6 complete.

## M7: Arrangement, automation, and effects

### Goal

Turn loop sketches into complete musical forms.

### Deliverables

- arrangement timeline and reusable clips;
- named sections and markers;
- copy, split, loop, move, trim, and duplicate operations;
- tempo and meter changes with tested scheduling;
- parameter, mixer, and effect automation curves;
- bounded parameter event publication to real-time processing;
- effect slots and buses with latency/tail handling;
- offline full-song rendering;
- deterministic arrangement and automation round trips.

## M8: AI music assistant

### Goal

Translate plain-language musical intent into proposals over the trusted command layer.

### Deliverables

- opt-in AI service boundary and privacy disclosure;
- prompt context assembled from symbolic selections rather than arbitrary local files;
- schema-constrained command output;
- host-side validation and deterministic resolution;
- preview, refine, apply, and reject workflow;
- initial operations for rhythm, articulation, velocity, register, motif variation, and section energy;
- no mutation on invalid, stale, canceled, or rejected proposals;
- clear disclosure of affected bars, tracks, notes, parameters, and seeds.

See [AI editing design](AI_EDITING_DESIGN.md) for the contract.

## M9: Game-music authoring and export

### Goal

Create assets and metadata that can respond musically to game state.

### Deliverables

- loop regions with verified seamless boundaries;
- named intensity and mood variants;
- vertical layers and stem groups;
- legal transition points and horizontal section changes;
- transition audition and simulated state changes;
- tail-aware full mixes and stems;
- sample-rate, bit-depth, normalization, dither, and loudness choices;
- deterministic render manifests and hashes;
- engine-neutral JSON metadata first, followed by targeted game-engine adapters.

Game-aware behavior should reference normal tracks, sections, automation, and renders rather than inventing a second song model.

## M10: Release hardening

### Goal

Turn the prototype into a supportable application.

### Deliverables

- explicit JUCE licensing and notices;
- supported Windows and audio-device matrix;
- installer, settings migration, crash logs, and recoverable autosave;
- plug-in update, missing, duplicate, and quarantine-management UX;
- performance budgets and large-project stress tests;
- loaded-plug-in crash containment strategy or documented limitation;
- accessibility and keyboard navigation pass;
- project migration and backup policy;
- public binary release checklist and rollback plan.

## Cross-cutting user gates

Every musical milestone needs both implementation evidence and a separate listening decision. Preserve each candidate project or render until the user approves the exact artifact. Do not advance a sound, loop, mix, or track into a default catalog solely because its technical report passed.

## Cross-cutting risks

- VST3 code runs natively after load.
- Opaque state can be large and version-sensitive.
- JUCE licensing must be settled before binary distribution.
- Project-schema growth can strand older songs without deliberate migrations.
- Multi-track processing can violate real-time budgets if allocations and locks leak into the callback.
- AI service use introduces privacy, reproducibility, cost, and availability constraints.
- Game-transition complexity can outrun ordinary arrangement quality if introduced too early.

## Roadmap maintenance

When a milestone begins, turn its deliverables into a narrow implementation plan and name its acceptance artifact. When it passes, update this roadmap, the current handoff, the documentation index, and any affected schema or ADR in the same change.
