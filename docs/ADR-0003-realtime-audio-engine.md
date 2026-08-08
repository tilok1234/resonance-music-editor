# ADR-0003: Start with one explicit WASAPI instrument path

Status: accepted for the first playable prototype
Date: 2026-08-08

## Context

The compatibility probe proved that Surge XT can render and expose its native editor, while the scanner proved that unknown VST3 discovery can be isolated. Neither checkpoint proved continuous playback, user-selectable audio hardware, loop timing, live MIDI, or a safe startup path from the accepted inventory.

A broad multi-track engine would hide failures behind too many moving parts. The first playable boundary therefore needs one complete path from a known MIDI pattern to a known instrument and the Windows output device.

## Decision

The first editor uses one custom JUCE audio-device callback and compiles the playable target with `JUCE_WASAPI=1`, `JUCE_DIRECTSOUND=0`, and `JUCE_ASIO=0`. The UI exposes JUCE's Windows Audio device selector rather than silently choosing or replacing the user's device.

At startup, the editor:

1. Reads the versioned inventory and quarantine files.
2. Gives quarantine precedence and refuses entries with unsupported schemas.
3. Recomputes the exact VST3 bundle fingerprint and rejects any drift.
4. Reconstructs the accepted plug-in description and requires the cached stable identifier.
5. Loads that known Surge instance directly, without scanning the bundle in the editor process.

The real-time callback processes exactly the sample count supplied by the active audio device. A non-owning JUCE buffer view limits the plug-in's process block to that callback span even though the host keeps a larger reusable backing allocation. Timestamped loop MIDI, mouse-keyboard MIDI, and enabled hardware MIDI inputs are merged for the same block. Transport position and loop points are exposed through the plug-in playhead.

Only the plug-in's enabled stereo main output reaches the master path. The host applies a default `-12 dB` master gain, sanitizes non-finite samples, hard-limits values outside `[-1, 1]`, and counts invalid samples, clipping, oversized blocks, processing exceptions, and available device underruns. Stop and panic send note-offs before rewinding.

The application provides two non-interactive acceptance modes:

- `--self-test` opens a real Windows Audio device, prepares the inventory-approved Surge instance, verifies its live identity and parameter count, emits no music, and writes structured JSON.
- `--ui-snapshot` constructs the packaged editor, renders its 1220x800 component to PNG while transport remains stopped, and exits.

## Evidence

On 2026-08-08, the packaged Release editor opened `Speakers (Bose Mini SoundLink)` through `Windows Audio` at 44,100 Hz with a 441-sample block and 441 samples reported output latency. It loaded stable identifier `VST3-Surge XT-bf38ca69-190e4fbd`, version 1.3.4, with 2,855 parameters matching inventory. The silent self-test reported `noRescanPerformed: true`, `audioEmitted: false`, and `passed: true`.

The deterministic loop suite passed 74 assertions across note starts, note-off offsets, tempo mapping, wrap-around scheduling, and four-loop event balance. The packaged UI snapshot completed without leaving an editor process running.

The snapshot gate initially exposed a process-block length error: the callback handed Surge the entire 4,096-sample backing buffer while the device requested 441 samples. Surge terminated with Windows fast-fail `0xC0000409`. Restricting the process buffer view to the device's exact `numSamples` fixed the crash. Keeping the packaged snapshot in the acceptance script makes this path repeatable.

The first interactive run exposed a separate UI-thread spin. JUCE's VST3 `hasEditor()` implementation calls `tryCreatingView()`, so it is an active capability probe rather than a cheap property lookup. Polling it from the 30 Hz status timer repeatedly created and released Surge views. The editor now reads the scanner's accepted `hasEditor` value once and creates a live view only on the explicit open-editor command. A four-second hidden UI idle mode is part of the acceptance script with a 3,000 ms process-CPU ceiling.

## Consequences and remaining risk

The editor now has a narrow, playable, diagnosable audio path. It does not yet provide editable notes, multiple tracks, project-state restoration, automation, undoable AI edits, or game-aware transitions.

Scanner isolation does not isolate real-time processing. An already-loaded VST3 executes inside the editor and can still crash or stall it. A future reliability milestone may move plug-in processing across a process boundary, but that is intentionally outside this first slice.

VST3 capability methods are treated as plug-in calls and are not polled from paint or timer paths. Any future live capability refresh must be explicit, bounded, and kept off the steady-state message loop.

Automated checks establish timing, identity, device, and lifecycle behavior. They do not approve the musical feel, preset choice, mix, distortion character, or listening quality.
