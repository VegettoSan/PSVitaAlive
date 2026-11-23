#!/usr/bin/env python3
"""Preserve approved PSVitaAlive VPK releases in owned Archive.org items, in batches.

All eligible apps with owned existing Archive.org items can be uploaded,
except explicit opt-outs. Candidates originate from the CURRENT canonical
Download/VPK URL, without waiting for an Orbit release-snapshot match.
Existing items only; no overwrite or deletion.
Each attempted upload gets a durable Git checkpoint BEFORE IA-S3 is called.
Never touch generated catalog.json, authors.json or categories.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote, urljoin, urlparse

import requests

from scripts.archive_backup_batch_preview import (
    candidates, item_owned_by_account, resolve_item, upload_map,
)
from scripts.archive_owner_preflight import fetch_item_metadata
from scripts.archive_upload_one_pilot import has_remote_name, safe_redirect
from scripts.catalog_schema_v2 import to_v1, to_v2
from scripts.sync_release_updates import (
    app_repositories, normalized_repo, select_current_download,
)

ROOT = Path(__file__).resolve().parents[1]
POLICY = Path("config/archive_backup_approved_apps.json")
STATE = Path("data/archive_backup_jobs.json")
MIB = 1024 * 1024
HARD_MAX_FILE = 200 * MIB
HARD_MAX_BATCH = 800 * MIB
HARD_MAX_FILES = 12
VALID_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,90}$")
VALID_VERSION = re.compile(r"^(?:v\.?)?[0-9]+(?:\.[0-9]+){0,3}(?:[-._]?(?:dev|test|alpha|beta|pre|preview|rc)[-._]?[0-9]*)?$", re.I)
REPO_PATH = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")


class Deferred(Exception):
    def __init__(self, status: str, reason: str):
        super().__init__(reason)
        self.status, self.reason = status, reason


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def load_policy(root: Path) -> dict:
    """Use an explicit opt-out list; no per-app license or whitelist gate.

    The asterisk is a default entry for all otherwise eligible canonical
    apps. Exclusions are deliberately preserved across release versions.
    """
    obj = json.loads((root / POLICY).read_text("utf-8"))
    excluded = obj.get("excluded_apps")
    if (obj.get("schema_version") != 2 or obj.get("default_enabled") is not True
            or not isinstance(excluded, list)
            or any(not isinstance(app_id, str) or not VALID_ID.fullmatch(app_id)
                   for app_id in excluded)
            or len(excluded) != len(set(excluded))):
        raise ValueError("Invalid automatic Archive.org upload policy")
    policy = {"*": {"enabled": True}}
    policy.update({app_id: {"enabled": False} for app_id in excluded})
    return policy


def load_state(root: Path) -> dict:
    path = root / STATE
    if not path.exists():
        return {"schema_version": 1, "inflight": {}}
    obj = json.loads(path.read_text("utf-8"))
    if obj.get("schema_version") != 1 or not isinstance(obj.get("inflight"), dict):
        raise ValueError("Corrupt backup checkpoint. Stop without uploads.")
    return obj


def write_state(root: Path, obj: dict) -> None:
    path = root / STATE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def git_publish(root: Path, paths: list[str], message: str, *, validate: bool) -> None:
    """No upload starts unless its checkpoint was already pushed to main."""
    def git(*args):
        return subprocess.run(["git", *args], cwd=root, text=True, capture_output=True)
    if validate:
        env = dict(os.environ, VITAHUB_SKIP_REMOTE_CHECKS="true", VITAHUB_SKIP_REMOTE_MEDIA="true")
        r = subprocess.run([sys.executable, "scripts/validate_catalog_ci.py"],
                           cwd=root, capture_output=True, text=True, env=env)
        if r.returncode:
            raise Deferred("CATALOG_PUSH_BLOCKED", "Canonical validation failed; stop batch to avoid unsafe later commits")
    r = git("add", "--", *paths)
    if r.returncode:
        raise Deferred("CATALOG_PUSH_BLOCKED", "Unable to stage canonical/checkpoint changes; stop batch")
    r = git("diff", "--cached", "--quiet")
    if r.returncode == 0:
        return
    if r.returncode != 1:
        raise Deferred("CATALOG_PUSH_BLOCKED", "Unable to inspect Git staged changes; stop batch")
    for args in (("commit", "-m", message), ("push", "origin", "HEAD:main")):
        r = git(*args)
        if r.returncode:
            raise Deferred("CATALOG_PUSH_BLOCKED", "GitHub main changed or rejected commit; stop batch before any other uploads")


def expected_filename(item: dict) -> str:
    app_id = item["app_id"]
    version = item["version"]
    if not VALID_ID.fullmatch(app_id) or not isinstance(version, str) or not VALID_VERSION.fullmatch(version):
        raise Deferred("PENDING_MANUAL", "Unsupported VPK filename/version")
    label = version if version.casefold().startswith("v") else "v" + version
    return f"{app_id} {label}.vpk"


def policy_entry(policy: dict, app_id: str) -> dict:
    """Absent app entries inherit the automatic default; explicit opt-outs win."""
    return policy.get(app_id, policy.get("*", {}))


def source_matches_approved(item: dict, policy: dict, app: dict) -> bool:
    """Source/ownership gates remain strict; SPDX metadata is not an upload gate."""
    entry = policy_entry(policy, item["app_id"])
    if not isinstance(entry, dict) or entry.get("enabled") is not True:
        return False
    url = item.get("source_url")
    repo = normalized_repo(url)
    if not repo or repo not in app_repositories(app):
        return False
    # Reject contradictory primary repository metadata rather than trusting a
    # stale Repository link in the same app. Missing source_url may rely on links.
    canonical_repo = normalized_repo(app.get("source_url"))
    if canonical_repo is not None and canonical_repo != repo:
        return False
    parsed = urlparse(url)
    if parsed.hostname != "github.com" or "/releases/download/" not in parsed.path:
        return False
    # A policy entry may specify one exact repository as a stricter override.
    pinned = entry.get("github_repo")
    if pinned is not None and (not isinstance(pinned, str)
                              or not REPO_PATH.fullmatch(pinned)
                              or pinned.lower() != repo):
        return False
    # Real managed items are not always named "psvitaalive-*".
    # Their identifier comes either from the unique historical upload log
    # or matching canonical icon/screenshots. Neither is sufficient to upload:
    # run_one() independently checks metadata.uploader against the PRIVATE
    # owner secret before downloading or writing anything.
    identifier = item.get("archive_identifier")
    return (isinstance(identifier, str)
            and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,100}", identifier) is not None)


def owned_metadata(identifier: str, owner: str, *, fetch=fetch_item_metadata) -> dict:
    try:
        data = fetch(identifier)
    except Exception:
        raise Deferred("WAITING_ARCHIVE", "Archive.org temporarily unavailable") from None
    valid, _ = item_owned_by_account(data, owner)
    if not valid:
        raise Deferred("PENDING_MANUAL", "Archive.org item ownership not confirmed")
    if not isinstance(data.get("files"), list):
        raise Deferred("WAITING_ARCHIVE", "Archive.org file manifest unavailable")
    return data


def existing_file(metadata: dict, filename: str) -> dict | None:
    if not has_remote_name(metadata, filename):
        return None
    exact = [f for f in metadata["files"]
             if isinstance(f, dict) and f.get("name") == filename]
    if len(exact) != 1:
        raise Deferred("PENDING_MANUAL", "Archive.org target filename collides with an existing file")
    return exact[0]


def safe_host(url: str, *, archive: bool = False) -> bool:
    try:
        obj = urlparse(url)
        host = (obj.hostname or "").lower()
        if (obj.scheme != "https" or obj.username or obj.password or obj.port
                or not host):
            return False
        return (host == "archive.org" or host.endswith(".archive.org")) if archive else safe_redirect(url)
    except ValueError:
        return False


def stream_download(url: str, filename: Path, maximum: int, *, archive: bool = False,
                    session=None) -> tuple[int, str, str]:
    """Validate EACH redirect before connecting to it; do not fetch arbitrary URLs."""
    sess = session or requests.Session()
    current = url
    response = None
    try:
        for _ in range(6):
            if not safe_host(current, archive=archive):
                raise Deferred("PENDING_MANUAL", "Untrusted asset/redirect host")
            try:
                response = sess.get(current, stream=True, allow_redirects=False,
                                    timeout=(20, 45),
                                    headers={"User-Agent": "PSVitaAlive-Archive-Batch/1.0"})
            except requests.RequestException:
                raise Deferred("WAITING_ARCHIVE" if archive else "WAITING_SOURCE",
                               "Temporary source/download connection problem") from None
            if response.status_code in (301, 302, 303, 307, 308):
                location = response.headers.get("Location")
                response.close()
                if not location:
                    raise Deferred("PENDING_MANUAL", "HTTP redirect without destination")
                current = urljoin(current, location)
                continue
            break
        else:
            raise Deferred("PENDING_MANUAL", "Too many upstream redirects")
        if response.status_code != 200:
            status = "WAITING_ARCHIVE" if archive else "WAITING_SOURCE"
            raise Deferred(status, f"Download not available: HTTP {response.status_code}")
        cl = response.headers.get("Content-Length")
        if cl and (not cl.isdigit() or int(cl) > maximum):
            raise Deferred("TOO_LARGE_PENDING", "Asset exceeds maximum download size")
        md5, sha256, size = hashlib.md5(), hashlib.sha256(), 0
        with filename.open("wb") as out:
            try:
                for chunk in response.iter_content(chunk_size=128 * 1024):
                    if not chunk:
                        continue
                    size += len(chunk)
                    if size > maximum:
                        raise Deferred("TOO_LARGE_PENDING", "Asset exceeded per-file limit")
                    out.write(chunk)
                    md5.update(chunk)
                    sha256.update(chunk)
            except requests.RequestException:
                raise Deferred("WAITING_ARCHIVE" if archive else "WAITING_SOURCE",
                               "Download interrupted") from None
        return size, md5.hexdigest(), sha256.hexdigest()
    finally:
        if response is not None:
            response.close()
        if session is None:
            sess.close()


def download_source(item: dict, local: Path, *, max_file: int) -> tuple[str, str]:
    size, md5, sha256 = stream_download(item["source_url"], local, max_file)
    if size != item["size_bytes"]:
        raise Deferred("PENDING_MANUAL", "Original release differs from monitor asset size")
    with local.open("rb") as f:
        if f.read(4) != b"PK\x03\x04":
            raise Deferred("PENDING_MANUAL", "Source download is not a valid VPK header")
    return md5, sha256


def verify_file_metadata(identifier: str, name: str, expected_bytes: int, md5: str,
                         owner: str, *, sleep=time.sleep) -> None:
    for delay in (0, 5, 12, 20, 28):
        if delay:
            sleep(delay)
        try:
            md = owned_metadata(identifier, owner)
        except Deferred as err:
            if err.status == "WAITING_ARCHIVE":
                continue
            raise
        f = existing_file(md, name)
        if f is None:
            continue
        if (str(f.get("size")) != str(expected_bytes)
                or str(f.get("md5") or "").lower() != md5.lower()):
            raise Deferred("PENDING_MANUAL", "Archive.org target file size/checksum conflicts with source")
        return
    raise Deferred("VERIFYING", "Archive.org upload not yet visible with verified checksum")


def public_url(identifier: str, name: str) -> str:
    return f"https://archive.org/download/{identifier}/{quote(name, safe='')}"


def verify_public_download(identifier: str, name: str, size: int, sha256: str,
                           *, sleep=time.sleep) -> None:
    with tempfile.TemporaryDirectory(prefix="psva_verify_") as folder:
        target = Path(folder) / "verify.vpk"
        for i, delay in enumerate((0, 8, 18, 32, 45)):
            if delay:
                sleep(delay)
            try:
                received, _md5, final_sha = stream_download(
                    public_url(identifier, name), target, size, archive=True)
            except Deferred as err:
                if err.status == "WAITING_ARCHIVE" and i < 4:
                    continue
                raise Deferred("VERIFYING", "Archive.org VPK public download unavailable") from None
            if received != size or final_sha != sha256:
                raise Deferred("PENDING_MANUAL", "Public VPK bytes or SHA256 do not match upstream")
            with target.open("rb") as stream:
                if stream.read(4) != b"PK\x03\x04":
                    raise Deferred("PENDING_MANUAL", "Public VPK header does not match ZIP")
            return
    raise Deferred("VERIFYING", "Archive.org public download still unavailable")


def attempt_upload(item: dict, path: Path, access: str, secret: str) -> None:
    import internetarchive
    try:
        result = internetarchive.get_item(item["archive_identifier"]).upload_file(
            str(path), key=expected_filename(item), access_key=access,
            secret_key=secret, verify=True, retries=0, queue_derive=False,
            verbose=False, request_kwargs={"timeout": 110})
    except Exception:
        raise Deferred("UPLOAD_RESULT_UNCERTAIN", "IA-S3 upload may have started; manual-safe reconciliation only") from None
    code = getattr(result, "status_code", 0)
    if code not in (200, 201, 204):
        raise Deferred("UPLOAD_RESULT_UNCERTAIN", "IA-S3 upload status uncertain")


def candidate_proposal(root: Path, item: dict, proposal_map: dict) -> tuple[Path, dict, bool]:
    path = root / "apps" / (item["app_id"] + ".json")
    raw = json.loads(path.read_text("utf-8"))
    app = to_v1(raw)
    link, _ = select_current_download(app.get("links"))
    allowed_status = ("Legacy", "Verified", "Archive") if item["kind"] == "CATALOG_BACKFILL" else ("Legacy", "Verified")
    if (app.get("id") != item["app_id"] or app.get("title_id") != item["title_id"]
            or app.get("status") not in allowed_status or link is None):
        raise Deferred("PENDING_MANUAL", "Canonical application changed")
    if item["kind"] in ("SAME_VERSION_BACKFILL", "CATALOG_BACKFILL"):
        if app.get("version") != item["version"] or link.get("url") != item["source_url"]:
            raise Deferred("PENDING_MANUAL", "Current VPK is no longer the approved release")
        proposed = app
    elif item["kind"] == "NEW_RELEASE":
        proposed = proposal_map.get(path)
        if not proposed or proposed.get("version") != item["version"]:
            raise Deferred("PENDING_MANUAL", "New upstream version has no trusted catalog proposal")
        future_link, _ = select_current_download(proposed.get("links"))
        if not future_link or future_link.get("url") != item["source_url"]:
            raise Deferred("PENDING_MANUAL", "New upstream VPK link changed")
        # Merge only the exact approved metadata fields; all other links and
        # metadata remain untouched (including Game Files and Repository).
        app["version"] = proposed["version"]
        app["version_date"] = proposed["version_date"]
        app["size"] = proposed["size"]
        if "size" in link:
            link["size"] = proposed["size"]
    else:
        raise Deferred("PENDING_MANUAL", "Unsupported candidate kind")
    link["url"] = public_url(item["archive_identifier"], expected_filename(item))
    compact = "tid" in raw and "title_id" not in raw
    return path, app, compact


def save_canonical(path: Path, app: dict, compact: bool) -> None:
    raw = path.read_text("utf-8")
    payload = to_v2(app) if compact else app
    formatting = {"separators": (",", ":")} if raw.count("\n") <= 1 else {"indent": 2}
    path.write_text(json.dumps(payload, ensure_ascii=False, **formatting) + "\n", encoding="utf-8")


def run_one(root: Path, item: dict, proposals: dict, state: dict,
            credentials: tuple[str, str, str], *, max_file: int,
            publish=git_publish) -> tuple[str, str]:
    access, secret, owner = credentials
    item_id, archive = item["app_id"], item["archive_identifier"]
    name = expected_filename(item)
    _path, proposed, _compact = candidate_proposal(root, item, proposals)
    log_map = upload_map(root / "scripts/archive_upload_log.csv")
    original = to_v1(json.loads((root / "apps" / (item_id + ".json")).read_text("utf-8")))
    target, evidence = resolve_item(original, log_map)
    if target != archive or evidence not in {"historical-upload-log", "catalog-media-candidate"}:
        raise Deferred("PENDING_MANUAL", "No unambiguous candidate Archive.org destination")
    # For catalog-media-candidate, the icon and screenshots are ONLY an item
    # locator. owned_metadata() below verifies the exact private uploader before
    # even downloading the VPK or attempting a write to Archive.org.
    if not source_matches_approved(item, load_policy(root), original):
        raise Deferred("PENDING_MANUAL", "App excluded or source does not match canonical repository")

    md = owned_metadata(archive, owner)
    remote = existing_file(md, name)
    checkpoint = state["inflight"].get(item_id)
    if checkpoint:
        if (checkpoint.get("version") != item["version"]
                or checkpoint.get("filename") != name
                or checkpoint.get("source_url") != item["source_url"]
                or checkpoint.get("archive_identifier") != archive):
            raise Deferred("PENDING_MANUAL", "Older unresolved upload checkpoint needs review")
        if remote is None:
            # Previous IA-S3 PUT may have been accepted. The file has not
            # appeared in the owned item yet, so NEVER send another PUT.
            # Do not re-download the origin VPK either: there is nothing to
            # compare until Archive.org advertises the uploaded filename.
            # VERIFYING is per-file and must not block other catalog updates.
            raise Deferred("VERIFYING",
                           "Checkpoint exists; Archive.org file not visible yet; read-only reconciliation only")

    with tempfile.TemporaryDirectory(prefix="psva_batch_") as td:
        local = Path(td) / "origin.vpk"
        md5, sha256 = download_source(item, local, max_file=max_file)
        if remote is None:
            owned_metadata(archive, owner)  # Ensure Archive.org still responds.
            final_md = owned_metadata(archive, owner)
            if existing_file(final_md, name) is not None:
                raise Deferred("REVIEW", "Target appeared during transfer; rerun to verify safely")
            state["inflight"][item_id] = {
                "version": item["version"], "source_url": item["source_url"],
                "archive_identifier": archive, "filename": name,
                "status": "TRANSFER_PREPARED", "created_at": now_utc(),
            }
            write_state(root, state)
            publish(root, [str(STATE)], f"chore: checkpoint Archive.org transfer for {item_id}", validate=False)
            # Checkpoint is now durably in GitHub. Even if the runner dies,
            # next run will not blindly upload another copy.
            attempt_upload(item, local, access, secret)
            remote = None
        # One of: upload accepted now; or remote file already existed from a
        # previous uncertain run. In both cases require strong checks.
        verify_file_metadata(archive, name, item["size_bytes"], md5, owner)
        verify_public_download(archive, name, item["size_bytes"], sha256)
    path, updated, compact = candidate_proposal(root, item, proposals)
    save_canonical(path, updated, compact)
    state["inflight"].pop(item_id, None)
    write_state(root, state)
    publish(root, [str(path.relative_to(root)), str(STATE)],
            f"chore: publish verified Archive.org VPK for {item_id} v{item['version']}",
            validate=True)
    return "PUBLISHED_VERIFIED", f"{item['size_bytes'] / MIB:.2f} MiB verified; only canonical VPK link/version updated"


def should_stop_batch(status: str) -> bool:
    """A not-yet-public VPK does not block unrelated uploads."""
    return status in {"WAITING_ARCHIVE", "UPLOAD_RESULT_UNCERTAIN", "CATALOG_PUSH_BLOCKED"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--max-files", type=int, default=12)
    parser.add_argument("--max-file-mib", type=int, default=200)
    parser.add_argument("--max-batch-mib", type=int, default=800)
    parser.add_argument("--max-minutes", type=int, default=38)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--report", type=Path, default=Path("archive-batch-report/run.json"))
    args = parser.parse_args()
    if (not 1 <= args.max_files <= HARD_MAX_FILES
            or not 1 <= args.max_file_mib * MIB <= HARD_MAX_FILE
            or not 1 <= args.max_batch_mib * MIB <= HARD_MAX_BATCH
            or not 1 <= args.max_minutes <= 38):
        parser.error("Requested batch limits exceed the supported safe range")
    root = args.root.resolve()
    state = load_state(root)
    policy = load_policy(root)
    # Read the canonical apps/ files directly; Orbit is useful for update
    # detection, not a prerequisite for preserving the current catalog VPK.
    candidates_list, reviews = candidates(root)
    proposal_map = {}
    deadline = time.monotonic() + args.max_minutes * 60
    results = []
    attempts, total = 0, 0
    uploads_ready = (os.environ.get("GITHUB_REPOSITORY") == "VegettoSan/PSVitaAlive"
                     and os.environ.get("GITHUB_REF") == "refs/heads/main")
    if args.execute and not uploads_ready:
        parser.error("Write mode is only permitted in the official main branch")
    creds = tuple(os.environ.get(key, "") for key in
                  ("IA_ACCESS_KEY", "IA_SECRET_KEY", "IA_EXPECTED_UPLOADER"))
    if args.execute and not all(x.strip() for x in creds):
        parser.error("Missing Archive.org secrets for real upload")
    for item in candidates_list:
        app_id = item["app_id"]
        if policy_entry(policy, app_id).get("enabled") is not True:
            continue
        size = item["size_bytes"]
        if size > args.max_file_mib * MIB:
            results.append({"app_id": app_id, "status": "TOO_LARGE_PENDING", "bytes": size})
            continue
        if attempts >= args.max_files or total + size > args.max_batch_mib * MIB:
            results.append({"app_id": app_id, "status": "DEFERRED", "bytes": size})
            continue
        if time.monotonic() + 180 >= deadline:
            results.append({"app_id": app_id, "status": "TIME_BUDGET_PENDING", "bytes": size})
            continue
        attempts += 1
        total += size
        if not args.execute:
            results.append({"app_id": app_id, "status": "SELECTED_PREVIEW", "bytes": size})
            continue
        try:
            status, detail = run_one(root, item, proposal_map, state, creds,
                                     max_file=args.max_file_mib * MIB)
        except Deferred as err:
            status, detail = err.status, err.reason
        except Exception:
            status, detail = "PENDING_MANUAL", "Unexpected error; no automatic retry"
        results.append({"app_id": app_id, "status": status, "bytes": size, "detail": detail})
        print(f"{app_id}: {status} — {detail}", flush=True)
        # validate.yml is triggered by workflow_run only after this batch finishes.
        # Its publisher lock then sees all successfully committed apps at once.
        if should_stop_batch(status):
            # Only systemic outages or uncertain PUTs stop the whole batch.
            # VERIFYING (public 404 during Archive.org processing) is per-file:
            # keep its checkpoint and continue to the next approved candidate.
            break
        time.sleep(8)
    report = {
        "mode": "EXECUTE" if args.execute else "DRY_RUN",
        "limits": {"files": args.max_files, "file_mib": args.max_file_mib,
                   "batch_mib": args.max_batch_mib, "minutes": args.max_minutes},
        "results": results, "review_count": len(reviews),
        "reviews": reviews, "generated_at": now_utc(),
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", "utf-8")
    summary = ["# Archive.org VPK backup batch", "",
               f"Mode: **{report['mode']}** — selected apps only, no third-party items.", "",
               "| App | Result | MiB |", "|---|---|---:|"]
    summary += [f"| {row['app_id']} | {row['status']} | {row['bytes']/MIB:.2f} |"
                for row in results]
    from collections import Counter
    tally = Counter(x["status"] for x in reviews)
    summary += ["", f"Current-catalog backup review notices: **{len(reviews)}**",
                f"By reason class: **{', '.join(f'{name}={amount}' for name, amount in sorted(tally.items())) or 'none'}**",
                "These review notices are not uninstalled apps or missing upstream updates.",
                "See run.json for each app_id and exact reason.",
                "All generated catalogs are owned exclusively by validate.yml."]
    text = "\n".join(summary) + "\n"
    print(text, flush=True)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as fh:
            fh.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
