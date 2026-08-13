#!/usr/bin/env python3

"""CI wrapper for the VitaHub source validator.

The source validator intentionally checks remote resources, but GitHub Actions
can occasionally lose connectivity to otherwise valid hosts. A transient
network failure must not make the whole catalog build fail. Structural errors
and HTTP errors remain blocking; only the explicit "remote URL could not be
reached" condition is downgraded to a warning.
"""

from __future__ import annotations

import re
import subprocess
import sys


TRANSIENT_REMOTE_RE = re.compile(
    r"remote URL could not be reached:"
)


def main() -> int:
    process = subprocess.run(
        [sys.executable, "scripts/validate_catalog.py"],
        text=True,
        capture_output=True,
    )

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
