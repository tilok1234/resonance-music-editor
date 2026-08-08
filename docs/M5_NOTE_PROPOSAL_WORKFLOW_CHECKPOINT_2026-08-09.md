# M5 note-proposal workflow checkpoint

Date: 2026-08-09

Status: technical editor workflow passed; seeded transforms and musical approval remain open

Source checkpoint: `3d3a91b` (`Add M5 note proposal workflow`)

## Scope

This slice connects the M5 command foundation to the packaged native editor without changing editor version `0.3.0` or song-project schema version 1. It deliberately uses one narrow manual producer, **Preview selected +1**, to prove the proposal lifecycle before adding a broader transform engine or any model service.

It does not add multi-track behavior, arrangement, factory-preset indexing, natural-language translation, network access, or musical acceptance.

## Editor-owned proposal lifecycle

The editor now owns at most one pending `EditCommandPreview`:

```text
selected active note A
  -> resolved +1-semitone EditCommand
  -> independent candidate SongProject B
  -> visible before/after diff
  -> audition A or B through SequenceSnapshot
  -> Apply once as one Undo transaction, or Reject unchanged
```

- The accepted project stays hash-identical and clean during preview and both audition choices.
- The proposal card shows add/update/remove counts, the exact first note change, and short A/B content hashes; full hashes are available in its tooltip.
- The piano roll keeps active-project hit testing while drawing accepted before-notes in orange and candidate after-notes in blue. The same overlay path supports add, update, and remove diffs.
- Audition A publishes `project.createSequenceSnapshot()`. Audition B publishes the candidate project's snapshot through the same lock-free engine boundary.
- Apply suppresses intermediate UI publications while the command core performs its grouped mutation, then publishes the accepted result once.
- Reject clears B and republishes A without dirtying the song.
- Any unrelated project mutation makes the content-hash precondition stale, clears the candidate, and republishes active A.
- Save serializes only accepted A while B remains pending. New, Open, and Close include the pending note proposal in their discard guard.
- The note and sound candidate controls are interlocked, preventing simultaneous A/B decision lanes.

## Accepted-loop compatibility found by the native test

The first packaged run exposed that the accepted starter loop uses `0.82`-beat note lengths, which predate exact 960-PPQ command timing. Quantizing those notes merely to transpose pitch would have changed the approved articulation.

The command validator now allows an update to preserve an existing note's timing exactly. Any start or length that the command actually changes must still resolve to an integer tick at 960 PPQ. Native regression coverage proves both sides of that rule, and the selected-note proposal changes pitch without altering `0.82`.

## Packaged native workflow test

`--m5-workflow-test` runs without browser automation and keeps transport stopped. Its schema-validated report proves:

- preview creation with one update diff;
- candidate pitch `48 -> 49` for stable `note-1`;
- project A remains clean and content-hash identical during preview and B audition;
- Save/reload contains accepted A while candidate B remains pending;
- sound controls are interlocked during the note decision;
- A and B toggle selection routes correctly;
- Reject consumes B without mutation;
- Apply produces the exact candidate hash and one named Undo transaction;
- Undo restores A and Redo restores B;
- an unrelated tempo edit invalidates a pending preview;
- cleanup restores the original project and Close proceeds without a warning;
- invalid-sample and processor-exception counts remain zero.

The deterministic report identities were:

```text
A  7833b2e817743f6079612c685f2a0659e154d769d530077d5da1f34544a117ff
B  90fdd1363b5e1855c985983fd47917c39c58b4f6e977f1fc507447e2cf5d2f88
```

## Verification

The Release sequence passed with:

- 83 deterministic scheduler assertions;
- 107 project, persistence, sound-snapshot, and edit-command assertions;
- the packaged M5 workflow report validated by `schema/m5-workflow-test.schema.json`;
- 13 schema-validated artifacts and fixtures in total;
- silent packaged Surge self-test with no rescan and no emitted music;
- Windows Audio at 44.1 kHz / 441 samples on the observed machine;
- 88,375-byte packaged UI snapshot, visually inspected with the proposal card and before/after overlay visible;
- 1,234.4 ms process CPU over the 6,049 ms idle observation;
- Surge XT 1.3.4 compatibility probe with 2,855 parameters and a bounded non-silent render;
- scanner-isolation timeout code 21, invalid-bundle code 22, one accepted inventory record, and zero production quarantine entries;
- packaged editor SHA-256 `1815e8d6fc9838d039f25c1021b00250f42620a2c06285edc86f9ff98bafad52`.

![M5 note proposal with before and after overlay](../artifacts/realtime-ui-snapshot.png)

## Remaining M5 gate

This is not complete M5. Next, add one seeded bounded multi-note transform over the same command and proposal lifecycle. Identical project content, selection, parameters, and seed must produce identical resolved changes. Its candidate must remain isolated until Apply, and the existing card must represent the larger diff without weakening Save, stale invalidation, Undo, or real-time boundaries.

Natural-language translation and any hosted model boundary remain deferred until that deterministic transform is dependable. A passing technical workflow is also not a listening judgment that the transposed candidate is musically better.
