# M6 multiple note selection checkpoint

Date: 2026-08-12

Status: implementation evidence only. This slice emits no music and carries no listening approval.

## Why this slice exists

`PianoRoll` stored its selection as a single `juce::String`, so multiple selection was not merely unimplemented — it was unrepresentable. Every edit therefore applied to exactly one note: one Delete, one velocity slider move, one drag. The 2026-08-12 authoring assessment identified this as the second half of the note-entry bottleneck, after external command loading.

## What changed

| Change | Detail |
| --- | --- |
| Selection model | An ordered `std::vector<juce::String>`; the back entry is the **primary** selection |
| Extend selection | Shift-click or Ctrl-click a note toggles it |
| Marquee | Dragging across empty space rubber-band selects; holding Shift or Ctrl adds to the existing selection |
| Select all | `Ctrl+A` |
| Delete | Removes the whole selection as one Undo transaction |
| Move | Dragging moves the whole selection by one shared delta |
| Velocity | The slider writes one absolute velocity across the selection as one Undo transaction |
| Transpose proposal | **Selected +1** now transposes every selected note |
| Dynamics scope | **Selected note** became **Selected notes** and targets the whole selection |

### Click-to-add is preserved

An empty-space press now begins a marquee rather than immediately adding a note. It becomes an added note only if released without ever exceeding a small drag threshold. The documented "click empty space to add a note" behavior is therefore unchanged, while dragging gains a second meaning.

### Move clamps the delta, not each note

A multi-note move resolves the dragged note's requested position into a beat and pitch delta, clamps that delta against every selected note's bounds, and then applies the same delta to all of them. Clamping per note instead would let notes at the loop edge collapse onto it and silently destroy the rhythm.

### Resize stays a single-note gesture

The resize grip is only offered when exactly one note is selected, and the grip is drawn only on the primary selection. Multi-note resize has no obvious correct semantics (absolute length, scaled length, or shared end), so it is deliberately left out rather than guessed at.

### Stale ids

Undo, Redo, and command Apply can delete notes that are still selected. `pruneSelection` drops ids whose notes no longer exist and is called from `projectChanged`. Dead ids were never a correctness hazard — every consumer resolves through `findNote` — but they would make a later command fail closed for a confusing reason.

## Accepted M5 behavior is unchanged

Extending the two M5 producers to operate on a selection is a strict superset: with one note selected they build byte-identical commands. The full gate confirms this, reproducing both recorded acceptance hashes exactly:

| Recorded value | This run |
| --- | --- |
| M5 proposal B SHA-256 `f8df619af0466939dad3999a8d7867c4bed0b6e6b7c45c1ee331870415bf588e` | matched |
| Parameterized selected-note B SHA-256 `4a392b4009a6d39ed2e074dfc1631563e78bc65fcfe83ab4c7230e2a8e3b2318` | matched |

Transposing a selection is all or nothing: a selection containing MIDI 127, or larger than the version-1 cap of 128 changes, is refused rather than partly applied.

## Evidence

New packaged mode `--selection-test` writes `artifacts/selection-test-report.json` against `schema/selection-test.schema.json`. `PianoRoll::setSelectedNotes` was added as the public entry point that makes the selection model drivable without synthesizing mouse events.

| Check | Result |
| --- | --- |
| Three notes select, last becomes primary | passed |
| Unknown and duplicate ids rejected | passed |
| Velocity applied across the selection | passed |
| Velocity undone in one step | passed |
| Transpose preview created | passed; 3 diffs |
| Active project unchanged during preview | passed |
| Transpose applied across the selection | passed |
| Transpose undone in one step | passed |
| Selection pruned after a note is removed | passed |
| Selection clears | passed |

**Not covered by automated tests:** the mouse gestures themselves — marquee sweep, shift-click toggling, the drag threshold that separates click-to-add from rubber band, and multi-note drag clamping. These are driven by `juce::MouseEvent` handling that the headless mode does not synthesize. The model beneath them is tested; the gestures are not.

## Full Release gate

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/topology/round-trip/command assertions | 212 passed |
| Schema-validated artifacts and fixtures | 21 passed (was 20) |
| Packaged M5 workflow | passed with both accepted hashes reproduced |
| Packaged external command load | passed |
| Packaged multiple selection | passed |
| UI idle CPU | below the 3,000 ms ceiling |

## What remains open

- No clipboard yet: copy, paste, and duplicate are the next slice, and they are what turn selection into real authoring speed.
- No arrow-key nudge or transpose; transposition is only available as a proposal through **Selected +1**.
- Multi-note resize is deliberately absent.
- Both visible tracks still instantiate the same accepted inventory record, so percussion remains impossible.
- The M6 two-track listening and interaction pass is still not run.
