# M6 two-track runtime checkpoint

Date: 2026-08-09

Status: implemented and technically verified; M6 remains in progress

## Result

The production `RealtimeEngine` now has a bounded multi-instance render path behind the one-track authoring surface. It owns eight stable runtime slots, accepts an immutable eight-lane mixer snapshot, and can schedule, render, mix, meter, state-test, and shut down two distinct accepted Surge XT instances without resizing or changing topology in the callback.

The visible editor and canonical schema version 2 still contain exactly one instrument track and one looping clip. The normal editor maps that track to runtime slot zero. This checkpoint therefore proves the engine boundary needed for multi-track authoring; it does not claim that a user can create or mix a second visible track yet.

## Runtime ownership

- Each fixed `RuntimeSlot` owns one optional plug-in instance, preallocated stereo audio scratch, preallocated MIDI scratch, prepared state, bounded left/right meters, and a processed-block counter.
- Plug-in creation, slot installation/removal, preparation, release, and topology changes remain on the message thread. Topology changes after preparation fail closed.
- A double-buffered immutable `MixerSnapshot` publishes at most eight sequences with enabled, gain, pan, mute, solo, and MIDI-routing values.
- All lanes use one shared transport position. Each lane schedules its own notes to its own MIDI output channel.
- The callback attempts the plug-in-access lock without waiting, renders already-prepared slots, applies resolved stereo gains, and accumulates into a preallocated master buffer.
- Per-track and master meters are bounded to `[0, 1]`. Invalid samples, clipped samples, processor exceptions, oversized blocks, and callback load are observed explicitly.
- Indexed state capture/restore isolates a selected slot. The original state API remains compatible with slot zero for M4.

## Deterministic native evidence

`RealtimeEngineTests.exe` uses deterministic fake processors so mix mathematics and failure handling do not depend on a vendor synth. Its 124 passing assertions cover:

- fixed capacity and stable two-slot installation;
- shared preparation format and fail-closed prepared topology;
- separate note schedules and MIDI output channels;
- deterministic stereo summing;
- gain, pan, mute, and solo behavior;
- bounded track and master meters;
- clipping count and output clamp;
- invalid-output and processor-exception silence;
- independent exact state capture/restore;
- oversized-block refusal without buffer growth;
- missing-slot preservation, release, and shutdown;
- average callback-load measurement below the native test threshold.

`SongProjectTests.exe` continues to pass 162 project, migration, round-trip, and edit-command assertions. This keeps the accepted M4/M5 contracts and version-1-to-version-2 migration protected while the engine changes underneath them.

## Real Surge packaged evidence

`ResonanceMusicEditor.exe --m6-runtime-test` opens the configured Windows Audio device only to obtain its real format. It never attaches the runtime callback, performs no scan, and emits no audio. The versioned report records:

| Observation | Recorded result |
| --- | --- |
| Runtime capacity | 8 slots |
| Plug-ins | 2 distinct Surge XT 1.3.4 instances; 2,855 parameters each |
| Device format | Windows Audio; 44,100 Hz; 441 samples |
| Processing | 100 render blocks plus 8 state-settle blocks; both tracks processed |
| Track/master output | nonzero and finite |
| Average callback load | `0.0079110` (0.791% of one device period) |
| Maximum callback load | `0.3994600` |
| Safety counters | 0 invalid samples; 0 clipped samples; 0 processor exceptions |
| Failure behavior | removing slot two preserved slot one |
| Lifecycle | clean release and shutdown |
| Scan/audio boundary | no rescan; `audioEmitted: false` |

The report is `artifacts/m6-runtime-test-report.json` and validates against `schema/m6-runtime-test.schema.json`. It stores filenames rather than absolute local paths.

## State evidence

The exact accepted user M4 candidate B is preserved as `artifacts/m4-accepted-candidate-b.resonance.json`. Its file SHA-256 is `b0265238ef823d660b198c6730066caace09e001eae3b3d3410521938fe74172`; its opaque stored state is 76,829 bytes with SHA-256 `ccaf99d4dc86d0b272e6ff1cc3be8afd07349bbcfe5055d992a001bea74da308`. The Release script enforces the full file hash, and the packaged gate requires its VST3 identity to be compatible with the accepted Surge record.

Surge normalises those stored bytes after restore and processing to the previously accepted live-equivalent hash `91ed214e64b35e95cf20ca773ccf57f650bbeecb547d1aa5f0ba8a2f2f5c36a3`. The test intentionally records `alternateStatePreservedExact: false` rather than misrepresenting that lifecycle encoding. Four silent processing blocks settle the alternate restore and four settle the return to baseline. Only slot two changes, and both current live baseline states then round-trip exactly.

## Reproduction

From the repository root:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-surge.ps1 -Configuration Release
.\scripts\test-scanner-isolation.ps1 -Configuration Release
.\scripts\test-realtime.ps1 -Configuration Release
python .\scripts\validate-artifacts.py
python .\scripts\check-docs.py
```

The completed sequence passes the packaged M4 and M5 regressions as well as the new M6 mode. Artifact validation covers 16 reports and fixtures. Exact packaged-binary hashes are in `artifacts/release-binaries.sha256`.

## Acceptance boundary

This checkpoint is silent technical evidence. It proves two real instances reach the production render/mix path and that deterministic processors obey exact mix semantics, but it does not prove audible balance, musical usefulness, or UI quality. No new listening approval was requested or inferred.

## Next gate

Define a bounded project-schema revision that migrates version 2 without rewriting its source, then expose a minimal second instrument track through the proven slots. Add track selection, gain/pan/mute/solo, meters, add/remove/reorder Undo, Save/Open, and user-facing missing-plug-in recovery. Complete an explicit packaged two-track listening pass before declaring M6 accepted.
