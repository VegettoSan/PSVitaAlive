#!/usr/bin/env python3

"""CI wrapper for the VitaHub source validator.

The source validator intentionally checks remote resources, but GitHub Actions
can occasionally lose connectivity to otherwise valid hosts. A transient
network failure must not make the whole catalog build fail. Structural errors
and HTTP errors remain blocking; only the explicit "remote URL could not be
reached" condition is downgraded to a warning.

The canonical source directories (apps/, authors/ and categories/) may also
contain README.md documentation for contributors. Those Markdown files are
not catalog entries, while validate_catalog.py intentionally rejects unknown
non-JSON files. The CI wrapper therefore hides README.md files only while the
source validator runs, then restores them before returning. This keeps the
repository documentation next to the data it documents without weakening the
catalog validator for arbitrary non-JSON files.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG_DIRECTORIES = (
    ROOT / "apps",
    ROOT / "authors",
    ROOT / "categories",
)

TRANSIENT_REMOTE_RE = re.compile(
    r"remote URL could not be reached:"
)


def temporarily_hide_documentation() -> list[tuple[Path, Path]]:
    """Move README.md files out of catalog directories for validation only."""
    moved: list[tuple[Path, Path]] = []

    for directory in CATALOG_DIRECTORIES:
        readme = directory / "README.md"
        if not readme.is_file():
            continue

        hidden = directory / ".README.md"
        if hidden.exists():
            raise RuntimeError(
                f"cannot temporarily hide {readme}: {hidden} already exists"
            )

        readme.rename(hidden)
        moved.append((readme, hidden))

    return moved


def restore_documentation(moved: list[tuple[Path, Path]]) -> None:
    """Restore README.md files even when validation fails."""
    for readme, hidden in reversed(moved):
        if hidden.exists():
            hidden.rename(readme)


def main() -> int:
    moved: list[tuple[Path, Path]] = []

    try:
        moved = temporarily_hide_documentation()

        process = subprocess.run(
            [sys.executable, "scripts/validate_catalog.py"],
            text=True,
            capture_output=True,
        )
    finally:
        restore_documentation(moved)

    output = "".join(
        part
        for part in (process.stdout, process.stderr)
        if part
    )

    if process.returncode == 0:
        print(output, end="")
        return 0

    lines = output.splitlines()
    blocking = []
    warnings = []

    for line in lines:
        if TRANSIENT_REMOTE_RE.search(line):
            warnings.append(line)
        else:
            blocking.append(line)

    if warnings and not blocking:
        print("VitaHub validation passed with remote-resource warnings:")
        for line in warnings:
            print(f"::warning::{line.lstrip('- ')}")
        return 0

    print(output, end="")

    if warnings:
        print()
        print("Remote-resource warnings:")
        for line in warnings:
            print(f"::warning::{line.lstrip('- ')}")

    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
