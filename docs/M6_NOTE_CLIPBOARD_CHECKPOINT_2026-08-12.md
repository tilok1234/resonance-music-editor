# M6 note clipboard checkpoint

Date: 2026-08-12

Status: implementation evidence only. This slice emits no music and carries no listening approval.

## Why this slice exists

Multiple selection landed in the previous slice, but a selection you cannot copy is only half the win. Writing an eight-bar loop still meant placing every note by hand, because there was no way to write two bars and repeat them. This slice turns selection into repetition.

## What changed

| Gesture | Behavior |
| --- | --- |
| `Ctrl+C` | Copies the selection into a session clipboard, normalised so the earliest note sits at beat 0 |
| `Ctrl+V` | Pastes at the insert marker, preserving relative rhythm and pitch |
| `Ctrl+D` | Duplicates the selection one selection-span later and selects the copies, so repeated presses chain |
| `Left` / `Right` | Nudges the selection by the current snap value |
| `Up` / `Down` | Transposes the selection a semitone, or an octave with `Shift` |

Every one of these is a single Undo transaction. Pasted and duplicated notes receive fresh UUID ids, and the new copies become the selection.

### The insert marker is drawn, not hidden

Paste needs a target. The playhead was rejected because Stop rewinds it to zero, which would make `Ctrl+V` almost always paste at bar 1. Instead, any press inside the grid sets a snapped insert beat, and that position is drawn as a dashed amber marker whenever the clipboard holds something. Hidden paste state that the user has to infer would have been worse than either option.

### Duplicate rounds up to the snap grid

`Ctrl+D` offsets by the selection's span rounded **up** to the current snap value, with a one-snap minimum. Notes are usually written slightly detached — a bar of material commonly spans 3.9 beats rather than 4.0 — so duplicating by the raw span would place each repeat progressively early and smear the groove. Rounding up to the grid lands bar two exactly on bar two.

### Refusals are whole

`insertCopies` validates every placement before touching the project. A paste or duplicate that would push any note past the loop end, or past the 512-note clip limit, is refused entirely and reported, rather than landing partly inside the loop. Keyboard transpose is refused the same way if any selected note would leave the MIDI range, so intervals never flatten against the edge. Nudge instead clamps one shared delta against every selected note, matching how a multi-note drag already behaves — a nudge that cannot move the whole selection simply moves it as far as it can.

## Evidence

The `--selection-test` mode grew from 11 checks to 19.

| Check | Result |
| --- | --- |
| Copy fills the clipboard | passed |
| Paste adds new notes with fresh ids | passed |
| Paste undone in one step | passed |
| Duplicate lands one snapped span later | passed |
| Duplicate undone in one step | passed |
| Octave transpose applied across the selection | passed |
| Nudge applied across the selection | passed |
| Keyboard edits undone in one step | passed |

Plus the eleven selection checks from the previous slice, unchanged.

**Not covered by automated tests:** the key presses themselves. The test drives `copySelection`, `pasteAtInsertBeat`, `duplicateSelection`, `nudgeSelection`, and `transposeSelection` directly; it does not synthesize `Ctrl+C` or arrow keys through `keyPressed`, and it does not exercise the mouse-driven insert marker. The operations are tested; the bindings that reach them are not.

## Full Release gate

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/topology/round-trip/command assertions | 212 passed |
| Schema-validated artifacts and fixtures | 21 passed |
| Packaged M5 workflow | passed with both accepted hashes reproduced |
| Packaged external command load | passed |
| Packaged selection and clipboard | passed |
| UI idle CPU | below the 3,000 ms ceiling |

## What remains open

- The clipboard is session-only and internal. It does not survive restart and cannot move material between the two tracks' clips or to another application.
- Copy and paste operate within one clip; cross-track paste would need a target-clip concept the editor does not have.
- Both visible tracks still instantiate the same accepted inventory record, so percussion remains impossible. This is now the largest single limit on making a real game cue.
- The M6 two-track listening and interaction pass is still not run.
