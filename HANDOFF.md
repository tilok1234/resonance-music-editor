# Resonance Music Editor handoff

Updated: 2026-08-09

Repository: <https://github.com/tilok1234/resonance-music-editor>

## Takeover summary

Resonance is a clean-room restart of a Windows game-music editor. The current application is a working bounded two-track authoring prototype, not a complete DAW. It preloads two distinct instances of the explicitly scanned and inventory-approved Surge XT 1.3.4 VST3, plays through Windows Audio/WASAPI, edits one shared-length looping MIDI clip per track, opens the selected track's native Surge editor, and saves independent accepted sound and mixer state with the symbolic song.

The accepted M4 snapshot-first sound workflow is frozen at commit `7af6573` on `codex/m4-accepted-0.3.0` and draft PR #1. It captures live Surge state as ephemeral B, auditions project A and candidate B through the same instance, applies B as one dirty Undo transaction, restores live sound state on Undo/Redo, and saves only the accepted project snapshot. The held-note scheduler defect is fixed and user-confirmed. The user preferred B as less annoying and passed Apply, dirty marker, Undo, Redo, Save, Close, and Open. Final recapture exposed lifecycle-dependent opaque Surge encodings for the same restored sound; the repaired packaged native workflow reopens and plays the exact saved B, captures unchanged B with matching `91ED214E` identities, rejects it, stays clean, and closes without a false warning. The user explicitly accepted M4 at 2026-08-08 23:54 +02:00.

M5 is accepted on `codex/m5-edit-command-foundation`, based directly on the accepted M4 commit. The command foundation implements schema version 1, exact project-content SHA-256 preconditions, resolved note add/update/remove, an independent candidate `SongProject`, explicit before/after note diffs, and consume-once Apply/Reject. Source checkpoint `3d3a91b` connects that core to an editor-owned note-proposal card. Checkpoint `2351cb6` adds the first deterministic multi-note resolver. Checkpoint `ef9710d` completes the bounded host input slice: **Whole loop** or **Selected note**, maximum velocity delta `1` through `32`, and seed `0` through `2147483647` resolve into concrete velocity-only B; invalid input is blocked and inputs freeze while B is pending. The selected-note `+1` producer remains available. The packaged user pass covered both target scopes, two strengths, A/B, Reject, frozen controls, and one-note targeting; a fresh hidden workflow rerun then passed Apply, Undo, deterministic repeat, invalid-input, schema, and cleanup checks. The user explicitly accepted M5 at 2026-08-09 11:02 +02:00. The accepted Surge sound responded only subtly to velocity, so acceptance covers the command/proposal lifecycle without claiming that candidate B sounded musically better. No natural-language translator or model service has been added.

M6 is in progress on `codex/m6-two-track-authoring`, stacked on the pushed runtime checkpoint `425661c`. The first two slices established editor 0.4.0/schema version 2 migration and an eight-slot realtime engine with a two-instance real-Surge gate. The current third slice is editor 0.5.0 and schema version 3: it reads versions 1, 2, and 3; migrates older files without source rewrite; persists one or two ordered tracks; enforces project-wide track/clip/note identity and one shared loop; and exposes track selection, duplicate, remove, reorder, gain, pan, mute, solo, active-track meters, and selected-track piano-roll/Surge/A-B behavior. Normal startup preloads both Surge instances before the callback is prepared, so topology Undo remaps state without mutating prepared plug-in ownership. The full Release gate passes 124 engine/runtime assertions, 209 project/migration/command assertions, the packaged M4/M5 regressions, 19 schema validations, the UI snapshot, and idle checks. The M6 authoring test emits no music. Listening approval and user-facing missing-plug-in recovery remain open.

The accepted M4 publication baseline is `7af6573` (`Accept M4 host-owned sound workflow`) on `codex/m4-accepted-0.3.0`, available as draft PR #1. The accepted M5 branch is stacked as draft PR #2; its final acceptance commit is `9d94780`. The first M6 foundation is commit `280c876`, and the pushed runtime checkpoint is `425661c` on `codex/m6-two-track-runtime`. The current authoring work is stacked on it. The M5 acceptance is recorded in `docs/M5_ACCEPTANCE_2026-08-09.md`; M6 evidence is in the three dated M6 checkpoint documents. Always verify live `HEAD`, upstream state, and the working tree before relying on those values or assuming a stacked milestone has landed on `main`.

Project version: `0.5.0` (bounded two-track authoring is technically proven; M4 and M5 remain accepted, while M6 still needs its explicit listening pass and missing-plug-in recovery)

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
- One or two editable piano-roll clips with selected-track add, move, resize, delete, velocity, snap, and shared loop length.
- Piano-roll vertical zoom from 12 to 72 rows, dim non-interactive ghost notes for the inactive track, and automatic pitch-range fitting on Open and New.
- Multiple note selection by shift/Ctrl-click, marquee drag, and Ctrl+A, with selection-wide delete, move, velocity, and transpose proposals as single Undo transactions.
- Session note clipboard with Ctrl+C copy, Ctrl+V paste at a drawn insert marker, Ctrl+D duplicate one snapped span later, and arrow-key nudge and semitone/octave transpose.
- Named sound shelf stored beside the settings file, holding up to 32 identity-checked snapshots that load as candidate B through the accepted A/B lane, so sound work survives New, Open, and restart.
- Gesture-level Undo/Redo for project edits.
- Versioned `.resonance.json` project with 960 PPQ and stable track, clip, and note IDs.
- Canonical song-project schema version 3 with lossless, non-rewriting version-1 and version-2 migration.
- Stable ordered track topology capped at two, with project-wide unique track/clip/note IDs and Undoable duplicate/remove/reorder.
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
- External version-1 edit-command files load into the accepted preview path through **Load command**, with **Copy hash** publishing the content SHA-256, track ID, and clip ID needed to author one.
- Save isolation: unapplied note or sound previews never silently replace accepted A.
- Mutually exclusive sound and note candidate lanes plus pending-preview discard warnings.
- Separate exact saved-state integrity from the live-equivalent hash observed after Surge restore.
- Silent packaged self-test, M4/M5 native workflow modes, M6 two-instance runtime and authoring modes, external command-load mode, UI snapshot mode, and idle CPU regression mode.
- Fixed-capacity eight-lane `MixerSnapshot` contract with native-tested gain, balance, mute, solo, and capacity semantics.
- Eight stable prepared runtime slots with preallocated per-slot scratch, indexed state, separate scheduling/MIDI channels, bounded meters, load/safety diagnostics, and clean missing-slot/shutdown behavior.
- Silent packaged proof of two distinct real Surge XT instances through the production render/mix path without rescanning or attaching an audio callback.
- Visible selected-track gain, pan, mute, solo, stereo meter, topology controls, piano roll, native Surge editor, and track-bound sound/note A-B lanes.
- Transactional two-track Open with per-slot rollback before active-model replacement, plus schema-v3 Save/Open evidence.

## Current verified local baseline

The latest machine-local reports available when this handoff was prepared showed:

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/ceiling/shelf/round-trip/command assertions | 253 passed |
| Schema-validated artifacts and fixtures | 23 passed |
| Packaged external command load | passed; 6 refusal paths, 1-diff preview, Apply, replay-after-Apply refused, one-step Undo |
| Song project schemas | canonical writer `4`; accepted previous inputs `3`, `2`, and `1` |
| Version-1 migration fixture | passed; source SHA-256 `4725dd74075981ceb6ecd605db270954deafb7743b98190d090eb42dd677c0f7`; `track-migrated` / `clip-migrated` preserved |
| Version-2 migration fixture | passed; source SHA-256 `4b15956b981e085602e3e000f94bd08992ff7ea9ba53669d67a0be917406f21b`; non-default mixer/MIDI and exact state preserved |
| Fixed runtime/mixer contract | 8 stable slots/lanes; double-buffered publication; two-slot scheduling, mix, meters, safety, state, failure, and shutdown cases passed |
| Packaged M6 real-Surge runtime | passed; 2 distinct instances, 2,855 parameters each, 100 render blocks + 8 state-settle blocks, both tracks processed, no rescan, no emitted audio |
| Packaged M6 callback load | average `0.007391499998048` (0.739% of period); maximum `0.387239992618561`; 0 invalid samples, clips, or processor exceptions |
| Packaged M6 authoring | passed; 2 preloaded distinct instances; Add/Reorder/Remove and Undo; independent notes/mixer/state; schema-v3 Save/Open; no emitted music or runtime fault |
| M6 alternate state | stored accepted M4 B `ccaf99d4...`; live normalised accepted identity `91ed214e...`; slot-two-only mutation and exact current-baseline round trip passed |
| Edit-command candidate SHA-256 | `d649ddf03e328457ac0e6bde6d69509e867aee0aa2f1a80e034ee9f125beea7c` |
| Seeded velocity command SHA-256 | `2ab6a4853e8c5fa1cd4036849266ae3a837f89e5facaa66dfb4b99ed6f482798` |
| Seeded velocity unit candidate SHA-256 | `2c82441589033703d536ca7f6df4e1ec6b44392815ba60d37023387bcaf9095e` |
| M5 proposal A SHA-256 | `d23bd972c4325f0df0423450acd9fab8d6f127bc979e451fc191828e21672ccb` |
| M5 proposal B SHA-256 | `f8df619af0466939dad3999a8d7867c4bed0b6e6b7c45c1ee331870415bf588e` |
| Packaged seeded velocity B SHA-256 | `e07ead8401ea7bcea542182bf97d5749c92e69d4d87ee2e8f4bb109da45d9d92` |
| Parameterized selected-note B SHA-256 | `4a392b4009a6d39ed2e074dfc1631563e78bc65fcfe83ab4c7230e2a8e3b2318` from seed `90210`, maximum delta `3` |
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
| Packaged UI snapshot | 104,046 bytes; SHA-256 `0f57c461b102795052f1f5481b7a7c0f00b683b963c643e3a0b6d7b20d8ede89`; includes the selected-track topology/mixer row and the command-load row |
| UI idle gate | passed at 1,796.9 ms with four preloaded Surge instances, below the 3,000 ms process-CPU threshold |
| Packaged editor SHA-256 | `fd8dfa0c5dae0db981acb9451d9dc98f1bbb88bef090f54ba88dc298889d6179` |

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

- One or two instrument tracks and one looping clip per track are visible and persisted; the public project ceiling is two even though the engine owns eight slots.
- Exactly one accepted current VST3 inventory record is selected; both visible tracks instantiate that same Surge record rather than different plug-in products.
- No factory-preset file browser or semantic parameter browser; the first host-owned workflow selects named captured snapshots only.
- Arbitrary native Surge edits remain preview state until explicit Capture B and Apply B; they do not mark the project dirty immediately. New, Open, and Close perform a one-time comparison and warn if live state matches neither A nor B.
- Only the accepted A snapshot is persisted. The ephemeral B candidate is intentionally not stored across application restarts.
- No user-facing missing-plug-in recovery, different instrument assignment, MIDI-routing control, buses, or effects chain. Schema version 3 persists routing even though this slice exposes only gain, pan, mute, solo, and meters.
- No arrangement timeline, sections, tempo changes, or automation lanes.
- The command/proposal core and first parameterized seeded velocity transform exist, but target scope is limited to whole loop or one selected note; additional transforms, natural-language translation, and a connected AI service do not.
- Active-track selection plus dynamics target, strength, seed, and pending B are session-only; they are intentionally not persisted in song-project schema version 3.
- No game-state transitions, stem management, offline final-song export, or engine adapter.
- Scanner isolation does not contain a failure from a VST3 already processing in the editor.
- The build script currently assumes the Visual Studio 18 Community installation and generator.
- JUCE licensing for public/commercial binary distribution remains unresolved.

## Recommended next gate

Continue [Roadmap M6: Multi-track and mixer](docs/ROADMAP.md#m6-multi-track-and-mixer) with the exact packaged two-track listening/interaction pass, then implement user-facing preservation and recovery when a required plug-in cannot load. Preserve the accepted M4 sound and M5 command/proposal contracts, and keep different plug-in products, more than two persisted tracks, arrangement, automation, effects expansion, factory `.fxp` indexing, and model-service integration outside this slice.

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

M4 is accepted as version 0.3.0 at commit 7af6573 on draft PR #1. M5 is accepted at commit 9d94780 on codex/m5-edit-command-foundation and draft PR #2; its sound was only subtly velocity-sensitive, so do not misstate candidate B as musically preferred. M6 is in progress on codex/m6-two-track-authoring as editor 0.5.0, stacked on pushed runtime checkpoint 425661c. Schema version 3 reads versions 1 through 3 without rewriting older sources and persists one or two ordered tracks with independent accepted state, notes, mixer, and MIDI data. The editor preloads two distinct Surge instances and exposes selection, duplicate/remove/reorder, gain/pan/mute/solo, active meters, and selected-track piano-roll/Surge/A-B behavior. The full gate passes 124 engine/runtime assertions, 209 project/migration/command assertions, all packaged M4/M5 regressions, and 19 schema validations without emitting music in the M6 authoring test. Next run the exact packaged two-track listening/interaction pass, then add user-facing missing-plug-in recovery before accepting M6. Do not add different plug-in products, more than two persisted tracks, arrangement, broad effects, a live AI service, or factory .fxp indexing in that slice.
```

## Handoff maintenance

At the end of the next milestone, update this file with the exact accepted artifact, test counts, current limitations, next slice, and published commit. Keep historical measurements in a new dated checkpoint rather than rewriting old evidence.
