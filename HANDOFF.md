# Resonance Music Editor handoff

Updated: 2026-08-09

Repository: <https://github.com/tilok1234/resonance-music-editor>

## Takeover summary

Resonance is a clean-room restart of a Windows game-music editor. The current application is a working one-track authoring prototype over a bounded multi-instance engine, not a complete DAW. Its visible project loads one explicitly scanned and inventory-approved Surge XT 1.3.4 VST3, plays through Windows Audio/WASAPI, edits one looping MIDI clip in a piano roll, opens the native Surge editor with audition controls, and saves an explicitly accepted named instrument state with the symbolic song. A silent packaged gate separately proves two distinct Surge instances through the production render/mix path.

The accepted M4 snapshot-first sound workflow is frozen at commit `7af6573` on `codex/m4-accepted-0.3.0` and draft PR #1. It captures live Surge state as ephemeral B, auditions project A and candidate B through the same instance, applies B as one dirty Undo transaction, restores live sound state on Undo/Redo, and saves only the accepted project snapshot. The held-note scheduler defect is fixed and user-confirmed. The user preferred B as less annoying and passed Apply, dirty marker, Undo, Redo, Save, Close, and Open. Final recapture exposed lifecycle-dependent opaque Surge encodings for the same restored sound; the repaired packaged native workflow reopens and plays the exact saved B, captures unchanged B with matching `91ED214E` identities, rejects it, stays clean, and closes without a false warning. The user explicitly accepted M4 at 2026-08-08 23:54 +02:00.

M5 is accepted on `codex/m5-edit-command-foundation`, based directly on the accepted M4 commit. The command foundation implements schema version 1, exact project-content SHA-256 preconditions, resolved note add/update/remove, an independent candidate `SongProject`, explicit before/after note diffs, and consume-once Apply/Reject. Source checkpoint `3d3a91b` connects that core to an editor-owned note-proposal card. Checkpoint `2351cb6` adds the first deterministic multi-note resolver. Checkpoint `ef9710d` completes the bounded host input slice: **Whole loop** or **Selected note**, maximum velocity delta `1` through `32`, and seed `0` through `2147483647` resolve into concrete velocity-only B; invalid input is blocked and inputs freeze while B is pending. The selected-note `+1` producer remains available. The packaged user pass covered both target scopes, two strengths, A/B, Reject, frozen controls, and one-note targeting; a fresh hidden workflow rerun then passed Apply, Undo, deterministic repeat, invalid-input, schema, and cleanup checks. The user explicitly accepted M5 at 2026-08-09 11:02 +02:00. The accepted Surge sound responded only subtly to velocity, so acceptance covers the command/proposal lifecycle without claiming that candidate B sounded musically better. No natural-language translator or model service has been added.

M6 is in progress on `codex/m6-two-track-runtime`, based on the schema/identity foundation commit `280c876`. The first slice bumps the editor to 0.4.0 and the canonical song writer to schema version 2. Version-1 files migrate in memory without source rewrite; track/clip IDs become authoritative model data; each persisted track gains bounded undoable gain, pan, mute, solo, and MIDI-routing state; and edit commands target the active stored IDs. The second slice replaces the one-instance engine shape with eight stable message-thread-owned slots, preallocated per-slot scratch, double-buffered immutable `MixerSnapshot` publication, indexed plug-in state, per-track scheduling/mix/meters, and bounded safety/load diagnostics. The hidden Release gate loads two distinct accepted Surge XT instances, renders both in memory, mutates only slot two with the accepted M4 B, restores both current baseline states, preserves slot one when slot two is missing, and shuts down cleanly. The full gate passes 124 engine/runtime assertions, 162 project/migration/command assertions, the packaged M4/M5 regressions, and 16 schema validations. The visible schema/UI remain one track, the M6 test emits no audio, and no new listening acceptance is implied.

The accepted M4 publication baseline is `7af6573` (`Accept M4 host-owned sound workflow`) on `codex/m4-accepted-0.3.0`, available as draft PR #1. The accepted M5 branch is stacked as draft PR #2; its final acceptance commit is `9d94780`. The first M6 foundation is commit `280c876`; the current runtime work is stacked on it. The M5 acceptance is recorded in `docs/M5_ACCEPTANCE_2026-08-09.md`; M6 evidence is in `docs/M6_MULTITRACK_FOUNDATION_CHECKPOINT_2026-08-09.md` and `docs/M6_TWO_TRACK_RUNTIME_CHECKPOINT_2026-08-09.md`. Always verify live `HEAD`, upstream state, and the working tree before relying on those values or assuming a stacked milestone has landed on `main`.

Project version: `0.4.0` (M6 two-instance runtime is technically proven; M4 and M5 remain the accepted interaction/listening baselines, while visible multi-track authoring keeps M6 in progress)

## Product direction

The goal is a music-only editor aimed primarily at video games. It should eventually support fluid and smooth, fast and aggressive, slow and calm, and intermediate musical styles. Manual and AI-assisted editing must share one project model. General sound-effect creation is outside scope.

Read [the product vision](docs/PRODUCT_VISION.md) before changing product scope.

## Implemented capabilities

- JUCE 9 / C++20 / CMake Windows-native application.
- VST3-first hosting with Surge XT as the first accepted instrument.
- Disposable one-bundle scanner child.
- Parent-owned deadline, quarantine, and accepted inventory.
- Exact VST3 bundle fingerprint revalidation before interactive load.
- Explicit Windows Audio/WASAPI device selector.
- Sample-accurate looping MIDI scheduler.
- Mouse and hardware MIDI input.
- Play/Pause, Stop/Rewind, Panic, master gain, meters, and diagnostics.
- Native Surge window with Resonance audition transport and keyboard.
- One editable piano-roll clip with add, move, resize, delete, velocity, snap, and loop length.
- Gesture-level Undo/Redo for project edits.
- Versioned `.resonance.json` project with 960 PPQ and stable track, clip, and note IDs.
- Canonical song-project schema version 2 with lossless, non-rewriting version-1 migration.
- Persisted per-track gain, pan, mute, solo, and MIDI-routing state with strict bounds and neutral migration defaults.
- Base64 VST3 state plus SHA-256 integrity.
- Candidate-then-replace project opening and exact state restore.
- Host-owned named A/project and ephemeral B/candidate sound snapshots.
- Explicit Capture B, Audition A/B, Apply B, and Reject B controls.
- One-transaction sound Apply with live-state Undo/Redo restoration.
- Strict edit-command version-1 parser and serializer for resolved note patches.
- Full-project content SHA-256 preconditions with stale-preview rejection.
- Non-mutating command candidates with explicit before/after note diffs.
- Consume-once command Apply/Reject; Apply is one named Undo transaction.
- Portable schema-validated command fixture with deterministic seed provenance.
- Deterministic whole-loop velocity resolver with canonical target ordering and strict seed, delta, duplicate, and missing-target rejection.
- Editor-owned M5 note-proposal card with exact change counts, first-note detail, and before/after hashes.
- Explicit whole-loop/selected-note target, maximum-delta, and seed controls above **Preview dynamics**.
- Fail-closed input ranges, selected-note requirement, and frozen controls while B is pending.
- Orange before-note and blue after-note piano-roll overlays for add, update, and remove diffs.
- Note A/B audition through normal immutable sequence snapshots without mutating the project.
- Explicit note Apply/Reject, automatic stale invalidation, and one-step Undo/Redo.
- Save isolation: unapplied note or sound previews never silently replace accepted A.
- Mutually exclusive sound and note candidate lanes plus pending-preview discard warnings.
- Separate exact saved-state integrity from the live-equivalent hash observed after Surge restore.
- Silent packaged self-test, M4/M5 native workflow modes, M6 two-instance runtime mode, UI snapshot mode, and idle CPU regression mode.
- Fixed-capacity eight-lane `MixerSnapshot` contract with native-tested gain, balance, mute, solo, and capacity semantics.
- Eight stable prepared runtime slots with preallocated per-slot scratch, indexed state, separate scheduling/MIDI channels, bounded meters, load/safety diagnostics, and clean missing-slot/shutdown behavior.
- Silent packaged proof of two distinct real Surge XT instances through the production render/mix path without rescanning or attaching an audio callback.

## Current verified local baseline

The latest machine-local reports available when this handoff was prepared showed:

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/round-trip/command assertions | 162 passed |
| Schema-validated artifacts and fixtures | 16 passed |
| Song project schemas | canonical writer `2`; accepted legacy input `1` |
| Version-1 migration fixture | passed; source SHA-256 `4725dd74075981ceb6ecd605db270954deafb7743b98190d090eb42dd677c0f7`; `track-migrated` / `clip-migrated` preserved |
| Fixed runtime/mixer contract | 8 stable slots/lanes; double-buffered publication; two-slot scheduling, mix, meters, safety, state, failure, and shutdown cases passed |
| Packaged M6 real-Surge runtime | passed; 2 distinct instances, 2,855 parameters each, 100 render blocks + 8 state-settle blocks, both tracks processed, no rescan, no emitted audio |
| Packaged M6 callback load | average `0.0079110` (0.791% of period); maximum `0.3994600`; 0 invalid samples, clips, or processor exceptions |
| M6 alternate state | stored accepted M4 B `ccaf99d4...`; live normalised accepted identity `91ed214e...`; slot-two-only mutation and exact current-baseline round trip passed |
| Edit-command candidate SHA-256 | `effc3fa6f6a8801cf5d984364a38a6182893b146c22108de56f2c9bc606cb305` |
| Seeded velocity command SHA-256 | `eaed800fde0ed0a377b2dd85880fd9c8f938a485f16b0582484f308a4f87483e` |
| Seeded velocity unit candidate SHA-256 | `42ab4cca2fe478e300c49508879fe34f17270f8f036db079e6a91d0db8a29589` |
| M5 proposal A SHA-256 | `00de28ee0860a8c6d00a2898f4d46f2231a7b984cde1f17091d4b8c636fd93a3` |
| M5 proposal B SHA-256 | `4894a2a10474312a679f33af13f1cc0fa7484d620a7055c83eb3d764e8609aca` |
| Packaged seeded velocity B SHA-256 | `a8cde7d15c2ecbfe2012f1c9f0bac40275a2f4f27f1da93410c3045835c33293` |
| Parameterized selected-note B SHA-256 | `2b3f3fa963d1ee5599b0c9c93fc92d7d238afb45aee858cdc4025292d8f850fb` from seed `90210`, maximum delta `3` |
| Packaged M5 workflow | passed; prior lifecycles plus parameter control consumption, deterministic repeat, invalid-input block, A/B, Reject, Apply, and one-Undo restore |
| Fresh M5 acceptance rerun | passed; byte-identical report SHA-256 `f40ad41ce8964129d416e6f77593ab0e0b2f8c9669fe9b393c6bc9fce473385f`; 13 schema validations; zero leftover editor processes |
| M5 user listening | accepted 2026-08-09 11:02 +02:00; A was slightly preferred but hard to distinguish at maximum delta `8`; delta `24` and selected-note delta `32` sounded about the same |
| Surge XT | 1.3.4 |
| Live Surge parameters | 2,855; matched inventory |
| Current accepted inventory records | 1 |
| Current production quarantine entries | 0 |
| Current VST3 UID suffix | `190e4fbd` |
| Audio backend | Windows Audio/WASAPI |
| Silent self-test | passed; no scan and no emitted music |
| Host-owned real-Surge sound name | `Self-test Surge state`; exact round trip |
| Latest captured real Surge state | 67,345 bytes; SHA-256 `a771b28878606e1b830c9c5f02a46686328cc690e03153d2bd141cf0eee8ea40` |
| Exact saved-B packaged workflow | passed; A/B `91ED214E`, clean Reject and Close |
| Packaged UI snapshot | 92,810 bytes; SHA-256 `cfe19c3b7051f28dcc20acc162782397ebfca8be353c6ac2c104f9aabbc35c57` |
| UI idle gate | passed below the 3,000 ms process-CPU threshold |
| Packaged editor SHA-256 | `c447f6931cab22cee7fbca6344a6a92ebdfdf77153f1c54d092019b5ba2972de` |

Device name, sample rate, block size, latency, full path-derived identifier, and bundle fingerprint are machine observations. Regenerate rather than copying them to another machine.

## Durable architecture decisions

1. VST3 is a foundation, not a later adapter: [ADR-0001](docs/ADR-0001-vst3-host-foundation.md).
2. Unknown discovery runs outside the editor: [ADR-0002](docs/ADR-0002-crash-isolated-plugin-scanning.md).
3. The first real-time path is one explicit WASAPI instrument: [ADR-0003](docs/ADR-0003-realtime-audio-engine.md).
4. The first sound workflow uses named host-owned opaque snapshots, not direct `.fxp` indexing: [ADR-0004](docs/ADR-0004-host-owned-sound-snapshots.md).
5. Project migration precedes multi-instance rendering; the first mixer and runtime are fixed at eight message-thread-owned lanes/slots: [ADR-0005](docs/ADR-0005-multitrack-project-and-mixer-ownership.md).
6. The mutable `SongProject` never crosses into the audio callback; it publishes fixed-capacity immutable snapshots.
7. Saved project state is versioned symbolic data plus opaque VST3 state, never bundled plug-in code.
8. Project open is fail-closed and transactional at the active-model boundary.
9. Technical acceptance and user listening approval are separate gates.

Read [Architecture](docs/ARCHITECTURE.md) and [VST3 hosting](docs/VST3_HOSTING.md) before implementation.

## Real-time invariants

Do not introduce file I/O, UI work, scanning, plug-in editor creation, unbounded allocation, or waits on message-thread locks in the audio callback. Process exactly the device-requested sample count. Keep automated self-test silent. Keep transport stopped and master gain at `-12 dB` on startup. Do not poll VST3 `hasEditor()` or similar capability methods from paint or timer paths.

The two regressions most worth remembering are:

- Surge fast-failed when the host passed the 4,096-sample backing capacity instead of the device's 441 requested samples.
- The UI appeared frozen when a 30 Hz timer repeatedly called JUCE VST3 `hasEditor()`, which constructed and released a native view.

Both paths are now covered by packaged acceptance modes.

## Known limitations

- Exactly one instrument track and one looping clip are visible and persisted; the engine has eight slots and two real instances are proven only in the hidden runtime gate.
- Exactly one accepted current VST3 inventory record is selected; the runtime can instantiate that record in more than one slot.
- No factory-preset file browser or semantic parameter browser; the first host-owned workflow selects named captured snapshots only.
- Arbitrary native Surge edits remain preview state until explicit Capture B and Apply B; they do not mark the project dirty immediately. New, Open, and Close perform a one-time comparison and warn if live state matches neither A nor B.
- Only the accepted A snapshot is persisted. The ephemeral B candidate is intentionally not stored across application restarts.
- No visible second track, track add/remove/reorder, user-facing missing-plug-in recovery, per-track mixer/meter controls, buses, or effects chain. Schema version 2 persists one track's gain, pan, mute, solo, and MIDI routing, and the engine consumes them in slot zero.
- No arrangement timeline, sections, tempo changes, or automation lanes.
- The command/proposal core and first parameterized seeded velocity transform exist, but target scope is limited to whole loop or one selected note; additional transforms, natural-language translation, and a connected AI service do not.
- Dynamics target, strength, and seed are session-only proposal inputs; pending B and its controls are intentionally not persisted in song-project schema version 2.
- No game-state transitions, stem management, offline final-song export, or engine adapter.
- Scanner isolation does not contain a failure from a VST3 already processing in the editor.
- The build script currently assumes the Visual Studio 18 Community installation and generator.
- JUCE licensing for public/commercial binary distribution remains unresolved.

## Recommended next gate

Continue [Roadmap M6: Multi-track and mixer](docs/ROADMAP.md#m6-multi-track-and-mixer) with a bounded authoring slice over the proven runtime. Define the next song-schema revision, migrate version 2 without rewriting its source, and preserve stable IDs plus accepted opaque state. Add a minimal second visible track, selection, gain/pan/mute/solo, meters, add/remove/reorder Undo, Save/Open, and user-facing missing-plug-in recovery. Finish with an explicit two-track listening pass before accepting M6. Preserve the accepted M4 sound and M5 command/proposal contracts, and keep arrangement, automation, effects expansion, factory `.fxp` indexing, and model-service integration outside this slice.

## First takeover actions

From the repository root:

```powershell
git status --short --branch
git log --oneline --decorate -5
python .\scripts\check-docs.py
```

Then:

1. read `README.md`, `docs/README.md`, this handoff, and the relevant ADRs;
2. inspect ignored local dependency, build, inventory, and quarantine state without adding it to Git;
3. run the full Release sequence before modifying a risky boundary, or record why a narrower gate is proportionate;
4. preserve unrelated local edits;
5. inspect ADR-0004 before changing the sound workflow and ADR-0005 before changing project topology or realtime mixer ownership.

The full commands and prerequisites are in [Development](docs/DEVELOPMENT.md) and [Testing and release](docs/TESTING_AND_RELEASE.md).

## Scope boundaries for takeover

- Do not add sound-effect creation to this repository.
- Do not copy an external music catalog, approved render, VST3 binary, preset library, or sample library into the repository without an explicit integration request and license review.
- Do not treat old diagnostic audio as a quality baseline.
- Do not treat tests as listening approval.
- Do not distribute editor binaries until licensing is resolved.
- Do not scan arbitrary installed plug-ins inside the interactive editor.
- Do not publish or rewrite Git history without user authorization.

## Ready-to-paste fresh-task prompt

```text
Take over the local clone of https://github.com/tilok1234/resonance-music-editor.

First read HANDOFF.md and docs/README.md completely, then inspect git status, the current branch/HEAD, recent commits, and the ignored local build/inventory state. Preserve any existing changes and do not publish anything without my approval.

The product is a music-only editor aimed mainly at video-game music. Manual and future AI edits must share one versioned project model, and technical tests must remain separate from listening approval.

M4 is accepted as version 0.3.0 at commit 7af6573 on draft PR #1. M5 is accepted at commit 9d94780 on codex/m5-edit-command-foundation and draft PR #2; its sound was only subtly velocity-sensitive, so do not misstate candidate B as musically preferred. M6 is in progress on codex/m6-two-track-runtime as editor 0.4.0, stacked on foundation commit 280c876. Schema version 2 migrates version 1 without rewriting the source, preserves stable track/clip IDs and exact state, and stores bounded mixer/MIDI settings. The production engine now owns eight stable prepared slots with double-buffered mixer publication and per-track scheduling/mix/meters. The silent packaged gate proves two distinct real Surge instances, slot-isolated state, missing-slot preservation, bounded CPU/safety counters, and clean shutdown. The full gate passes 124 engine/runtime assertions, 162 project/migration/command assertions, all packaged M4/M5 regressions, and 16 schema validations. The visible schema/UI still expose one track and M6 has no new listening approval. Next define a bounded multi-track schema revision and minimal second-track UI with topology Undo, Save/Open, recovery, mixer controls/meters, and an explicit listening pass. Do not add arrangement, broad effects, a live AI service, or factory .fxp indexing in that slice.
```

## Handoff maintenance

At the end of the next milestone, update this file with the exact accepted artifact, test counts, current limitations, next slice, and published commit. Keep historical measurements in a new dated checkpoint rather than rewriting old evidence.
