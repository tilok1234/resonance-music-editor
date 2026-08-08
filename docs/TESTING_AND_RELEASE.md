# Testing and release

Status: current validation contract for the single-track prototype

## Gate philosophy

Resonance uses layered gates because a VST3 host can fail in independent ways: compilation, discovery, process isolation, identity, state persistence, timing, real-time lifecycle, UI behavior, schema integrity, sound quality, and licensing.

No single test proves the whole editor. In particular:

- technical tests do not approve a song, preset, or mix;
- a listening approval does not prove project integrity or real-time safety;
- scanner isolation does not contain faults from a plug-in that is already loaded;
- a local source build is not automatically a licensed distributable binary.

## Required Release sequence

From the repository root:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-surge.ps1 -Configuration Release
.\scripts\test-scanner-isolation.ps1 -Configuration Release
.\scripts\test-realtime.ps1 -Configuration Release
python .\scripts\validate-artifacts.py
python .\scripts\check-docs.py
```

Run the complete sequence after changes to audio processing, scheduling, project persistence, VST3 identity, scanning, application lifecycle, build scripts, or schemas. A documentation-only change requires at least the docs checker and a diff audit; run broader tests if the documentation work exposes or corrects an implementation inconsistency.

## Gate inventory

### Build gate

`scripts/build.ps1` configures and compiles all production and test targets, then proves that every expected binary exists. It copies only the four production programs into `bin`.

### VST3 compatibility probe

`scripts/test-surge.ps1` drives `ResonanceHostProbe.exe` against the explicit portable Surge bundle. The probe covers:

- discovery and instrument selection;
- instantiation;
- bus and MIDI behavior;
- state capture, reload, and post-editor state observation;
- pre-render, post-render, post-reset, and host-parameter state-drift observation;
- native editor creation;
- timestamped MIDI delivery;
- bounded offline stereo render;
- finite-sample and non-silent checks;
- a structured compatibility report.

It writes a diagnostic WAV. That WAV is a technical probe artifact, not a composition baseline or listening approval.

The optional `--state-project <song.resonance.json>` probe input restores an exact saved state without modifying the project. It records the saved hash, headless and native-editor-created post-restore hashes, repeated-restore idempotence, prepared render hashes, differing-byte evidence, and before/after host-parameter hashes. The probe fails if either lifecycle identity is not stable, render changes the prepared state, or host-visible parameters drift.

### Scanner-isolation gate

`scripts/test-scanner-isolation.ps1` verifies:

- forced termination of a 30-second child through a 250 ms deadline;
- exit code 21 and one timeout quarantine record;
- no leftover hang-fixture process;
- rejection of `tests/not-a-plugin.vst3` through code 22;
- preservation of bounded structured error detail;
- real Surge scan and accepted inventory creation;
- exact bundle identity agreement across paths;
- one current inventory record after relocation;
- removal of Surge from production quarantine after success.

### Scheduler and project-model gate

`scripts/test-realtime.ps1` first runs the two C++ test programs. The current baseline is:

| Suite | Current passed assertions |
| --- | ---: |
| Real-time loop scheduler | 83 |
| Song project, round trip, and edit commands | 122 |

The tests cover timing boundaries, the real 44.1 kHz / 441-sample exact-block case, loop wrap, note-off behavior, event balance, project defaults, edit constraints, stable IDs, note and sound Undo/Redo, named opaque sound integrity, backward-compatible `soundName` loading, JSON round trips, malformed data, state hashes, and relocation-compatible VST3 identity. The M5 cases additionally prove strict versioned command parsing and round trip, deterministic seed preservation, exact content-hash preconditions, non-mutating candidates, concrete note diffs, consume-once Apply/Reject, one-Undo Apply, stale preview and stale Apply rejection, unknown targets, invalid bounds, and duplicate-target rejection against the portable command fixture. The seeded velocity cases prove canonical target ordering, same-input command and candidate identity, changed-seed divergence, velocity-only deltas of 1 through the declared maximum, one-Undo restoration, and fail-closed seed, bound, duplicate, empty, and missing-target validation.

### M5 edit-command core gate

`scripts/test-realtime.ps1` passes `tests/fixtures/edit-command-note-patch-v1.json` to `SongProjectTests.exe`. The fixture's schema-valid all-zero content hash is replaced in memory with the exact active-project SHA-256, keeping the committed fixture portable while exercising a current command. The structured report records command version 1, the fixture name and candidate SHA-256, plus the seeded velocity command and candidate hashes. These tests prove the host command and resolver boundaries only; they do not prove a visible diff, transform quality, or musical approval.

### Packaged silent self-test

The same script launches `bin\ResonanceMusicEditor.exe --self-test`. It must:

- open the Windows Audio backend;
- load the accepted current Surge record without scanning;
- match the current plug-in identity and 2,855-parameter inventory count;
- preserve exact state and song-project payloads through save/open;
- preserve the host-owned sound name with the exact real Surge state;
- report `passed: true`;
- report `noRescanPerformed: true`;
- report `audioEmitted: false`;
- exit without leaving an editor process.

The sample rate, block size, device name, and latency are observations from the selected Windows device, not fixed cross-machine requirements.

### Packaged UI and idle gates

`--ui-snapshot` constructs the packaged UI, writes the versioned screenshot, and exits. `--ui-idle-test` keeps a hidden editor alive for a four-second observation window. The Release script fails if total process CPU exceeds 3,000 ms or the observation exits early.

These gates preserve the two defects caught during the first playable milestone: passing a 4,096-sample backing capacity to Surge when the device requested 441 samples, and polling VST3 `hasEditor()` at 30 Hz.

### Packaged M4 native workflow gate

For an explicitly selected saved project, the Release editor can exercise the production open, restore, real-time processing, unchanged Capture B, Reject B, and Close-guard paths without browser automation:

```powershell
.\bin\ResonanceMusicEditor.exe --m4-workflow-test --project <song.resonance.json> --report <report.json>
```

This hidden mode opens the configured Windows Audio device and processes the song for 4.5 seconds. It requires A's post-processing live-equivalent hash to remain stable through playback, unchanged B to match A with `STATE MATCHES A`, Reject to preserve a clean project, and Close to proceed without a false discard warning. Unlike `--self-test`, this mode can emit audible audio; use a safe output level. It is a technical identity and interaction gate, not a new listening judgment.

### Packaged M5 native proposal gate

The Release editor exercises the production note-proposal controls without browser automation:

```powershell
.\bin\ResonanceMusicEditor.exe --m5-workflow-test --report <report.json>
```

This hidden mode keeps transport stopped. It first creates a selected-note `+1` candidate, proves the active song remains clean and hash-identical, saves and reloads accepted A while B remains pending, checks the sound/note candidate interlock, switches both A/B controls, rejects without mutation, applies as one Undo transaction, verifies Undo/Redo hashes, invalidates a stale preview after an unrelated edit, and restores the original project. It then creates the default eight-note dynamics candidate with seed `18421` and maximum delta `8`, verifies velocity-only bounds, auditions and rejects without mutating A, repeats the preview, applies exact B, and restores A with one Undo. Finally it enters selected-note scope, maximum delta `3`, and seed `90210` through the production controls; verifies one targeted bounded diff and a changed candidate; repeats A/B, Reject, deterministic preview, Apply, and Undo; and proves invalid delta `33` disables Preview before clean Close. `schema/m5-workflow-test.schema.json` requires every recorded lifecycle condition to pass.

### Artifact-schema gate

`scripts/validate-artifacts.py` validates the current JSON reports, project fixtures, and edit-command fixture against the schemas under `schema/`. The current full sequence validates 13 artifacts and fixtures.

Machine-specific reports are ignored by Git. Schemas and portable `.resonance.json` fixtures are versioned.

### Documentation gate

`scripts/check-docs.py` scans root and `docs/` Markdown files and fails on missing repository-local link or image targets. It intentionally skips external URLs and same-document anchors.

## Manual interaction gates

Before calling an interaction milestone complete, verify the exact packaged Release executable:

- startup and shutdown;
- device selector behavior;
- Play/Pause, Stop/Rewind, and Panic;
- piano-roll add, select, drag, resize, velocity, delete, and vertical scroll;
- gesture-level Undo/Redo and keyboard shortcuts;
- save, overwrite warning, open, unsaved-change confirmation, and failed-open preservation;
- native Surge open, audition strip, keyboard, close, and reopen;
- sound B capture, A/B audition, Apply, Reject, dirty state, and discard warning;
- sound Undo/Redo restoring the live instance, then Save, close, and Open preserving the applied sound;
- whole-loop/selected-note target, maximum-delta, and seed entry; invalid-input feedback; frozen pending controls; proposal overlays/counts; seed and velocity detail; A/B audition; Save-A isolation; Apply; Reject; one-step Undo/Redo; and discard warning;
- no stuck note or leftover process;
- responsive idle behavior.

Record UI evidence when a change is visual. Structural tests alone do not approve layout or interaction quality.

## Listening gates

Listening review is an explicit user gate. Use a named exact render or saved project and record what was heard. Evaluate at least:

- unwanted distortion or clipping;
- attacks and transitions;
- loop seam;
- preset and timbre suitability;
- balance and masking;
- dynamics and fatigue;
- musical coherence and emotional fit;
- behavior across headphones or speakers when relevant.

Do not silently replace an approved track or treat an automated render as catalog integration approval. New music should remain isolated until the user approves the exact artifact.

## Artifact handling

### Versioned

- source and build scripts;
- JSON schemas;
- current documentation and ADRs;
- bounded dated checkpoint evidence;
- portable project fixtures;
- selected UI screenshots;
- `artifacts/release-binaries.sha256`.

### Local only

- `bin` executables;
- `.local` dependencies and build tree;
- installed VST3 bundles and content;
- inventory and quarantine files containing local paths;
- test and self-test reports;
- diagnostic WAV files;
- IDE state and logs.

Before a source push, inspect the exact staged file list and scan for absolute user paths, tokens, credentials, private assets, plug-ins, binaries, and generated reports.

## Source publication checklist

1. Confirm intended scope and inspect `git status` before editing.
2. Run the tests appropriate to the change and record failures honestly.
3. Run `git diff --check`.
4. Run the documentation checker.
5. Inspect staged names, sizes, and diff.
6. Confirm ignored local dependencies and binaries are not staged.
7. Search staged content for machine-specific paths and credential-like strings.
8. Confirm current docs, handoff, roadmap, schemas, and fixtures agree.
9. Commit one intentional scope with a descriptive message.
10. Push without force and verify local HEAD equals the remote branch SHA.

## Binary release checklist

A public binary release requires everything in the source checklist plus:

1. an explicit JUCE licensing decision and any required notices;
2. review of third-party licenses and redistribution boundaries;
3. a clean Release build from the intended commit;
4. the full automated and manual gate on the packaged binaries;
5. regenerated SHA-256 values matching `artifacts/release-binaries.sha256`;
6. a supported-platform statement and known-risk list;
7. confirmation that no VST3 binary, factory content, or sample library is bundled without permission;
8. installation and uninstall behavior if an installer is introduced;
9. release notes and project-format compatibility statement.

The repository currently proves a local prototype and source publication. It does not yet declare a public binary distribution license.

## Failure policy

Do not update a golden artifact or lower a threshold merely to make a gate green. Diagnose the failure, decide whether behavior or the contract is wrong, and obtain explicit approval before accepting a changed musical baseline or weakening a safety boundary.
