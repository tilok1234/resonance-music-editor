# M4 Surge state-equivalence diagnosis and fix - 2026-08-08

## Result

The final manual M4 persistence check exposed opaque byte encodings for the same restored Surge sound. The saved project remained valid and the named A snapshot reopened correctly, but an unchanged live recapture displayed a different raw SHA-256. The repair now separates exact saved-payload integrity from post-processing live equivalence, and the corrected packaged native workflow passes against the exact saved B project.

## Manual observation

The accepted project contained:

- sound name `m4 candidate b`;
- exact saved state SHA-256 `ccaf99d4dc86d0b272e6ff1cc3be8afd07349bbcfe5055d992a001bea74da308`;
- eight loop notes;
- schema version 1.

Save, Close, and Open succeeded, and the UI showed the saved name and short `CCAF99D4` hash. After Play, Stop, and an unchanged Capture B, the live snapshot showed `91ED214E`. B was not applied.

## Reproduction evidence

The extended `ResonanceHostProbe` restored the exact machine-local saved project in a separate Surge instance and recorded:

| Observation | Result |
| --- | --- |
| Saved state | 76,829 bytes; `ccaf99d4dc86d0b272e6ff1cc3be8afd07349bbcfe5055d992a001bea74da308` |
| Headless prepared restore | `91ed214e64b35e95cf20ca773ccf57f650bbeecb547d1aa5f0ba8a2f2f5c36a3`; repeated restore exact |
| Native-editor-created prepared restore | `ccaf99d4dc86d0b272e6ff1cc3be8afd07349bbcfe5055d992a001bea74da308`; repeated restore exact |
| Prepared state after four-second render | Exact match; zero differing bytes |
| State after reset | Exact match |
| Host parameters changed by render | 0 of 2,855 |
| Non-finite rendered samples | 0 |

This is lifecycle-dependent VST3 state serialization, not evidence of a changed host-visible sound parameter. Both the headless and editor-created restore contexts reach their own stable fixed point. A whole-blob hash remains valid for the exact payload that produced it, but it cannot serve as a universal semantic sound fingerprint across plug-in lifecycle stages.

The first hidden native workflow run exposed one additional transition that the offline probe did not model: immediately after the UI restore, Surge reported `a771b288...`, then settled to `91ed214e...` after real device processing began. The editor had captured A before that first processing boundary, so unchanged B still appeared different after playback. `RealtimeEngine` now waits for two successful post-restore `processBlock` calls, bounded to 250 ms and outside the plug-in access lock, before recapturing the live-equivalent identity.

## Repair contract

- `SongProject.stateSha256` remains the exact integrity hash of the Base64-decoded bytes stored in the file.
- `RealtimeEngine::restorePluginState` restores and resets under the plug-in lock, releases it for bounded device processing, then recaptures under the lock after two successful blocks.
- `MainEditorComponent` keeps ephemeral live-equivalent A and B hashes from that recapture.
- A/B display, audition selection, candidate equality, and one-time discard comparisons use the live-equivalent hashes.
- Tooltips expose the exact saved/snapshot hashes when they differ from the live-equivalent values.
- Save does not rewrite, normalize, or silently substitute the accepted project payload.
- No project-schema change or Surge-specific state parsing is introduced.

## Current verification

- Full Release 0.3.0 package compiled; editor SHA-256 `e3280f804819dd1945e7969c0f7c80ba468f28d6d96f769a6dcfcdc00dbfaa91`.
- Strengthened Release host probe passed; host-probe SHA-256 `1684927fa828f3b9c6593829d885bd0c98fbf31d0dcdb6fe3b05aa5cf591d964`.
- Scheduler suite: 83 assertions passed.
- Project suite: 63 assertions passed.
- Exact saved B probe: passed with idempotent headless and editor-created restore identities, zero host-parameter drift, and finite output.
- Scanner timeout, invalid-bundle, accepted inventory, silent self-test, UI snapshot, and idle gates passed.
- Exact packaged saved-B workflow passed: restored A and unchanged B both `91ed214e...`, `STATE MATCHES A`, clean Reject, and warning-free Close.

## Accepted status

The state-equivalence repair and its packaged native regression pass. The exact saved B retains the user's earlier listening preference, while the automated test proves technical identity and lifecycle behavior only. The user explicitly accepted the complete M4 gate at 2026-08-08 23:54 +02:00, and the editor milestone is versioned as 0.3.0.
