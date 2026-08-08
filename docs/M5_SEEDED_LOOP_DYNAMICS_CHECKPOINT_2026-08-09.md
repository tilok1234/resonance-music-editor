# M5 seeded loop-dynamics checkpoint

Date: 2026-08-09

Status: deterministic transform and native proposal lifecycle passed; broader M5 controls and musical approval remain open

Source checkpoint: `2351cb6` (`Add deterministic M5 loop dynamics proposal`)

## Scope

This slice adds the first bounded multi-note transform above the existing version-1 edit-command and A/B proposal contracts. It does not change editor version `0.3.0`, song-project schema version 1, accepted M4 sound behavior, or the realtime callback. It does not connect a model service or claim that the resulting dynamics are musically preferable.

## Deterministic resolver

`SeededVelocityVariation` accepts explicit note IDs, a non-negative 31-bit seed, and a maximum velocity delta. The host resolver:

- accepts from 1 through 128 targets;
- accepts seeds from 0 through 2,147,483,647;
- accepts maximum deltas from 1 through 32;
- canonicalizes target IDs before advancing its fixed 32-bit integer mixer;
- rejects duplicate or missing note IDs and invalid bounds without changing the destination command;
- changes every targeted velocity by at least 1 and no more than the declared maximum, clamped to MIDI velocity 1 through 127;
- preserves note ID, pitch, start, and length;
- emits an ordinary fully resolved version-1 `editNotes` command carrying the exact active-project content hash and seed.

The seed is provenance for resolution, not an instruction replayed during Apply. Preview and Apply consume the concrete resolved note updates. Therefore the same project, target set, seed, and maximum delta produce the same canonical command and candidate regardless of input target order.

## Native editor workflow

The proposal card now exposes two manual producers:

- **Selected +1** keeps the focused one-note transposition proof;
- **Loop dynamics** targets all current loop notes with seed `18421` and maximum delta `8`.

Loop dynamics does not require a selected note. Its pending card shows eight updates for the starter loop, the first note's pitch and velocity before/after values, seed provenance, and short A/B content hashes. The existing orange/blue overlay, immutable A/B audition, Save-A isolation, sound-lane interlock, Apply, Reject, stale invalidation, discard guard, and one-Undo behavior are reused unchanged.

The deterministic UI snapshot prepares the eight-note candidate and selects **Audition B** so the larger proposal is visible without browser automation.

![Seeded eight-note loop-dynamics proposal](../artifacts/realtime-ui-snapshot.png)

## Automated evidence

The Release gates passed on the implementation checkpoint:

| Evidence | Result |
| --- | --- |
| Realtime scheduler | 83 assertions passed |
| Project, round-trip, sound, command, and resolver suite | 122 assertions passed |
| Seed / maximum delta | `18421` / `8` |
| Canonical resolved-command SHA-256 | `57bb6c6fbd803b92f552b0ff07a6dec961721e32fed28b3666242f4493bf6970` |
| Unit candidate SHA-256 | `18c6b17dacad9e2fd44c7705892d58115e43c1ec2e8ef6893d114bb3274e6f85` |
| Packaged starter-project A SHA-256 | `7833b2e817743f6079612c685f2a0659e154d769d530077d5da1f34544a117ff` |
| Packaged seeded candidate SHA-256 | `3a41dece2c8ba4e6ee149c8122f088ac57f306d64a567d50631599593c706a9c` |
| Packaged seeded diff | 8 bounded velocity-only updates |
| Seeded packaged lifecycle | repeat matched; A/B and Reject preserved A; Apply matched B; one Undo restored A |
| Schema validation | 13 acceptance artifacts and fixtures passed |
| UI snapshot | 90,049 bytes; SHA-256 `159bd5d3764b178849033e8ac3f24338ac979b3a1c32a4463b1aa733ec342cec`; reproduced byte-identically twice and visually inspected |
| UI idle gate | 1,218.8 ms process CPU over 5,528 ms including startup |
| Packaged editor | 9,603,584 bytes; SHA-256 `070732f8766ea7fb35be088fdc08ecae567c4c5bf8fc0df6a61c17fd3dfa67fc` |

The resolver tests also prove that reversing the supplied target order leaves the serialized command unchanged, a different seed changes the candidate, and empty targets, negative seeds, excessive bounds, duplicates, and unknown IDs fail closed. The packaged `--m5-workflow-test` independently exercises the production buttons and candidate lifecycle with transport stopped.

## Remaining M5 gate

This checkpoint proves one fixed transform, not a complete music-assistant surface. The next bounded slice should expose an explicit target scope, maximum delta, and seed while continuing to resolve locally into the same concrete command contract. It must retain deterministic repeatability, visible diff and A/B review, candidate isolation, and one-Undo Apply. Natural-language translation, additional transform families, multi-track behavior, arrangement, and factory `.fxp` indexing remain separate work.

Technical validity is not listening approval. Any claim that the dynamics sound better still requires the user to audition the exact candidate and explicitly accept it.
