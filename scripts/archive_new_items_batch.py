#!/usr/bin/env python3
"""Bounded next-phase pilot for two reviewed GitHub releases.

The original VitaForge checkpoint stays untouched. All transfers reuse the
tested per-file checkpoint and batched verification functions. Other apps
remain inventory-only until both new items have been verified.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
import os
import tempfile
import zipfile
from collections import Counter
from pathlib import Path
from urllib.parse import urlparse

from scripts import archive_item_creation as initial
from scripts.archive_backup_batch import (
    ROOT, Deferred, HARD_MAX_FILE, download_source, expected_filename,
    git_publish, load_policy, policy_entry, now_utc, public_url, save_canonical,
)
from scripts.archive_backup_batch_preview import catalog_current_asset, upload_map
from scripts.archive_media_backup import fetch_image, safe_media_url
from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import select_current_download

LOG = initial.LOG
STATE_DIR = Path("data/archive_new_item_jobs")
APPROVED = {
    "tntv": {
        "title_id": "NTET00001", "name": "TNTV - The New Tetris", "search_title": "TNTV",
        "version": "1.05", "repo": "T64NCollection/TNTV", "creator": "T64NCollection",
        "tag": "v1.05", "asset": "TNTV_v1.05.vpk", "size": 746058,
        "sha256": "cb299a5fe87660da4f4879a41b717333f61216ec6236d727f14c9f817719ca0f",
    },
    "vitatwitch": {
        "title_id": "VTWI00001", "name": "VitaTwitch", "search_title": "VitaTwitch",
        "version": "1.32", "repo": "Dexlei/VitaTwitch", "creator": "Dexlei",
        "tag": "v1.32", "asset": "VitaTwitch.vpk", "size": 2221609,
        "sha256": "bffce9ac4a96c30cb4b2e9125edc279ae467e80b1c4257bc5f3e54b037d91c1e",
    },
}


def source_url(spec):
    return f"https://github.com/{spec['repo']}/releases/download/{spec['tag']}/{spec['asset']}"


def check_candidate(app, app_id, spec, counts):
    if (app.get("id") != app_id or app.get("title_id") != spec["title_id"]
            or app.get("name") != spec["name"] or app.get("status") not in {"Verified", "Legacy"}
            or counts[spec["title_id"]] != 1):
        raise Deferred("PENDING_MANUAL", "App identity/status or Title ID conflict")
    asset, reason = catalog_current_asset(app)
    if asset is None:
        raise Deferred("SOURCE_REVIEW", reason)
    if (asset["version"] != spec["version"] or asset["url"] != source_url(spec)
            or asset["size"] != spec["size"]):
        raise Deferred("PENDING_MANUAL", "Current release differs from reviewed release")
    return asset


def source_artwork(url, spec):
    if not safe_media_url(url):
        return False
    u = urlparse(url)
    return (u.hostname == "raw.githubusercontent.com"
            and u.path.lower().startswith("/" + spec["repo"].lower() + "/")
            and not u.query and not u.fragment)


def stage(app, item, folder, spec):
    name = expected_filename(item)
    target = folder / name
    md5, sha = download_source(item, target, max_file=HARD_MAX_FILE)
    if sha != spec["sha256"]:
        raise Deferred("PENDING_MANUAL", "Official GitHub VPK SHA-256 mismatch")
    try:
        with zipfile.ZipFile(target) as z:
            if "sce_sys/param.sfo" not in z.namelist() or "eboot.bin" not in z.namelist():
                raise ValueError()
    except (ValueError, zipfile.BadZipFile):
        raise Deferred("PENDING_MANUAL", "Source is not a complete VPK") from None
    files = {name: {"size": item["size_bytes"], "md5": md5, "sha256": sha}}
    media, by_hash = {}, {}
    entries = [("icon0", app.get("icon"))] + [
        (f"shot_{i:02}", u) for i, u in enumerate(app.get("screenshots") or [], 1)]
    if len(entries) > 6:
        raise Deferred("PENDING_MANUAL", "Too many screenshots")
    for prefix, url in entries:
        if not url or url in media:
            continue
        if not source_artwork(url, spec):
            raise Deferred("SOURCE_REVIEW", "Artwork not in reviewed upstream repository")
        local = folder / "artwork.tmp"
        info, ext = fetch_image(url, local)
        if info["sha256"] in by_hash:
            media[url] = by_hash[info["sha256"]]
            local.unlink()
            continue
        filename = prefix + ext
        local.rename(folder / filename)
        files[filename] = info
        by_hash[info["sha256"]] = filename
        media[url] = filename
    repo = "https://github.com/" + spec["repo"]
    source = folder / "SOURCES.txt"
    source.write_text(
        f"{app['name']} - preserved by PSVitaAlive\n"
        f"Original developer: {spec['creator']}\n"
        f"Original binary: {item['source_url']}\n"
        f"Upstream release: {repo}/releases/tag/{spec['tag']}\n"
        f"Upstream source: {repo}/tree/{spec['tag']}\n"
        + "".join(f"{filename}: {url}\n" for url, filename in media.items()), "utf-8")
    files[source.name] = initial.digest(source)
    return files, media


def metadata(app, spec):
    return {"title": app["name"], "creator": spec["creator"],
            "description": html.escape(app.get("long_description") or app.get("description") or "")
            + '<br><br>Preserved by <a href="https://vegettosan.github.io/PSVitaAlive/">PSVitaAlive</a>.',
            "mediatype": "software", "subject": ["PS Vita", "Homebrew", "PSVitaAlive", spec["search_title"]],
            "date": app.get("version_date"), "source": "https://github.com/" + spec["repo"]}


def job_path(app_id):
    if app_id not in APPROVED:
        raise Deferred("PENDING_MANUAL", "App outside reviewed batch")
    return STATE_DIR / (app_id + ".json")


def read_job(root, app_id):
    path = root / job_path(app_id)
    if not path.exists():
        return {}
    job = json.loads(path.read_text("utf-8"))
    if not isinstance(job, dict) or job.get("app_id") != app_id:
        raise Deferred("PENDING_MANUAL", "Invalid batch checkpoint")
    return job


def write_job(root, app_id, job):
    path = root / job_path(app_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(job, ensure_ascii=False, indent=2) + "\n", "utf-8")


def checkpoint(root, app_id, job):
    write_job(root, app_id, job)
    git_publish(root, [str(job_path(app_id))], f"chore: checkpoint initial preservation for {app_id}", validate=False)


def publish(root, path, raw, app, item, files, media, job):
    app_id = item["app_id"]
    identifier = item["archive_identifier"]
    if json.loads(path.read_text("utf-8")) != raw:
        raise Deferred("PENDING_MANUAL", "Canonical record changed")
    if initial.archive_hints(app, upload_map(root / LOG)):
        raise Deferred("PENDING_MANUAL", "Existing archive reference appeared")
    link, _ = select_current_download(app["links"])
    if not link or link["url"] != item["source_url"]:
        raise Deferred("PENDING_MANUAL", "Primary source changed")
    link["url"] = public_url(identifier, expected_filename(item))
    if app.get("icon") in media:
        app["icon"] = public_url(identifier, media[app["icon"]])
    app["screenshots"] = [public_url(identifier, media[u]) if u in media else u
                          for u in app.get("screenshots", [])]
    save_canonical(path, app, "tid" in raw and "title_id" not in raw)
    with (root / LOG).open("a", newline="", encoding="utf-8") as f:
        csv.writer(f).writerow([app_id, identifier, "ok", ";".join(files), "",
                               "https://archive.org/details/" + identifier])
    job.update(status="PUBLISHED_VERIFIED", completed_at=now_utc())
    write_job(root, app_id, job)
    git_publish(root, [str(path.relative_to(root)), str(LOG), str(job_path(app_id))],
                f"chore: publish verified initial preservation for {app_id}", validate=True)


def run_one(root, app_id, spec, creds, counts):
    path = root / "apps" / (app_id + ".json")
    raw = json.loads(path.read_text("utf-8"))
    app = to_v1(raw)
    job = read_job(root, app_id)
    if job.get("status") == "PUBLISHED_VERIFIED":
        return "ALREADY_PUBLISHED"
    if policy_entry(load_policy(root), app_id).get("enabled") is not True:
        raise Deferred("EXCLUDED", "Existing exclusion policy")
    if initial.archive_hints(app, upload_map(root / LOG)):
        raise Deferred("PENDING_MANUAL", "Existing archive reference; no new item")
    asset = check_candidate(app, app_id, spec, counts)
    def choose():
        return initial.select_destination(app_id, creds[2],
            search=lambda owner: initial.search_pilot(owner, app_id, spec["search_title"]))
    identifier = job.get("identifier") if job else choose()
    if identifier not in initial.identifier_options(app_id):
        raise Deferred("PENDING_MANUAL", "Invalid checkpoint identifier")
    item = {"app_id": app_id, "version": asset["version"], "source_url": asset["url"],
            "size_bytes": asset["size"], "archive_identifier": identifier}
    if job:
        files, media = initial.validate_job(job, app, item, spec["sha256"])
        if set(job["attempted"]) == set(files):
            with tempfile.TemporaryDirectory(prefix="psva_verify_") as td:
                initial.verify_batch(identifier, files, Path(td), job, creds[2])
            publish(root, path, raw, app, item, files, media, job)
            return "PUBLISHED_VERIFIED"
        if job["attempted"]:
            initial.wait_owned_item(identifier, creds[2])
    with tempfile.TemporaryDirectory(prefix="psva_new_") as td:
        folder = Path(td)
        staged_files, staged_media = stage(app, item, folder, spec)
        if job:
            if files != staged_files or media != staged_media:
                raise Deferred("PENDING_MANUAL", "Source bytes changed since checkpoint")
        else:
            identifier = choose()
            item["archive_identifier"] = identifier
            files, media = staged_files, staged_media
            job = {"app_id": app_id, "identifier": identifier, "source_url": asset["url"],
                   "files": files, "media": media, "attempted": [], "status": "CREATING",
                   "created_at": now_utc()}
        initial.transfer_batch(root, app, identifier, files, folder, job, creds,
            checkpoint=lambda r, j: checkpoint(r, app_id, j),
            metadata_factory=lambda a: metadata(a, spec))
        initial.verify_batch(identifier, files, folder, job, creds[2])
        publish(root, path, raw, app, item, files, media, job)
    return "PUBLISHED_VERIFIED"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--execute", action="store_true")
    p.add_argument("--report", type=Path, default=Path("archive-batch-report/new-items-batch.json"))
    args = p.parse_args()
    root = args.root.resolve()
    creds = tuple(os.environ.get(k, "") for k in ("IA_ACCESS_KEY", "IA_SECRET_KEY", "IA_EXPECTED_UPLOADER"))
    if args.execute and (os.environ.get("GITHUB_REPOSITORY") != "VegettoSan/PSVitaAlive"
                         or os.environ.get("GITHUB_REF") != "refs/heads/main" or not all(creds)):
        p.error("Official main and configured secrets required")
    if args.execute and initial.load_job(root).get("status") != "PUBLISHED_VERIFIED":
        p.error("VitaForge pilot must be fully published first")
    counts = Counter(str(to_v1(json.loads(path.read_text("utf-8"))).get("title_id") or "").upper()
                     for path in (root / "apps").glob("*.json"))
    rows = []
    for app_id, spec in APPROVED.items():
        try:
            if args.execute:
                status = run_one(root, app_id, spec, creds, counts)
            else:
                status = "ALREADY_PUBLISHED" if read_job(root, app_id).get("status") == "PUBLISHED_VERIFIED" else "PREVIEW"
            detail = ""
        except Deferred as exc:
            status, detail = exc.status, exc.reason
        except Exception:
            status, detail = "PENDING_MANUAL", "Unexpected batch error; stop safely"
            rows.append({"app_id": app_id, "status": status, "detail": detail})
            break
        row = {"app_id": app_id, "status": status, "detail": detail}
        rows.append(row)
        print("Initial preservation: " + json.dumps(row), flush=True)
        if status in {"CATALOG_PUSH_BLOCKED", "UPLOAD_RESULT_UNCERTAIN", "WAITING_ARCHIVE"}:
            break
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"rows": rows, "mode": "EXECUTE" if args.execute else "DRY_RUN"}, indent=2) + "\n", "utf-8")
    return 1 if any(r["status"] == "CATALOG_PUSH_BLOCKED" for r in rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
