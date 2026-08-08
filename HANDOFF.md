# Resonance Music Editor handoff

Updated: 2026-08-09

Repository: <https://github.com/tilok1234/resonance-music-editor>

## Takeover summary

Resonance is a clean-room restart of a Windows game-music editor. The current application is a working single-track technical foundation, not a complete DAW. It loads one explicitly scanned and inventory-approved Surge XT 1.3.4 VST3, plays through Windows Audio/WASAPI, edits one looping MIDI clip in a piano roll, opens the native Surge editor with audition controls, and saves an explicitly accepted named instrument state with the symbolic song.

The accepted M4 snapshot-first sound workflow is frozen at commit `7af6573` on `codex/m4-accepted-0.3.0` and draft PR #1. It captures live Surge state as ephemeral B, auditions project A and candidate B through the same instance, applies B as one dirty Undo transaction, restores live sound state on Undo/Redo, and saves only the accepted project snapshot. The held-note scheduler defect is fixed and user-confirmed. The user preferred B as less annoying and passed Apply, dirty marker, Undo, Redo, Save, Close, and Open. Final recapture exposed lifecycle-dependent opaque Surge encodings for the same restored sound; the repaired packaged native workflow reopens and plays the exact saved B, captures unchanged B with matching `91ED214E` identities, rejects it, stays clean, and closes without a false warning. The user explicitly accepted M4 at 2026-08-08 23:54 +02:00.

M5 is now in progress on `codex/m5-edit-command-foundation`, based directly on that accepted M4 commit. The command foundation implements schema version 1, exact project-content SHA-256 preconditions, resolved note add/update/remove, an independent candidate `SongProject`, explicit before/after note diffs, and consume-once Apply/Reject. Source checkpoint `3d3a91b` connects that core to an editor-owned note-proposal card: the piano roll shows before/after overlays, A/B publishes active or candidate through the normal immutable sequence path, Save preserves accepted A, Apply is one Undo transaction, Reject is non-mutating, and any unrelated project edit invalidates stale B. Implementation checkpoint `2351cb6` adds the first deterministic multi-note resolver and a **Loop dynamics** producer: target IDs are canonicalized, seed `18421` resolves all eight starter notes into concrete velocity-only updates bounded by `8`, and the existing larger-diff lifecycle passes unchanged. The selected-note `+1` producer remains available. No natural-language translator or model service has been added.

The accepted M4 publication baseline is `7af6573` (`Accept M4 host-owned sound workflow`) on `codex/m4-accepted-0.3.0`, available as draft PR #1. The M5 branch is a stacked draft PR #2; its command foundation is `290fdfb`, proposal implementation is `3d3a91b`, deterministic UI-evidence follow-up is `c6f62f2`, final one-note evidence is `4a660ef`, and seeded loop-dynamics implementation is `2351cb6`. Always verify live `HEAD`, upstream state, and the working tree before relying on those values or assuming M4 has landed on `main`.

Project version: `0.3.0` (accepted M4 host-owned sound workflow)

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
- Versioned `.resonance.json` project with 960 PPQ and stable note IDs.
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
- Fixed **Loop dynamics** proposal for all eight starter notes using seed `18421` and maximum delta `8`.
- Orange before-note and blue after-note piano-roll overlays for add, update, and remove diffs.
- Note A/B audition through normal immutable sequence snapshots without mutating the project.
- Explicit note Apply/Reject, automatic stale invalidation, and one-step Undo/Redo.
- Save isolation: unapplied note or sound previews never silently replace accepted A.
- Mutually exclusive sound and note candidate lanes plus pending-preview discard warnings.
- Separate exact saved-state integrity from the live-equivalent hash observed after Surge restore.
- Silent packaged self-test, M4 and M5 native workflow modes, UI snapshot mode, and idle CPU regression mode.

## Current verified local baseline

The latest machine-local reports available when this handoff was prepared showed:

| Gate | Result |
| --- | --- |
| Scheduler assertions | 83 passed |
| Project/round-trip/command assertions | 122 passed |
| Schema-validated artifacts and fixtures | 13 passed |
| Edit-command candidate SHA-256 | `27a69dbc6331f951a7d06a16bbf02970b77f9cc0af52a1365b095654867babea` |
| Seeded velocity command SHA-256 | `57bb6c6fbd803b92f552b0ff07a6dec961721e32fed28b3666242f4493bf6970` |
| Seeded velocity unit candidate SHA-256 | `18c6b17dacad9e2fd44c7705892d58115e43c1ec2e8ef6893d114bb3274e6f85` |
| M5 proposal A SHA-256 | `7833b2e817743f6079612c685f2a0659e154d769d530077d5da1f34544a117ff` |
| M5 proposal B SHA-256 | `90fdd1363b5e1855c985983fd47917c39c58b4f6e977f1fc507447e2cf5d2f88` |
| Packaged seeded velocity B SHA-256 | `3a41dece2c8ba4e6ee149c8122f088ac57f306d64a567d50631599593c706a9c` |
| Packaged M5 workflow | passed; one-note lifecycle plus deterministic eight-note preview, repeat, A/B, Reject, Apply, and one-Undo restore |
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
| Packaged UI snapshot | 90,049 bytes; SHA-256 `159bd5d3764b178849033e8ac3f24338ac979b3a1c32a4463b1aa733ec342cec`; visually inspected and reproduced byte-identically twice |
| UI idle gate | 1,218.8 ms process CPU over 5,528 ms |
| Packaged editor SHA-256 | `070732f8766ea7fb35be088fdc08ecae567c4c5bf8fc0df6a61c17fd3dfa67fc` |

Device name, sample rate, block size, latency, full path-derived identifier, and bundle fingerprint are machine observations. Regenerate rather than copying them to another machine.

## Durable architecture decisions

1. VST3 is a foundation, not a later adapter: [ADR-0001](docs/ADR-0001-vst3-host-foundation.md).
2. Unknown discovery runs outside the editor: [ADR-0002](docs/ADR-0002-crash-isolated-plugin-scanning.md).
3. The first real-time path is one explicit WASAPI instrument: [ADR-0003](docs/ADR-0003-realtime-audio-engine.md).
4. The first sound workflow uses named host-owned opaque snapshots, not direct `.fxp` indexing: [ADR-0004](docs/ADR-0004-host-owned-sound-snapshots.md).
5. The mutable `SongProject` never crosses into the audio callback; it publishes fixed-capacity immutable snapshots.
6. Saved project state is versioned symbolic data plus opaque VST3 state, never bundled plug-in code.
7. Project open is fail-closed and transactional at the active-model boundary.
8. Technical acceptance and user listening approval are separate gates.

Read [Architecture](docs/ARCHITECTURE.md) and [VST3 hosting](docs/VST3_HOSTING.md) before implementation.

## Real-time invariants

Do not introduce file I/O, UI work, scanning, plug-in editor creation, unbounded allocation, or waits on message-thread locks in the audio callback. Process exactly the device-requested sample count. Keep automated self-test silent. Keep transport stopped and master gain at `-12 dB` on startup. Do not poll VST3 `hasEditor()` or similar capability methods from paint or timer paths.

The two regressions most worth remembering are:

- Surge fast-failed when the host passed the 4,096-sample backing capacity instead of the device's 441 requested samples.
- The UI appeared frozen when a 30 Hz timer repeatedly called JUCE VST3 `hasEditor()`, which constructed and released a native view.

Both paths are now covered by packaged acceptance modes.

## Known limitations

- Exactly one instrument track and one looping clip.
- Exactly one accepted current VST3 instrument is selected from inventory.
- No factory-preset file browser or semantic parameter browser; the first host-owned workflow selects named captured snapshots only.
- Arbitrary native Surge edits remain preview state until explicit Capture B and Apply B; they do not mark the project dirty immediately. New, Open, and Close perform a one-time comparison and warn if live state matches neither A nor B.
- Only the accepted A snapshot is persisted. The ephemeral B candidate is intentionally not stored across application restarts.
- No multiple tracks, mixer, pan, mute, solo, buses, or effects chain.
- No arrangement timeline, sections, tempo changes, or automation lanes.
- The note-command core, one-note producer, and first fixed seeded whole-loop dynamics transform exist, but target/strength/seed controls, broader transforms, a natural-language translator, and a connected AI service do not.
- No game-state transitions, stem management, offline final-song export, or engine adapter.
- Scanner isolation does not contain a failure from a VST3 already processing in the editor.
- The build script currently assumes the Visual Studio 18 Community installation and generator.
- JUCE licensing for public/commercial binary distribution remains unresolved.

## Recommended next gate

Continue [Roadmap M5: Unified edit-command layer](docs/ROADMAP.md#m5-unified-edit-command-layer) by turning the fixed whole-loop demonstration into an explicit bounded transform request with target scope, strength, and seed controls. Identical project content, targets, parameters, and seed must still resolve to the same concrete command and candidate; changed parameters must remain visible before Apply. Do not combine this gate with `.fxp` indexing, multi-track, arrangement, or live model-service integration.

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
5. inspect ADR-0004 and the accepted M4 diff before changing the sound-workflow contract.

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

M4 is accepted as version 0.3.0 and published at commit 7af6573 on draft PR #1. M5 is in progress on codex/m5-edit-command-foundation and draft PR #2. Its command foundation and visual editor-owned proposal workflow provide immutable A/B audition, Save-A isolation, explicit Apply/Reject, one Undo/Redo, and stale invalidation. Checkpoint 2351cb6 adds a deterministic eight-note Loop dynamics proposal with seed 18421 and maximum velocity delta 8; 122 native project assertions and 13 schema validations pass. Next expose explicit bounded target, strength, and seed controls over that resolver. Do not add a live AI service, multi-track, arrangement, or factory .fxp indexing in that slice.
```

## Handoff maintenance

At the end of the next milestone, update this file with the exact accepted artifact, test counts, current limitations, next slice, and published commit. Keep historical measurements in a new dated checkpoint rather than rewriting old evidence.
