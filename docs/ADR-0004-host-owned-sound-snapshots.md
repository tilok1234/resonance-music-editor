# ADR-0004: Use host-owned opaque snapshots for the first sound workflow

Status: accepted for M4
Date: 2026-08-08

## Context

The editable-song foundation already captures and restores the complete Surge XT VST3 state, but native Surge edits are not yet explicit Resonance operations. A user can change a patch or parameters in the native window and Save captures the result, but the change has no host-owned name, preview boundary, dirty transition, or Undo/Redo transaction.

The accepted Surge XT 1.3.4 inventory reports 2,855 parameters and `programCount: 0`. The matching portable distribution contains 3,008 `.fxp` patch files under Surge-owned factory and third-party data folders. Those files are real sound content, but they are not exposed through the VST3 program interface and their interpretation is a Surge-specific contract. Indexing filenames without a supported load path would create a browser that cannot safely apply what it shows.

## Decision

The first host-owned sound workflow uses named opaque VST3 state snapshots captured from the one already-loaded, inventory-approved Surge instance.

Resonance presents two roles:

- **A / Project sound** is the state currently accepted by `SongProject` and persisted in `.resonance.json`.
- **B / Candidate** is an ephemeral named snapshot captured explicitly from the live Surge instance.

The user may audition A and B through the existing instance and audio path. Capturing B does not mutate the project. Applying B restores it, writes its name, bytes, and SHA-256 to the project as one Undo transaction, marks the song dirty, and clears the candidate. Rejecting B restores A and leaves the project unchanged. Global Undo/Redo restores the corresponding accepted plug-in state as well as the symbolic model.

Save serializes the accepted project sound, not whichever unaccepted preview or native edit happens to be live. New, Open, and Close perform one explicit live-state comparison and warn before discarding an unapplied candidate or uncaptured native change. Technical state capture and restoration remain separate from listening approval.

`soundName` is added as a backward-compatible optional field inside the schema-version-1 instrument object. New saves always write it; older version-1 projects load it as `Project sound`. This does not reinterpret any existing field, and the opaque state remains the authoritative sound payload.

## Rejected alternatives

### Treat VST3 programs as the browser

Rejected for M4 because the accepted Surge instance reports zero programs. A host UI based on `getNumPrograms()` would be empty.

### Index and load Surge `.fxp` files directly

Deferred. The files are vendor-specific content outside the repository, include factory and third-party material, and do not have a current host-side loading contract. A future Surge adapter may index explicit roots after it defines parsing/loading, duplicate identity, licensing, missing-file, relocation, and upgrade behavior.

### Implement both file indexing and snapshots now

Rejected for the first slice because it combines two trust and persistence boundaries. Snapshot A/B is already supported by the proven state API and gives native Surge editing a safe acceptance workflow without startup scanning.

## M4 implementation contract

1. Keep exactly one inventory-approved Surge instance and one instrument track.
2. Capture candidate state only on an explicit user action; never poll opaque state from a timer or paint path.
3. Show the accepted and candidate names, bounded live-equivalent hash evidence, and which snapshot is being auditioned. Keep exact saved-state integrity hashes distinct from lifecycle-dependent bytes returned after restore.
4. Audition by restoring state through the existing plug-in-access boundary; do not create a second synth or scan path.
5. Apply one candidate as one project Undo transaction containing its name, Base64 state, and SHA-256.
6. Make global Undo/Redo restore the live plug-in whenever the accepted state hash changes.
7. Save only the accepted project snapshot and preserve it exactly through close and reopen.
8. On New, Open, or Close, compare live state once against the live-equivalent A/B hashes observed after restore and warn before removing an unapplied candidate or uncaptured native change.
9. Keep transport, note editing, silent self-test, packaged UI, and idle behavior green.
10. Require a separate user listening judgment for meaningfully different A/B sounds.

## Acceptance gate

Technical acceptance requires:

- model tests for snapshot naming, integrity, dirty state, Undo/Redo, backward-compatible loading, and JSON round trip;
- an exact real-Surge saved-payload round trip plus stable live-equivalent restore observation in the packaged and host-probe tests;
- a packaged UI snapshot showing the bounded A/B workflow;
- the established scheduler, note-editing, schema, documentation, and idle-CPU gates;
- manual interaction proof that Capture B, Audition A, Audition B, Apply, Undo, Redo, Save, and Open affect the same instance as specified.

Musical acceptance requires the user to listen to the exact A and B sounds and decide whether either is suitable. Passing the technical gate does not select a preset or approve a timbre.

## Consequences

The editor gains an explicit non-destructive sound-design boundary without depending on undocumented preset parsing. Opaque snapshots consume Undo memory, so the existing 16 MiB history budget bounds retained sound transactions. Snapshot byte differences remain state evidence rather than proof of audible difference. Surge may reserialize one restored sound differently across unprepared, prepared, and editor-created lifecycle states, so the project hash remains exact payload integrity while discard checks use the live-equivalent hash captured atomically after restore.

Factory preset browsing, semantic parameter diffs, parameter automation, reusable cross-project sound libraries, and arbitrary plug-in support remain later work.
