#!/usr/bin/env python3
"""New-item preservation pilot. Only VitaForge may write; inventory is read-only."""
from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import os
import re
import tempfile
from collections import Counter
from pathlib import Path
from urllib.parse import urlparse, quote

import requests

from scripts.archive_backup_batch import (
    Deferred, ROOT, download_source, expected_filename, existing_file,
    git_publish, load_policy, policy_entry, public_url, save_canonical,
    stream_download, now_utc, HARD_MAX_FILE,
)
from scripts.archive_backup_batch_preview import (
    catalog_current_asset, upload_map, archive_id, item_owned_by_account,
)
from scripts.archive_owner_preflight import fetch_item_metadata
from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import select_current_download

PILOT = "vitaforge"
STATE = Path("data/archive_item_creation_jobs.json")
LOG = Path("scripts/archive_upload_log.csv")
MAX_MEDIA = 8 * 1024 * 1024
PILOT_SHA256 = "fa5a0990bd6858c0cf6d40714f74d520ebc20967ce4820efcfbb23df62c80504"


def archive_hints(app, known):
    urls = [app.get("icon"), *(app.get("screenshots") or [])]
    for link in app.get("links", []):
        if str(link.get("type", "")).casefold() not in {
            "game files", "data files", "plugin", "plugins", "dlc", "update"
        }:
            urls.append(link.get("url"))
    hints = set(known.get(app["id"], set()))
    for url in urls:
        if not isinstance(url, str):
            continue
        host = (urlparse(url).hostname or "").lower()
        if host == "archive.org" or host.endswith(".archive.org"):
            hints.add(archive_id(url) or "unresolved-archive-reference")
    return hints


def inventory(root):
    known = upload_map(root / LOG)
    apps = [to_v1(json.loads(p.read_text("utf-8")))
            for p in sorted((root / "apps").glob("*.json"))]
    counts = Counter(str(a.get("title_id") or "").upper() for a in apps)
    policy = load_policy(root)
    rows = []
    for app in apps:
        if app.get("status") not in {"Verified", "Legacy", "Archive"}:
            continue
        if archive_hints(app, known):
            continue
        asset, reason = catalog_current_asset(app)
        status = "DISCOVERY_REQUIRED" if asset else "SOURCE_REVIEW"
        if not app.get("title_id") or counts[str(app["title_id"]).upper()] != 1:
            status, reason = "PENDING_MANUAL", "missing or duplicate Title ID"
        elif policy_entry(policy, app["id"]).get("enabled") is not True:
            status, reason = "EXCLUDED", "existing exclusion policy"
        elif asset and asset["size"] > HARD_MAX_FILE:
            status, reason = "TOO_LARGE_PENDING", "VPK exceeds existing limit"
        rows.append({"app_id": app["id"], "title_id": app.get("title_id"),
                     "status": status, "reason": reason,
                     "version": app.get("version"), "size": app.get("size")})
    return rows


def lookup(identifier):
    try:
        data = fetch_item_metadata(identifier)
    except Exception:
        raise Deferred("WAITING_ARCHIVE", "Metadata unavailable; absence not confirmed") from None
    if data == {}:
        return None
    if not isinstance(data.get("metadata"), dict) or not isinstance(data.get("files"), list):
        raise Deferred("WAITING_ARCHIVE", "Incomplete metadata; absence not confirmed")
    return data


def search_pilot():
    # Retrieve actual identifiers; ownership is evaluated separately.
    try:
        r = requests.get("https://archive.org/advancedsearch.php", params={
            "q": 'identifier:psvitaalive-vitaforge OR identifier:vitaforge OR title:VitaForge OR identifier:*vitaforge*',
            "fl[]": "identifier", "rows": 100, "output": "json",
        }, timeout=(15, 30), allow_redirects=False)
        r.raise_for_status()
        if r.status_code != 200:
            raise ValueError()
        result = r.json()["response"]
        docs, count = result["docs"], result["numFound"]
        if not isinstance(docs, list) or type(count) is not int or count < 0 or len(docs) != min(count, 100):
            raise ValueError()
        if count > 20:
            raise Deferred("PENDING_MANUAL", "Too many search matches for safe ownership review")
        ids = [doc["identifier"] for doc in docs]
        if any(not isinstance(i, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,99}", i) for i in ids):
            raise ValueError()
        return sorted(set(ids))
    except Deferred:
        raise
    except Exception:
        raise Deferred("WAITING_ARCHIVE", "Search unavailable; do not create") from None


def require_free_destination(identifier, owner, *, fetch=lookup, search=search_pilot):
    """An item from another uploader is not an existing PSVitaAlive backup.

    Exact identifier collisions always block, irrespective of account.
    Missing/ambiguous ownership never permits creation. Only public
    identifiers and categorical ownership labels are logged, not emails.
    """
    exact = fetch(identifier)
    if exact is not None:
        matched = item_owned_by_account(exact, owner)[0]
        print(f"Archive discovery: {identifier} exact identifier occupied; own={matched}", flush=True)
        raise Deferred("PENDING_MANUAL", f"Exact identifier occupied: {identifier}; own={matched}")
    matches = search()
    print(f"Archive discovery: {len(matches)} search identifiers: {', '.join(matches) or 'none'}", flush=True)
    for candidate in matches:
        md = fetch(candidate)
        if md is None:
            raise Deferred("WAITING_ARCHIVE", f"Search metadata not visible: {candidate}")
        uploader = md.get("metadata", {}).get("uploader")
        if not isinstance(uploader, str) or not uploader.strip():
            raise Deferred("PENDING_MANUAL", f"Unknown owner: {candidate}")
        if candidate.casefold() == identifier.casefold():
            raise Deferred("PENDING_MANUAL", f"Exact identifier collision: {candidate}")
        if item_owned_by_account(md, owner)[0]:
            print(f"Archive discovery: {candidate} belongs to configured account", flush=True)
            raise Deferred("PENDING_MANUAL", f"Existing own candidate: {candidate}")
        print(f"Archive discovery: {candidate} belongs to another account; not a destination", flush=True)


def load_job(root):
    path = root / STATE
    if not path.exists():
        return {}
    data = json.loads(path.read_text("utf-8"))
    if not isinstance(data, dict):
        raise Deferred("PENDING_MANUAL", "Invalid creation checkpoint")
    return data


def persist(root, job):
    path = root / STATE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(job, indent=2, ensure_ascii=False) + "\n", "utf-8")
    git_publish(root, [str(STATE)], "chore: checkpoint VitaForge archive creation", validate=False)


def digest(path):
    data = path.read_bytes()
    return {"size": len(data), "md5": hashlib.md5(data).hexdigest(),
            "sha256": hashlib.sha256(data).hexdigest()}


def image_asset(url, folder):
    # Pilot artwork is pinned to an explicit upstream GitHub tag. No arbitrary
    # hosts, redirects, local-network requests, or unbounded download.
    u = urlparse(url)
    if (u.scheme != "https" or u.netloc != "raw.githubusercontent.com"
            or not u.path.lower().startswith("/josephinoo/vitaforge/")
            or u.query or u.fragment):
        raise Deferred("SOURCE_REVIEW", "Unsupported artwork source")
    path = folder / "artwork.tmp"
    try:
        with requests.get(url, stream=True, timeout=(15, 30), allow_redirects=False) as r:
            if r.status_code != 200:
                raise Deferred("WAITING_SOURCE", "Artwork not available")
            size = 0
            with path.open("wb") as out:
                for chunk in r.iter_content(65536):
                    size += len(chunk)
                    if size > MAX_MEDIA:
                        raise Deferred("TOO_LARGE_PENDING", "Artwork exceeds limit")
                    out.write(chunk)
    except requests.RequestException:
        raise Deferred("WAITING_SOURCE", "Artwork download interrupted") from None
    with path.open("rb") as f:
        magic = f.read(12)
    if magic.startswith(b"\x89PNG\r\n\x1a\n"):
        ext = ".png"
    elif magic.startswith(b"\xff\xd8\xff"):
        ext = ".jpg"
    elif magic.startswith(b"RIFF") and magic[8:12] == b"WEBP":
        ext = ".webp"
    else:
        raise Deferred("SOURCE_REVIEW", "Artwork is not a recognized image")
    return path, ext


def stage(app, item, folder):
    name = expected_filename(item)
    vpk = folder / name
    md5, sha256 = download_source(item, vpk, max_file=HARD_MAX_FILE)
    if app["version"] == "0.1.15" and sha256 != PILOT_SHA256:
        raise Deferred("PENDING_MANUAL", "VitaForge release digest mismatch")
    files = {name: {"size": item["size_bytes"], "md5": md5, "sha256": sha256}}
    media = {}
    media_digests = {}
    entries = [("icon0", app.get("icon"))] + [
        (f"shot_{i:02}", url) for i, url in enumerate(app.get("screenshots") or [], 1)]
    for prefix, url in entries:
        if not url or url in media:
            continue
        temp, ext = image_asset(url, folder)
        info = digest(temp)
        if info["sha256"] in media_digests:
            media[url] = media_digests[info["sha256"]]
            temp.unlink()
            continue
        filename = prefix + ext
        temp.rename(folder / filename)
        files[filename] = info
        media_digests[info["sha256"]] = filename
        media[url] = filename
    source = folder / "SOURCES.txt"
    tag = item["source_url"].split("/releases/download/", 1)[1].split("/", 1)[0]
    source.write_text(
        "VitaForge - preserved by PSVitaAlive\n"
        "Original developer: josephinoo\n"
        f"Original binary: {item['source_url']}\n"
        f"Corresponding source: https://github.com/josephinoo/vitaForge/tree/{tag}\n"
        f"Source archive: https://github.com/josephinoo/vitaForge/archive/refs/tags/{tag}.tar.gz\n"
        + "".join(f"{filename}: {url}\n" for url, filename in media.items()),
        encoding="utf-8")
    files[source.name] = digest(source)
    return files, media


def require_owner(md, owner):
    if not item_owned_by_account(md, owner)[0]:
        raise Deferred("PENDING_MANUAL", "Item ownership not confirmed")


def verify_remote(identifier, name, info, md, folder):
    remote = existing_file(md, name)
    if remote is None:
        raise Deferred("VERIFYING", "Checkpointed file not visible; no duplicate PUT")
    if str(remote.get("size")) != str(info["size"]) or remote.get("md5", "").lower() != info["md5"]:
        raise Deferred("PENDING_MANUAL", "Remote filename/checksum conflict")
    target = folder / "public-verification.tmp"
    size, _, sha = stream_download(public_url(identifier, name), target, info["size"], archive=True)
    if size != info["size"] or sha != info["sha256"]:
        raise Deferred("PENDING_MANUAL", "Public bytes differ from source")
    target.unlink()


def put_file(identifier, name, folder, creds, metadata=None):
    import internetarchive
    kwargs = {"key": name, "access_key": creds[0], "secret_key": creds[1],
              "verify": True, "retries": 0, "queue_derive": False,
              "verbose": False, "request_kwargs": {"timeout": 110}}
    if metadata is not None:
        kwargs["metadata"] = metadata
    try:
        response = internetarchive.get_item(identifier).upload_file(str(folder / name), **kwargs)
        if getattr(response, "status_code", None) not in (200, 201, 204):
            raise ValueError()
    except Exception:
        raise Deferred("UPLOAD_RESULT_UNCERTAIN", "PUT may have completed; checkpoint retained") from None


def publish_complete(root, app, raw, item, files, media, job):
    path = root / "apps" / (PILOT + ".json")
    current = json.loads(path.read_text("utf-8"))
    if current != raw:
        raise Deferred("PENDING_MANUAL", "Canonical record changed during preservation")
    link, _ = select_current_download(app["links"])
    link["url"] = public_url(item["archive_identifier"], expected_filename(item))
    if app.get("icon") in media:
        app["icon"] = public_url(item["archive_identifier"], media[app["icon"]])
    app["screenshots"] = [public_url(item["archive_identifier"], media[u]) if u in media else u
                          for u in app.get("screenshots", [])]
    save_canonical(path, app, "tid" in raw and "title_id" not in raw)
    with (root / LOG).open("a", newline="", encoding="utf-8") as f:
        csv.writer(f).writerow([PILOT, item["archive_identifier"], "ok",
                               ";".join(files), "",
                               "https://archive.org/details/" + item["archive_identifier"]])
    job["status"] = "PUBLISHED_VERIFIED"
    job["completed_at"] = now_utc()
    (root / STATE).write_text(json.dumps(job, indent=2, ensure_ascii=False) + "\n", "utf-8")
    git_publish(root, [str(path.relative_to(root)), str(LOG), str(STATE)],
                "chore: publish verified VitaForge preservation item", validate=True)


def run_pilot(root, creds):
    raw = json.loads((root / "apps" / (PILOT + ".json")).read_text("utf-8"))
    app = to_v1(raw)
    job = load_job(root)
    if job.get("status") == "PUBLISHED_VERIFIED":
        return "ALREADY_PUBLISHED"
    if policy_entry(load_policy(root), PILOT).get("enabled") is not True:
        raise Deferred("EXCLUDED", "Existing exclusion policy")
    if app.get("id") != PILOT or app.get("title_id") != "VITAFORGE":
        raise Deferred("PENDING_MANUAL", "Pilot identity mismatch")
    if sum(to_v1(json.loads(p.read_text("utf-8"))).get("title_id", "").upper() == "VITAFORGE"
           for p in (root / "apps").glob("*.json")) != 1:
        raise Deferred("PENDING_MANUAL", "Duplicate pilot Title ID")
    if archive_hints(app, upload_map(root / LOG)):
        raise Deferred("PENDING_MANUAL", "Existing archive reference; no new item")
    asset, reason = catalog_current_asset(app)
    if asset is None:
        raise Deferred("SOURCE_REVIEW", reason)
    if asset["version"] != "0.1.15":
        raise Deferred("PENDING_MANUAL", "Pilot is pinned to reviewed release 0.1.15")
    identifier = "psvitaalive-" + PILOT
    item = {"app_id": PILOT, "version": asset["version"], "source_url": asset["url"],
            "size_bytes": asset["size"], "archive_identifier": identifier}
    md = lookup(identifier)
    if job:
        if job.get("identifier") != identifier or job.get("source_url") != asset["url"]:
            raise Deferred("PENDING_MANUAL", "Creation checkpoint does not match current source")
        if md is None:
            raise Deferred("VERIFYING", "Creation checkpoint exists; item not visible yet")
        require_owner(md, creds[2])
    else:
        require_free_destination(identifier, creds[2])
    with tempfile.TemporaryDirectory(prefix="psva_create_") as td:
        folder = Path(td)
        files, media = stage(app, item, folder)
        if job:
            if job.get("files") != files or job.get("media") != media:
                raise Deferred("PENDING_MANUAL", "Source payload changed since checkpoint")
        else:
            # Check once more immediately before persisting intent/creating.
            require_free_destination(identifier, creds[2])
            job = {"identifier": identifier, "source_url": asset["url"],
                   "files": files, "media": media, "attempted": [],
                   "status": "CREATING", "created_at": now_utc()}
        for name, info in files.items():
            md = lookup(identifier)
            if md is not None:
                require_owner(md, creds[2])
                remote = existing_file(md, name)
                if remote is not None:
                    if name not in job["attempted"]:
                        raise Deferred("PENDING_MANUAL", "Unplanned existing remote filename")
                    verify_remote(identifier, name, info, md, folder)
                    continue
            if name in job["attempted"]:
                raise Deferred("VERIFYING", "Previous PUT not visible; no automatic repeat")
            create = not job["attempted"]
            if create and md is not None:
                raise Deferred("PENDING_MANUAL", "Identifier became occupied")
            if not create and md is None:
                raise Deferred("VERIFYING", "Owned item not visible")
            job["attempted"].append(name)
            persist(root, job)
            metadata = None
            if create:
                description = html.escape(app.get("long_description") or app.get("description") or "")
                description += '<br><br>Preserved by <a href="https://vegettosan.github.io/PSVitaAlive/">PSVitaAlive</a>.'
                metadata = {"title": app["name"], "creator": "josephinoo",
                            "description": description, "mediatype": "software",
                            "subject": ["PS Vita", "Homebrew", "PSVitaAlive", "VitaForge"],
                            "date": app.get("version_date"), "source": "https://github.com/josephinoo/vitaForge"}
            put_file(identifier, name, folder, creds, metadata)
            # IA may take time to expose files: next invocation reconciles only.
            md = lookup(identifier)
            if md is None:
                raise Deferred("VERIFYING", "New item not visible yet")
            require_owner(md, creds[2])
            verify_remote(identifier, name, info, md, folder)
        publish_complete(root, app, raw, item, files, media, job)
    return "PUBLISHED_VERIFIED"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--execute", action="store_true")
    p.add_argument("--report", type=Path, default=Path("archive-batch-report/new-items.json"))
    args = p.parse_args()
    root = args.root.resolve()
    rows = inventory(root)
    report = {"inventory": rows, "counts": dict(Counter(r["status"] for r in rows)),
              "pilot": PILOT, "status": "PREVIEW", "mode": "EXECUTE" if args.execute else "DRY_RUN"}
    if args.execute:
        creds = tuple(os.environ.get(k, "") for k in ("IA_ACCESS_KEY", "IA_SECRET_KEY", "IA_EXPECTED_UPLOADER"))
        if (os.environ.get("GITHUB_REPOSITORY") != "VegettoSan/PSVitaAlive"
                or os.environ.get("GITHUB_REF") != "refs/heads/main" or not all(creds)):
            p.error("Official main and configured secrets required")
        try:
            report["status"] = run_pilot(root, creds)
        except Deferred as exc:
            report.update(status=exc.status, detail=exc.reason)
        except Exception:
            report.update(status="PENDING_MANUAL", detail="Unexpected pilot error; checkpoint retained")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", "utf-8")
    summary = f"New Archive.org items: VitaForge — {report['status']}; inventory: {report['counts']}"
    print(summary, flush=True)
    if report.get("detail"):
        print("Pilot detail: " + report["detail"], flush=True)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write("## " + summary + "\n")
            if report.get("detail"):
                f.write(report["detail"] + "\n")
    return 1 if report["status"] == "CATALOG_PUSH_BLOCKED" else 0


if __name__ == "__main__":
    raise SystemExit(main())
