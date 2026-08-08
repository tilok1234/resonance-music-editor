# Interactive startup freeze fix - 2026-08-08

## Result

Fixed and regression-tested. The first visible editor build appeared frozen because its main window thread continuously reconstructed Surge XT's VST3 editor capability probe. The corrected build idles responsively and only creates the native Surge view after the user presses **Open Surge XT**.

## Observed failure

The affected process still returned `Responding: True` and rendered a complete window, but the window-owning thread consumed effectively one full CPU core:

| Measurement | Affected build |
| --- | --- |
| CPU added during two-second process sample | 2,062 ms |
| CPU added by window-owning thread | 2,031 ms |
| Other I/O operations during two seconds | 136,267 |
| Window thread and hot thread | Same thread |
| Crash or Windows application error | No |

A four-second Visual Studio CPU capture recorded 41,711 samples for that thread. Nearly all samples remained under one active root, with substantial Surge, filesystem, filter-manager, and NTFS activity. This matched repeated construction and teardown of Surge's resource-heavy native view.

## Root cause

The editor's 30 Hz `updateStatus()` timer enabled the plug-in button with a live call to `plugin->hasEditor()`.

For JUCE's VST3 host, `VST3PluginInstance::hasEditor()` is not a stored boolean. If no editor is already active, it calls `tryCreatingView()` and releases the returned view after the check. Calling it from every UI timer tick therefore created and destroyed approximately 30 Surge views per second.

The scanner had already persisted `hasEditor: true` in the accepted inventory. Repeating the live probe added no useful information.

## Fix

- Added `hasEditor` to `KnownPluginRecord` and loaded it from the validated inventory record.
- Removed all live `hasEditor()` calls from the interactive editor component.
- Used the cached value for button state and command eligibility.
- Kept the one real `createEditorAndMakeActive()` call behind the explicit **Open Surge XT** action.
- Added `--ui-idle-test`, a hidden four-second steady-state window used by `test-realtime.ps1`.
- Failed the acceptance script if that observation consumes more than 3,000 ms of process CPU or exits early.

The scanner and one-shot compatibility probe may still call `hasEditor()` because they run it once as an explicit capability test, outside the interactive steady-state loop.

## Fixed-build evidence

| Measurement | Fixed build |
| --- | --- |
| Visible app CPU during two-second idle sample | 125 ms |
| Visible app responding | Yes |
| Automated idle wall time including startup | 6,076 ms |
| Automated idle CPU time including startup | 1,218.8 ms |
| Idle CPU acceptance ceiling | 3,000 ms |
| Scheduler assertions | 74 passed |
| Silent self-test | Passed |
| UI snapshot | Passed, 57,380 bytes |
| Native Surge editor interaction | Opened and closed cleanly; 234.4 ms CPU over two seconds while open |
| Automated test leftover editor process | No |

The visible steady-state CPU sample dropped by about 94 percent. The automated number includes plug-in loading, audio-device setup, teardown, and the four-second observation window, so it is intentionally higher than the steady-state sample.

## Scope boundary

This checkpoint accepts the startup responsiveness fix. It does not approve the loop, preset, sound quality, distortion character, native Surge editor workflow, or the future piano-roll design. Those remain separate listening and interaction gates.
