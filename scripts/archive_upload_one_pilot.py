#!/usr/bin/env python3
"""Fail-closed one-VPK Internet Archive pilot; never modifies PSVitaAlive JSON.

Hard-coded to one approved open-source release and one existing owned item.
The first bulk uploader will be a separate phase after this is validated.
"""
from __future__ import annotations

import csv
import hashlib
import json
import os
import sys
import tempfile
import time
import unicodedata
from pathlib import Path
from urllib.parse import urlparse

import requests

from scripts.archive_owner_preflight import fetch_item_metadata
from scripts.archive_backup_batch_preview import item_owned_by_account
from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import select_current_download

APP = "better-homebrew-browser"
TITLE_ID = "BHBB00001"
VERSION = "1.46"
ITEM = "psvitaalive-better-homebrew-browser"
REMOTE = "BetterHomebrewBrowser v1.46.vpk"
SOURCE = "https://github.com/Ibrahim778/BetterHomebrewBrowser/releases/download/V1.46/BetterHomebrewBrowser.vpk"
REPOSITORY = "https://github.com/Ibrahim778/BetterHomebrewBrowser"
EXPECTED_BYTES = 1129327
MAX_BYTES = 5 * 1024 * 1024
ROOT = Path(__file__).resolve().parents[1]


class PilotStop(Exception):
    def __init__(self, status: str, message: str):
        super().__init__(message)
        self.status = status
        self.message = message


def validate_catalog(root: Path) -> None:
    app = to_v1(json.loads((root / "apps" / (APP + ".json")).read_text("utf-8")))
    link, _ = select_current_download(app.get("links") or [])
    if (app.get("id") != APP or app.get("title_id") != TITLE_ID
            or app.get("version") != VERSION or app.get("status") not in ("Legacy", "Verified")
            or app.get("source_url") != REPOSITORY
            or link is None or link.get("url") != SOURCE):
        raise PilotStop("PENDING_MANUAL", "App or source no longer matches approved pilot")
    with (root / "scripts/archive_upload_log.csv").open(newline="", encoding="utf-8") as stream:
        rows = [r for r in csv.DictReader(stream)
                if r.get("app_id") == APP and r.get("status") == "ok"]
    if not rows or {row.get("identifier") for row in rows} != {ITEM}:
        raise PilotStop("PENDING_MANUAL", "No unique historical managed Archive.org item")


def has_remote_name(metadata: dict, name: str) -> bool:
    """Never overwrite an existing (even differently cased) target filename."""
    expected = unicodedata.normalize("NFC", name).casefold()
    return any(
        isinstance(f, dict) and isinstance(f.get("name"), str)
        and unicodedata.normalize("NFC", f["name"]).casefold() == expected
        for f in metadata.get("files", [])
    )


def preflight(owner: str, *, fetch=fetch_item_metadata) -> None:
    if not owner.strip():
        raise PilotStop("CONFIG_REQUIRED", "Owner secret is not set")
    try:
        metadata = fetch(ITEM)
    except Exception:
        raise PilotStop("WAITING_ARCHIVE", "Archive.org metadata is unavailable") from None
    matches, _ = item_owned_by_account(metadata, owner)
    if not matches:
        raise PilotStop("PENDING_MANUAL", "Archive.org item is not owned by the expected account")
    if has_remote_name(metadata, REMOTE):
        raise PilotStop("ALREADY_EXISTS_REVIEW", "Versioned target already exists; no replacement attempted")


def safe_redirect(url: str) -> bool:
    u = urlparse(url)
    return (u.scheme == "https" and not u.username and not u.password
            and u.port is None and (u.hostname or "").lower() in {
                "github.com", "release-assets.githubusercontent.com",
                "objects.githubusercontent.com"
            })


def fetch_source(path: Path, *, getter=requests.get) -> tuple[str, str]:
    with getter(SOURCE, stream=True, timeout=(15, 50), allow_redirects=True,
                headers={"User-Agent": "PSVitaAlive-Archive-Pilot/1.0"}) as response:
        if not all(safe_redirect(r.url) for r in [*response.history, response]):
            raise PilotStop("PENDING_MANUAL", "Source redirected to an untrusted host")
        if response.status_code != 200:
            raise PilotStop("WAITING_SOURCE", "GitHub release is temporarily unavailable")
        length = response.headers.get("Content-Length")
        if length is not None and (not length.isdigit() or int(length) > MAX_BYTES):
            raise PilotStop("PENDING_MANUAL", "Source has unexpected Content-Length")
        md5, sha256, size = hashlib.md5(), hashlib.sha256(), 0
        with path.open("wb") as output:
            for chunk in response.iter_content(chunk_size=65536):
                if not chunk:
                    continue
                size += len(chunk)
                if size > MAX_BYTES:
                    raise PilotStop("TOO_LARGE_PENDING", "Source exceeds 5 MiB")
                output.write(chunk)
                md5.update(chunk)
                sha256.update(chunk)
    if size != EXPECTED_BYTES:
        raise PilotStop("PENDING_MANUAL", "Source byte count does not match release metadata")
    if path.open("rb").read(4) != b"PK\x03\x04":
        raise PilotStop("PENDING_MANUAL", "Source does not look like a VPK/ZIP")
    return md5.hexdigest(), sha256.hexdigest()


def upload_once(file_path: Path, access: str, secret: str) -> None:
    # No auto-retry: if response is lost, retry needs targeted file reconciliation.
    import internetarchive
    try:
        response = internetarchive.get_item(ITEM).upload_file(
            str(file_path), key=REMOTE, access_key=access, secret_key=secret,
            verify=True, queue_derive=False, retries=0, verbose=False,
            request_kwargs={"timeout": 110},
        )
    except Exception:
        raise PilotStop("UPLOAD_RESULT_UNCERTAIN", "Upload failed or timed out; do not retry blindly") from None
    status = getattr(response, "status_code", 0)
    if status not in (200, 201, 204):
        raise PilotStop("UPLOAD_RESULT_UNCERTAIN", "Archive.org did not confirm upload")


def verify_new_file(md5: str, *, fetch=fetch_item_metadata, sleep=time.sleep) -> None:
    for delay in (0, 6, 12, 20, 25):
        if delay:
            sleep(delay)
        try:
            metadata = fetch(ITEM)
        except Exception:
            continue
        for item in metadata.get("files", []):
            if not isinstance(item, dict) or item.get("name") != REMOTE:
                continue
            if str(item.get("size")) != str(EXPECTED_BYTES):
                raise PilotStop("PENDING_MANUAL", "Target name is present with conflicting size")
            if str(item.get("md5") or "").lower() != md5.lower():
                raise PilotStop("PENDING_MANUAL", "Target checksum missing or mismatched")
            return
    raise PilotStop("VERIFYING", "Archive.org file metadata not ready; do not retry upload blindly")


def run(root: Path = ROOT) -> tuple[str, str]:
    validate_catalog(root)
    access, secret, owner = (os.environ.get(name, "").strip() for name in
                             ("IA_ACCESS_KEY", "IA_SECRET_KEY", "IA_EXPECTED_UPLOADER"))
    if not access or not secret or not owner:
        raise PilotStop("CONFIG_REQUIRED", "One or more required GitHub secrets are missing")
    preflight(owner)
    with tempfile.TemporaryDirectory(prefix="psva_ia_pilot_") as temp:
        file_path = Path(temp) / REMOTE
        md5, sha256 = fetch_source(file_path)
        preflight(owner)
        upload_once(file_path, access, secret)
        verify_new_file(md5)
    return "UPLOADED_VERIFIED", ("Verified one new VPK, "
            + f"{EXPECTED_BYTES / 1048576:.2f} MiB, sha256={sha256}; catalog unchanged")


def main() -> int:
    try:
        status, message = run()
        okay = True
    except PilotStop as error:
        status, message = error.status, error.message
        okay = status == "ALREADY_EXISTS_REVIEW"
    except Exception:
        status, message, okay = "PENDING_MANUAL", "Unexpected error; no automatic retry", False
    print(f"Archive.org pilot: {APP} | {ITEM} | status={status}")
    print(message)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("# Archive.org one-VPK pilot\n\n")
            f.write(f"App: {APP}\n\nArchive item: {ITEM}\n\n")
            f.write(f"Status: {status}\n\n{message}\n\n")
            f.write("No existing file overwritten; canonical JSON unchanged.\n")
    return 0 if okay else 1


if __name__ == "__main__":
    sys.exit(main())
