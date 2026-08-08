# Contributing to Resonance Music Editor

Resonance is an early native audio application. Small changes can affect project compatibility, the message thread, the real-time callback, third-party native code, and a user's hearing. Contributions should be narrow, testable, and explicit about which layer they change.

## Before making a change

1. Read the [documentation index](docs/README.md) and [current handoff](HANDOFF.md).
2. Inspect the live branch, `git status`, and recent commits.
3. Preserve unrelated local changes; do not reset or overwrite a dirty worktree.
4. Identify the source-of-truth contract: code, schema, ADR, or dated evidence.
5. State the scope and the acceptance gate before implementation.
6. If the change alters a durable boundary, write or update an ADR first.

Do not publish, force-push, rewrite history, accept a listening baseline, or distribute binaries without explicit authorization.

## Architectural guardrails

### Real-time code

Inside or directly reachable from the audio callback:

- do not perform file or network I/O;
- do not create UI;
- do not allocate or grow unbounded containers;
- do not wait for message-thread locks;
- do not scan plug-ins or probe native editors;
- process exactly the device-supplied number of samples;
- keep failure behavior bounded and audible output safe.

Read [Architecture](docs/ARCHITECTURE.md) before editing `realtime_engine.*` or `loop_scheduler.h`.

### Plug-ins

- Scan unknown bundles only through the isolated inventory controller.
- Treat inventory as machine-local accepted state, not a portable project asset.
- Reverify the bundle before load.
- Give quarantine precedence.
- Do not poll VST3 capability methods from paint or timer paths.
- Never commit third-party VST3 binaries, factory content, or sample libraries.

Read [VST3 hosting](docs/VST3_HOSTING.md) before changing plug-in behavior.

### Project persistence

- Keep stable IDs stable.
- Parse into a candidate and validate before replacing the active project.
- Verify opaque-state integrity before restore.
- Never reinterpret a released schema version silently.
- Add migration and failure-path fixtures with any schema evolution.
- Update the JSON schema and [project-format reference](docs/PROJECT_FORMAT.md) in the same change.

### AI-assisted editing

- AI output must be a bounded structured proposal.
- Validate against an exact project revision.
- Preview before mutation.
- Apply accepted work as a normal Undo transaction.
- Keep AI calls and generation off the audio callback.
- Preserve fully manual operation.

Read [AI editing design](docs/AI_EDITING_DESIGN.md) before implementing an AI surface.

## Coding style

- Use C++20 and existing JUCE idioms.
- Match the surrounding brace, spacing, naming, and ownership style.
- Prefer explicit ownership and bounded data structures in audio paths.
- Keep platform and plug-in assumptions visible rather than hidden behind fallback behavior.
- Return actionable errors at file, device, inventory, and state boundaries.
- Avoid opportunistic refactors in a focused feature or defect fix.
- Comments should explain non-obvious invariants and hazards, not restate the code.

No automatic formatter is currently enforced. A formatting-only rewrite should be a separate, explicitly approved change.

## Change and test matrix

| Change area | Minimum focused verification |
| --- | --- |
| Documentation only | `python .\scripts\check-docs.py`, `git diff --check`, factual diff review |
| Build/CMake/scripts | Release build plus the scripts affected |
| Scheduler | `RealtimeEngineTests.exe` through `test-realtime.ps1` |
| Project model/schema | `SongProjectTests.exe`, artifact validation, old and new fixtures |
| VST3 discovery/inventory | Surge probe and scanner-isolation gate |
| Audio engine/device lifecycle | full Release sequence, self-test, UI snapshot, idle gate |
| Main UI/piano roll | focused C++ tests plus packaged manual interaction review and screenshot when visual |
| Native Surge editor/state | scanner gate, self-test state round trip, Save/Open, manual audition |
| Musical content/defaults | technical gates plus explicit listening review of the exact artifact |

The complete sequence is documented in [Testing and release](docs/TESTING_AND_RELEASE.md).

## Documentation expectations

- Update current references when behavior changes.
- Preserve dated checkpoint evidence as historical truth; add a new checkpoint for a new gate.
- Mark planned designs as unimplemented.
- Link new documents from `docs/README.md`.
- Update `HANDOFF.md` and `docs/ROADMAP.md` at milestone boundaries.
- Do not include user-specific absolute paths, credentials, private audio, or ignored machine reports.
- Run the documentation link checker.

## Commit preparation

Before committing:

```powershell
git status --short --branch
git diff --check
python .\scripts\check-docs.py
```

Then inspect the exact staged diff and file sizes. A source publication should contain only intended source, schemas, documentation, portable fixtures, and bounded evidence. Build outputs and machine-local plug-in data must remain ignored.

Use a concise imperative commit message. Keep generated artifacts in the same commit only when they are required evidence for that exact change.

## Pull requests and publication

A useful change description states:

- the user-visible or architectural outcome;
- the exact scope and exclusions;
- important safety or compatibility decisions;
- commands run and their results;
- manual visual or listening checks still required;
- schema, artifact, and licensing impact.

Never claim musical approval from automated tests. Never claim a binary is distributable until the JUCE and third-party licensing boundary is explicitly resolved.

## Reporting a plug-in failure

Include the editor commit, plug-in name and version, current bundle fingerprint if safe to share, scanner or quarantine failure kind, test command, and bounded error text. Do not attach the commercial plug-in binary or private preset content.
