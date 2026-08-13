# Resonance Music Editor handoff

Updated: 2026-08-13

Repository: <https://github.com/tilok1234/resonance-music-editor>

## Takeover summary

Resonance is a clean-room restart of a Windows game-music editor. The current application is a working four-track, 64-bar authoring prototype, not a complete DAW. It preloads four distinct instances of the explicitly scanned and inventory-approved Surge XT 1.3.4 VST3, plays through Windows Audio/WASAPI, edits one clip per track on a shared canvas of one through 64 bars, opens the selected track's native Surge editor, and saves independent accepted sound and mixer state with the symbolic song.

M4 (host-owned sound A/B) and M5 (validated edit-command/proposal layer) remain explicitly accepted by the user. M6 (multi-track and mixer) is technically verified but **has never passed a listening gate**. M7 (arrangement) is in progress: slice M7.1 delivered the song-length canvas on 2026-08-13.

Project version: `0.5.0`. It has not been bumped since M4's acceptance at 0.3.0 because no milestone has been accepted since, although the capability set has grown considerably beyond what "0.5.0" implied a week ago. Bumping it is reasonable to propose, but it should follow an acceptance rather than precede one.

## Current branch and history

Working branch `codex/m6-two-track-authoring`, HEAD `58405d6`, clean tree.

| Commit | Slice |
| --- | --- |
| `58405d6` | M7.1 song canvas to 64 bars, schema v5, roll time axis |
| `7e9262e` | Per-track audio probe after the first listening report |
| `47103e9` | Four-track ceiling, schema v4 |
| `d8de105` | Named sound shelf |
| `79c75ce` | Note clipboard, duplicate, nudge, transpose |
| `1dc6bc8` | Multiple note selection |
| `9dc6e43` | Piano-roll visibility: zoom and ghost notes |
| `dc7a767` | External edit-command loading |
| `b6af336` | Bounded M6 two-track authoring (prior baseline) |

Nothing since `b6af336` has been pushed or reviewed. The accepted M4 baseline is `7af6573` on `codex/m4-accepted-0.3.0` (draft PR #1); the accepted M5 commit is `9d94780` (draft PR #2). Verify live `HEAD` and upstream before relying on any of these.

## What changed since 2026-08-09, and why

A 2026-08-12 assessment found that the binding constraint on making songs was not arrangement or AI, but **note-entry throughput and sound variety**. Neither was a roadmap milestone. Eight slices followed, none of which altered the accepted M4 or M5 contracts:

1. **External command loading.** Version-1 edit-command files load into M5's accepted preview path; **Copy hash** publishes the content SHA-256, track ID, and clip ID needed to author one. `scripts/make-edit-command.py` builds them.
2. **Piano-roll visibility.** Vertical zoom 12–72 rows, dim ghost notes for inactive tracks, pitch-range fitting on Open and New.
3. **Multiple selection.** Shift/Ctrl-click, marquee, Ctrl+A, with selection-wide delete, move, velocity, and transpose as single Undo transactions.
4. **Note clipboard.** Ctrl+C/V/D, arrow-key nudge and semitone/octave transpose, paste at a drawn insert marker.
5. **Sound shelf.** Up to 32 named snapshots beside the settings file; loading one produces candidate B and flows through the accepted A/B lane.
6. **Four tracks, schema v4.**
7. **Per-track audio probe.** See below.
8. **M7.1 song canvas, schema v5.** 64 bars, 1,024 notes per clip, horizontal zoom and scroll.

## The most important thing to understand

**Every packaged gate except one is silent by contract, and the first time a human listened they immediately found a defect that all of them had passed.**

On 2026-08-13 the user played a four-track project and reported hearing one instrument. Investigation found no editor defect: the three real Surge patches available on this machine differ in intrinsic output by **8.5 dB**, and the mix had been authored assuming parity, which buried the melody. That prompted `--audio-probe`, the only non-silent gate — and the probe then caught a second, unrelated defect within hours: `sanitiseMixerSnapshot` was clamping every published sequence to an older 32-beat ceiling, so a 128-beat song looped its first 32 beats and **silently discarded every note past beat 32**.

Carry both lessons forward:

- A silent gate cannot observe silence. Run `--audio-probe` on any project-shaped change.
- The probe catches silence, not badness. The mix that started this was audible and still wrong. Only a person can find that.

## Current verified local baseline

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/ceiling/canvas/shelf/command assertions | 270 passed |
| Schema-validated artifacts and fixtures | 25 passed |
| Song project schemas | canonical writer `5`; accepted inputs `4`, `3`, `2`, `1` |
| Track ceiling | 4 persisted; 8 runtime lanes; 4 Surge instances preloaded |
| Clip canvas | 4–256 beats (1–64 bars); 1,024 notes per clip |
| Packaged audio probe | 1/1 expected-audible tracks, no clipping, no invalid samples |
| Packaged command load | 6 refusal paths, Apply, replay-after-Apply refused, one-step Undo |
| Packaged selection/clipboard | 19 checks |
| Packaged sound shelf | 10 checks against two genuinely different Surge states |
| UI idle gate | 2,093.8 ms with four preloaded instances, below the 3,000 ms ceiling |
| Packaged UI snapshot | 104,134 bytes; SHA-256 `7b520b966da04274a00fc70891e0eebc580dd6de048aa3482ca2e95153fc6e49` |
| Packaged editor SHA-256 | `1d9e1a662146cc338fb02bd8bb3b3a95ed77830d7e5637f6235aa51f6525ffbd` |
| Surge XT | 1.3.4; 2,855 parameters; VST3 UID suffix `190e4fbd` |

Device name, sample rate, block size, latency, and path-derived identifiers are machine observations. Regenerate rather than copying them.

**Command and content hashes are not carried forward.** `schemaVersion` is part of the hashed canonical material, so every schema bump changes every project's content hash and invalidates previously authored command files. The M5 acceptance hashes recorded in older checkpoints are historical from v4 onward; the behavior they describe is unchanged.

## Traps this codebase has already sprung

Each of these cost real time. They are recorded so they cost less next time.

1. **The same limit hard-coded in several places.** The old clip ceiling lived independently in the model clamp, the realtime sanitiser, and the command parser. Bounds now live once in `src/loop_scheduler.h` (`minimumLoopBeats`, `maximumLoopBeats`, `maxSequenceNotes`). Check that header before changing any limit.
2. **Snapshot structures are huge.** `MixerSnapshot` embeds a fixed-capacity sequence per lane. At 2,048 notes an engine reached ~1.2 MB and overflowed a thread stack. Never construct `MixerSnapshot` or `RealtimeEngine` as a stack local.
3. **A schema bump touches more than the schema.** Expect to update the loader, an archived schema copy, a new migration fixture, the report schemas (`song-project-test`, `realtime-self-test`, `m6-authoring-test`), and several hard-coded version assertions in `scripts/test-realtime.ps1`. The v4 and v5 bumps each needed multiple gate runs to flush these out.
4. **The editor holds `bin/`.** A running editor blocks the build's copy step, the gate's no-leftover-process check, and screenshots. Close it before verifying.
5. **Paint code compiling is not paint code working.** Ghost notes and horizontal zoom both looked correct in source and were only proven by screenshot. `--ui-snapshot --project <file>` captures any project.

## Implemented capabilities

- JUCE 9 / C++20 / CMake Windows-native application; VST3-first hosting with Surge XT.
- Crash-isolated one-bundle scanner child, parent-owned deadline, quarantine, accepted inventory, exact bundle fingerprint revalidation.
- Explicit Windows Audio/WASAPI selector; sample-accurate looping MIDI scheduler; mouse and hardware MIDI input; Play/Pause, Stop/Rewind, Panic, master gain, meters, diagnostics.
- Native Surge window with a Resonance audition strip.
- One through four editable piano-roll clips on a 1–64 bar shared canvas, with add, move, resize, delete, velocity, snap, multiple selection, marquee, clipboard, nudge, transpose, vertical and horizontal zoom, ghost notes, and gesture-level Undo/Redo.
- Song-project schema version 5 with lossless, non-rewriting migration from versions 1 through 4; stable project-wide track/clip/note identity; per-track gain, pan, mute, solo, MIDI routing; Base64 VST3 state plus SHA-256 integrity; candidate-then-replace transactional Open.
- Host-owned named A/B sound snapshots with Capture, Audition, Apply, Reject, one-transaction Undo, and a persistent named sound shelf.
- Strict version-1 edit-command parser/serializer, full-project SHA-256 preconditions, non-mutating candidates, before/after diffs and overlays, consume-once Apply/Reject, deterministic seeded velocity resolver, and external command-file loading.
- Fixed eight-lane `MixerSnapshot` and eight prepared runtime slots with preallocated scratch, separate scheduling/MIDI channels, bounded meters, and clean shutdown.
- Eleven packaged self-test modes, one of which renders audio.

## Known limitations

- **No listening approval for anything since M4/M5.** This is the largest open item.
- One clip per track. No sections, markers, reusable clip instances, arrangement timeline, tempo or meter changes, or automation.
- No offline render or export; nothing can leave the editor.
- Exactly one accepted inventory record; all four tracks instantiate the same Surge product. The shelf varies the patch, not the plug-in.
- No factory-preset browser and no `.fxp` interpretation (ADR-0004, deliberate).
- No user-facing missing-plug-in recovery.
- The mixer row shows only the selected track; there is no simultaneous multi-channel view.
- Mouse and keyboard gestures across the roll are verified by screenshot, not automated test.
- The sound shelf is per-machine and is not part of a song project.
- Scanner isolation does not contain a failure from a VST3 already processing in the editor.
- The build script assumes the Visual Studio 18 Community installation; JUCE licensing for distribution is unresolved.

## Recommended next steps, in order

1. **Run the listening pass.** Open `songs/emberline-long.resonance.json` (32 bars, four tracks, intro/A/B/A'/outro) and judge it. Does it read as four instruments? Do the sections land? This is the only step that can invalidate eight slices of unvalidated work, and an agent cannot do it.
2. **M7.5 offline WAV render.** Small now: `--audio-probe` already renders a whole project through the production callback, so writing the buffer to a file is a modest delta. Closes the last open finding from the 2026-08-12 assessment and makes every future listening pass shareable and repeatable.
3. **M7.3 reusable clip instances.** The real arrangement work, and what stops a long song's note list growing linearly with its length.
4. **M7.2 sections and markers**, then tempo and meter changes.
5. **User-facing missing-plug-in recovery**, still required before M6 can be called complete.

## First takeover actions

```powershell
git status --short --branch
git log --oneline --decorate -9
python .\scripts\check-docs.py
python .\scripts\validate-artifacts.py
```

Then read `README.md`, `docs/README.md`, this handoff, and the ADRs. Build and verify with:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-realtime.ps1 -Configuration Release
```

Close any running editor first. Full prerequisites are in [Development](docs/DEVELOPMENT.md) and [Testing and release](docs/TESTING_AND_RELEASE.md).

## Scope boundaries

- Do not add sound-effect creation to this repository.
- Do not copy an external music catalog, approved render, VST3 binary, preset library, or sample library into the repository without an explicit integration request and license review.
- Do not treat old diagnostic audio as a quality baseline, and do not treat tests as listening approval.
- Do not distribute editor binaries until JUCE licensing is resolved.
- Do not scan arbitrary installed plug-ins inside the interactive editor.
- Do not publish or rewrite Git history without user authorization.
- Dated checkpoint documents are historical evidence. Do not rewrite them to look current; add a new dated document instead.

## Ready-to-paste fresh-task prompt

```text
Take over the local clone of https://github.com/tilok1234/resonance-music-editor.

Read HANDOFF.md and docs/README.md completely first, then inspect git status, the
current branch and HEAD, recent commits, and the ignored local build state. Preserve
existing changes and publish nothing without my approval.

The product is a music-only editor aimed at video-game music. Manual and future AI
edits share one versioned project model, and technical tests are deliberately separate
from listening approval.

Current state: branch codex/m6-two-track-authoring at 58405d6, unpushed. Editor 0.5.0
writes song-project schema version 5 and reads 1 through 5 without rewriting sources.
Up to four tracks, one clip each, on a canvas of one through 64 bars with up to 1024
notes per clip. Four Surge XT instances preload at startup against an eight-lane
runtime. The gate passes 124 scheduler assertions, 270 project assertions, and 25
schema validations.

M4 and M5 are user-accepted. M6 is technically verified but has NEVER passed a
listening gate. M7 is in progress; M7.1 delivered the song-length canvas.

Most important context: every packaged gate except --audio-probe is silent by
contract. The first time the user listened they found a defect all of them had passed,
and the probe added in response then caught a second one. Run --audio-probe on any
project-shaped change, and do not mistake it for listening approval.

Recommended next: (1) get the user to run the listening pass on
songs/emberline-long.resonance.json; (2) M7.5 offline WAV render, which is cheap
because the probe already renders through the production callback; (3) M7.3 reusable
clip instances. Do not add different plug-in products per track, automation, a live AI
service, or factory .fxp indexing without an explicit request.
```

## Handoff maintenance

At the end of the next milestone, update this file with the exact accepted artifact, test counts, current limitations, next slice, and published commit. Keep historical measurements in a new dated checkpoint rather than rewriting old evidence.
