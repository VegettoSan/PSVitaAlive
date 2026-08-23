#!/usr/bin/env python3
"""One-time migration of the public source name to VitaHomebrewDB.

The repository URL used by the upstream source may retain its historical path,
so URL tokens are intentionally left untouched. Human-readable source names,
notes, documentation, JSON metadata, and similar text are migrated.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD = "VitaDBtoo"
NEW = "VitaHomebrewDB"

TEXT_SUFFIXES = {
    ".md", ".json", ".py", ".yml", ".yaml", ".txt", ".html", ".js", ".css"
}
SKIP_DIRS = {
    ".github/workflows/build", "Client PSVitaAlive/build", "node_modules"
}
URL_RE = re.compile(r'''https?://[^\s"'<>`]+''')


def replace_non_url_text(text: str) -> str:
    parts = []
    cursor = 0
    for match in URL_RE.finditer(text):
        parts.append(text[cursor:match.start()].replace(OLD, NEW))
        parts.append(match.group(0))
        cursor = match.end()
    parts.append(text[cursor:].replace(OLD, NEW))
    return "".join(parts)


def should_skip(path: Path) -> bool:
    relative = path.relative_to(ROOT).as_posix()
    for directory in SKIP_DIRS:
        if relative == directory or relative.startswith(directory.rstrip("/") + "/"):
            return True
    return any(part == ".git" for part in path.relative_to(ROOT).parts)


def main() -> None:
    changed = 0
    for path in ROOT.rglob("*"):
        if not path.is_file() or should_skip(path) or path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        try:
            current = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue

        updated = replace_non_url_text(current)
        if updated == current:
            continue

        path.write_text(updated, encoding="utf-8")
        changed += 1
        print(f"renamed source references: {path.relative_to(ROOT)}")

    print(f"VitaHomebrewDB source-name migration changed {changed} files.")


if __name__ == "__main__":
    main()
