#!/usr/bin/env python3
"""Bounded automatic initial preservation for canonical GitHub releases.

The original VitaForge checkpoint stays untouched. All transfers reuse the
tested per-file checkpoint and batched verification functions. Other apps
are selected independently, without waiting for any other app to publish.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
import os
import re
import struct
import time
import tempfile
import zipfile
from collections import Counter
from pathlib import Path
from urllib.parse import unquote, urlparse

from scripts import archive_item_creation as initial
from scripts.archive_backup_batch import (
    ROOT, Deferred, HARD_MAX_FILE, download_source, expected_filename,
    official_asset_metadata,
    git_publish, load_policy, policy_entry, now_utc, public_url, save_canonical,
)
from scripts.archive_backup_batch_preview import catalog_current_asset, upload_map
from scripts.archive_media_backup import fetch_image, safe_media_url
from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import select_current_download, normalized_repo

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
    "mofa-vita": {
        "title_id": "KNIF00540", "name": "mofa-vita - Witch on the Holy Night", "search_title": "mofa-vita",
        "version": "1.20", "repo": "ATSPwang618/mofa-vita", "creator": "ATSPwang618, SpriteLisen, DeQxJ00",
        "tag": "1.20", "asset": "mofa-vita-1.20.vpk", "size": 4938904,
        "sha256": "06a0701c21f5ddccc3570359f10c4a77f86278789fed8c545e441dc9fc43d402",
    },
    "supertuxkart-vita": {
        "title_id": "STKV00001", "name": "SuperTuxKart Vita", "search_title": "SuperTuxKart Vita",
        "version": "1.02", "repo": "edgarlugo/stk-code", "creator": "edgarlugo, Rinnegatamante, SuperTuxKart contributors",
        "tag": "vita-1.02", "asset": "supertuxkart.vpk", "size": 12251364,
        "sha256": "d2359fd4bf98d4dca75d31014f5f2a76bfb7d07f782023453d9b4a85e790a74d",
    },
}
MAX_NEW = 6
MAX_RESUME = 8  # per-run checks, not a cap on outstanding jobs
MAX_VPK_BYTES = 400 * 1024 * 1024  # separate budget per lane
AUTO_SECONDS = 24 * 60
RESUME_SECONDS = 10 * 60
MAX_PREFLIGHT = 20
STOP_BATCH = {"CATALOG_PUSH_BLOCKED", "BATCH_ABORTED"}


def source_url(spec):
    return spec.get("url") or f"https://github.com/{spec['repo']}/releases/download/{spec['tag']}/{spec['asset']}"


def check_candidate(app, app_id, spec, counts):
    if (app.get("id") != app_id or app.get("title_id") != spec["title_id"]
            or app.get("name") != spec["name"] or app.get("status") not in {"Verified", "Legacy"}
            or counts[spec["title_id"].upper()] != 1):
        raise Deferred("PENDING_MANUAL", "App identity/status or Title ID conflict")
    asset, reason = catalog_current_asset(app)
    if asset is None:
        raise Deferred("SOURCE_REVIEW", reason)
    if (asset["version"] != spec["version"] or asset["url"] != source_url(spec)
            ):
        raise Deferred("PENDING_MANUAL", "Current release differs from reviewed release")
    return asset


def source_artwork(url, spec):
    if not safe_media_url(url):
        return False
    u = urlparse(url)
    if u.query or u.fragment or not u.path.lower().startswith("/" + spec["repo"].lower() + "/"):
        return False
    if u.hostname == "raw.githubusercontent.com":
        return True
    # Official release artwork from the SAME upstream repo; the existing
    # bounded image downloader independently validates every redirect/format.
    return (u.hostname == "github.com"
            and len([p for p in u.path.split("/") if p]) == 6
            and u.path.split("/")[3:5] == ["releases", "download"]
            and u.path.lower().endswith((".png", ".jpg", ".jpeg", ".webp")))


def automatic_identity(app, app_id, counts):
    """Local eligibility; never consult a guessed latest release."""
    if (not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,70}", app_id)
            or app.get("id") != app_id or app.get("status") not in {"Verified", "Legacy", "Archive"}):
        raise Deferred("SOURCE_REVIEW", "Unsupported app identity or status")
    tid = app.get("title_id")
    if not isinstance(tid, str) or not tid or counts[tid.upper()] != 1:
        raise Deferred("PENDING_MANUAL", "Missing or duplicate Title ID")
    asset, reason = catalog_current_asset(app)
    if asset is None:
        raise Deferred("SOURCE_REVIEW", reason)
    expected_filename({"app_id": app_id, "version": asset["version"]})
    parts = [unquote(p) for p in urlparse(asset["url"]).path.split("/") if p]
    repo = normalized_repo(asset["url"])
    if len(parts) != 6 or not repo or "/".join(parts[:2]).lower() != repo:
        raise Deferred("SOURCE_REVIEW", "Unsupported GitHub asset identity")
    name = app.get("name")
    if not isinstance(name, str) or not name.strip():
        raise Deferred("SOURCE_REVIEW", "Missing application name")
    title = re.sub(r"[^A-Za-z0-9 ._()-]", " ", name).strip()[:100].strip()
    if not title:
        raise Deferred("SOURCE_REVIEW", "Title requires safe account-search normalization")
    return {"automatic": True, "title_id": tid, "name": name, "search_title": title,
            "version": asset["version"], "repo": "/".join(parts[:2]), "tag": parts[4],
            "asset": parts[5], "url": asset["url"]}


def author_credit(root, app):
    authors = app.get("author_ids")
    if not isinstance(authors, list) or not authors:
        raise Deferred("SOURCE_REVIEW", "Missing canonical author profiles")
    names = []
    for aid in authors:
        if not isinstance(aid, str) or not re.fullmatch(r"[a-zA-Z0-9_.-]+", aid) or aid in {".", ".."}:
            raise Deferred("SOURCE_REVIEW", "Invalid author ID")
        path = root / "authors" / (aid + ".json")
        if not path.is_file():
            raise Deferred("SOURCE_REVIEW", "Missing canonical author profile")
        author = to_v1(json.loads(path.read_text("utf-8")))
        name = author.get("name")
        if not isinstance(name, str) or not name.strip():
            raise Deferred("SOURCE_REVIEW", "Missing author name")
        names.append(name)
    return ", ".join(dict.fromkeys(names))


def artwork_download_url(url, spec):
    """Convert only canonical GitHub blob raw links; keep original provenance."""
    if not spec.get("automatic"):
        return url if source_artwork(url, spec) else None
    if not safe_media_url(url):
        return None
    u = urlparse(url)
    repos = (spec["repo"].lower(), "vegettosan/psvitaalive")
    if not any(u.path.lower().startswith("/" + repo + "/") for repo in repos):
        return None
    if u.hostname == "github.com" and u.query == "raw=true":
        parts = u.path.split("/")
        if len(parts) >= 6 and parts[3] == "blob":
            return "https://raw.githubusercontent.com/" + "/".join(parts[1:3] + parts[4:])
        return None
    if source_artwork(url, spec):
        return url
    if u.hostname == "raw.githubusercontent.com" and not u.query and not u.fragment:
        return url
    return None


def validate_vpk_title(path, expected):
    """Read the bounded SFO without extracting or executing any archive entry."""
    try:
        with zipfile.ZipFile(path) as z:
            if z.namelist().count("sce_sys/param.sfo") != 1 or z.namelist().count("eboot.bin") != 1:
                raise ValueError()
            entry = z.getinfo("sce_sys/param.sfo")
            if not 20 <= entry.file_size <= 65536:
                raise ValueError()
            with z.open(entry) as src:
                data = src.read(65537)
        if len(data) != entry.file_size or data[:4] != b"\x00PSF":
            raise ValueError()
        _, _, keys, values, count = struct.unpack_from("<4sIIII", data)
        if not 0 < count <= 512 or not 20 + count * 16 <= keys <= values <= len(data):
            raise ValueError()
        ids = []
        for i in range(count):
            keyoff, fmt, length, maximum, offset = struct.unpack_from("<HHIII", data, 20 + i*16)
            start = keys + keyoff
            if not keys <= start < values:
                raise ValueError()
            end = data.find(b"\x00", start, values)
            if end < 0:
                raise ValueError()
            if data[start:end] != b"TITLE_ID":
                continue
            if fmt != 0x0204 or not 1 <= length <= maximum or values + offset + length > len(data):
                raise ValueError()
            ids.append(data[values+offset:values+offset+length].rstrip(b"\x00").decode("ascii"))
        if len(ids) != 1 or ids[0].upper() != expected.upper():
            raise ValueError()
    except (ValueError, UnicodeError, struct.error, OSError, RuntimeError, zipfile.BadZipFile, NotImplementedError):
        raise Deferred("PENDING_MANUAL", "VPK Title ID/structure does not match canonical record") from None


def auto_spec(root, app, app_id, counts, job=None):
    identity = automatic_identity(app, app_id, counts)
    if job:
        saved = job.get("source_spec")
        if (not isinstance(saved, dict) or any(saved.get(k) != v for k, v in identity.items())
                or type(saved.get("size")) is not int or not 0 < saved["size"] <= HARD_MAX_FILE
                or not isinstance(saved.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", saved["sha256"])
                or not isinstance(saved.get("creator"), str) or not saved["creator"].strip()):
            raise Deferred("PENDING_MANUAL", "Automatic checkpoint source identity changed")
        return dict(saved)
    size, sha = official_asset_metadata({"source_url": identity["url"]})
    if not 0 < size <= HARD_MAX_FILE:
        raise Deferred("TOO_LARGE_PENDING", "Official VPK exceeds limit")
    spec = dict(identity, size=size, sha256=sha, creator=author_credit(root, app))
    for url in [app.get("icon"), *(app.get("screenshots") or [])]:
        if url and artwork_download_url(url, spec) is None:
            raise Deferred("SOURCE_REVIEW", "Artwork requires a provider-specific adapter")
    return spec


def stage(app, item, folder, spec):
    name = expected_filename(item)
    target = folder / name
    md5, sha = download_source(item, target, max_file=HARD_MAX_FILE)
    if spec["sha256"] is not None and sha != spec["sha256"]:
        raise Deferred("PENDING_MANUAL", "Official GitHub VPK SHA-256 mismatch")
    if spec.get("automatic"):
        validate_vpk_title(target, spec["title_id"])
        # Older GitHub releases may have no official hash; pin the measured
        # SHA-256 before any PUT, after real size and VPK identity checks.
        spec["sha256"] = sha
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
        download_url = artwork_download_url(url, spec)
        if download_url is None:
            raise Deferred("SOURCE_REVIEW", "Artwork not in reviewed upstream repository")
        local = folder / "artwork.tmp"
        info, ext = fetch_image(download_url, local)
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
    if app_id == initial.PILOT or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,70}", app_id):
        raise Deferred("PENDING_MANUAL", "Invalid or reserved app checkpoint identity")
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


def reserved_identifiers(root, app_id):
    """Protect identifiers even before an uncertain PUT becomes publicly visible."""
    reserved = set()
    old = initial.load_job(root)
    if old.get("identifier"):
        reserved.add(old["identifier"].casefold())
    for path in (root / STATE_DIR).glob("*.json"):
        if path.stem == app_id:
            continue
        try:
            job = read_job(root, path.stem)
            identifier = job.get("identifier")
            if not isinstance(identifier, str) or not identifier:
                raise ValueError()
            reserved.add(identifier.casefold())
        except Exception:
            raise Deferred("BATCH_ABORTED", "Cannot establish other checkpoint identifier reservations") from None
    for other, ids in upload_map(root / LOG).items():
        if other != app_id:
            reserved.update(i.casefold() for i in ids)
    return reserved


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
    if spec.get("automatic"):
        identity = automatic_identity(app, app_id, counts)
        if any(spec.get(k) != v for k, v in identity.items()):
            raise Deferred("PENDING_MANUAL", "Canonical source changed during selection")
        asset, _ = catalog_current_asset(app)
    else:
        asset = check_candidate(app, app_id, spec, counts)
    reserved = reserved_identifiers(root, app_id)
    def choose():
        candidate = initial.select_destination(app_id, creds[2],
            search=lambda owner: initial.search_pilot(owner, app_id, spec["search_title"]))
        if candidate.casefold() in reserved:
            raise Deferred("PENDING_MANUAL", "Identifier reserved by another app checkpoint")
        return candidate
    identifier = job.get("identifier") if job else choose()
    if identifier not in initial.identifier_options(app_id) or identifier.casefold() in reserved:
        raise Deferred("PENDING_MANUAL", "Invalid checkpoint identifier")
    item = {"app_id": app_id, "version": asset["version"], "source_url": asset["url"],
            "size_bytes": spec["size"], "archive_identifier": identifier}
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
            if spec.get("automatic"):
                job["source_spec"] = dict(spec)
        initial.transfer_batch(root, app, identifier, files, folder, job, creds,
            checkpoint=lambda r, j: checkpoint(r, app_id, j),
            metadata_factory=lambda a: metadata(a, spec))
        initial.verify_batch(identifier, files, folder, job, creds[2])
        publish(root, path, raw, app, item, files, media, job)
    return "PUBLISHED_VERIFIED"


def automatic_queue(root, creds, counts, *, execute):
    rows = []
    known = upload_map(root / LOG)
    policy = load_policy(root)
    pending, fresh = [], []
    for path in sorted((root / "apps").glob("*.json")):
        app_id = path.stem
        if app_id in APPROVED or app_id == initial.PILOT:
            continue
        app = to_v1(json.loads(path.read_text("utf-8")))
        if app.get("status") not in {"Verified", "Legacy", "Archive"}:
            continue
        try:
            if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,70}", app_id):
                continue
            job = read_job(root, app_id)
            if job.get("status") == "PUBLISHED_VERIFIED":
                continue
            if not job and initial.archive_hints(app, known):
                continue
            if policy_entry(policy, app_id).get("enabled") is not True:
                raise Deferred("EXCLUDED", "Existing exclusion policy")
            automatic_identity(app, app_id, counts)
            (pending if job else fresh).append((app_id, app, job))
        except Deferred as e:
            rows.append({"app_id": app_id, "status": e.status, "detail": e.reason})
    # Report vanished checkpoint sources rather than losing them silently.
    for p in (root / STATE_DIR).glob("*.json"):
        if not (root / "apps" / p.name).is_file():
            rows.append({"app_id": p.stem, "status": "PENDING_MANUAL", "detail": "Canonical app removed; checkpoint retained"})
    if not execute:
        return rows + [{"app_id": a, "status": "RESUME_PREVIEW" if j else "DISCOVERY_REQUIRED"}
                       for a, _, j in pending + fresh]
    # Separate budgets prevent waiting jobs from exhausting new-item capacity.
    # Rotate BOTH lanes so a permanently pending early job cannot starve later ones.
    hour = int(time.time() // 3600)
    for lane, seconds, max_checks in ((pending, RESUME_SECONDS, MAX_RESUME),
                                      (fresh, AUTO_SECONDS, MAX_PREFLIGHT)):
        if lane:
            offset = hour % len(lane)
            lane = lane[offset:] + lane[:offset]
        deadline = time.monotonic() + seconds
        started = checked = used = 0
        for app_id, app, job in lane:
            if (time.monotonic() + 120 >= deadline or checked >= max_checks
                    or (not job and started >= MAX_NEW)):
                rows.append({"app_id": app_id, "status": "DEFERRED",
                             "detail": "Per-run lane budget; retry on later run"})
                continue
            checked += 1
            try:
                spec = auto_spec(root, app, app_id, counts, job)
                if used + spec["size"] > MAX_VPK_BYTES:
                    rows.append({"app_id": app_id, "status": "DEFERRED", "detail": "Real VPK byte budget for lane"})
                    continue
                used += spec["size"]
                status = run_one(root, app_id, spec, creds, counts)
                detail = ""
                if not job and read_job(root, app_id):
                    started += 1
            except Deferred as e:
                status, detail = e.status, e.reason
                # An uncertain PUT has a durable checkpoint and counts against
                # this run's creation quota, but does not stop unrelated apps.
                if not job and status not in STOP_BATCH and read_job(root, app_id):
                    started += 1
            except Exception:
                rows.append({"app_id": app_id, "status": "BATCH_ABORTED",
                             "detail": "Unexpected error; Git/checkpoint state may be unsafe"})
                return rows
            row = {"app_id": app_id, "status": status, "detail": detail}
            rows.append(row)
            print("Automatic initial preservation: " + json.dumps(row), flush=True)
            if status in STOP_BATCH:
                return rows
    return rows

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--execute", action="store_true")
    p.add_argument("--automatic", action="store_true", help="Enable independent bounded automatic selection")
    p.add_argument("--report", type=Path, default=Path("archive-batch-report/new-items-batch.json"))
    args = p.parse_args()
    root = args.root.resolve()
    creds = tuple(os.environ.get(k, "") for k in ("IA_ACCESS_KEY", "IA_SECRET_KEY", "IA_EXPECTED_UPLOADER"))
    if args.execute and (os.environ.get("GITHUB_REPOSITORY") != "VegettoSan/PSVitaAlive"
                         or os.environ.get("GITHUB_REF") != "refs/heads/main" or not all(creds)):
        p.error("Official main and configured secrets required")
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
            status, detail = "BATCH_ABORTED", "Unexpected batch error; stop safely"
            rows.append({"app_id": app_id, "status": status, "detail": detail})
            break
        row = {"app_id": app_id, "status": status, "detail": detail}
        rows.append(row)
        print("Initial preservation: " + json.dumps(row), flush=True)
        if status in STOP_BATCH:
            break
    if args.automatic and not any(r["status"] in STOP_BATCH for r in rows):
        rows.extend(automatic_queue(root, creds, counts, execute=args.execute))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"rows": rows, "mode": "EXECUTE" if args.execute else "DRY_RUN"}, indent=2) + "\n", "utf-8")
    return 1 if any(r["status"] in STOP_BATCH for r in rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
