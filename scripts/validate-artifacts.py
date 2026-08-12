#!/usr/bin/env python3
"""Validate committed acceptance evidence against the versioned JSON schemas."""

from __future__ import annotations

import json
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker


PROJECT_ROOT = Path(__file__).resolve().parents[1]

VALIDATION_GROUPS = (
    (
        PROJECT_ROOT / "schema" / "edit-command.schema.json",
        (PROJECT_ROOT / "tests" / "fixtures" / "edit-command-note-patch-v1.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "song-project.schema.json",
        (
            PROJECT_ROOT / "artifacts" / "realtime-song-project.resonance.json",
            PROJECT_ROOT / "artifacts" / "m6-two-track-authoring.resonance.json",
        ),
    ),
    (
        PROJECT_ROOT / "schema" / "song-project-v1.schema.json",
        (
            PROJECT_ROOT
            / "tests"
            / "fixtures"
            / "song-project-v1-migration.resonance.json",
            PROJECT_ROOT / "artifacts" / "ui-save-open-roundtrip.resonance.json",
            PROJECT_ROOT / "artifacts" / "m4-accepted-candidate-b.resonance.json",
        ),
    ),
    (
        PROJECT_ROOT / "schema" / "song-project-v2.schema.json",
        (
            PROJECT_ROOT
            / "tests"
            / "fixtures"
            / "song-project-v2-migration.resonance.json",
        ),
    ),
    (
        PROJECT_ROOT / "schema" / "song-project-v3.schema.json",
        (
            PROJECT_ROOT
            / "tests"
            / "fixtures"
            / "song-project-v3-migration.resonance.json",
        ),
    ),
    (
        PROJECT_ROOT / "schema" / "plugin-inventory.schema.json",
        (
            PROJECT_ROOT / "artifacts" / "plugin-inventory.json",
            PROJECT_ROOT / "artifacts" / "scanner-timeout-inventory.json",
            PROJECT_ROOT / "artifacts" / "scanner-invalid-inventory.json",
        ),
    ),
    (
        PROJECT_ROOT / "schema" / "plugin-quarantine.schema.json",
        (
            PROJECT_ROOT / "artifacts" / "plugin-quarantine.json",
            PROJECT_ROOT / "artifacts" / "scanner-timeout-quarantine.json",
            PROJECT_ROOT / "artifacts" / "scanner-invalid-quarantine.json",
        ),
    ),
    (
        PROJECT_ROOT / "schema" / "realtime-engine-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "realtime-engine-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "realtime-self-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "realtime-self-test.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "m6-runtime-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "m6-runtime-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "m6-authoring-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "m6-authoring-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "song-project-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "song-project-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "m5-workflow-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "m5-workflow-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "command-load-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "command-load-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "selection-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "selection-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "sound-shelf-test.schema.json",
        (PROJECT_ROOT / "artifacts" / "sound-shelf-test-report.json",),
    ),
    (
        PROJECT_ROOT / "schema" / "audio-probe.schema.json",
        (PROJECT_ROOT / "artifacts" / "audio-probe-report.json",),
    ),
)


def load_json(path: Path) -> object:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    validated = 0

    for schema_path, artifact_paths in VALIDATION_GROUPS:
        schema = load_json(schema_path)
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema, format_checker=FormatChecker())

        for artifact_path in artifact_paths:
            errors = sorted(
                validator.iter_errors(load_json(artifact_path)),
                key=lambda error: tuple(str(part) for part in error.absolute_path),
            )
            if errors:
                locations = []
                for error in errors:
                    location = ".".join(str(part) for part in error.absolute_path) or "<root>"
                    locations.append(f"{location}: {error.message}")
                raise SystemExit(f"{artifact_path.name} failed validation:\n" + "\n".join(locations))

            print(f"validated {artifact_path.name} with {schema_path.name}")
            validated += 1

    print(f"validated {validated} acceptance artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
