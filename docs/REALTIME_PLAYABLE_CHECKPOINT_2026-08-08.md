# Real-time playable checkpoint - 2026-08-08

## Result

Accepted as the first playable technical slice. The packaged Windows editor can open an explicit WASAPI device, load the exact inventory-approved Surge XT bundle without rescanning, schedule a seamless two-bar MIDI loop, accept mouse or hardware MIDI, display the native Surge editor, and shut down cleanly.

This result makes the project usable for a first listening pass. It does not approve the musical quality of the fixed loop or preset.

## Reproduction

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-realtime.ps1 -Configuration Release
python .\scripts\validate-artifacts.py
```

Open the editor manually with:

```powershell
.\bin\ResonanceMusicEditor.exe
```

## Recorded results

| Check | Result |
| --- | --- |
| Scheduler assertions | 74 passed |
| Loop | 8 notes over 8 beats |
| Audio backend | `Windows Audio` / WASAPI |
| Accepted device during test | `Speakers (Bose Mini SoundLink)` |
| Device format during test | 44,100 Hz, 441-sample block, stereo |
| Reported output latency | 441 samples |
| Plug-in | Surge XT 1.3.4 VST3 |
| Stable identifier | `VST3-Surge XT-bf38ca69-190e4fbd` |
| Live parameter count | 2,855; matched inventory |
| Bundle fingerprint reverified | Yes |
| Scan performed in editor | No |
| Audio emitted by self-test | No |
| UI snapshot | 1220x800 PNG, packaged app exited cleanly |
| UI idle CPU gate | 1,218.8 ms CPU over 6,076 ms including startup |
| Test process left running | No |

## Visual artifact

![Packaged real-time editor snapshot](../artifacts/realtime-ui-snapshot.png)

The snapshot verifies the first-pass layout for transport, BPM, master level, track identity, loop view, mouse keyboard, device controls, diagnostics, and native Surge-editor access. It is a visual implementation check, not approval of every future interaction.

## Defect caught during acceptance

The first packaged snapshot run triggered a Windows fast-fail in Surge XT. The device callback requested 441 samples, but the host passed the full 4,096-sample capacity of its reusable backing buffer to the plug-in. The engine now creates a non-owning buffer view with the callback's exact sample count before calling `processBlock`. The same packaged snapshot path now exits successfully and remains part of `test-realtime.ps1`.

The first visible user launch then appeared frozen while Windows still marked the process as responding. Its window-owning thread consumed 2,062 ms of CPU during a two-second sample. The 30 Hz status timer was polling JUCE's VST3 `hasEditor()`, which constructs and releases a plug-in view on every call. The editor now uses the scanner's cached capability. A fixed visible launch consumed 125 ms during the equivalent two-second idle sample and remained responsive.

## Evidence artifacts

- `artifacts/realtime-engine-test-report.json` - deterministic scheduler results.
- `artifacts/realtime-self-test.json` - device, plug-in identity, inventory match, and silent-start contract.
- `artifacts/realtime-ui-snapshot.png` - packaged UI render and lifecycle gate.
- `artifacts/plugin-inventory.json` - accepted Surge XT record consumed at startup.
- `artifacts/plugin-quarantine.json` - production quarantine state checked before loading.

## Safety and scope boundary

Transport starts stopped and master gain defaults to `-12 dB`. The automated modes do not intentionally emit music. The earlier diagnostic WAV's distorted opening remains listening feedback about that artifact; it is not treated as approved source material or as a failure of this silent gate.

This checkpoint does not prove sound quality, editable piano-roll behavior, save/reopen fidelity, multiple tracks, automation, AI editing, game-state transitions, plug-in crash recovery, or a releasable JUCE licensing choice.

## Next gate

Replace the fixed loop display with a small editable piano roll backed by a versioned project model. Manual and later AI edits should operate on the same note data, with undo/redo and seamless playback preserved from this checkpoint.
