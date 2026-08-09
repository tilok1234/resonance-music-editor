# M5 unified edit-command layer acceptance

Date: 2026-08-09

Status: accepted at 2026-08-09 11:02 +02:00

Accepted branch: `codex/m5-edit-command-foundation` (stacked draft PR #2)

Pre-acceptance checkpoint: `2ee6521` (`Document M5 dynamics controls checkpoint`)

## Accepted scope

M5 establishes one trusted edit-command and proposal lifecycle before any model service is connected:

- strict version-1 resolved note commands with exact project-content SHA-256 preconditions;
- isolated candidate B with concrete add, update, and remove diffs;
- immutable realtime A/B audition without mutating accepted A;
- explicit Apply and Reject with consume-once semantics;
- Apply as one Undo transaction and stale-candidate invalidation;
- deterministic seeded velocity resolution with concrete values authoritative after preview;
- explicit **Whole loop** or **Selected note**, maximum delta `1` through `32`, and seed `0` through `2147483647` inputs;
- invalid-input blocking, selected-note requirements, and frozen request controls while B is pending;
- Save-A isolation and interlock with the accepted M4 sound-candidate lane.

The selected-note `+1` pitch proof remains available. Additional transform families, natural-language translation, a connected model service, multiple tracks, arrangement, and factory `.fxp` indexing are not part of this acceptance.

## Packaged user review

The user reviewed the exact packaged native editor rather than a browser surrogate:

- **Whole loop**, maximum delta `8`, seed `18421`: A felt slightly better, but the difference was difficult to hear;
- **Whole loop**, maximum delta `24`, seed `18421`: A and B sounded about the same;
- **Selected note**, maximum delta `32`, seed `90210`: A and B sounded about the same;
- the selected-note card showed one targeted update and frozen target, maximum-delta, and seed controls;
- A/B and Reject behaved as expected.

The accepted Surge state appears only weakly velocity-sensitive. This listening result is a product limitation, not a failed command invariant. Acceptance does not claim that candidate B sounded better or that velocity variation will be equally audible with every instrument state.

The user explicitly said **accept M5** at 2026-08-09 11:02 +02:00 with that caveat understood.

## Automated acceptance rerun

After the listening pass, the visible editor was clean and held only an ephemeral B. That one process was closed, discarding only the reviewed preview, and the packaged `--m5-workflow-test` was rerun in its single-instance hidden mode.

The fresh report passed every field, including:

- active A remains hash-identical and clean during preview and audition;
- Save serializes accepted A while B remains pending;
- sound and note proposal lanes remain interlocked;
- Reject is non-mutating;
- Apply creates exactly one Undo step;
- Undo restores A and Redo restores the exact reviewed candidate;
- stale preview invalidation works;
- default whole-loop and parameterized selected-note requests are deterministic and bounded;
- identical inputs reproduce the same candidate;
- invalid maximum delta `33` blocks Preview;
- Close finishes without a warning, invalid samples, processor exceptions, or a leftover editor process.

The fresh report was byte-identical to `artifacts/m5-workflow-test-report.json`, with SHA-256 `f40ad41ce8964129d416e6f77593ab0e0b2f8c9669fe9b393c6bc9fce473385f`. All 13 acceptance artifacts passed their schemas. The broader Release evidence remains 83 scheduler assertions, 122 project/round-trip/sound/command/resolver assertions, and the scanner, Surge, UI snapshot, UI idle, and binary-manifest gates recorded in the preceding checkpoint.

## Version and next gate

Acceptance does not change editor version `0.3.0`, song-project schema version 1, or edit-command schema version 1. No `0.4.0` release contract has been defined.

M6 should start contract-first: define the project migration, stable track and clip identities, and preallocated mixer ownership before adding multiple plug-in instances or mixer UI. Version-1 project loading, accepted opaque sound retention, the realtime callback rules, and the M5 command/proposal lifecycle must remain protected by tests.
