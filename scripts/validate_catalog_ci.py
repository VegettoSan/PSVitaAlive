#!/usr/bin/env python3

"""CI wrapper for the VitaHub source validator.

The source validator intentionally checks remote resources, but GitHub Actions
can occasionally lose connectivity to otherwise valid hosts. A transient
network failure must not make the whole catalog build fail.

The wrapper keeps the canonical validator as the source of truth while adding
CI-specific resilience for remote resources:

* 30 second timeout for remote HTTP requests.
* Up to 3 attempts for transient HTTP/network failures.
* Exponential backoff between attempts.
* HTTP 429/5xx and connection/time-out failures are warnings after retries,
  while structural errors and permanent HTTP errors remain blocking.

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
import runpy
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG_DIRECTORIES = (
    ROOT / "apps",
    ROOT / "authors",
    ROOT / "categories",
)

REMOTE_TIMEOUT = 30
REMOTE_ATTEMPTS = 3
REMOTE_BACKOFF_SECONDS = (2, 5)
RETRYABLE_HTTP_CODES = {429, 500, 502, 503, 504}

TRANSIENT_REMOTE_RE = re.compile(
    r"remote URL could not be reached:"
    r"|remote URL returned HTTP (?:429|500|502|503|504)"
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


def resilient_urlopen(request, timeout=REMOTE_TIMEOUT, *args, **kwargs):
    """Retry transient remote failures without changing the canonical validator."""
    effective_timeout = max(
        REMOTE_TIMEOUT,
        timeout if isinstance(timeout, (int, float)) else 0,
    )

    last_error = None

    for attempt in range(REMOTE_ATTEMPTS):
        try:
            return _ORIGINAL_URLOPEN(
                request,
                timeout=effective_timeout,
                *args,
                **kwargs,
            )

        except urllib.error.HTTPError as exc:
            last_error = exc

            if exc.code not in RETRYABLE_HTTP_CODES:
                raise

            if attempt + 1 >= REMOTE_ATTEMPTS:
                raise

        except (
            urllib.error.URLError,
            TimeoutError,
        ) as exc:
            last_error = exc

            if attempt + 1 >= REMOTE_ATTEMPTS:
                raise

        if attempt < len(REMOTE_BACKOFF_SECONDS):
            time.sleep(REMOTE_BACKOFF_SECONDS[attempt])

    if last_error is not None:
        raise last_error

    raise RuntimeError("remote request failed without an exception")


_ORIGINAL_URLOPEN = urllib.request.urlopen


def run_validator() -> subprocess.CompletedProcess[str]:
    """Run the canonical validator with resilient urllib behavior."""
    runner = """
import runpy
import urllib.request
from pathlib import Path

from scripts import validate_catalog_ci as ci

urllib.request.urlopen = ci.resilient_urlopen
runpy.run_path(str(Path('scripts/validate_catalog.py')), run_name='__main__')
"""

    return subprocess.run(
        [sys.executable, "-c", runner],
        text=True,
        capture_output=True,
    )


def main() -> int:
    moved: list[tuple[Path, Path]] = []

    try:
        moved = temporarily_hide_documentation()
        process = run_validator()
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
