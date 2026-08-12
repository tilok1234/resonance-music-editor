# M6 external command load checkpoint

Date: 2026-08-12

Status: implementation evidence only. This slice emits no music and carries no listening approval.

## Why this slice exists

An authoring assessment on 2026-08-12 found that the binding constraint on making songs was not arrangement or AI translation but **note entry throughput**. Before this slice the only ways to put a note into a project were one mouse click in the piano roll, or the two bounded resolver producers (`Selected +1` and seeded velocity variation). There was no MIDI recording, no MIDI file import, no multi-note selection, and no clipboard, so a 69-note two-part loop required roughly 69 click-drags plus 69 individual velocity-slider moves.

The M5 command layer was already the correct substrate for bulk edits and was already accepted, but nothing could reach it from outside the editor. This slice opens that door without widening any trust boundary.

## What changed

Two controls were added to the proposal card in `src/editor_component.cpp`:

| Control | Behavior |
| --- | --- |
| **Copy hash** | Copies the active project's content SHA-256, track ID, and clip ID to the clipboard as three lines |
| **Load command** | Reads a `.json` edit command of at most 256 KB and installs it as candidate B through the existing preview path |

`Copy hash` exists because the three values a command author needs were previously unobtainable: the content hash was only surfaced after a preview already existed, which is a chicken-and-egg problem for authoring the command that would create that preview.

`Load command` performs no application of its own. It parses through the same `parseEditCommand` as the resolver lane and installs through the same `installEditPreview`, so a loaded command inherits every property M5 proved: hash precondition, target-ID validation, non-mutating candidate project, before/after diff overlays, A/B audition, one-transaction Apply, Reject, and stale-preview invalidation.

`loadEditCommandFile` and `installEditPreview` now return `juce::Result` and accept a `reportFailure` flag. This separates the decision from the modal dialog, which is what makes the refusal paths testable in a headless run.

The proposal card grew by one 26 px row; `proposalHeight` moved from `jlimit (210, 230, ...)` to `jlimit (240, 260, ...)`, which shortens the device selector by 30 px and changes the committed UI snapshot.

## Scope boundaries

This slice adds **no** new transform family, natural-language translation, model service, or network boundary. A command file must still carry fully resolved concrete note changes. The song-project schema is unchanged at version 3 and no migration was added.

## Evidence

New packaged mode `--command-load-test` writes `artifacts/command-load-test-report.json` against `schema/command-load-test.schema.json`. A separate mode was added rather than extending the M5 workflow report, because that report is accepted evidence with a recorded byte-identical SHA-256.

| Check | Result |
| --- | --- |
| Stale content hash refused | passed |
| Wrong track ID refused | passed |
| Wrong clip ID refused | passed |
| Malformed JSON refused | passed |
| Oversize file (> 256 KB) refused | passed |
| Missing file refused | passed |
| Valid command created a preview | passed; 1 note diff |
| Candidate carries the file's edit | passed |
| Active project unchanged and clean during preview | passed |
| Sound lane interlocked while B pending | passed |
| Apply as one transaction | passed |
| Replay after Apply refused | passed |
| Undo restored the pre-command hash in one step | passed |

Every refusal path asserts that the active project's content SHA-256 is byte-identical to its pre-load value and that the project is still undirtied.

Command-load candidate SHA-256: `b0300ac5b9b3e8b017f8c992208f2df2ea63b8373d4932162267e795a5af7532`

### One expectation was corrected during the slice

The first draft asserted that a command file becomes unusable once consumed. That is false, and the code was right: after Undo the project's content hash legitimately returns to its pre-edit value, so the same file is valid again — which is the deterministic, reproducible behavior the command contract intends. The real staleness guarantee is that replay must fail **while the applied edit still stands**, and the report now records `replayAfterApplyRefused` instead.

## Authoring a command

A loader is only useful with a way to produce commands, so `scripts/make-edit-command.py` builds one that replaces a target track's notes from any `.resonance.json` source. It reads the saved target project for the track ID, clip ID, existing note IDs, and loop length; the content hash comes from **Copy hash**.

```text
1. set the loop length in the editor and Save
2. press Copy hash and take the first clipboard line
3. python scripts/make-edit-command.py --project <saved> --source <song> \
       --source-track <name> --hash <sha256> --out <command.json>
4. Load command, audition A/B, then Apply or Reject
```

The script fails closed before writing when the hash is not 64 hex characters, when source notes do not fit the target loop, when a source note ID collides with one being removed in the same command, or when removes plus adds exceed the version-1 cap of 128 changes.

That loop-length check exists because of a real constraint this slice does not remove: **an edit command can change notes, but not loop length, tempo, meter, or track topology.** The target project must already have the loop the material needs.

## Full Release gate

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/topology/round-trip/command assertions | 209 passed |
| Schema-validated artifacts and fixtures | 20 passed (was 19) |
| Mixer contract / two-track runtime | passed |
| M5 stale-preview invalidation | passed |
| Command-load refusals | all passed |
| Packaged UI snapshot | 99,152 bytes (was 97,045; the card gained a row) |
| UI idle CPU | 1,296.9 ms, below the 3,000 ms ceiling |
| Documentation links | 33 files, 109 local links passed |

## What remains open

- The M6 two-track listening and interaction pass is still not run, and this slice does not substitute for it.
- User-facing missing-plug-in recovery is still absent.
- Manual note entry is still one click at a time; multi-note selection and a clipboard are the next slices in the authoring plan.
- Both visible tracks still instantiate the same accepted inventory record, so percussion remains impossible and a listening pass would still be judging that ceiling.
