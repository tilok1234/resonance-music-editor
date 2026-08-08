"""Validate repository-local links in Resonance Markdown documentation."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


LINK_PATTERN = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
EXTERNAL_SCHEMES = {"http", "https", "mailto"}


def markdown_files(root: Path) -> list[Path]:
    files = list(root.glob("*.md"))
    files.extend((root / "docs").rglob("*.md"))
    return sorted(path for path in files if path.is_file())


def link_target(raw: str) -> str:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1 : value.index(">")]

    # Markdown permits an optional quoted title after a whitespace separator.
    return value.split(maxsplit=1)[0]


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    failures: list[str] = []
    local_link_count = 0
    files = markdown_files(root)

    for document in files:
        text = document.read_text(encoding="utf-8")
        for match in LINK_PATTERN.finditer(text):
            target = link_target(match.group(1))
            if not target or target.startswith("#"):
                continue

            parsed = urlsplit(target)
            if parsed.scheme.lower() in EXTERNAL_SCHEMES:
                continue

            path_text = unquote(parsed.path)
            if not path_text:
                continue

            local_link_count += 1
            candidate = (document.parent / path_text).resolve()
            if not candidate.exists():
                line = text.count("\n", 0, match.start()) + 1
                relative_document = document.relative_to(root)
                failures.append(f"{relative_document}:{line}: missing link target {target}")

    if failures:
        print("Documentation link validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        f"Documentation links passed: {len(files)} Markdown files, "
        f"{local_link_count} repository-local links"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
