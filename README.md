# Resonance Music Editor

Resonance Music Editor is a clean-room restart of the game-music editor, with VST3 hosting as a foundation rather than a later add-on. The project now has its first editable song slice: a native Windows editor with real-time WASAPI output, a piano roll, lossless song projects, and one inventory-approved Surge XT instrument track.

The current scope is deliberately one instrument track and one looping clip. Notes, tempo, loop length, grid snap, velocity, and Surge state are editable and saveable; arrangement tracks, automation, AI commands, and game-state music tools come next.

![First playable Resonance Music Editor UI](artifacts/realtime-ui-snapshot.png)

The native Surge window also has a Resonance audition strip, so sound design can be heard without returning to the main window:

![Surge XT with Resonance audition controls](artifacts/surge-audition-ui.png)

## Run the playable editor

From PowerShell:

```powershell
.\bin\ResonanceMusicEditor.exe
```

Keep the master level low for the first listen. It defaults to `-12 dB`, and transport is stopped on startup. The earlier diagnostic WAV that sounded distorted is not used as a sound-quality baseline for this editor.

## Edit and save a song

- Click empty piano-roll space to add a note.
- Drag a note to change its beat and pitch; drag its highlighted right edge to resize it.
- Select a note and edit velocity, or right-click/press **Delete** to remove it.
- Choose `1/32`, `1/16`, `1/8`, or `1/4` grid snap and a one-, two-, four-, or eight-bar loop.
- Use **Undo/Redo** or `Ctrl+Z` / `Ctrl+Y` while playback continues.
- Use **Save** to create a `.resonance.json` song. **Open** restores the notes, tempo, loop, snap, and exact versioned Surge state.

Unsaved changes are marked with `*`. New, Open, and window close ask before discarding edits.

## Proven checkpoints

### Editable song project

The Release acceptance gate passed on 2026-08-08:

- added, selected, moved, resized, and velocity-edited notes in the packaged native piano roll;
- handed immutable, fixed-capacity note snapshots to the audio callback without allocation or blocking;
- kept sample-accurate scheduling across editable four- through 32-beat loop lengths;
- grouped each gesture into one undo transaction and verified native velocity Undo/Redo behavior;
- saved and reopened the native Windows project chooser flow;
- stored note timing at 960 PPQ with stable IDs and a versioned JSON schema;
- captured a real 67,340-byte Surge state in a 91,334-byte project, reopened it, restored it, and recaptured it byte-for-byte;
- passed 79 scheduler assertions, 41 project-model/round-trip assertions, the silent Surge self-test, and 11 schema-validated artifacts;
- passed the four-second packaged UI idle regression at 1,171.9 ms process CPU over 5,519 ms including startup.

See `docs/EDITABLE_SONG_CHECKPOINT_2026-08-08.md` for architecture and evidence. This is technical acceptance, not listening approval of a preset or composition.

### First playable real-time editor

The Release acceptance gate passed on 2026-08-08:

- opened the JUCE `Windows Audio` WASAPI backend on the selected output;
- loaded Surge XT 1.3.4 directly from the accepted inventory without rescanning;
- reverified the exact VST3 bundle fingerprint before loading;
- matched the live 2,855 parameters to the cached inventory record;
- scheduled an eight-note, two-bar loop at exact sample offsets, including loop wrap;
- passed 74 deterministic scheduler assertions;
- exposed play/pause, stop/rewind, panic, BPM, master gain, stereo meters, device selection, MIDI input, mouse keyboard, and Surge's native editor;
- placed Play/Pause, Stop, Panic, and a C1-C7 mouse keyboard directly above the native Surge editor for live sound auditioning;
- generated a 1220x800 UI snapshot through the packaged application and exited cleanly;
- cached Surge's editor capability from the accepted inventory instead of repeatedly constructing a VST3 view on the UI timer;
- passed a four-second UI idle gate at 1,218.8 ms total process CPU across 6,076 ms including startup;
- kept the automated self-test silent.

The latest machine-specific device result was 44.1 kHz, 441 samples, stereo output, and 441 samples reported output latency. Those values are selected by the active Windows device and are not hard-coded requirements.

This is a technical acceptance result, not listening approval of the loop or Surge preset. Musical judgment remains a separate user gate.

The first interactive build had a startup-freeze defect: JUCE's VST3 `hasEditor()` query creates and releases a native plug-in view, and the status timer accidentally called it 30 times per second. That drove the main window thread to one full CPU core. The editor now trusts the scanner's cached `hasEditor` result and creates the real Surge view only when **Open Surge XT** is pressed. See `docs/STARTUP_FREEZE_FIX_2026-08-08.md` for the diagnosis and before/after evidence.

### VST3 compatibility probe

The Release compatibility probe passed against Surge XT 1.3.4 on 2026-08-08:

- discovered the 64-bit VST3 instrument and its stable JUCE identifier;
- loaded and recreated the plug-in instance;
- restored its 67,340-byte state byte-for-byte before UI creation;
- created the 1141x711 native editor and enumerated 2,855 parameters;
- delivered timestamped MIDI and rendered 192,000 stereo samples;
- produced a 4.0-second, 48 kHz, 24-bit PCM diagnostic WAV;
- found no non-finite samples and returned `passed: true`.

Creating the Surge editor adds UI metadata to the subsequent state blob. The host therefore records state provenance and does not equate every byte change with an audible change.

### Crash-isolated scanner

The Release scanner-isolation gate passed on 2026-08-08:

- ran each VST3 scan and instantiation in a disposable child process;
- terminated a deterministic 30-second hang fixture after a 250 ms deadline;
- returned the dedicated timeout code `21` and persisted a quarantine record in under one second across repeated runs;
- rejected a deterministic invalid VST3 with code `22` and preserved its bounded structured error;
- fingerprinted the exact three-file, 21,124,046-byte Surge bundle with SHA-256;
- evicted stale inventory data whenever a scan failed;
- cached Surge's stable identifier, version, capabilities, parameter count, and state hash;
- atomically cleared its production quarantine entry after the successful scan.

Unknown plug-ins are still native code. The child process contains discovery crashes and hangs; it is not a malware sandbox, and a failure in an already-loaded real-time plug-in can still terminate the editor.

## Current dependency pins

- JUCE 9.0.0, commit `f8f8864172464b9adf9eba6101e1f784838d1597`
- Surge XT 1.3.4 portable VST3
- Visual Studio 18 Community C++ toolchain
- Python `jsonschema` 4.26.0 for development-artifact validation only
- Windows Audio/WASAPI for the playable editor

## Build and test

From PowerShell:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\test-surge.ps1 -Configuration Release
.\scripts\test-scanner-isolation.ps1 -Configuration Release
.\scripts\test-realtime.ps1 -Configuration Release
python .\scripts\validate-artifacts.py
```

Machine-local JUCE and Surge copies plus generated build files stay under the ignored `.local` directory. `RESONANCE_JUCE_DIR`, `RESONANCE_BUILD_DIR`, and `RESONANCE_SURGE_VST3` can override those defaults. Generated executables under `bin`, machine-specific JSON reports, and the diagnostic WAV remain local and are ignored by Git; source, schemas, documentation, UI snapshots, and portable `.resonance.json` fixtures remain versioned.

The root `Open Resonance Music Editor.bat` launcher uses the repository location automatically. It expects a local Release build in `bin`; run `scripts\build.ps1` first when starting from a fresh clone.

The schema check is optional development verification; install its pinned dependency with `python -m pip install -r requirements-dev.txt` if `jsonschema` is not already available.

The build copies four production executables to `bin`:

- `ResonanceMusicEditor.exe` - the playable visual editor;
- `ResonanceHostProbe.exe` - compatibility and offline-render diagnostic;
- `ResonancePluginScanner.exe` - disposable one-bundle VST3 scanner;
- `ResonancePluginInventory.exe` - parent timeout, inventory, and quarantine controller.

`ScannerHangFixture.exe`, `RealtimeEngineTests.exe`, and `SongProjectTests.exe` stay in the build tree because they are development-only test programs. Exact SHA-256 values for the packaged binaries are recorded in `artifacts/release-binaries.sha256`.

## Next implementation slice

1. Add Surge preset browsing, parameter discovery, automation, and track mixing.
2. Add arrangement sections and multiple instrument tracks without weakening the real-time boundary.
3. Add structured, previewable, reversible AI edit commands over the same model used by manual editing.
4. Add game-music exports, stems, loops, and transition tools after ordinary arrangement editing is dependable.

Architecture and evidence are recorded in `docs/ADR-0001-vst3-host-foundation.md`, `docs/ADR-0002-crash-isolated-plugin-scanning.md`, `docs/ADR-0003-realtime-audio-engine.md`, and the dated checkpoint files under `docs/`.
