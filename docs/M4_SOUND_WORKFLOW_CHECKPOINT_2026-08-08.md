# M4 host-owned sound workflow accepted - 2026-08-08

## Result

M4 was explicitly accepted by the user at 2026-08-08 23:54 +02:00 and is versioned as editor 0.3.0. The automated technical candidate passed, the held-note fix was user-confirmed, and manual Capture B, A/B listening, Apply, dirty marker, Undo, Redo, Save, Close, and Open passed. The user preferred B because it was less annoying. Final recapture exposed lifecycle-dependent opaque Surge encodings; the repaired packaged native workflow passes against that exact saved B project.

Resonance now gives native Surge edits an explicit host-owned boundary. A is the named opaque state accepted by the project. B is an ephemeral candidate captured from the same live Surge instance. A and B can be restored for audition, B can be applied as one dirty Undo transaction or rejected without project mutation, and global Undo/Redo restores the corresponding live state.

Save serializes only A. It does not silently capture an unapplied B or arbitrary native Surge edit.

## Architecture decision

[ADR-0004](ADR-0004-host-owned-sound-snapshots.md) selected named opaque snapshots for the first M4 slice. The accepted Surge inventory reports zero host programs, while the portable data contains 3,008 vendor-specific `.fxp` files. Direct factory-file indexing and loading therefore remain behind a later Surge-specific adapter rather than becoming an undocumented VST3 assumption.

## Reproduction

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-realtime.ps1 -Configuration Release
python .\scripts\validate-artifacts.py
python .\scripts\check-docs.py
```

The full VST3 probe and scanner-isolation gates should also pass before publication.

## Recorded automated results

| Check | Result |
| --- | --- |
| Scheduler assertions | 83 passed, including 44.1 kHz / 441-sample exact-boundary coverage |
| Song model and round-trip assertions | 63 passed |
| Named snapshot state integrity | Passed |
| Apply dirty state | Passed |
| Snapshot Undo/Redo | Exact A/B bytes and names restored |
| Older schema-v1 file without `soundName` | Loaded as `Project sound` |
| Real Surge state bytes | 67,345 |
| Real Surge state SHA-256 | `a771b28878606e1b830c9c5f02a46686328cc690e03153d2bd141cf0eee8ea40` |
| Real host-owned sound name | `Self-test Surge state`; exact round trip |
| Real self-test project bytes | 91,379 |
| Silent self-test | Passed; no scan and no emitted music |
| Exact saved-B packaged workflow | Passed; A/B `91ED214E`, clean Reject and Close |
| Packaged UI snapshot | 75,562 bytes |
| UI idle regression | 1,203.1 ms CPU / 5,538 ms wall |
| Packaged editor SHA-256 | `e3280f804819dd1945e7969c0f7c80ba468f28d6d96f769a6dcfcdc00dbfaa91` |

The listening-found scheduler defect and its exact regression are recorded separately in [the first-play MIDI boundary fix checkpoint](FIRST_PLAY_MIDI_BOUNDARY_FIX_2026-08-08.md). The user subsequently passed the corrected-build listening retest; that scheduler-only result did not by itself approve candidate B or complete M4. The later A/B preference and explicit acceptance recorded here completed those separate gates.

## Recorded manual sequence

| Step | Result |
| --- | --- |
| Capture and A/B audition | Passed |
| Listening preference | B preferred because it was less annoying; A also sounded good |
| Apply and dirty marker | Passed; `Untitled *` shown |
| Undo A / Redo B | Both worked while listening |
| Save / Close / Open | Passed for `m4-candidate-b.resonance.json` |
| Saved sound | `m4 candidate b`; exact project hash `ccaf99d4dc86d0b272e6ff1cc3be8afd07349bbcfe5055d992a001bea74da308` |
| Final raw recapture | Exposed equivalent live hash `91ed214e64b35e95cf20ca773ccf57f650bbeecb547d1aa5f0ba8a2f2f5c36a3` |

See [the Surge state-equivalence diagnosis](M4_SURGE_STATE_EQUIVALENCE_FIX_2026-08-08.md). The mismatch was not evidence that the sound or project failed to restore. The corrected packaged build now shows matching post-processing live-equivalent A/B identities.

## Visual evidence

![Host-owned A/B sound workflow](../artifacts/realtime-ui-snapshot.png)

The track card shows the accepted A name and short state hash, candidate naming, Capture B, Audition A/B, Apply B, and Reject B without obscuring the piano roll or device controls. This is layout evidence, not proof that every button sequence has passed manual interaction QA.

## Persistence contract

`soundName` is a backward-compatible optional property in the schema-version-1 instrument object. New saves always write it. Older version-1 projects remain valid and receive the fallback name `Project sound`.

The accepted sound's Base64 state and SHA-256 remain the authoritative payload. B is intentionally session-only preview state. New, Open, and Close compare live state once and include an unapplied B or an uncaptured live state in their discard warning.

## Accepted milestone

The corrected packaged Release executable completed the bounded native workflow against the local Downloads copy of `m4-candidate-b.resonance.json`:

1. exact saved payload remained `ccaf99d4dc86d0b272e6ff1cc3be8afd07349bbcfe5055d992a001bea74da308`;
2. restored A normalized to live-equivalent `91ed214e64b35e95cf20ca773ccf57f650bbeecb547d1aa5f0ba8a2f2f5c36a3`;
3. the real WASAPI transport advanced through playback with zero invalid samples and zero processor exceptions;
4. unchanged Capture B produced the same `91ED214E` label and `STATE MATCHES A`;
5. Capture did not dirty the project or create an uncaptured-live-state warning;
6. Reject restored A, left the project clean, and Close proceeded without a false warning.

This completes the technical, interaction, and listening evidence. The automated rerun did not make a new subjective sound judgment; it linked the reopened project to the exact saved B that already had the user's recorded preference. The user then explicitly accepted M4. This acceptance authorizes the 0.3.0 milestone record, but it does not authorize a commit, push, binary distribution, or any expansion of M5 scope.

## Scope boundary

This candidate does not index or parse Surge `.fxp` files, expose semantic parameter diffs, retain a cross-project sound library, add automation, add tracks, connect AI, or approve any sound. Scanner isolation and all real-time invariants remain unchanged.
