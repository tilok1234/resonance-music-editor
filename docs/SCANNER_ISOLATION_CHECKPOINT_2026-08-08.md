# Scanner isolation checkpoint — 2026-08-08

## Result

Accepted as a technical foundation gate. Unknown VST3 scanning now happens in a disposable child process, and a timeout or scanner failure produces durable quarantine evidence without bringing down the parent controller.

## Reproduction

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-scanner-isolation.ps1 -Configuration Release
```

The Release build completed for the host probe, scanner, inventory controller, and test-only hang fixture.

## Recorded results

| Check | Result |
| --- | --- |
| Deliberate child duration | 30 seconds |
| Configured test deadline | 250 ms |
| Controller return code | 21 (`timeout`) |
| Total timeout-path elapsed time | 823–942 ms across four runs; 823 ms latest expanded run |
| Timed-out child still running | No |
| Failure persisted | Yes, one quarantine entry |
| Failed bundle removed from timeout inventory | Yes |
| Invalid VST3 result | Code 22 (`scanner-exit`), structured error preserved |
| Bundle fingerprint | `c7c19e30c2defbf4b78e4362b6f4a20acd3e11e55ad2c428727c725b5bc5b731` |
| Real plug-in | Surge XT 1.3.4 VST3 |
| Real scan elapsed time | 846–1,055 ms across four runs; 971 ms latest expanded run |
| Stable identifier | `VST3-Surge XT-bf38ca69-190e4fbd` |
| Parameter count | 2,855 |
| Production quarantine after success | Empty for Surge XT |

## Evidence artifacts

- `artifacts/scanner-timeout-quarantine.json` — forced timeout and exact bundle identity.
- `artifacts/scanner-timeout-inventory.json` — proves no failed bundle was accepted.
- `artifacts/scanner-invalid-quarantine.json` — normal scanner rejection with bounded error detail.
- `artifacts/scanner-invalid-inventory.json` — proves the invalid fixture was not accepted.
- `artifacts/plugin-inventory.json` — accepted Surge XT metadata.
- `artifacts/plugin-quarantine.json` — production quarantine after the good scan.

## Scope boundary

This gate proves scan-time process isolation and cache transitions. It does not prove real-time playback recovery, sound quality, the visual editor, piano-roll editing, AI editing, or a releasable JUCE licensing choice. No new music was rendered, so no listening approval is requested from the diagnostic WAV noted in the earlier checkpoint.

## Next gate

Build a minimal real-time Windows audio engine with explicit device selection, transport, MIDI scheduling, and one Surge instrument track. It must consume the accepted inventory record rather than rescanning Surge inside the editor process.
