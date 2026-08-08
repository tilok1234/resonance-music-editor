# ADR-0001: VST3 is a day-one foundation

Status: accepted for the new prototype
Date: 2026-08-08

## Decision

Use JUCE 9, C++20, CMake, and a Windows-native audio process for the first implementation. VST3 instrument and effect hosting is part of the foundation rather than a later add-on. The first compatibility target is Surge XT 1.3.4.

The editor's own project data remains a versioned symbolic model. A plug-in assignment stores the stable plug-in identifier, vendor, version, opaque state, MIDI routing, automation mapping, and a missing-plug-in fallback. Third-party plug-ins and sample libraries are never bundled into an editor project.

## First gate

The isolated host probe must:

1. discover the Surge XT VST3 bundle;
2. enumerate and select its instrument type;
3. instantiate it at 48 kHz and a 512-sample block size;
4. capture and restore opaque plug-in state;
5. deliver timestamped MIDI notes;
6. render a non-silent 24-bit WAV without invalid samples;
7. emit a machine-readable compatibility report.

## Deferred from this gate

- Real-time device selection and low-latency playback.
- Embedded or floating plug-in editor windows.
- Scanner subprocess and crash quarantine, subsequently addressed by ADR-0002.
- Piano roll, arrangement timeline, mixer, automation editor, and AI edits.
- Other plug-in formats.

These remain required product work; the command-line probe exists to prove the risky VST3 boundary before UI work depends on it.

## Licensing boundary

JUCE 9 is available under its commercial terms or AGPLv3. This local prototype does not decide the eventual distribution license. A release must choose and document a compatible JUCE license before binaries are distributed publicly or commercially.
