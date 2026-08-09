# Product vision

Status: product direction; only the first single-track slice is implemented

## Purpose

Resonance Music Editor is intended to make expressive music for video games without forcing a choice between manual composition and AI assistance. It should support fluid, smooth pieces; fast and aggressive pieces; slow and calm pieces; and useful territory between those extremes. The tool is about **music**, not general-purpose sound-effect creation.

The editor should eventually be capable enough for complete game-music workflows while remaining approachable when the user wants to sketch, audition, and refine a small musical idea quickly.

## Product promise

A user should be able to:

1. choose or design convincing instrument sounds through real VST3 instruments;
2. compose notes, chords, rhythms, dynamics, and structure manually;
3. ask AI for bounded musical changes in plain language;
4. preview exactly what an AI operation will change;
5. accept, refine, undo, or reject that operation without losing manual work;
6. turn a musical idea into sections, variations, loops, stems, and smooth game transitions;
7. export technically clean music suitable for a game engine;
8. keep musical taste and final listening approval under the user's control.

## Scope

### In scope

- MIDI and symbolic composition.
- VST3 instruments and, later, VST3 effects.
- Piano-roll, arrangement, mixer, automation, and musical-form editing.
- AI-assisted composition, orchestration, variation, transition, and mix suggestions.
- Loop-aware and game-state-aware music structures.
- Offline music renders, stems, loop metadata, and transition assets.
- Technical metering, loudness, clipping, and export validation.

### Explicitly out of scope

- A general-purpose sound-effect generator or sound-effect library manager.
- Bundling third-party plug-ins or sample libraries in project files.
- Treating generated output as musically approved merely because it passes technical tests.
- Letting an AI mutate a project invisibly or bypass undo history.
- Scanning arbitrary native plug-ins inside the interactive editor process.
- Shipping public or commercial binaries before the JUCE licensing decision is resolved.

## Core design principles

### One musical model

Manual controls and AI tools must operate on the same versioned song model. There should not be a hidden AI-only representation or a second playback path. A note dragged by hand and a note moved by an AI command should serialize, play, diff, and undo in the same way.

### Non-destructive by default

AI changes should follow a propose, validate, preview, apply, and undo lifecycle. Larger generations should create a new section, take, or variation until the user explicitly replaces existing material.

### Real sounds early

Instrument quality is foundational. The editor started with Surge XT because a music workflow cannot be judged fairly using one weak placeholder sound per instrument. Plug-in state belongs in the project so a song reopens with the sound that was actually edited.

### Music stays playable while it is edited

The architecture should preserve seamless playback for ordinary edits. Work that can block, allocate unpredictably, touch disk, open UI, or invoke unsafe plug-in behavior must stay away from the real-time callback.

### Technical and musical gates remain separate

Automated checks can prove timing, state integrity, identity, clipping behavior, file validity, and lifecycle safety. Only a listening review can approve feel, sound selection, balance, emotional effect, and musical quality.

### Game music is more than a loop

The long-term model must represent musical sections and intentional transitions, not only render a linear song. Game integration should be built on a dependable ordinary arrangement and mixer rather than becoming a separate fragile authoring system.

## Target creative workflow

1. **Sound** - choose a synth, patch, orchestral instrument, or layered texture and audition it in context.
2. **Sketch** - create a playable motif, chord sequence, rhythm, bass line, or texture.
3. **Shape** - edit timing, articulation, velocity, register, density, harmony, and timbre.
4. **Arrange** - build sections, tracks, transitions, climaxes, breakdowns, and endings.
5. **Vary** - derive alternatives that retain identity while changing energy, orchestration, or tension.
6. **Adapt** - mark loop regions and game states, then define musically safe transition points.
7. **Mix** - automate levels and effects, inspect peaks and loudness, and compare sections.
8. **Export** - render full mixes and stems with deterministic settings and game-facing metadata.

## Musical control dimensions

The UI and future AI commands should expose musical intent through concrete dimensions rather than vague style labels alone:

- tempo and perceived pulse;
- note density and rhythmic subdivision;
- syncopation and rhythmic regularity;
- articulation and note length;
- register and pitch range;
- velocity, dynamics, and accent shape;
- harmonic stability and tension;
- melodic contour and repetition;
- orchestration thickness and spectral brightness;
- ambience, space, and effect intensity;
- section length, transition time, and loop continuity.

These dimensions make requests such as “more aggressive,” “calmer,” or “smoother” inspectable and editable instead of turning them into opaque one-click generation.

## Current reality

The implemented editor is intentionally narrower than the vision. Version 0.4.0 still renders one inventory-approved Surge XT instrument and one looping clip, with a piano roll, tempo and loop controls, exact plug-in-state persistence, real-time WASAPI playback, native Surge editing, the accepted M4 sound A/B workflow, and the accepted M5 validated note-proposal workflow. The first M6 slice adds schema-version-2 migration, stable track/clip identity, persisted per-track mixer/MIDI settings, and a fixed eight-lane realtime ownership contract without yet claiming audible multi-track mixing. Multiple runtime instruments, factory-preset indexing, arrangement, automation, natural-language AI translation, game-state logic, effects, and final exports remain roadmap work.
