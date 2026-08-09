# Development guide

Status: current Windows development workflow

## Supported development baseline

The implemented build is Windows x64 only. It currently assumes:

- Windows with a working `Windows Audio`/WASAPI output device;
- Visual Studio 18 Community with Desktop C++ and CMake support;
- CMake 3.25 or newer;
- a JUCE 9 source checkout pinned to commit `f8f8864172464b9adf9eba6101e1f784838d1597`;
- Surge XT 1.3.4 as a portable 64-bit VST3 bundle;
- Python 3 for artifact-schema validation;
- the Python dependency pinned in `requirements-dev.txt` when schema validation is run.

`scripts/build.ps1` currently names the Visual Studio 18 CMake executable and `Visual Studio 18 2026` generator explicitly. Supporting a different Visual Studio installation requires a deliberate script change and a full test pass.

## Fresh clone layout

Clone the repository, then keep machine-local dependencies and outputs under the ignored `.local` directory:

```text
resonance-music-editor/
|-- .local/
|   |-- deps/JUCE/
|   |-- surge-xt-portable/Surge Synth Team/Surge XT.vst3/
|   `-- build/
|-- artifacts/
|-- bin/
|-- docs/
|-- schema/
|-- scripts/
|-- src/
`-- tests/
```

The default paths are defined in `scripts/local-paths.ps1`. Do not commit `.local`, `bin`, installed plug-ins, or machine-specific JSON reports.

## Dependency configuration

Place the pinned JUCE checkout and the portable Surge bundle at the default paths above, or set these environment variables before invoking a script:

| Variable | Default | Purpose |
| --- | --- | --- |
| `RESONANCE_JUCE_DIR` | `.local/deps/JUCE` | JUCE source directory containing `CMakeLists.txt` |
| `RESONANCE_BUILD_DIR` | `.local/build` | generated CMake and Visual Studio build tree |
| `RESONANCE_SURGE_VST3` | `.local/surge-xt-portable/Surge Synth Team/Surge XT.vst3` | exact Surge VST3 bundle to scan and test |

Example for the current PowerShell session:

```powershell
$env:RESONANCE_JUCE_DIR = 'D:\audio-dev\JUCE'
$env:RESONANCE_BUILD_DIR = 'D:\audio-build\resonance'
$env:RESONANCE_SURGE_VST3 = 'D:\vst-portable\Surge XT.vst3'
```

The repository does not redistribute JUCE, Surge XT, presets, or sample libraries. Obtain dependencies from their official sources and review their licenses independently.

Install the development-only schema validator when needed:

```powershell
python -m pip install -r requirements-dev.txt
```

## Build

From the repository root:

```powershell
.\scripts\build.ps1 -Configuration Release
```

Use `Debug` while diagnosing host code:

```powershell
.\scripts\build.ps1 -Configuration Debug
```

The script configures CMake, builds all production and test targets, locates the selected configuration outputs, and copies these production programs to `bin`:

- `ResonanceMusicEditor.exe`
- `ResonanceHostProbe.exe`
- `ResonancePluginScanner.exe`
- `ResonancePluginInventory.exe`

The hang fixture and C++ test executables remain in the generated build tree.

## Establish the accepted Surge inventory

The interactive editor requires both an accepted inventory and a quarantine file. Generate and validate them with:

```powershell
.\scripts\test-scanner-isolation.ps1 -Configuration Release
```

This is intentionally more than a convenience scan. It proves timeout handling with the hang fixture, rejects the invalid VST3 fixture, scans the real Surge bundle, writes one accepted record, and confirms that Surge is absent from production quarantine.

Inventory and quarantine files are machine-local because they contain absolute bundle paths and fingerprints. Moving the repository or VST3 bundle requires rerunning the scanner-isolation script.

## Run

After building and establishing the inventory:

```powershell
.\bin\ResonanceMusicEditor.exe
```

or double-click `Open Resonance Music Editor.bat`. The launcher derives the repository root from its own location and reports a clear error if `bin\ResonanceMusicEditor.exe` has not been built.

Keep the first listening level low. Transport starts stopped and the master defaults to `-12 dB`.

## Full verification sequence

Run the scripts in this order:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-surge.ps1 -Configuration Release
.\scripts\test-scanner-isolation.ps1 -Configuration Release
.\scripts\test-realtime.ps1 -Configuration Release
python .\scripts\validate-artifacts.py
```

The order matters: later scripts consume binaries and inventory artifacts created by earlier steps. See [Testing and release](TESTING_AND_RELEASE.md) for the contracts behind each gate.

The realtime script also runs the M5 command-core cases using the portable `tests/fixtures/edit-command-note-patch-v1.json` fixture. The test replaces its schema-valid placeholder content hash in memory with the exact active-project hash; do not hard-code a machine report or mutable local path into the fixture. The same native suite resolves the seeded whole-loop velocity transform twice from reordered target IDs and records canonical command and candidate SHA-256 evidence.

M6 migration coverage uses `tests/fixtures/song-project-v1-migration.resonance.json`. Keep its non-default `track-migrated` and `clip-migrated` identities, exact four-byte state/hash pair, and version-1 shape intact: the test proves the source remains byte-identical, migration defaults are deterministic, commands use stored IDs, and a later explicit save writes schema version 2. `scripts/validate-artifacts.py` checks this fixture and the historical UI round-trip file against the archived version-1 schema while validating the current real-Surge project against version 2.

## Non-interactive editor modes

The packaged editor has five test modes used by the Release gates:

| Argument | Behavior |
| --- | --- |
| `--self-test` | opens the accepted device and Surge instance, checks identity/state/project behavior, writes JSON, and intentionally emits no music |
| `--ui-snapshot` | constructs the packaged UI, writes the 1220x800 snapshot, and exits |
| `--ui-idle-test` | holds the UI for a four-second observation window used to catch message-thread CPU regressions |
| `--m4-workflow-test` | opens an explicit accepted song, plays it, and exercises unchanged sound Capture/Reject/Close lifecycle; it can emit audio |
| `--m5-workflow-test` | keeps transport stopped and exercises selected-note pitch, default eight-note dynamics, explicit target/strength/seed controls, invalid-input blocking, Save-A, A/B, Reject, Apply, Undo/Redo, deterministic repeat, stale invalidation, and cleanup |

These are automated gates, not normal authoring modes.

## Generated and versioned artifacts

Versioned artifacts are limited to portable evidence:

- UI PNGs;
- the release binary hash manifest;
- portable `.resonance.json` fixtures;
- JSON schemas and documentation.

Ignored machine-local outputs include:

- generated executables;
- CMake and Visual Studio build trees;
- plug-in inventory and quarantine files;
- scanner, self-test, and unit-test JSON reports;
- the diagnostic host-probe WAV;
- local dependency copies.

Do not force-add ignored VST3 bundles, binaries, or reports merely to make another machine run. Recreate machine-specific acceptance state there.

## Troubleshooting

### JUCE was not found

Confirm that the resolved `RESONANCE_JUCE_DIR` contains JUCE's root `CMakeLists.txt` and is the pinned JUCE 9 revision. The build script fails before configuration if it is absent.

### Surge XT was not found

Confirm that `RESONANCE_SURGE_VST3` points to the `.vst3` bundle itself, not only its parent folder. Rerun scanner isolation after correcting it.

### The editor reports a missing or changed accepted plug-in

The loader fails closed when inventory is absent, quarantine contains the path, the module is missing, or the live bundle fingerprint differs. Rerun `test-scanner-isolation.ps1` against the intended bundle. Do not hand-edit the accepted fingerprint.

### A project reports a different instrument after relocation

Current saved-project compatibility accepts an exact VST3 identifier or matching VST3 UID suffix. The inventory still describes the exact current scanned path. Rescan after moving the bundle, then retry the project. A different name or UID remains a real mismatch.

### The editor opens but produces no sound

Check the Windows Audio device selection, start transport, keep master gain low but audible, and inspect the stereo meters and diagnostic label. Use Panic if a note is stuck. The automated self-test is intentionally silent and cannot be used as a listening test.

### The UI becomes busy while idle

Do not add live VST3 capability queries such as `hasEditor()` to timer or paint paths. Surge's JUCE `hasEditor()` route creates and destroys a native view. Reproduce idle regressions with `test-realtime.ps1` and consult [the startup-freeze checkpoint](STARTUP_FREEZE_FIX_2026-08-08.md).

### A VST3 scan hangs or crashes

Use the inventory controller, not the interactive editor or scanner alone. The controller owns the deadline, terminates a timeout, evicts stale accepted data, and writes quarantine evidence.

## Before changing architecture

Read [Architecture](ARCHITECTURE.md), [VST3 hosting](VST3_HOSTING.md), and the relevant ADR. Add an ADR before changing a durable boundary. Any change to real-time code, project persistence, plug-in identity, or scanning requires its focused tests plus the full Release gate before publication.
