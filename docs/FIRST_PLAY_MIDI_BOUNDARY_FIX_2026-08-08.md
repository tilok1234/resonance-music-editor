# First-play MIDI boundary diagnosis and fix - 2026-08-08

## Result

The user's first listening pass found a real scheduler defect: starting the starter track could produce one long first note while several later notes were expected. The corrected scheduler and a device-shaped automated regression pass. The user then confirmed that the corrected packaged build plays the notes separately without the first note hanging.

## Root cause

The observed Windows Audio device runs at 44,100 Hz with 441-sample blocks. At 120 BPM, one callback advances the transport by exactly 0.02 beats. Several starter-loop note-ons and note-offs therefore land exactly on block boundaries.

The old scheduler treated the current beat interval as half-open, but converted an event to a sample with nearest-integer rounding. At the end of a block, a boundary event could round to sample offset 441 and be rejected as outside a 441-sample buffer. Accumulated floating-point transport drift then made the same event appear infinitesimally earlier than the next block, so recurrence logic advanced it by a full loop. Both a note-off and later note-ons could be lost, producing the held-note symptom.

One reproduced boundary was the first note-off at beat 0.82:

- the preceding block began at approximately `0.8000000000000004` beats;
- the event converted to approximately sample `440.99999999999056`, which rounded to 441 and was discarded;
- the next block began at approximately `0.8200000000000004`, making the event look just late enough to skip.

## Repair

Recurring events are now assigned to the block containing their nearest sample centre. A half-sample beat window makes boundary ownership explicit, and sample offsets use the same nearest-sample rule. Every event belongs to one block even when transport accumulation lies microscopically above the symbolic beat.

The realtime callback contract is unchanged: the scheduler still operates on its fixed-capacity note snapshot without allocation, blocking, locking, or I/O.

## Regression

`testExactDeviceBlockBoundaries` runs one full eight-beat starter loop at the observed shape:

- 44,100 Hz sample rate;
- 441 samples per block;
- 120 BPM;
- 400 consecutive blocks;
- all eight note-ons and all eight note-offs required exactly once;
- first note-off required at block 41, sample 0;
- second note-on required at block 50, sample 0.

The Release scheduler suite passes 83 assertions, and the full silent realtime gate passes with the same device shape. The packaged editor binary tested for the retest has SHA-256 `3ea9016b9b34814dacedfceb92538c5f97848518a7c892f8d0df8b3d0d8f1cc0`.

## Listening confirmation

The exact packaged Release executable with SHA-256 `3ea9016b9b34814dacedfceb92538c5f97848518a7c892f8d0df8b3d0d8f1cc0` was opened from rewind. On 2026-08-08, the user confirmed that the notes play separately. This closed the held-note defect but did not by itself approve a Surge sound or complete the then-remaining M4 A/B interaction sequence; those later gates are recorded in the accepted M4 checkpoint.
