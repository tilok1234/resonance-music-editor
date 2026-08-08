# M5 dynamics-controls checkpoint

Date: 2026-08-09

Status: technical implementation candidate passed; packaged user interaction and listening acceptance remain open

Source checkpoint: `ef9710d` (`Add parameterized M5 dynamics controls`)

## Scope

This slice turns the fixed seeded demonstration into an explicit bounded request without changing editor version `0.3.0`, song-project schema version 1, the version-1 concrete edit-command format, accepted M4 sound behavior, or the realtime callback. The inputs remain session-only proposal controls; pending commands and their input fields are not persisted in `.resonance.json`.

## Explicit dynamics request

The M5 proposal card now exposes:

- **TARGET**: **Whole loop** or **Selected note**;
- **MAX +/-**: integer maximum velocity delta from 1 through 32;
- **SEED**: integer from 0 through 2,147,483,647;
- **Preview dynamics**: resolves those inputs into an ordinary concrete version-1 command and isolated candidate B.

Defaults remain whole loop, maximum delta `8`, and seed `18421`. Selected-note scope fails closed until a current piano-roll note is selected. Empty or out-of-range numeric fields disable Preview and show a bounded settings error. The separate **Selected +1** pitch producer remains available.

Once B exists, target, strength, seed, and both producer buttons are disabled. The card shows the resolved summary, exact first velocity change, seed, and A/B hashes. Apply never reruns pseudo-random resolution; it applies exactly the reviewed concrete command. Apply, Reject, A/B, Save-A isolation, sound-lane interlock, stale invalidation, and one-Undo restoration retain their prior contracts.

## Native workflow evidence

The expanded `--m5-workflow-test` first preserves the original one-note and default eight-note lifecycles. It then enters **Selected note**, maximum delta `3`, and seed `90210` through the production controls and proves:

- the request targets only selected stable ID `note-1`;
- the command records seed `90210` and reports maximum delta `3`;
- its one velocity-only change stays within 1 through 3 while preserving pitch and timing;
- candidate SHA-256 `896eb3db48ebac34cb888a01a8ec2566805aa7e6f376e74f2f3dc17f1bd9b882` differs from the default whole-loop candidate;
- audition and Reject preserve clean A;
- repeating identical controls reproduces the same candidate hash;
- Apply matches the reviewed candidate and one Undo restores A;
- maximum delta `33` disables Preview;
- cleanup closes without a warning, invalid samples, processor exceptions, or a leftover process.

## Release evidence

| Evidence | Result |
| --- | --- |
| Realtime scheduler | 83 assertions passed |
| Project, round-trip, sound, command, and resolver suite | 122 assertions passed |
| Default whole-loop request | seed `18421`; maximum delta `8`; 8 updates; candidate `3a41dece2c8ba4e6ee149c8122f088ac57f306d64a567d50631599593c706a9c` |
| Parameterized selected-note request | seed `90210`; maximum delta `3`; 1 update; candidate `896eb3db48ebac34cb888a01a8ec2566805aa7e6f376e74f2f3dc17f1bd9b882` |
| Invalid settings | maximum delta `33` blocked before preview |
| Schema validation | 13 acceptance artifacts and fixtures passed |
| UI snapshot | 92,959 bytes; SHA-256 `749f82b847006ae62a4910eb69cae8e7941aa84dddfbe12fb076cdbf82ec4435`; reproduced byte-identically twice and visually inspected |
| UI idle gate | 1,359.4 ms process CPU over 5,836 ms including startup |
| Packaged editor | 9,617,408 bytes; SHA-256 `8f33cc8ffc114fbde6b5857cf53fc43aee2f2ac4e060fe00bc29736ce03b0730` |
| Surge compatibility and scanner isolation | passed with one accepted Surge XT 1.3.4 record and zero production quarantine entries |

![Parameterized dynamics controls with a frozen pending B](../artifacts/realtime-ui-snapshot.png)

The snapshot deliberately uses the default eight-note B and **Audition B** so the input row, disabled pending state, larger diff, and decision controls are visible together.

## Remaining M5 acceptance

All planned M5 host-side boundaries now have a working vertical slice: strict resolved commands, exact project preconditions, isolated visual/audio preview, deterministic bounded resolution, explicit host inputs, Apply/Reject, and one Undo. This does not make the transform musically useful by assertion.

The next gate is a short packaged user pass over both target scopes and at least two strengths or seeds. The user should judge control clarity and the audible dynamics, then explicitly accept M5 or request a narrow repair. Additional transform families and natural-language translation are separate later work; M6 must not begin merely because automated tests passed.
