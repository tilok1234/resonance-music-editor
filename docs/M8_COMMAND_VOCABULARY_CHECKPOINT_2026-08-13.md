# M8 command vocabulary checkpoint

Date: 2026-08-13

Status: implementation evidence only. No listening approval.

## Why this slice exists

After the agent control surface landed, an agent could read a project and change it — but a command could only ever say one kind of thing: add, update, or remove **notes**. Nothing else.

So "make the melody higher" was expressible and "make it faster", "add a track", "turn the bass down" were not. An agent could rearrange material inside a structure, but a human still had to build the structure with a mouse — which is the opposite of the intended workflow.

## What changed

Edit-command **version 2**, which adds a `projectOperations` array alongside the existing note edit. Version 1 remains accepted exactly as it was, and its archived contract is `schema/edit-command-v1.schema.json`.

| Operation | Effect |
| --- | --- |
| `setTempo` | 40 to 240 BPM |
| `setSongLength` | within the published canvas bounds |
| `setSnap` | 0.125, 0.25, 0.5, or 1.0 |
| `setTitle` | 1 to 120 characters |
| `setTrackMixer` | gain, pan, mute, solo; only the supplied fields change |
| `setSound` | assigns a shelf sound to a track |
| `addTrack` | with caller-supplied identity |
| `removeTrack` | by id |

A command may carry project operations, note changes, or both. Project operations run first, so one command can lengthen the song and then write notes into the space it just created. The note-change cap rose from 128 to 1,024, enough to rewrite a whole part in one command.

Everything still flows through the accepted M5 lane: hash precondition, non-mutating candidate, preview, one Apply, one Undo.

## Three problems this surfaced

### Preview and apply had two implementations

The first version applied project operations while building the preview candidate but not when applying to the real project. The apply then failed its hash comparison and rolled back — the safety mechanism catching an incomplete implementation, which is what it is for.

The fix was structural rather than local: `applyProjectOperations` is now a single function called by both paths. They cannot diverge because there is only one of them.

### Generated identity made the result nondeterministic

`duplicateActiveTrack` mints a UUID for the track, its clip, and every note. Preview and apply would therefore always produce different projects, and the hash comparison would always roll back. Transplanting the candidate instead was not an option either, because `installRoot` clears the undo history.

So `addTrack` **requires the caller to supply `trackId`**, and `addTrackWithIdentity` derives the clip id as `<trackId>-clip` and note ids as `<trackId>-note-N`. The result is fully deterministic.

This is better for the intended workflow rather than merely tolerable: a command author knows the new track's id in advance and can target it with notes in the same command, which a generated id would have made impossible.

### Composed operations split the undo transaction

`addTrackWithIdentity` initially opened its own undo transaction, so a command containing it needed two Undo presses. It no longer opens one; it is only ever composed inside a larger command application that has already begun a transaction.

## Evidence

Native assertions rose from 270 to 282.

| Check | Result |
| --- | --- |
| A project-operations-only command previews | passed |
| The preview reports the created track id | passed |
| A pending preview does not mutate the active project | passed |
| Operations apply atomically | passed |
| Every operation lands: tempo, title, track count | passed |
| A created track uses the supplied deterministic identity | passed |
| One Undo restores everything the command changed | passed |
| Identical commands produce identical content hashes | passed |
| A command cannot exceed the published track ceiling | passed |
| A mixer operation on an unknown track fails closed | passed |
| Version 2 parses the version-1 note-edit shape | passed |
| Version 3 is rejected | passed |
| Project operations are rejected inside a version-1 command | passed |

### End-to-end

A five-operation command was authored from `--describe` output alone and applied headlessly to a two-track project:

| Property | Before | After |
| --- | --- | --- |
| Title | Emberline | Emberline (faster) |
| Tempo | 100 | 116 |
| Tracks | 2 | 3 |
| Length | 32 beats | 48 beats |
| Bass gain / pan | −4.0 / −0.15 | −9.0 / −0.30 |

The same command against a project already at the four-track ceiling was refused with the project left byte-identical.

A stale error message was also corrected along the way: the track-ceiling refusal still said "at most two instrument tracks" after the ceiling moved to four. It now derives from `maxProjectTracks`.

## Choosing a sound

`setSound` assigns a named shelf sound to a track. It is the one operation that touches more than the model: the model holds the state bytes, but a live plug-in instance also has to be restored or playback keeps the old sound. The GUI apply path therefore calls `synchronisePluginSlotsFromProject` afterwards, which restores exactly the slots whose state no longer matches.

`applyPluginSound` writes to the selected track, so the operation moves the selection for the write and restores it immediately; note changes later in the same command still resolve against their own target.

Both `--describe` and `--apply-command` resolve the shelf from `--sound-shelf`, defaulting to the same file the editor uses. `--describe` now lists the available sound names, because an agent cannot ask for a sound it does not know exists.

### It immediately caused an overload

Assigning two different shelf sounds to a two-track project left the lead track pinned at exactly full scale. Per-track meters are clamped to 1.0, so this was invisible: the summed master read 0.94 and the probe passed.

The patches involved differ by 8.5 dB in intrinsic output at a fixed chord, and far more across register and velocity, so **changing a track's sound can move its level enormously**. A `setSound` should be followed by a level check.

`--audio-probe` now reports a per-track `overloaded` flag for any track at or above 0.999 and fails on it. It caught the case above, a corrective `setTrackMixer` command brought the lead from −7 dB to −21 dB, and the probe then passed with the master at −14.5 dBFS.

## What an agent still cannot do

- **Create or place clips.** Reusable clip instances (M7.3) do not exist yet. When they do, they must ship with their command operations, or an agent will not be able to reach them.
- **Undo a headless apply.** `--apply-command` saves the file; the undo history is per-session. Recovering means restoring the previous file.
