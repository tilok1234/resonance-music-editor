# ADR-0002: Scan untrusted plug-ins outside the editor process

Status: accepted for the new prototype
Date: 2026-08-08

## Context

A VST3 module executes native code while it is discovered and instantiated. A malformed, incompatible, or stalled plug-in can therefore crash or freeze the process that scans it. Loading unknown modules directly in the future editor would make startup reliability depend on every installed plug-in.

## Decision

The editor process will not scan an unknown VST3 bundle. It will launch `ResonancePluginScanner.exe` as a disposable child for one bundle, wait for a bounded deadline, and consume only the scanner's completed JSON report.

`ResonancePluginInventory.exe` owns the parent side of that protocol:

1. Verify the requested paths and compute a deterministic SHA-256 identity from every file in the VST3 bundle.
2. Launch one scanner process with a unique temporary report path.
3. Terminate the child when the deadline expires.
4. Record launch, timeout, non-zero exit, missing-report, malformed-report, and missing-plugin-record failures in an atomic quarantine file.
5. Evict the bundle's prior inventory entry on every failure so an updated or corrupted module cannot inherit a stale known-good result.
6. On success, atomically replace that path's inventory record and remove that path from quarantine.

The parent does not capture arbitrary child stdout or stderr. A noisy module could otherwise fill the pipe while the parent waits, turning log volume into a false hang. Expected scanner failures are instead written to the bounded temporary JSON report. Quarantine takes precedence if a path ever appears in both files, making the two-file update sequence fail closed.

The inventory stores the stable plug-in identifier, vendor version, content fingerprint, bus and MIDI capabilities, editor availability, parameter count, latency, tail length, and opaque-state hash. Editor startup will read this cache; rescanning is an explicit operation rather than a side effect of opening a song.

## Process contract

- Scanner success is exit code `0` plus a valid report whose `passed` property is true.
- Controller timeout is exit code `21`.
- The current default deadline is 20 seconds and may be overridden from 50 to 120,000 ms.
- Inventory and quarantine files conform to the schemas in `schema/`.
- The scanner does not create a plug-in editor window during discovery.
- The test hang executable is never copied into the distributable `bin` directory.

## Evidence

On 2026-08-08, a child fixture that sleeps for 30 seconds was given a 250 ms deadline. Across four acceptance runs, the controller terminated it, returned code `21`, and wrote an exact-bundle quarantine record in 823–942 ms total, including fingerprint computation and process startup. A separate invalid VST3 fixture returned code `22`, and its structured scanner error reached quarantine without captured stdout or stderr.

The same controller then scanned the isolated Surge XT 1.3.4 bundle successfully in 846–1,055 ms. The inventory recorded identifier `VST3-Surge XT-bf38ca69-190e4fbd`, 2,855 parameters, and bundle fingerprint `c7c19e30c2defbf4b78e4362b6f4a20acd3e11e55ad2c428727c725b5bc5b731`. Its production quarantine entry was absent after acceptance.

## Consequences and remaining risk

This contains discovery-time crashes and hangs; it does not yet contain a plug-in failure during real-time playback or offline rendering. The audio-engine slice must define recovery for already-loaded instances. Signature verification, duplicate-version policy, user-facing quarantine controls, and plug-in update detection at editor startup also remain product work.

The child process is a reliability boundary, not a malware sandbox. A VST3 still executes native code with the user's permissions; only trusted plug-ins should be installed or scanned.
