#!/usr/bin/env python3
"""One-time, fail-closed VPK mirror into an existing PSVitaAlive Archive.org item.

Runs exclusively on the unpublished integration branch. Never creates Archive
items, never overwrites a named file and never alters generated catalog JSON.
The original PR Download URL, version and Game Files are preserved unchanged.
"""
from __future__ import annotations

import csv
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from scripts.archive_backup_batch import (
    Deferred, attempt_upload, existing_file, expected_filename, owned_metadata,
    public_url, stream_download, verify_file_metadata, verify_public_download,
)
from scripts.archive_upload_one_pilot import has_remote_name
from scripts.catalog_schema_v2 import to_v1

ROOT = Path(__file__).resolve().parents[1]
BRANCH = "integration/retroboy-ace-attorney-121"
APP_ID = "phoenix-wright-ace-attorney-trilogy"
TITLE_ID = "OBJECTION"
VERSION = "1.0.0"
SOURCE = "https://archive.org/download/media_20261127/pwaatvita.vpk"
IDENTIFIER = "pwace-attorney-ps-vita"
APP_FILE = Path("apps") / (APP_ID + ".json")
STATE_FILE = Path("data/archive_republish_pw_ace_attorney.json")
LOG_FILE = Path("scripts/archive_upload_log.csv")
MAX_SIZE = 200 * 1024 * 1024


def git_commit(paths, message):
    # This tool is never authorized to update main directly.
    if os.getenv("GITHUB_REF") != "refs/heads/" + BRANCH:
        raise Deferred("PENDING_MANUAL", "Wrong branch for isolated preservation")
    def call(*args):
        r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            raise Deferred("PENDING_MANUAL", "Unable to persist staging checkpoint/change; stop")
    call("config", "user.name", "github-actions[bot]")
    call("config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com")
    call("add", "--", *[str(p) for p in paths])
    check = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=ROOT)
    if check.returncode == 0:
        return
    if check.returncode != 1:
        raise Deferred("PENDING_MANUAL", "Could not read staged changes")
    call("commit", "-m", message)
    call("push", "origin", "HEAD:" + BRANCH)


def source_app():
    raw = json.loads((ROOT / APP_FILE).read_text("utf-8"))
    app = to_v1(raw)
    if (app.get("id") != APP_ID or app.get("title_id") != TITLE_ID
            or app.get("version") != VERSION or app.get("author_ids") != ["bekalife08"]):
        raise Deferred("PENDING_MANUAL", "Unreviewed catalog identity/version; stop")
    vpk = [x for x in app.get("links", []) if x.get("type") == "Download" and x.get("name") == "VPK"]
    if len(vpk) != 1 or vpk[0].get("url") != SOURCE:
        raise Deferred("PENDING_MANUAL", "Original developer VPK source changed")
    if app.get("icon") != public_url(IDENTIFIER, "icon0.png") or app.get("screenshots") != [public_url(IDENTIFIER, "shot0.png")]:
        raise Deferred("PENDING_MANUAL", "Historical Archive.org media references changed")
    return raw, app


def validate_manifest(md, filename):
    names = {x.get("name") for x in md.get("files", []) if isinstance(x, dict)}
    if not {"icon0.png", "shot0.png"} <= names:
        raise Deferred("PENDING_MANUAL", "Historical item does not contain the required icon/screenshot")
    if has_remote_name(md, filename):
        matched = existing_file(md, filename)
        if matched is None:
            raise Deferred("PENDING_MANUAL", "Target filename collides with a different-case file")
        return matched
    return None


def state_file():
    if not (ROOT / STATE_FILE).exists():
        return None
    value = json.loads((ROOT / STATE_FILE).read_text("utf-8"))
    if not isinstance(value, dict) or value.get("app_id") != APP_ID or value.get("identifier") != IDENTIFIER:
        raise Deferred("PENDING_MANUAL", "Existing checkpoint has conflicting identity")
    return value


def put_state(value):
    (ROOT / STATE_FILE).parent.mkdir(parents=True, exist_ok=True)
    (ROOT / STATE_FILE).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", "utf-8")


def finish(raw, state, filename):
    # Never replace the developer's Download; publish the verified mirror alone.
    path = ROOT / APP_FILE
    if json.loads(path.read_text("utf-8")) != raw:
        raise Deferred("PENDING_MANUAL", "App changed during the upload")
    mirror = public_url(IDENTIFIER, filename)
    links = raw.get("l")
    if not isinstance(links, list):
        raise Deferred("PENDING_MANUAL", "Expected compact v2 app links")
    existing = [x for x in links if x.get("t") == "Mirror" and x.get("u") == mirror]
    if not existing:
        links.append({"t": "Mirror", "n": f"Archive.org v{VERSION}", "u": mirror})
        path.write_text(json.dumps(raw, ensure_ascii=False, indent=2) + "\n", "utf-8")
    state["status"] = "PUBLISHED_VERIFIED"
    put_state(state)
    # Historical owned-item mapping for later release preservation.
    with (ROOT / LOG_FILE).open(newline="", encoding="utf-8") as f:
        previous = list(csv.DictReader(f))
    if not any(row.get("app_id") == APP_ID and row.get("identifier") == IDENTIFIER and row.get("status") == "ok" for row in previous):
        with (ROOT / LOG_FILE).open("a", newline="", encoding="utf-8") as f:
            csv.writer(f).writerow([APP_ID, IDENTIFIER, "ok", filename, "", "https://archive.org/details/" + IDENTIFIER])
    git_commit([APP_FILE, STATE_FILE, LOG_FILE], "chore: add verified RetroBoy VPK mirror in existing Archive.org item")
    return "PUBLISHED_VERIFIED"


def run():
    if os.getenv("GITHUB_REF") != "refs/heads/" + BRANCH or os.getenv("GITHUB_REPOSITORY") != "VegettoSan/PSVitaAlive":
        raise Deferred("PENDING_MANUAL", "Not the approved owner integration branch")
    access = os.getenv("IA_ACCESS_KEY", "")
    secret = os.getenv("IA_SECRET_KEY", "")
    owner = os.getenv("IA_EXPECTED_UPLOADER", "")
    if not all(v.strip() for v in (access, secret, owner)):
        raise Deferred("CONFIG_REQUIRED", "Archive.org secret configuration unavailable")
    raw, app = source_app()
    item = {"app_id": APP_ID, "version": VERSION, "archive_identifier": IDENTIFIER}
    filename = expected_filename(item)
    md = owned_metadata(IDENTIFIER, owner)
    remote = validate_manifest(md, filename)
    state = state_file()
    if state and state.get("status") == "PUBLISHED_VERIFIED":
        # Do not report success unless the canonical mirror is present too.
        if any(x.get("type") == "Mirror" and x.get("url") == public_url(IDENTIFIER, filename) for x in app["links"]):
            return "ALREADY_PUBLISHED"
        raise Deferred("PENDING_MANUAL", "Checkpoint published without canonical mirror")
    # File may already exist from a previous uncertain upload. Reconcile only;
    # checkpoint never permits a second PUT without manual intervention.
    if remote is None and state:
        raise Deferred("VERIFYING", "Prepared transfer not visible; will not repeat upload")
    with tempfile.TemporaryDirectory(prefix="psva_pw_republish_") as directory:
        source = Path(directory) / filename
        size, md5, sha = stream_download(SOURCE, source, MAX_SIZE, archive=True)
        if size < 1024 or source.open("rb").read(4) != b"PK\x03\x04":
            raise Deferred("PENDING_MANUAL", "Original PR VPK file is not a valid ZIP/VPK")
        if state and any(state.get(k) != v for k, v in {"filename": filename,"size": size,"md5": md5,"sha256": sha}.items()):
            raise Deferred("PENDING_MANUAL", "Original PR VPK bytes differ from prepared checkpoint")
        if remote is not None:
            if str(remote.get("size")) != str(size) or str(remote.get("md5") or "").lower() != md5:
                raise Deferred("PENDING_MANUAL", "Versioned filename already exists with different bytes")
        else:
            state = {"app_id": APP_ID,"identifier": IDENTIFIER,"filename": filename,"size": size,
                     "md5": md5,"sha256": sha,"source_url": SOURCE,"status": "TRANSFER_PREPARED"}
            put_state(state)
            git_commit([STATE_FILE], "chore: checkpoint RetroBoy Archive.org VPK transfer before upload")
            # Revalidate ownership and absence immediately before the first PUT.
            md = owned_metadata(IDENTIFIER, owner)
            validate_manifest(md, filename)
            if has_remote_name(md, filename):
                raise Deferred("PENDING_MANUAL", "Target filename appeared during pre-upload check")
            attempt_upload(item, source, access, secret)
        verify_file_metadata(IDENTIFIER, filename, size, md5, owner)
        verify_public_download(IDENTIFIER, filename, size, sha)
    return finish(raw, state or {"app_id": APP_ID,"identifier":IDENTIFIER,"filename":filename}, filename)


if __name__ == "__main__":
    try:
        result = run()
    except Deferred as e:
        print(f"RetroBoy Archive.org staging backup: {e.status} — {e.reason}")
        sys.exit(1)
    except Exception:
        print("RetroBoy Archive.org staging backup: PENDING_MANUAL — unexpected error")
        sys.exit(1)
    print(f"RetroBoy Archive.org staging backup: {result} — existing item reused; original download unchanged")
