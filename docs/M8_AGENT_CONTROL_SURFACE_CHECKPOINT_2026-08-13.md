# M8 agent control surface checkpoint

Date: 2026-08-13

Status: implementation evidence only. No listening approval.

## The direction change this records

The roadmap's M8 describes **the application calling a model**: an opt-in AI service boundary, privacy disclosure, prompt context assembled inside the editor, and presumably a prompt box in the UI.

The user's stated workflow is the opposite. They intend to keep a chat open with an agent, tell the agent what they want, and have the agent operate the editor — without typing instructions into the program. The model drives the app; the app does not call a model.

This is a better fit for the product and considerably cheaper. Every cross-cutting risk the roadmap lists for M8 — privacy, reproducibility, cost, service availability — belongs to an embedded service and simply does not arise. No credentials live in the application and no network boundary is added.

It is a smaller departure from `PRODUCT_VISION.md` than it first appears. The vision already requires that "manual and AI editing must share one project model." What changes is only the direction of integration, not the model. The graphical editor remains a first-class surface for hand tweaks and for demonstrating examples; it is not demoted to a viewer.

## What was missing

An agent needs to read state, change it safely, and let the user hear the result. Before this slice:

| Step | State |
| --- | --- |
| Read the project | Missing. The only route was parsing a 400 KB JSON file with embedded Base64. |
| Author a change | Possible, but notes only, at most 128 per command. |
| Apply it | Required a mouse: **Load command** in the GUI. |
| Hear the result | Available since `--render` earlier the same day. |

## What changed

Two modes, both of which exit before the window is built, so neither loads an instrument. Both complete in about 0.1 s against roughly 8 s for a mode that preloads four Surge instances.

### `--describe`

```powershell
.\bin\ResonanceMusicEditor.exe --describe --project <song.resonance.json> [--out <file.json>]
```

Emits a compact view: title, tempo, sample rate, snap, canvas length, the published bounds an agent must respect, and per track the identity, mixer, MIDI routing, sound name, state hash, pitch range, and every note with its id, tick position, length, pitch, and velocity.

It also emits `projectContentSha256`, so one call gives an agent both the musical state and the precondition needed to author a valid command against exactly that state.

The opaque Base64 instrument state is deliberately omitted; identity and integrity hashes are kept. For a 32-bar four-track song this is 63 KB against 438 KB on disk, about 14%.

### `--apply-command`

```powershell
.\bin\ResonanceMusicEditor.exe --apply-command --project <song.resonance.json> `
    --command <command.json> [--out <song.resonance.json>] [--report <report.json>]
```

Parses and validates a version-1 edit command, checks the content-hash precondition, applies it through ordinary `SongProject` note operations, and saves. The report records the before and after content hashes and the add/update/remove counts.

Validation is the accepted M5 path — `parseEditCommand` and `createEditCommandPreview`, unchanged. There is no second application route.

### One deliberate behavioural difference

`createEditCommandPreview` requires a command to target the **selected** track. Selection is a UI concept and is not serialised, so headlessly it has no meaning. `--apply-command` therefore resolves the command's own `trackId` to a track index and selects it before previewing, letting an agent address any track without a session. Selection is not part of the saved document, so this does not change what is written.

Without this, an agent could only ever edit whichever track happened to be active — in practice always track one.

## Evidence

The loop was exercised end to end:

1. `--describe` on the 32-bar song produced a 63 KB view including the content hash.
2. A command was authored from that view alone — lift the lead by 18 velocity across bars 13 through 20 — and validated against `schema/edit-command.schema.json`.
3. `--apply-command` applied 26 updates, reporting before `5c9232ad…` and after `cea962ef…`.
4. Replaying the identical command was refused: *"the active project content has changed"*. The staleness guarantee holds headlessly.
5. `--render` produced a WAV of the result.

## Single-instance handoff made every headless mode a silent no-op

The application declared `moreThanOneInstanceAllowed() = false`, which is right for the interactive editor: two windows must not fight over the audio device. But JUCE applies it to *every* invocation, so running `--describe`, `--render`, `--apply-command`, or any packaged test while an editor window happened to be open handed the command line to the running instance and exited **0 without doing anything**.

This is a bad failure mode for an agent loop: the exit code says success, no output file appears, and nothing explains why. It was found by accident when a describe produced no file while an editor was open from an earlier launch.

The flag now returns true when the command line names any headless mode and false otherwise, so the interactive editor stays single-instance while agent and test modes run alongside it.

## What this slice does not do

The command vocabulary is still **one operation**, `editNotes`, capped at 128 changes. An agent can move notes and nothing else: not tempo, song length, tracks, mixer values, or shelf sounds. Widening that vocabulary is the next slice and is what makes the loop genuinely useful, because at present an agent can only rearrange material inside a structure a human had to build.

There is also no way to undo a headless apply other than restoring the previous file. The GUI Undo history is per-session and does not span invocations.

## A finding that came out of testing this

Verifying the applied edit by comparing renders did not work, because **the render is not reproducible**: two renders of an identical project differ by up to ±2.3 dB per four-bar section. See the [M7.5 render checkpoint](M7_OFFLINE_RENDER_CHECKPOINT_2026-08-13.md).

The consequence for agent workflows is direct. An agent cannot verify a small edit by measuring the audio; it can only verify that the symbolic change applied, and the user decides whether it sounds right. Measurement can still catch gross faults such as silence, clipping, or a track producing nothing.
