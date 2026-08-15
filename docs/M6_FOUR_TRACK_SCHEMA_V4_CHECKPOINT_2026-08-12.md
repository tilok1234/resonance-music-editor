# M6 four-track schema version 4 checkpoint

Date: 2026-08-12

Status: implementation evidence only. This slice emits no music and carries no listening approval.

## Why this slice exists

The persisted project ceiling was two tracks while the realtime engine and mixer had been built and tested for eight. [ADR-0005](ADR-0005-multitrack-project-and-mixer-ownership.md) set that gap deliberately: the project would not publish more tracks than multi-instance rendering had proven. Multi-instance rendering was proven in the M6 runtime slice, so the gap was no longer serving a purpose.

Two tracks is enough for bass and lead. A game cue usually wants bass, drums, harmony, and lead, and the sound shelf slice made genuinely different timbres reachable. Four tracks is what turns that into an arrangement's worth of texture.

## Why four rather than eight

Preloading is deliberately eager: every runtime slot is created and prepared before the audio callback attaches, so prepared plug-in topology never changes at runtime. That is a real-time safety property, not an implementation detail, so raising the ceiling means instantiating that many Surge instances at startup rather than on demand.

The measured cost supports stopping at four:

| Preloaded Surge instances | UI idle process CPU |
| --- | --- |
| 2 | ~1,300 ms |
| 4 | 1,797 ms |

The regression ceiling is 3,000 ms. Extrapolating the ~250 ms per instance seen here, eight instances would land near 2,800 ms — inside the limit but with almost no margin. Four leaves real headroom, and the runtime already supports eight whenever a later slice wants to pay that cost with evidence rather than extrapolation.

## What changed

| Change | Detail |
| --- | --- |
| Schema version | 4; reads 1 through 4, writes 4 |
| Track ceiling | `maxProjectTracks` 2 → 4, `maxItems` 2 → 4 |
| Archived contract | Version 3 archived as `schema/song-project-v3.schema.json` |
| Loader | The multi-track path was generalised from exactly-two to one-through-four |
| Migration fixture | `tests/fixtures/song-project-v3-migration.resonance.json` |

Version 4 differs from version 3 only by the ceiling. No field was added, removed, or reinterpreted, so a version-3 document is already a valid version-4 document once its version is raised in memory. As with every prior migration, the source file is left byte-identical until the user explicitly saves.

The loader's multi-track branch previously hard-coded two tracks throughout. It now parses each track in isolation through the ordinary single-track path — so no per-track rule is relaxed by being in a multi-track document — then checks track, clip, and note ID uniqueness and a shared loop length across the whole set. A fifth track fails closed at the loader, not only in the UI.

## The consequence worth knowing about

**Every project's content hash changed, so every previously authored edit-command file is now stale.**

The command precondition hashes the complete canonical material and `schemaVersion` is part of that material. Raising the version therefore changes the content SHA-256 of every project. This is correct fail-closed behavior — the document really did change — but it has two visible effects:

1. An edit-command file authored against a version-3 project is refused against the same project opened as version 4. Re-author it with a fresh hash from **Copy hash**.
2. Recorded acceptance hashes become historical. The M5 proposal candidate moved from `f8df619af0466939dad3999a8d7867c4bed0b6e6b7c45c1ee331870415bf588e` to `833e7181c40500b249716a0c19e54333f1ae82c9e44bcf30f65b886d4d8f557b`. The M5 *behavior* is unchanged; only the document version inside the hashed material differs.

The previous five slices each reproduced the recorded M5 hashes byte-for-byte, and that property genuinely ends here. It ends for a legitimate reason, and it will recur at every future schema bump, so `PROJECT_FORMAT.md` now states it as a standing consequence rather than a one-off note.

## Evidence

Native assertions rose from 230 to 253.

| Check | Result |
| --- | --- |
| Version-3 two-track fixture loads and becomes the current schema in memory | passed |
| Non-default track/clip identity preserved | passed; `track-v3-alpha` / `clip-v3-beta` |
| Non-default mixer and MIDI preserved across both tracks | passed |
| Version-3 source file byte-identical after load | passed; `1148a4ba…` |
| Tracks addable up to the published ceiling | passed |
| A fifth track fails closed | passed, at the loader as well as the model |
| Track, clip, and note IDs unique across four tracks | passed |
| Four-track save, reopen, and exact content round trip | passed |
| A third track is accepted with unique identity | passed |

The fixture uses non-default IDs and non-default mixer/MIDI values on both tracks, so migration cannot pass by relying on defaults or on `track-1`.

## Full Release gate

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/ceiling/shelf/command assertions | 253 passed (was 230) |
| Schema-validated artifacts and fixtures | 23 passed (was 22) |
| Packaged preloaded instances | 4, distinct |
| Packaged M5, command-load, selection, and shelf workflows | all passed |
| UI idle CPU | 1,796.9 ms, below the 3,000 ms ceiling |

Three gate assertions and three report schemas carried hard-coded version-3 or two-track values and were updated: the project gate, the self-test document version, the authoring reopen version, and the preloaded instance count. Each was a stale expectation rather than a defect, but they are worth recording because they are exactly the places a future version bump will need to touch again.

## What remains open

- The mixer row still shows only the selected track. Four tracks are selectable, persisted, and independently mixed, but there is no simultaneous four-channel mixer view.
- Both the ceiling and the engine still instantiate the same accepted inventory record; the shelf varies the patch, not the plug-in product.
- Eight tracks remain reachable in the runtime but unproven at startup cost.
- The M6 listening and interaction pass is still not run. It is now the highest-value remaining step: four tracks with four shelved timbres is the first configuration where a listening judgment is about the music rather than the ceiling.
