# M7.1 song-length canvas checkpoint

Date: 2026-08-13

Status: implementation evidence only. No listening approval.

## Why this slice exists

The editor could hold at most 8 bars. That is a loop, not a song. The user asked for real song structure — material that changes over time — which is milestone M7. This is its first slice: widen the canvas enough for structure to exist, and make a canvas that size actually editable.

The remaining M7 slices are named in the roadmap: sections and markers, reusable clip instances, tempo and meter changes, and offline full-song render.

## What changed

| Change | From | To |
| --- | --- | --- |
| Clip length ceiling | 32 beats (8 bars) | 256 beats (64 bars) |
| Notes per clip | 512 | 1024 |
| Song-project schema | 4 | 5, with 4 archived |
| Piano roll horizontal axis | whole song fixed to grid width | scrollable, zoomable window |

Version 5 differs from version 4 only by the clip ceiling; no field was added, removed, or reinterpreted, so a version-4 document is a valid version-5 document once its version is raised in memory. Source files are left byte-identical until the user saves, as with every prior migration.

### The piano roll needed a horizontal dimension

At 64 bars the previous roll mapped the entire song across the grid width, which is unreadable. Every beat-to-pixel conversion now runs through a visible window:

- `Shift` + wheel scrolls through time; `Ctrl` + `Shift` + wheel zooms horizontally, anchored on the centre beat exactly as vertical zoom anchors on centre pitch;
- `Home` and `End` jump to the start and end of the song;
- notes entirely outside the window are culled before layout;
- grid density follows the zoom: sub-beat lines appear only once they are at least 6 px apart, and bar labels thin to every 2, 4, or 8 bars as the view widens. Without this a 64-bar song at 1/16 snap would draw 4,096 vertical lines;
- Open and New fit the whole song horizontally as well as by pitch.

## Two defects this slice introduced and one it exposed

### A stack overflow, found by running the editor

Raising the note capacity to 2048 made `SequenceSnapshot` about 48 KB and `MixerSnapshot` about 393 KB. `RealtimeEngine` holds two published slots plus a pending one, so an engine became roughly 1.2 MB — and both the engine and bare snapshots were constructed as **stack locals** in the editor, the runtime test, and the engine tests. The first snapshot attempt died with `0xC00000FD`.

Fixed by lowering the capacity to 1024 and heap-allocating the oversized owners at every site. `mixer_snapshot.h` now documents the hazard so a future capacity increase does not silently reintroduce it. Flat per-clip note storage is in any case the wrong axis to scale indefinitely; reusable clip instances (M7.3) are the real answer for long songs.

### The engine clamped long songs back to the old ceiling

`sanitiseMixerSnapshot` contained `juce::jlimit (4.0, 32.0, track.sequence.loopBeats)`. Every published sequence was therefore clamped to 32 beats regardless of the project, and `LoopScheduler::addBlock` discards any note whose beat is at or beyond the loop length.

The consequence was severe and quiet: a 128-beat song looped its first 32 beats forever, and **every note past beat 32 was silently discarded**. In the test song the bass, harmony, and lead all happened to have notes below beat 32 so they still sounded; the pulse part, whose earliest note is at beat 49.5, produced exact silence.

This was caught by the per-track audio probe added the same day, not by any other gate. Every other packaged test is silent by contract and all of them passed.

### The same literal existed in three places

`setLoopLengthBeats`, `sanitiseMixerSnapshot`, and the edit-command parser each hard-coded the old ceiling independently. `minimumLoopBeats` and `maximumLoopBeats` now live in `loop_scheduler.h`, which the model, the engine, and the command parser all include, and every bound and error message derives from them.

## Evidence

Native assertions rose from 253 to 270.

| Check | Result |
| --- | --- |
| Version-4 four-track fixture loads and migrates | passed; `e73922d0…` byte-identical after load |
| Version-4 identity, mixer, and MIDI preserved | passed |
| Shared bounds publish the widened canvas | passed |
| A 64-bar project accepts the full canvas | passed |
| A longer request clamps rather than failing open | passed |
| A note at beat 200 is insertable and survives publication | passed |
| A 64-bar project saves and reopens with late notes intact | passed |

Every archived schema version now has its own migration fixture: v1 single-track, v2 non-default mixer, v3 two-track, v4 four-track.

### Rendered evidence

A 32-bar structured song (intro / A / B / A′ / outro, 359 notes over four tracks) renders 7,680 blocks through the production callback with all four tracks audible, no clipping, and no invalid samples:

| Track | Peak | Notes |
| --- | --- | --- |
| Bass | −9.5 dBFS | 100 |
| Harmony | −15.3 dBFS | 96 |
| Lead | −9.5 dBFS | 99 |
| Pulse | −18.1 dBFS | 64 |

Before the ceiling fix, the same probe reported Pulse at exactly `0.0`.

## What remains open

- No sections or markers; structure exists but is not named or navigable. That is M7.2.
- Clips are still one flat note list per track. Writing four bars and placing them six times is M7.3, and is what stops long songs from growing the note list linearly.
- No tempo or meter changes, and no offline render.
- The mouse and keyboard gestures for horizontal scroll and zoom are verified by screenshot, not by automated test.
- The M6 listening pass remains unrun.
