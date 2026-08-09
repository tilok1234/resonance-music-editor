# M5 edit-command foundation checkpoint

Date: 2026-08-09

Status: technical foundation passed; M5 interaction and listening gates remain open

## Scope

This slice began M5 on `codex/m5-edit-command-foundation`, based directly on the accepted M4 commit `7af6573`. It does not alter the accepted M4 sound workflow, connect an AI service, add multi-track behavior, index `.fxp` files, or claim musical approval.

The implemented boundary is:

```text
version-1 JSON command
  -> strict parse and project-hash validation
  -> independent candidate SongProject
  -> concrete before/after note diff
  -> Apply once as one Undo transaction, or Reject with no mutation
```

## Contract delivered

- `schema/edit-command.schema.json` defines the closed version-1 `editNotes` envelope.
- Commands target the current stable `track-1` and `loop-1` IDs.
- Every command carries the exact active-project content SHA-256.
- Resolved add, update, and remove entries use stable note IDs and integer ticks at 960 PPQ.
- The parser rejects unsupported versions, fields, operations, target shapes, numeric bounds, seeds, and duplicate note targets.
- Preview clones the active project, revalidates project-specific bounds and IDs, and changes only the clone.
- The preview exposes before/after content hashes and a concrete before/after record for each note change.
- Apply rechecks the active hash, reproduces the candidate exactly, and groups all changes in one named Undo transaction.
- Reject and successful Apply consume the preview; a stale Apply fails without consuming it so the caller can still reject.
- The optional 31-bit non-negative seed round-trips as provenance for later deterministic transforms.

The project hash excludes the editor build-version label and includes every material serialized project field, including the accepted opaque instrument state. Marking a project clean does not change the hash.

## Portable fixture

`tests/fixtures/edit-command-note-patch-v1.json` contains one update, one removal, and one addition. Its committed all-zero SHA-256 is schema-valid but intentionally stale. The native test replaces that placeholder in memory with the exact starter-project content hash, then parses, serializes, previews, applies, undoes, redoes, and rejects through the production command code.

The resolved candidate content SHA-256 is:

```text
27a69dbc6331f951a7d06a16bbf02970b77f9cc0af52a1365b095654867babea
```

## Verification

The Release gates passed with:

- 83 deterministic scheduler assertions;
- 103 project, persistence, sound-snapshot, and edit-command assertions;
- command version 1 and exact fixture name recorded in `artifacts/song-project-test-report.json`;
- silent packaged Surge self-test with no rescan and no emitted music;
- repeated silent self-test project SHA-256 `4239e011ba82122319901586231722c77a7379036dff438fb3bd31a4ecf43afc` before and after a second run, using stable note ID `note-self-test-1`;
- Windows Audio at 44.1 kHz / 441 samples on the observed machine;
- 75,579-byte packaged UI snapshot;
- 1,140.6 ms process CPU over the 5,492 ms idle observation;
- 12 schema-validated artifacts and fixtures;
- Surge XT 1.3.4 compatibility probe with 2,855 stable parameters and a bounded non-silent render;
- scanner-isolation timeout code 21, invalid-bundle code 22, one accepted inventory record, and zero production quarantine entries;
- packaged editor SHA-256 `c43d72941325d2568d1bac0ed4661a6c5b4c17bb9e5ef422b05e8e5c2dd71d91`.

The M5 command cases cover command JSON round trip, seed preservation, non-mutating preview, exact candidate content, one-shot Apply/Reject, one-Undo/Redo restoration, stale command, stale Apply, unknown clip, unknown note, invalid timing bounds, duplicate target, and unsupported-version rejection.

## Version decisions

- Editor version remains `0.3.0`; that version still denotes the user-accepted M4 milestone.
- Song-project schema remains version 1; the content hash and command machinery do not add persisted project fields.
- Edit-command schema begins independently at version 1.
- The fixture and checkpoint are portable; machine-specific inventory paths and reports remain ignored.

## Remaining M5 gate

This checkpoint is not the complete M5 milestone. Next:

1. give the editor ownership of a pending `EditCommandPreview`;
2. render added, updated, and removed notes as a visible before/after diff;
3. publish A or candidate B through the normal immutable sequence path for audition without mutating the project;
4. expose explicit Apply and Reject controls backed by the consume-once core;
5. add the first bounded deterministic transform and prove identical resolution from the same seed;
6. run a packaged interaction test and keep musical approval separate from technical acceptance.

Do not attach a live model service until this proposal and audition loop is dependable.
