# Resonance Music Editor documentation

This index is the entry point for product, architecture, development, and handoff information. Resonance is an early but working Windows-native game-music editor: version 0.5.0 edits one or two visible looping Surge XT tracks, plays through WASAPI, persists independent accepted states and mixer settings in song-project schema version 3, and migrates versions 1 and 2 without source rewrite. M4's sound A/B workflow and M5's version-1 command/proposal workflow remain accepted, with the current sound's subtle velocity response recorded honestly. M6 is still in progress because the bounded two-track authoring slice has technical but not listening approval, and user-facing missing-plug-in recovery remains open.

## Read this first

| If you want to... | Read |
| --- | --- |
| Understand what the editor is trying to become | [Product vision](PRODUCT_VISION.md) |
| Understand the implemented system | [Architecture](ARCHITECTURE.md) |
| Build or run a fresh clone | [Development guide](DEVELOPMENT.md) |
| Change `.resonance.json` | [Project format](PROJECT_FORMAT.md) |
| Work on plug-in discovery, loading, or state | [VST3 hosting](VST3_HOSTING.md) |
| Design or implement AI-assisted edits | [AI editing design](AI_EDITING_DESIGN.md) |
| Choose and verify the next milestone | [Roadmap](ROADMAP.md) |
| Validate or prepare a release | [Testing and release](TESTING_AND_RELEASE.md) |
| Continue work in a fresh Codex task | [Current handoff](../HANDOFF.md) |
| Contribute a scoped change | [Contributing guide](../CONTRIBUTING.md) |

## Documentation types

The repository uses three types of documentation:

1. **Current references** describe the live code and are expected to move with it. These include this index, the architecture, development, project-format, VST3, testing, roadmap, and handoff documents.
2. **Design documents** describe an intended contract that is not fully implemented. They must say so explicitly. The AI editing design and later roadmap milestones are in this category.
3. **Dated evidence** records what was proven at a particular acceptance checkpoint. These files are historical records and should not be silently rewritten to look current.

When two documents appear to disagree, use this order:

1. executable code and JSON schemas;
2. current validation artifacts generated from that code;
3. current reference documentation;
4. accepted ADRs for architectural intent;
5. dated checkpoint evidence for historical context.

For example, a dated checkpoint can contain a path-derived JUCE identifier recorded before the repository and local VST3 were relocated. The current inventory is authoritative for the current machine, while the VST3 UID suffix is the relocation-compatible identity used by saved projects.

## Current reference set

- [Product vision](PRODUCT_VISION.md)
- [Architecture](ARCHITECTURE.md)
- [Development guide](DEVELOPMENT.md)
- [Project format](PROJECT_FORMAT.md)
- [VST3 hosting](VST3_HOSTING.md)
- [AI editing design](AI_EDITING_DESIGN.md)
- [Testing and release](TESTING_AND_RELEASE.md)
- [Roadmap](ROADMAP.md)
- [Current handoff](../HANDOFF.md)
- [Contributing guide](../CONTRIBUTING.md)

## Accepted architecture decisions

- [ADR-0001: VST3 is a day-one foundation](ADR-0001-vst3-host-foundation.md)
- [ADR-0002: Scan untrusted plug-ins outside the editor process](ADR-0002-crash-isolated-plugin-scanning.md)
- [ADR-0003: Start with one explicit WASAPI instrument path](ADR-0003-realtime-audio-engine.md)
- [ADR-0004: Use host-owned opaque snapshots for the first sound workflow](ADR-0004-host-owned-sound-snapshots.md)
- [ADR-0005: Migrate projects before publishing a fixed-capacity mixer](ADR-0005-multitrack-project-and-mixer-ownership.md)

An ADR records a durable decision and its consequences. Add a new ADR when a change alters a system boundary, persistence contract, plug-in safety policy, real-time rule, licensing assumption, or public extension point.

## Dated implementation evidence

- [Scanner isolation checkpoint](SCANNER_ISOLATION_CHECKPOINT_2026-08-08.md)
- [First real-time playable checkpoint](REALTIME_PLAYABLE_CHECKPOINT_2026-08-08.md)
- [Startup freeze diagnosis and fix](STARTUP_FREEZE_FIX_2026-08-08.md)
- [Surge audition controls checkpoint](SURGE_AUDITION_CHECKPOINT_2026-08-08.md)
- [Editable song checkpoint](EDITABLE_SONG_CHECKPOINT_2026-08-08.md)
- [Accepted M4 host-owned sound workflow](M4_SOUND_WORKFLOW_CHECKPOINT_2026-08-08.md)
- [First-play MIDI boundary diagnosis and fix](FIRST_PLAY_MIDI_BOUNDARY_FIX_2026-08-08.md)
- [M4 Surge state-equivalence diagnosis and fix](M4_SURGE_STATE_EQUIVALENCE_FIX_2026-08-08.md)
- [M5 edit-command foundation](M5_EDIT_COMMAND_FOUNDATION_2026-08-09.md)
- [M5 note-proposal workflow](M5_NOTE_PROPOSAL_WORKFLOW_CHECKPOINT_2026-08-09.md)
- [M5 seeded loop-dynamics transform](M5_SEEDED_LOOP_DYNAMICS_CHECKPOINT_2026-08-09.md)
- [M5 parameterized dynamics controls](M5_DYNAMICS_CONTROLS_CHECKPOINT_2026-08-09.md)
- [Accepted M5 unified edit-command layer](M5_ACCEPTANCE_2026-08-09.md)
- [M6 schema, identity, and mixer-ownership foundation](M6_MULTITRACK_FOUNDATION_CHECKPOINT_2026-08-09.md)
- [M6 two-track runtime](M6_TWO_TRACK_RUNTIME_CHECKPOINT_2026-08-09.md)
- [M6 bounded two-track authoring](M6_TWO_TRACK_AUTHORING_CHECKPOINT_2026-08-09.md)
- [M6 external command load](M6_EXTERNAL_COMMAND_LOAD_CHECKPOINT_2026-08-12.md)
- [M6 piano roll visibility](M6_PIANO_ROLL_VISIBILITY_CHECKPOINT_2026-08-12.md)
- [M6 multiple note selection](M6_MULTIPLE_SELECTION_CHECKPOINT_2026-08-12.md)
- [M6 note clipboard](M6_NOTE_CLIPBOARD_CHECKPOINT_2026-08-12.md)

The acceptance files distinguish implementation proof from musical approval. A passing scheduler, schema, or audio-device test does not mean that a preset, loop, mix, or composition has passed a listening review.

## Maintainer rule

Every implementation milestone should update the current handoff and roadmap, link its evidence from this index, and state whether it changes the project schema. Keep machine-specific reports and installed third-party binaries out of Git; keep portable schemas, fixtures, screenshots, and bounded evidence in the repository.
