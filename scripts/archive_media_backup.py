#!/usr/bin/env python3
"""Preserve changed external app icons/screenshots in existing owned items.

Separate from VPK updates and initial item creation. Never creates items,
never overwrites files, never downloads Game/Data Files. Canonical apps/
remain authoritative; generated catalogs are handled by validate.yml.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import tempfile
import time
from collections import Counter
from pathlib import Path
from urllib.parse import urlparse, urljoin

import requests
from PIL import Image

from scripts.archive_backup_batch import (
    ROOT, Deferred, git_publish, load_policy, policy_entry, owned_metadata,
    existing_file, public_url, now_utc, VALID_ID,
)
from scripts.archive_backup_batch_preview import (
    archive_id, media_archive_id, upload_map, resolve_item,
)
from scripts.archive_item_creation import put_file, verify_remote, identifier_options
from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import select_current_download

STATE = Path("data/archive_media_backup_jobs.json")
LOG = Path("scripts/archive_upload_log.csv")
MAX_BYTES = 8 * 1024 * 1024
MAX_PIXELS = 4096 * 4096
# Deliberately finite; new providers can be reviewed/added without changing catalog.
HOSTS = {"raw.githubusercontent.com", "github.com", "media.githubusercontent.com",
         "user-images.githubusercontent.com", "release-assets.githubusercontent.com",
         "objects.githubusercontent.com", "opengraph.githubassets.com"}


def safe_media_url(url):
    try:
        u = urlparse(url)
        return (u.scheme == "https" and u.netloc.lower() in HOSTS
                and not u.username and not u.password and not u.fragment
                and bool(u.path))
    except (ValueError, TypeError, AttributeError):
        return False


def media_urls(app):
    values = [app.get("icon"), *(app.get("screenshots") or [])]
    return list(dict.fromkeys(u for u in values if isinstance(u, str) and u))


def external_urls(app):
    result = []
    for url in media_urls(app):
        try:
            u = urlparse(url)
            host = (u.hostname or "").lower()
        except ValueError:
            continue
        if u.scheme not in {"http", "https"}:
            continue
        if host == "archive.org" or host.endswith(".archive.org"):
            continue
        # Do not mirror generic PSVitaAlive fallback artwork into each item.
        if host == "raw.githubusercontent.com" and "/vegettosan/psvitaalive/" in u.path.lower():
            if re.search(r"/(?:default[-_]?icon|placeholder|appicon|autoricon|no[-_]?image)\.", u.path, re.I):
                continue
        result.append(url)
    return result


def resolve_target(app, known):
    """Historical mapping wins. Otherwise require independent matching clues.

    A primary VPK plus surviving matching artwork, or a conventional
    app-specific identifier on that VPK, can locate a prior owned item.
    Actual ownership is checked before network source reads or any write.
    """
    target, evidence = resolve_item(app, known)
    if target:
        return target, evidence
    if known.get(app["id"]):
        return None, evidence
    link, _ = select_current_download(app.get("links", []))
    primary = archive_id(link.get("url")) if link else None
    refs = {i for u in media_urls(app) if (i := media_archive_id(u))}
    if primary and (refs == {primary} or
                    (not refs and primary in identifier_options(app["id"]))):
        return primary, "primary-vpk-corroborated"
    return None, "No unambiguous managed item; review required"


def rewrite_media(raw, source, destination):
    """Change only exact matching current icon/screenshot URLs, preserving schema."""
    # Canonical records are compact v2 or long v1. No round-trip conversion.
    result = dict(raw)
    for key in ("ic", "icon"):
        if result.get(key) == source:
            result[key] = destination
    for key in ("ss", "screenshots"):
        if isinstance(result.get(key), list):
            result[key] = [destination if u == source else u for u in result[key]]
    return result


def fetch_image(url, target):
    """Bounded download with host checks on every redirect and real decode."""
    current = url
    with requests.Session() as sess:
        # Do not use environment proxy credentials or netrc for untrusted artwork.
        sess.trust_env = False
        for _ in range(6):
            if not safe_media_url(current):
                raise Deferred("SOURCE_REVIEW", "Unsupported media host or redirect")
            try:
                with sess.get(current, stream=True, allow_redirects=False,
                              timeout=(10, 20)) as r:
                    if r.status_code in (301, 302, 303, 307, 308):
                        dest = r.headers.get("Location")
                        if not dest:
                            raise Deferred("SOURCE_REVIEW", "Media redirect has no location")
                        current = urljoin(current, dest)
                        continue
                    if r.status_code != 200:
                        raise Deferred("WAITING_SOURCE", f"Media returned HTTP {r.status_code}")
                    length = r.headers.get("Content-Length")
                    if length and (not length.isdigit() or int(length) > MAX_BYTES):
                        raise Deferred("TOO_LARGE_PENDING", "Media exceeds byte limit")
                    size = 0
                    with target.open("wb") as out:
                        for chunk in r.iter_content(65536):
                            size += len(chunk)
                            if size > MAX_BYTES:
                                raise Deferred("TOO_LARGE_PENDING", "Media exceeded byte limit")
                            out.write(chunk)
                    break
            except requests.RequestException:
                raise Deferred("WAITING_SOURCE", "Media download interrupted") from None
        else:
            raise Deferred("SOURCE_REVIEW", "Too many media redirects")
    return image_info(target)


def image_info(path):
    try:
        with Image.open(path) as img:
            fmt = img.format
            if (fmt not in {"PNG", "JPEG", "WEBP"} or img.width > 4096 or img.height > 4096
                    or img.width * img.height > MAX_PIXELS or getattr(img, "n_frames", 1) != 1):
                raise ValueError()
            img.verify()
        with Image.open(path) as img:
            img.load()
    except Exception:
        raise Deferred("SOURCE_REVIEW", "Invalid, animated, oversized or unsupported image") from None
    b = path.read_bytes()
    if not b or len(b) > MAX_BYTES:
        raise Deferred("TOO_LARGE_PENDING", "Media exceeds byte limit")
    return {"size": len(b), "md5": hashlib.md5(b).hexdigest(),
            "sha256": hashlib.sha256(b).hexdigest()}, {"PNG": ".png", "JPEG": ".jpg", "WEBP": ".webp"}[fmt]


def load_state(root):
    p = root / STATE
    if not p.exists():
        return {"schema_version": 1, "pending": {}, "completed": {}}
    data = json.loads(p.read_text("utf-8"))
    if (data.get("schema_version") != 1 or not isinstance(data.get("pending"), dict)
            or not isinstance(data.get("completed"), dict)):
        raise Deferred("PENDING_MANUAL", "Invalid media checkpoint")
    return data


def write_state(root, state):
    p = root / STATE
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", "utf-8")


def publish_media(root, path, raw, source, identifier, name, info, state, key, known):
    if json.loads(path.read_text("utf-8")) != raw:
        raise Deferred("PENDING_MANUAL", "App changed during media backup")
    url = public_url(identifier, name)
    updated = rewrite_media(raw, source, url)
    if updated == raw:
        raise Deferred("PENDING_MANUAL", "Source is no longer present in canonical media")
    original = path.read_text("utf-8")
    formatting = {"separators": (",", ":")} if original.count("\n") <= 1 else {"indent": 2}
    path.write_text(json.dumps(updated, ensure_ascii=False, **formatting) + "\n", "utf-8")
    state["pending"].pop(key, None)
    state["completed"][key] = {"app_id": path.stem, "identifier": identifier, "filename": name,
                               "source_url": source, "sha256": info["sha256"], "completed_at": now_utc()}
    write_state(root, state)
    paths = [str(path.relative_to(root)), str(STATE)]
    if not known.get(path.stem):
        with (root / LOG).open("a", newline="", encoding="utf-8") as f:
            csv.writer(f).writerow([path.stem, identifier, "ok", name, "",
                                    "https://archive.org/details/" + identifier])
        paths.append(str(LOG))
    git_publish(root, paths, f"chore: preserve verified artwork for {path.stem}", validate=True)


def run_one(root, path, source, identifier, state, creds, known):
    raw = json.loads(path.read_text("utf-8"))
    app = to_v1(raw)
    if (app.get("id") != path.stem or source not in external_urls(app)
            or resolve_target(app, known)[0] != identifier
            or policy_entry(load_policy(root), path.stem).get("enabled") is not True):
        raise Deferred("PENDING_MANUAL", "Canonical media candidate changed")
    # Reuse owner check; never write to a third-party item.
    md = owned_metadata(identifier, creds[2])
    with tempfile.TemporaryDirectory(prefix="psva_media_") as tmp:
        folder = Path(tmp)
        local = folder / "source.tmp"
        info, ext = fetch_image(source, local)
        name = "media_" + info["sha256"] + ext
        key = hashlib.sha256((path.stem + "\n" + source).encode()).hexdigest()
        record = {"app_id": path.stem, "identifier": identifier, "filename": name,
                  "source_url": source, "info": info}
        pending = state["pending"].get(key)
        if pending and any(pending.get(k) != v for k, v in record.items()):
            raise Deferred("PENDING_MANUAL", "Source changed after an uncertain transfer")
        if not pending:
            # The same content may already be archived under a legacy name.
            # Size/MD5 are only a lookup hint; public SHA-256 remains mandatory.
            for f in md["files"]:
                n = f.get("name", "") if isinstance(f, dict) else ""
                if (re.fullmatch(r"[A-Za-z0-9._ -]{1,150}", n)
                        and n.lower().endswith((".png", ".jpg", ".jpeg", ".webp"))
                        and str(f.get("size")) == str(info["size"])
                        and str(f.get("md5", "")).lower() == info["md5"]):
                    name = n
                    break
            record["filename"] = name
        md = owned_metadata(identifier, creds[2])
        remote = existing_file(md, name)
        if remote is None:
            if pending or any(r.get("identifier") == identifier and r.get("filename") == name
                              for r in state["pending"].values()):
                raise Deferred("VERIFYING", "Previous image PUT not visible; no repeat")
            if any(r.get("identifier") == identifier and r.get("filename") == name
                   for r in state["completed"].values()):
                raise Deferred("PENDING_MANUAL", "Previously published image missing; no repeat")
            local.rename(folder / name)
            state["pending"][key] = dict(record, created_at=now_utc())
            write_state(root, state)
            git_publish(root, [str(STATE)], f"chore: checkpoint media for {path.stem}", validate=False)
            put_file(identifier, name, folder, creds)
            md = owned_metadata(identifier, creds[2])
        verify_remote(identifier, name, info, md, folder)
        publish_media(root, path, raw, source, identifier, name, info, state, key, known)
    return "PUBLISHED_VERIFIED"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--execute", action="store_true")
    p.add_argument("--max-images", type=int, default=3)
    p.add_argument("--report", type=Path, default=Path("archive-batch-report/media.json"))
    args = p.parse_args()
    if not 1 <= args.max_images <= 3:
        p.error("Initial media batch limit is 1–3 images")
    root = args.root.resolve()
    creds = tuple(os.environ.get(k, "") for k in ("IA_ACCESS_KEY", "IA_SECRET_KEY", "IA_EXPECTED_UPLOADER"))
    if args.execute and (os.environ.get("GITHUB_REPOSITORY") != "VegettoSan/PSVitaAlive"
                         or os.environ.get("GITHUB_REF") != "refs/heads/main" or not all(creds)):
        p.error("Official main and configured secrets required")
    state = load_state(root)
    policy = load_policy(root)
    known = upload_map(root / LOG)
    apps = [(path, to_v1(json.loads(path.read_text("utf-8"))))
            for path in sorted((root / "apps").glob("*.json"))]
    tids = Counter(str(a.get("title_id", "")).upper() for _, a in apps)
    rows, tasks = [], []
    for path, app in apps:
        urls = external_urls(app)
        if not urls:
            continue
        app_id = app.get("id")
        if (app_id != path.stem or not VALID_ID.fullmatch(path.stem)
                or not app.get("title_id") or tids[str(app["title_id"]).upper()] != 1
                or app.get("status") not in {"Verified", "Legacy", "Archive"}):
            rows.append({"app_id": path.stem, "status": "PENDING_MANUAL", "count": len(urls)})
            continue
        if policy_entry(policy, app_id).get("enabled") is not True:
            continue
        target, evidence = resolve_target(app, known)
        for url in urls:
            if not target:
                rows.append({"app_id": app_id, "status": "ITEM_REVIEW", "reason": evidence})
            elif not safe_media_url(url):
                rows.append({"app_id": app_id, "status": "SOURCE_REVIEW", "reason": "Unsupported media provider"})
            else:
                key = hashlib.sha256((app_id + "\n" + url).encode()).hexdigest()
                tasks.append((key not in state["pending"], path, url, target))
    tasks.sort(key=lambda x: (x[0], str(x[1]), x[2]))
    deadline = time.monotonic() + 5 * 60
    for idx, (_, path, url, target) in enumerate(tasks):
        if not args.execute:
            rows.append({"app_id": path.stem, "status": "PREVIEW", "identifier": target})
            continue
        if idx >= args.max_images or time.monotonic() + 90 > deadline:
            rows.append({"app_id": path.stem, "status": "DEFERRED"})
            continue
        try:
            status = run_one(root, path, url, target, state, creds, upload_map(root / LOG))
            detail = ""
        except Deferred as exc:
            status, detail = exc.status, exc.reason
        except Exception:
            status, detail = "PENDING_MANUAL", "Unexpected media error; stopping batch"
            rows.append({"app_id": path.stem, "status": status, "detail": detail})
            break
        rows.append({"app_id": path.stem, "identifier": target, "status": status, "detail": detail})
        if status in {"CATALOG_PUSH_BLOCKED", "UPLOAD_RESULT_UNCERTAIN", "WAITING_ARCHIVE"}:
            break
    report = {"mode": "EXECUTE" if args.execute else "DRY_RUN", "rows": rows,
              "counts": dict(Counter(r["status"] for r in rows))}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", "utf-8")
    summary = "Existing-item media preservation: " + str(report["counts"])
    print(summary, flush=True)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write("## " + summary + "\n")
    return 1 if any(r["status"] == "CATALOG_PUSH_BLOCKED" for r in rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
