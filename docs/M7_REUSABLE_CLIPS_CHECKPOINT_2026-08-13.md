# M7.3 reusable clips checkpoint

Date: 2026-08-13

Status: implementation evidence only. No listening approval.

## Why this slice exists

`emberline-long.resonance.json` is a 32-bar song made of 359 individually placed
notes. Its four-bar figures repeat, but nothing in the model knew that: every repeat
was a fresh copy of the notes. Changing a bass figure meant changing it eight times,
and neither a person nor an agent could say "play that again."

A track held exactly one clip, that clip was stretched over the whole song, and the
engine looped it. "Clip" was a serialisation artifact — the in-memory model had no clip
node at all, only a `clipId` property on the track and a flat list of notes.

## What changed

A clip is now **content**, and a **placement** is one occurrence of it.

| Concept | Before | After |
| --- | --- | --- |
| Clip length | always the song length | its own value, at most the song length |
| Note positions | song-absolute | clip-relative |
| Repeats | duplicated notes | placements of one clip |
| Song length | derived from the clip | its own `songLengthTicks` |

Song-project **schema version 6**, with version 5 archived as
`schema/song-project-v5.schema.json`.

### The expansion happens on the message thread

`SequenceSnapshot` is still a flat, song-absolute, fixed-capacity array, and the audio
callback is untouched. `createSequenceSnapshotForTrack` expands every placement into
that array on the message thread, exactly where the snapshot was already built.

Placements multiply notes, so the product is what the 1,024-note ceiling applies to.
`setPlacements` **refuses** a list whose expansion would exceed it rather than
truncating. A silent truncation here would drop music that no gate could observe — the
same failure mode as the 32-beat clamp found on 2026-08-13.

### Migration is behaviour-preserving by construction

Every version 1 through 5 project becomes a clip whose length is the song length with
exactly one placement at beat zero. Expanding one placement at zero over a clip that
spans the song reproduces the previous note list identically.

### Resizing a song still resizes a default clip

`setLoopLengthBeats` is what the editor's **LOOP** combo calls, and choosing 32 bars has
always meant "make the song 32 bars." A clip that spans its whole song and is placed
once at the start keeps that behaviour and grows with the song. A clip that has been
given its own length and placements keeps them, and is only clamped if the song shrank
beneath it — with any placement that no longer fits dropped rather than left addressing
beats past the end.

## Command operations

Edit-command **version 3**, with version 2 archived as
`schema/edit-command-v2.schema.json`. Versions 1 and 2 are still accepted; a version 1
or 2 command naming a clip operation is refused rather than applied.

| Operation | Effect |
| --- | --- |
| `setClipLength` | a track's clip length in ticks, within the published canvas |
| `setPlacements` | replaces the whole placement list from an array of start ticks |

`setPlacements` replaces rather than appends, which is what keeps it deterministic:
the same command against the same project always produces the same placements, so a
preview and its later apply agree. Start ticks are sorted for the author; genuinely
overlapping placements are refused.

`--describe` now reports `clipLengthTicks`, the `placements` array, `expandedNoteCount`
per track, and the `maxClipPlacements` bound, because an agent cannot place a clip it
cannot see.

## Evidence

The gate is green. Project assertions rose from **282 to 338**.

| Check | Result |
| --- | --- |
| A new clip spans its song with one placement at zero | passed |
| A clip is shortenable within its song; longer or shorter than the bounds fails closed | passed |
| Four placements of an eight-beat clip fill a 32-beat song | passed |
| Placement ids derive from the clip id, in order | passed |
| Every placement offsets its clip's notes by its own start | passed |
| Unsorted starts are normalised; overlapping starts fail closed | passed |
| A placement past the end of the song fails closed | passed |
| A refused list leaves the accepted one in place | passed |
| An expansion past the 1,024-note ceiling fails closed rather than truncating | passed |
| Placements survive save, reopen, and the content hash | passed |
| One Undo restores the previous placement list | passed |
| Shrinking a song keeps the first placement and drops only what cannot fit | passed |
| Version-5 migration yields one placement at zero and an unchanged sequence | passed |
| A clip-operation command previews, applies atomically, and undoes in one step | passed |
| A clip operation inside a version-2 command is refused | passed |
| A command placing a clip over itself fails closed | passed |

### End to end on a real song

Two commands, both applied headlessly through `--apply-command`, against a copy of the
32-bar four-track song:

1. The Pulse part was reduced to the two-bar figure at bar 13 and moved to the head of
   its clip: 8 notes kept, 56 removed, 8 moved.
2. `setClipLength` to 8 beats and `setPlacements` at 16 positions.

**8 stored notes now play 128 times.** The result validates against the version 6
schema, and `--audio-probe` reports 4/4 tracks audible, no track overloaded, master peak
0.492.

### The migration was verified symbolically, not by ear or by meter

`--describe` before and after the change produced **byte-identical note lists** for all
four tracks of the 32-bar song. That is the check that carries information.

Comparing probe peaks does not. Two `--audio-probe` runs of the *same* file, back to
back, differ by up to **3.54 dB** on a per-track peak. That is a larger spread than the
±2.3 dB per four-bar section recorded in the
[M7.5 render checkpoint](M7_OFFLINE_RENDER_CHECKPOINT_2026-08-13.md), and it means the
probe can only ever establish that a track sounds *at all*, never that it sounds the
same. The peak differences between the version 5 and version 6 runs were all inside
that noise and carry no meaning.

The project content hash does change, because `schemaVersion`, `songLengthTicks`, and
the placement list are all part of the hashed canonical material. That invalidates
command files authored against version 5 projects, which is the documented consequence
of every schema bump.

## What this does not do

- **One clip per track.** The schema still caps `clips` at one item. Reusing *different*
  clips on the same track — a verse clip and a chorus clip — needs note operations to
  name a clip, which is a command-layer change of its own.
- **No editor UI.** Placements are reachable through commands and `--describe` only. The
  piano roll shows the clip's own notes, so a shortened clip shows a shorter roll, but
  nothing draws the placements along the song timeline.
- **Project operations run before note changes**, so one command cannot both shorten a
  clip and rewrite the notes that would have to fit inside it first. Compressing an
  existing long part into a repeated figure takes two commands, as the demonstration
  above does.
- No listening approval. Nothing since M5 has any.
