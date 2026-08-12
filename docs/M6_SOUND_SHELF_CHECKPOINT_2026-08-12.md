# M6 sound shelf checkpoint

Date: 2026-08-12

Status: implementation evidence only. This slice emits no music and carries no listening approval.

## Why this slice exists

The 2026-08-12 authoring assessment found three severe limits on making songs. Two were note-entry throughput, addressed by the command-load, selection, and clipboard slices. The third was sound: every accepted snapshot lived inside one song project, so a sound existed only for as long as that project was open. New and Open discarded it. Dialing a patch in Surge and keeping it meant never starting a new song.

The practical consequence was that both visible tracks always carried the same timbre, and percussion was impossible — which in turn meant M6's listening gate would be judging a ceiling rather than a mix.

## What changed

A named snapshot library outside the song project, stored beside the settings file as `sound-shelf.json`.

| Control | Behavior |
| --- | --- |
| **Save to shelf** | Stores candidate B if one is pending, otherwise the accepted project sound |
| Shelf chooser | Lists saved sounds by name |
| **Load sound** | Restores the chosen sound as **candidate B** |
| **Remove** | Deletes the chosen sound from the shelf |

### Loading produces a candidate, never an accepted change

This is the design decision the slice turns on. **Load sound** does not change the project's sound. It restores the shelf state into the live plug-in and installs it as candidate B, so the sound then flows through the already-accepted M4 lane: Audition A, Audition B, Apply as one dirty Undo transaction, or Reject. Nothing new was added to the trust boundary, exactly as the command-load slice reused M5's preview path rather than building a second way to mutate the project.

### Constraints

`SoundShelf` holds at most 32 entries. Names are 1 to 80 characters and unique ignoring case and surrounding space, so the shelf cannot grow two entries a user would read as the same sound. Every entry carries its plug-in identity and a SHA-256 that is verified on load; a mismatched hash is refused. Loading into the editor additionally checks the entry's identity against the accepted inventory record using the same relocation-tolerant comparison as project open, so a snapshot captured from a different plug-in fails closed.

A missing shelf file is an empty shelf, not an error. A present but invalid file fails closed, leaves any in-memory entries untouched, and does not stop the editor from starting — it starts with an empty shelf and says so in the status line.

## What this does and does not unblock

It **does** make sound work durable: a patch dialed once in the native Surge window survives New, Open, and restart, and can be placed on either track. Two tracks with genuinely different timbres are now reachable, which is what percussion requires.

It **does not** create sounds. Resonance still has no factory-preset browser and does not interpret vendor `.fxp` files — [ADR-0004](ADR-0004-host-owned-sound-snapshots.md) deferred that deliberately and this slice does not revisit it. A drum patch still has to be dialed by hand inside Surge's own interface, captured, and shelved. The shelf removes the requirement to do that repeatedly; it does not remove the requirement to do it once.

## Evidence

### Native, in `SongProjectTests`

18 new assertions covering the shelf file contract: missing file loads empty, distinct names accepted, case- and whitespace-insensitive duplicates refused, empty and over-long names refused, hash mismatch refused, case-insensitive lookup, save and reload preserving exact state, hash and identity, unsupported schema version failing closed without discarding loaded entries, a missing sounds array failing closed, case-insensitive removal, absent removal failing, and capacity failing closed at 32.

Project assertions rose from 212 to 230.

### Packaged, `--sound-shelf-test`

Ten checks against two genuinely different real Surge states — the init state and the accepted M4 candidate B — so "two sounds" is not a fiction:

| Check | Result |
| --- | --- |
| The two states differ | passed; `a771b288…` vs `ccaf99d4…` |
| Accepted sound saved to the shelf | passed |
| Second sound added | passed |
| Shelf survives reload with exact state | passed |
| Load produced candidate B | passed |
| Accepted sound unchanged and clean during load | passed |
| Reject restored the accepted sound | passed |
| Apply installed the shelf sound and marked dirty | passed |
| Undo restored the accepted sound | passed |
| Foreign plug-in identity refused | passed |

The test redirects the shelf path to a temporary file so it never touches the user's real shelf, and restores it afterwards.

**Not covered:** the shelf controls as mouse interactions, and any claim about how a shelved sound *sounds*.

## Full Release gate

| Gate | Result |
| --- | --- |
| Scheduler/mixer/runtime assertions | 124 passed |
| Project/migration/topology/shelf/command assertions | 230 passed (was 212) |
| Schema-validated artifacts and fixtures | 22 passed (was 21) |
| Packaged M5 workflow | passed with both accepted hashes reproduced |
| Packaged external command load | passed |
| Packaged selection and clipboard | passed |
| Packaged sound shelf | passed |
| UI idle CPU | below the 3,000 ms ceiling |

The track card grew by one row, so the committed UI snapshot changed.

## What remains open

- No factory-preset browser and no `.fxp` interpretation; sounds still originate in the native Surge window.
- The shelf is per-machine user data and is not part of a song project, so sharing a song still does not share the shelf. The song continues to carry its own accepted state, so a shared song still opens with the right sound.
- Assigning a different plug-in *product* per track remains out of scope; both tracks still instantiate the same accepted inventory record, and the shelf varies the patch rather than the plug-in.
- The M6 two-track listening and interaction pass is still not run. With the shelf in place it is now worth running: a two-track mix with two different timbres is finally something a listening judgment can be made about.
