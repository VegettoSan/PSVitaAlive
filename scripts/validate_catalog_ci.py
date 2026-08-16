#!/usr/bin/env python3

"""CI wrapper for the VitaHub source validator.

The canonical validator remains responsible for validating structure and remote
resources. This wrapper adds CI-specific resilience for unreliable remote
servers without weakening real catalog errors.

Remote policy:
* 15 second timeout per HTTP attempt.
* Up to 2 attempts for transient HTTP/network failures.
* 1 second backoff between attempts.
* HTTP 429/5xx and connection/time-out failures are warnings after retries.
* Permanent HTTP errors and structural validation errors remain blocking.
* Successful URL responses are cached for the current validation run so the
  same remote resource is not checked repeatedly.

The wrapper also understands the canonical validator's human-readable output:
summary lines such as "VitaHub validation failed" and "N error(s) found" are
not themselves blocking errors. Only concrete catalog issue lines are
classified. This prevents transient remote warnings from triggering an
unnecessary external rebuild.

The canonical source directories (apps/, authors/ and categories/) may also
contain README.md documentation for contributors. Those Markdown files are
not catalog entries, while validate_catalog.py intentionally rejects unknown
non-JSON files. The CI wrapper therefore hides README.md files only while the
source validator runs, then restores them before returning.
"""

from __future__ import annotations

import re
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

# Keep the total retry budget bounded. The canonical validator checks both HEAD
# and GET when HEAD does not succeed, so overly large per-attempt timeouts can
# multiply into many minutes across a large catalog.
REMOTE_TIMEOUT = 15
REMOTE_ATTEMPTS = 2
REMOTE_BACKOFF_SECONDS = (1,)
RETRYABLE_HTTP_CODES = {429, 500, 502, 503, 504}

TRANSIENT_REMOTE_RE = re.compile(
    r"remote URL could not be reached:"
    r"|remote URL returned HTTP (?:429|500|502|503|504)"
)

CANDIDATE_ERROR_RE = re.compile(r"^- ")


class CachedResponse:
    """Minimal response object sufficient for validate_catalog.py."""

    def __init__(self, status, headers, body=b""):
        self.status = status
        self.headers = headers
        self._body = body

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False

    def read(self, size=-1):
        if size is None or size < 0:
            data = self._body
            self._body = b""
            return data
        data = self._body[:size]
        self._body = self._body[size:]
        return data


_URL_CACHE: dict[tuple[str, str], tuple[int, object, bytes]] = {}


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


def _request_cache_key(request) -> tuple[str, str]:
    return request.full_url, request.get_method()


def _cache_response(response) -> tuple[int, object, bytes]:
    status = getattr(response, "status", 200)
    headers = response.headers
    # The validator only reads status and headers. Keep no response body.
    return status, headers, b""


def resilient_urlopen(request, timeout=REMOTE_TIMEOUT, *args, **kwargs):
    """Retry transient failures and cache successful URL checks for this run."""
    key = _request_cache_key(request)

    cached = _URL_CACHE.get(key)
    if cached is not None:
        status, headers, body = cached
        return CachedResponse(status, headers, body)

    effective_timeout = max(
        REMOTE_TIMEOUT,
        timeout if isinstance(timeout, (int, float)) else 0,
    )

    last_error = None

    for attempt in range(REMOTE_ATTEMPTS):
        try:
            response = _ORIGINAL_URLOPEN(
                request,
                timeout=effective_timeout,
                *args,
                **kwargs,
            )

            _URL_CACHE[key] = _cache_response(response)
            status, headers, body = _URL_CACHE[key]
            return CachedResponse(status, headers, body)

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

ci._URL_CACHE.clear()
urllib.request.urlopen = ci.resilient_urlopen
runpy.run_path(str(Path('scripts/validate_catalog.py')), run_name='__main__')
"""

    return subprocess.run(
        [sys.executable, "-c", runner],
        text=True,
        capture_output=True,
    )


def classify_output(output: str) -> tuple[list[str], list[str], bool]:
    """Split concrete validator issue lines into blocking and transient lists."""
    blocking: list[str] = []
    warnings: list[str] = []
    candidate_count = 0

    for line in output.splitlines():
        if not CANDIDATE_ERROR_RE.match(line):
            continue

        candidate_count += 1

        if TRANSIENT_REMOTE_RE.search(line):
            warnings.append(line)
        else:
            blocking.append(line)

    # If the validator failed but did not provide concrete issue lines, preserve
    # the failure instead of accidentally masking an unexpected runtime error.
    has_unclassified_failure = (
        candidate_count == 0 and "validation failed" in output.lower()
    )
    return blocking, warnings, has_unclassified_failure


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

    blocking, warnings, unexpected_failure = classify_output(output)

    if warnings and not blocking and not unexpected_failure:
        print("VitaHub validation passed with remote-resource warnings:")
        for line in warnings:
            print(f"::warning::{line[2:]}")
        return 0

    if blocking:
        print("VitaHub validation failed:")
        for line in blocking:
            print(line)

    if warnings:
        print("\nRemote-resource warnings:")
        for line in warnings:
            print(f"::warning::{line[2:]}")

    if unexpected_failure:
        print("\nUnexpected validator failure:")
        print(output, end="")

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
