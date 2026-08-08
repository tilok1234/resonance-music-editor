# Surge sound-design audition checkpoint - 2026-08-08

## Result

Accepted as an interaction implementation gate. The native Surge XT window now includes a compact Resonance audition strip, allowing the current sound to be tested while presets and parameters are edited.

The strip provides:

- **Play loop / Pause loop** using the same sample-accurate transport as the main editor;
- **Stop** to stop, send note-offs, and rewind the loop;
- **Panic** for immediate all-notes-off and all-sound-off messages;
- a clickable C1-C7 keyboard routed through the same shared MIDI collector as the main keyboard and enabled hardware inputs;
- a status message that distinguishes ready and playing states.

There is no second synth, duplicate signal path, or separate audition volume. Surge is heard through the existing real-time engine and the main `-12 dB` master safety level.

## Visual evidence

![Surge XT with Resonance audition controls](../artifacts/surge-audition-ui.png)

The captured native window measured 1157x836 including Windows borders. The 86-pixel audition region fits above Surge XT's 1141x711 editor without obscuring its controls.

## Recorded checks

| Check | Result |
| --- | --- |
| Packaged editor build | Passed |
| Native Surge editor created | Passed |
| Audition Play control exposed | Enabled |
| Stop and Panic controls exposed | Enabled |
| Audition status exposed | Passed |
| Shared mouse keyboard | C1-C7 |
| App responding with Surge window open | Yes |
| CPU during two-second open-window idle sample | 187.5 ms |
| Scheduler assertions | 74 passed |
| Silent self-test | Passed |
| Four-second UI idle regression | 1,203.1 ms CPU over 6,049 ms including startup |
| Automated audio emitted | No |

## Lifecycle behavior

Closing the Surge window hides it and requests panic, preventing a held audition note from continuing. Reopening it reuses the existing editor instance rather than repeating the VST3 capability probe that caused the earlier startup freeze.

## Scope boundary

This gate verifies routing, controls, layout, responsiveness, and lifecycle behavior. It does not approve the sound of the current preset or loop. Preset choice, parameter changes, and musical quality remain a manual listening decision.

This limitation was removed by the editable-song milestone later on 2026-08-08. The editor now captures and restores the exact Surge state through the versioned `.resonance.json` project format; see `EDITABLE_SONG_CHECKPOINT_2026-08-08.md`.
