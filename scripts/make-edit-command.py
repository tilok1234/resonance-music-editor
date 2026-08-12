#!/usr/bin/env python3
"""Build a version-1 edit command that replaces one track's notes.

An edit command can only add, update, and remove notes. It cannot change loop
length, tempo, meter, or track topology, so the target project must already have
the loop length the source material needs.

Workflow:

1. in the editor, set the loop length and Save the project;
2. press **Copy hash** and take the first clipboard line;
3. run this script;
4. press **Load command**, audition A/B, then Apply or Reject.

Example:

    python scripts/make-edit-command.py \
        --project work.resonance.json \
        --source songs/emberline.resonance.json \
        --source-track Bass \
        --hash <content-sha256> \
        --out songs/commands/emberline-bass.json
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

MAX_CHANGES = 128
SHA256 = re.compile(r"^[0-9a-fA-F]{64}$")


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def pick_track(project: dict, name_or_index: str | None, label: str) -> dict:
    tracks = project["tracks"]
    if name_or_index is None:
        return tracks[0]
    if name_or_index.isdigit():
        index = int(name_or_index)
        if not 0 <= index < len(tracks):
            raise SystemExit(f"{label} index {index} is out of range (0..{len(tracks) - 1})")
        return tracks[index]
    for track in tracks:
        if track["name"] == name_or_index or track["id"] == name_or_index:
            return track
    available = ", ".join(t["name"] for t in tracks)
    raise SystemExit(f"{label} '{name_or_index}' not found; available: {available}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project", required=True, type=Path,
                        help="saved target .resonance.json (supplies ids and loop length)")
    parser.add_argument("--source", required=True, type=Path,
                        help="song .resonance.json to take notes from")
    parser.add_argument("--hash", required=True,
                        help="active project content SHA-256 from the Copy hash button")
    parser.add_argument("--out", required=True, type=Path, help="command file to write")
    parser.add_argument("--target-track", help="target track name, id, or index (default: first)")
    parser.add_argument("--source-track", help="source track name, id, or index (default: first)")
    parser.add_argument("--summary", help="command summary (default: derived from the source)")
    args = parser.parse_args()

    if not SHA256.match(args.hash):
        raise SystemExit("--hash must be 64 hexadecimal characters")

    project = load(args.project)
    source = load(args.source)

    target_track = pick_track(project, args.target_track, "--target-track")
    source_track = pick_track(source, args.source_track, "--source-track")
    target_clip = target_track["clips"][0]
    source_clip = source_track["clips"][0]

    loop_ticks = target_clip["lengthTicks"]
    source_notes = source_clip["notes"]

    overflow = [n for n in source_notes if n["startTick"] + n["lengthTicks"] > loop_ticks]
    if overflow:
        raise SystemExit(
            f"{len(overflow)} source note(s) do not fit the target loop of {loop_ticks} ticks "
            f"({loop_ticks / 960:g} beats). Set the target loop length first; a command "
            f"cannot change it."
        )

    existing_ids = [n["id"] for n in target_clip["notes"]]
    collisions = existing_ids and {n["id"] for n in source_notes} & set(existing_ids)
    if collisions:
        raise SystemExit(
            f"{len(collisions)} source note id(s) collide with ids being removed in the same "
            f"command, e.g. {sorted(collisions)[0]}. Rename them in the source."
        )

    changes = [{"action": "remove", "noteId": note_id} for note_id in existing_ids]
    changes += [{"action": "add", "note": note} for note in source_notes]

    if len(changes) > MAX_CHANGES:
        raise SystemExit(
            f"{len(changes)} changes ({len(existing_ids)} removes + {len(source_notes)} adds) "
            f"exceeds the version-1 cap of {MAX_CHANGES}. Split the part across two commands."
        )

    command = {
        "commandVersion": 1,
        "projectContentSha256": args.hash,
        "operation": "editNotes",
        "target": {"trackId": target_track["id"], "clipId": target_clip["id"]},
        "summary": args.summary or f"{source['title']} {source_track['name']} part",
        "changes": changes,
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
    print(
        f"wrote {args.out}: {len(existing_ids)} remove + {len(source_notes)} add "
        f"-> track {target_track['id']} / clip {target_clip['id']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
