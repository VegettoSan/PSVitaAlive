#!/usr/bin/env python3
"""Bounded catalog-first ZIP inspection: no uploads or canonical app edits."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import tempfile
import time
import zipfile
import zlib
from collections import Counter, deque
from pathlib import Path
from urllib.parse import quote, unquote, urlparse

import requests

from scripts.archive_backup_batch import Deferred, ROOT, git_publish, stream_download
from scripts.archive_new_items_batch import validate_vpk_title
from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import (
    app_repositories, load_snapshot, normalized_repo, plan_changes,
    release_instant, select_current_download, newer_version, SHA_RE,
)

STATE = Path("data/archive_zip_inspections.json")
SCHEMA = 1
MIB = 1024 * 1024
MAX_ARCHIVE = 200 * MIB
MAX_BATCH = 1024 * MIB
MAX_INNER_TOTAL = 400 * MIB  # unchanged per-archive expansion safety limit
MAX_VPK = 200 * MIB
MAX_EXPANDED = 512 * MIB
MAX_ENTRIES = 4096
MAX_CANDIDATES = 8
MAX_DOWNLOADS = 24
MAX_RELEASES = 32
MAX_SECONDS = 720
MAX_CACHE = 5000
TERMINAL = {"NO_VPK", "NO_VALID_VPK", "VPK_FOUND", "AMBIGUOUS_VPK",
            "INVALID_ZIP", "PACKAGE_REVIEW", "LIMIT_REVIEW"}
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]{0,90}")
TITLE_RE = re.compile(r"[A-Za-z0-9]{9}")
REPO_RE = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")


def safe_entry(info):
    name = info.filename
    if name != info.orig_filename or "\\" in name or ":" in name or len(name) > 1024:
        return False
    parts = name.rstrip("/").split("/")
    if any(not p or p in {".", ".."} or any(ord(c) < 32 for c in p) for p in parts):
        return False
    kind = stat.S_IFMT(info.external_attr >> 16)
    return kind in {0, stat.S_IFREG, stat.S_IFDIR} and not (info.flag_bits & 1)


def check_entries(infos):
    if len(infos) > MAX_ENTRIES:
        return "LIMIT_REVIEW"
    names = [i.filename.rstrip("/").casefold() for i in infos]
    if len(set(names)) != len(names) or any(not safe_entry(i) for i in infos):
        return "PACKAGE_REVIEW"
    return None


def check_inner(path, title_id, deadline):
    # Never extract the VPK itself. Check all CRCs within a bounded expanded
    # size, then verify the canonical param.sfo Title ID.
    try:
        with zipfile.ZipFile(path) as inner:
            infos = inner.infolist()
            gate = check_entries(infos)
            if gate:
                return gate
            if sum(i.file_size for i in infos) > MAX_EXPANDED:
                return "LIMIT_REVIEW"
            if ("eboot.bin" not in inner.namelist()
                    or not inner.getinfo("eboot.bin").file_size):
                return "INVALID_VPK"
            total = 0
            for info in infos:
                if info.is_dir():
                    continue
                with inner.open(info) as src:
                    while chunk := src.read(128 * 1024):
                        total += len(chunk)
                        if total > MAX_EXPANDED:
                            return "LIMIT_REVIEW"
                        if time.monotonic() >= deadline:
                            return "TIME_BUDGET"
        validate_vpk_title(path, title_id)
        return "VALID"
    except (Deferred, zipfile.BadZipFile, zlib.error):
        return "INVALID_VPK"
    except (RuntimeError, NotImplementedError, ValueError, EOFError):
        return "PACKAGE_REVIEW"


def inspect_zip(path, title_id, *, deadline=None):
    deadline = deadline if deadline is not None else time.monotonic() + MAX_SECONDS
    if not isinstance(title_id, str) or not TITLE_RE.fullmatch(title_id):
        raise ValueError("Invalid expected Title ID")
    try:
        with zipfile.ZipFile(path) as outer, tempfile.TemporaryDirectory(prefix="psva_zip_") as td:
            infos = outer.infolist()
            gate = check_entries(infos)
            if gate:
                return {"status": gate, "matches": []}
            vpks = [i for i in infos if not i.is_dir() and i.filename.lower().endswith(".vpk")]
            if not vpks:
                return {"status": "NO_VPK", "matches": []}
            if (len(vpks) > MAX_CANDIDATES or any(i.file_size > MAX_VPK for i in vpks)
                    or sum(i.file_size for i in vpks) > MAX_INNER_TOTAL):
                return {"status": "LIMIT_REVIEW", "matches": []}
            matches, rejected = [], []
            for n, info in enumerate(vpks):
                if time.monotonic() >= deadline:
                    return {"status": "TIME_BUDGET", "matches": []}
                if info.file_size <= 0:
                    rejected.append("INVALID_VPK")
                    continue
                local = Path(td) / f"candidate-{n}.vpk"
                sha = hashlib.sha256()
                count = 0
                # Fixed local name: never use ZIP entry names as filesystem paths.
                with outer.open(info) as src, local.open("wb") as dest:
                    while chunk := src.read(128 * 1024):
                        count += len(chunk)
                        if count > MAX_VPK:
                            return {"status": "LIMIT_REVIEW", "matches": []}
                        if time.monotonic() >= deadline:
                            return {"status": "TIME_BUDGET", "matches": []}
                        sha.update(chunk)
                        dest.write(chunk)
                if count != info.file_size:
                    return {"status": "INVALID_ZIP", "matches": []}
                result = check_inner(local, title_id, deadline)
                if result == "VALID":
                    matches.append({"entry": info.filename, "size": count,
                                    "sha256": sha.hexdigest(), "title_id": title_id.upper()})
                else:
                    rejected.append(result)
                local.unlink()
            # An uninspectable alternative could also match. Never claim
            # uniqueness until every VPK candidate has been inspected.
            for status in ("TIME_BUDGET", "LIMIT_REVIEW", "PACKAGE_REVIEW"):
                if status in rejected:
                    return {"status": status, "matches": matches}
            status = ("AMBIGUOUS_VPK" if len(matches) > 1 else "VPK_FOUND" if matches
                      else "NO_VALID_VPK")
            return {"status": status, "matches": matches, "vpk_entries": len(vpks)}
    except (zipfile.BadZipFile, zlib.error):
        return {"status": "INVALID_ZIP", "matches": []}
    except (RuntimeError, NotImplementedError, ValueError, EOFError):
        return {"status": "PACKAGE_REVIEW", "matches": []}


def release_assets(repo, tag, expected_release_id=None, *, get=None):
    if not REPO_RE.fullmatch(repo) or not isinstance(tag, str) or not tag or len(tag) > 200:
        raise ValueError("Untrusted release identity")
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "PSVitaAlive-ZIP-Audit"}
    if os.environ.get("GH_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GH_TOKEN"]
    response = (get or requests.get)(
        f"https://api.github.com/repos/{repo}/releases/tags/{quote(tag, safe='')}",
        headers=headers, timeout=(10, 20), allow_redirects=False)
    if response.status_code != 200:
        raise ValueError("Release metadata unavailable")
    release = response.json()
    rid = release.get("id")
    if (type(rid) is not int or rid <= 0 or release.get("tag_name") != tag
            or release.get("draft") is not False or type(release.get("prerelease")) is not bool
            or (expected_release_id and str(rid) != str(expected_release_id))):
        raise ValueError("Release identity changed")
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise ValueError("Malformed asset list")
    # Direct VPKs take precedence; do not download ZIP alternatives.
    if any(isinstance(a, dict) and a.get("state") == "uploaded"
           and str(a.get("name", "")).lower().endswith(".vpk") for a in assets):
        return {"direct": True, "release_id": rid, "assets": []}
    result = []
    for asset in assets:
        if not isinstance(asset, dict) or asset.get("state") != "uploaded":
            continue
        name = asset.get("name")
        if not isinstance(name, str) or not name.lower().endswith(".zip"):
            continue
        url, size, aid = asset.get("browser_download_url"), asset.get("size"), asset.get("id")
        if not isinstance(url, str):
            raise ValueError("Missing ZIP asset URL")
        u = urlparse(url)
        parts = [unquote(p) for p in u.path.split("/") if p]
        digest = asset.get("digest")
        if (u.scheme != "https" or u.netloc.lower() != "github.com" or u.query or u.fragment
                or normalized_repo(url) != repo.lower() or parts[2:] != ["releases", "download", tag, name]
                or type(size) is not int or size <= 0 or type(aid) is not int or aid <= 0
                or release_instant(asset.get("updated_at")) is None
                or (digest is not None and not re.fullmatch(r"sha256:[0-9a-fA-F]{64}", str(digest)))):
            raise ValueError("Malformed or untrusted ZIP asset metadata")
        result.append({"repo": repo.lower(), "tag": tag, "release_id": rid,
                       "asset_id": aid, "name": name, "url": url, "size": size,
                       "updated_at": asset["updated_at"], "digest": digest.lower() if digest else None})
    result.sort(key=lambda a: (not bool(re.search(r"(?:^|[-_.])(?:psvita|vita)(?:[-_.]|$)", a["name"], re.I)),
                               a["name"].casefold(), a["asset_id"]))
    if len({a["asset_id"] for a in result}) != len(result):
        raise ValueError("Duplicate asset IDs")
    return {"direct": False, "release_id": rid, "prerelease": release["prerelease"],
            "published": release.get("published_at"), "assets": result}


def cache_key(asset, app):
    identity = {"inspector_schema": SCHEMA, "app_id": app["id"],
                "title_id": app["title_id"].upper(), "asset": asset}
    return hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def inspect_asset(asset, app, *, deadline, download=None):
    if asset["size"] > MAX_ARCHIVE:
        return {"status": "LIMIT_REVIEW", "matches": []}
    download = download or stream_download
    with tempfile.TemporaryDirectory(prefix="psva_release_zip_") as td:
        path = Path(td) / "release.zip"
        try:
            with requests.Session() as session:
                session.trust_env = False
                size, _, sha = download(asset["url"], path, min(MAX_ARCHIVE, asset["size"]),
                                        session=session, deadline=deadline)
            if size != asset["size"] or (asset["digest"] and "sha256:" + sha != asset["digest"]):
                return {"status": "SOURCE_INTEGRITY_RETRY", "matches": []}
            outcome = inspect_zip(path, app["title_id"], deadline=deadline)
            outcome["outer_sha256"] = sha
            return outcome
        except (Deferred, OSError, requests.RequestException):
            return {"status": "SOURCE_RETRY", "matches": []}


def pinned_targets(app):
    if select_current_download(app.get("links"))[0]:
        return []
    if app.get("status") not in {"Verified", "Legacy", "Archive"}:
        return []
    refs = [app.get("release_page")] + [l.get("url") for l in app.get("links", []) if isinstance(l, dict)]
    targets = set()
    for ref in refs:
        if not isinstance(ref, str):
            continue
        try:
            u = urlparse(ref)
            p = [unquote(s) for s in u.path.split("/") if s]
            repo = normalized_repo(ref)
            if not (u.scheme == "https" and u.netloc.lower() == "github.com" and not u.query and not u.fragment
                    and repo in app_repositories(app)):
                continue
            if len(p) == 5 and p[2:4] == ["releases", "tag"]:
                targets.add((repo, p[4]))
            if len(p) == 6 and p[2:4] == ["releases", "download"] and p[5].lower().endswith(".zip"):
                targets.add((repo, p[4]))
        except ValueError:
            continue
    return list(targets) if len(targets) == 1 else []


def collect_targets(root, snapshot):
    apps = [to_v1(json.loads(p.read_text("utf-8"))) for p in sorted((root / "apps").glob("*.json"))]
    counts = Counter(str(a.get("title_id", "")).upper() for a in apps)
    repo_counts = Counter(repo for a in apps for repo in app_repositories(a))
    def valid(app, repo):
        return (isinstance(app.get("id"), str) and ID_RE.fullmatch(app["id"])
                and isinstance(app.get("title_id"), str) and TITLE_RE.fullmatch(app["title_id"])
                and counts[app["title_id"].upper()] == 1 and repo_counts[repo] == 1)
    updates = []
    plan_changes(root, snapshot, zip_candidates=updates)
    targets = {}
    for app, release, repo in updates:
        canonical = release.get("canonical_repo")
        if (release.get("platform") != "github" or not valid(app, repo)
                or (canonical is not None and str(canonical).lower() != repo.lower())
                or not release.get("tag") or not release.get("release_id")):
            continue
        key = (app["id"], repo, release["tag"])
        targets[key] = (app, repo, release["tag"], release["release_id"], bool(release.get("prerelease")))
    for app in apps:
        for repo, tag in pinned_targets(app):
            if valid(app, repo):
                key = (app["id"], repo, tag)
                targets.setdefault(key, (app, repo, tag, None, False))
    return [targets[k] for k in sorted(targets)]


def load_cache(root):
    path = root / STATE
    if not path.exists():
        return {"schema_version": SCHEMA, "entries": {}}
    obj = json.loads(path.read_text("utf-8"))
    if (not isinstance(obj, dict) or obj.get("schema_version") != SCHEMA
            or not isinstance(obj.get("entries"), dict)
            or any(not isinstance(v, dict) or not isinstance(v.get("result"), dict)
                   or v["result"].get("status") not in TERMINAL for v in obj["entries"].values())):
        raise ValueError("Unknown or invalid inspection cache; no overwrite")
    return obj


def vita_named(asset):
    # Name is only a scheduling hint, never a reason to reject a ZIP.
    return bool(re.search(r"(?:^|[-_. ])(?:psvita|ps-vita|vita)(?:[-_. ]|$)",
                          asset["name"], re.I))


def scan(root, snapshot, cache, *, metadata=None, inspector=None, rotation=0):
    metadata = metadata or release_assets
    inspector = inspector or inspect_asset
    started = time.monotonic()
    deadline = started + MAX_SECONDS
    # Preserve most of the budget for actual inspection if upstream metadata
    # is slow. The next run rotates the starting position.
    metadata_deadline = started + MAX_SECONDS / 3
    targets = collect_targets(root, snapshot)
    if targets:
        offset = max(0, rotation) % len(targets)
        targets = targets[offset:] + targets[:offset]
    rows, downloads, total = [], 0, 0
    entries = cache["entries"]
    queues = {}
    deferred_counts = Counter()
    cache_hits = 0
    for index, (app, repo, tag, rid, allow_pre) in enumerate(targets[:MAX_RELEASES]):
        row = {"app_id": app["id"], "title_id": app["title_id"], "repo": repo, "tag": tag, "assets": []}
        rows.append(row)
        if time.monotonic() + 25 >= metadata_deadline:
            row["status"] = "TIME_BUDGET"
            continue
        try:
            release = metadata(repo, tag, rid)
            if release.get("direct"):
                row["status"] = "DIRECT_VPK_AVAILABLE"
                continue
            if release.get("prerelease") and not allow_pre:
                row["status"] = "PRERELEASE_REVIEW"
                continue
        except (ValueError, TypeError, requests.RequestException):
            row["status"] = "METADATA_RETRY"
            continue
        update = bool(select_current_download(app.get("links", []))[0]
                      and newer_version(app.get("version"), tag))
        # Cache hits and deterministic oversize cases require no download.
        for asset in release["assets"]:
            key = cache_key(asset, app)
            if key in entries:
                row["assets"].append({"asset": asset, "cache_hit": True, **entries[key]["result"]})
                cache_hits += 1
                continue
            if asset["size"] > MAX_ARCHIVE:
                outcome = {"status": "LIMIT_REVIEW", "matches": []}
                entries[key] = {"app_id": app["id"], "title_id": app["title_id"],
                                "asset": asset, "result": outcome}
                row["assets"].append({"asset": asset, "cache_hit": False, **outcome})
                continue
            # Prefer actual updates and Vita-named archives. A low-priority
            # ZIP still receives a turn; all releases share an app queue.
            slot = {"asset": asset, "cache_hit": False, "status": "DEFERRED", "matches": []}
            row["assets"].append(slot)
            priority = (0 if update and vita_named(asset) else
                        1 if update or vita_named(asset) else 2)
            queues.setdefault(app["id"], []).append((priority, index, asset["size"], key, app, slot))
    # Each round processes at most one unknown ZIP per application, even
    # when several release tags refer to it. Preserve rotation for ties,
    # and interleave one ordinary job after two high-priority jobs.
    for app_id, jobs in queues.items():
        queues[app_id] = deque(sorted(jobs, key=lambda j: (j[0], j[1], j[2], j[3])))
    rounds = 0
    while queues and downloads < MAX_DOWNLOADS and time.monotonic() + 25 < deadline:
        rounds += 1
        high, normal = deque(), deque()
        for jobs in queues.values():
            job = jobs.popleft()
            (high if job[0] < 2 else normal).append(job)
        high = deque(sorted(high, key=lambda j: (j[0], j[1])))
        normal = deque(sorted(normal, key=lambda j: j[1]))
        ordered = []
        while high or normal:
            for _ in range(2):
                if high:
                    ordered.append(high.popleft())
            if normal:
                ordered.append(normal.popleft())
        for _, _, size, key, app, slot in ordered:
            if downloads >= MAX_DOWNLOADS:
                deferred_counts["DOWNLOAD_COUNT"] += 1
                continue
            if total + size > MAX_BATCH:
                deferred_counts["BYTE_BUDGET"] += 1
                continue
            if time.monotonic() + 25 >= deadline:
                deferred_counts["TIME_BUDGET"] += 1
                continue
            asset = slot["asset"]
            downloads += 1
            total += size
            outcome = inspector(asset, app, deadline=deadline)
            slot.update(outcome)
            if outcome["status"] in TERMINAL:
                entries[key] = {"app_id": app["id"], "title_id": app["title_id"],
                                "asset": asset, "result": outcome}
        queues = {app_id: jobs for app_id, jobs in queues.items() if jobs}
    for jobs in queues.values():
        for _, _, size, _, _, _slot in jobs:
            reason = ("DOWNLOAD_COUNT" if downloads >= MAX_DOWNLOADS else
                      "BYTE_BUDGET" if total + size > MAX_BATCH else "TIME_BUDGET")
            deferred_counts[reason] += 1
    for row in rows:
        if "status" in row:
            continue
        outcomes = [a["status"] for a in row["assets"]]
        matches = sum(len(a.get("matches", [])) for a in row["assets"])
        if any(s not in {"NO_VPK", "NO_VALID_VPK", "VPK_FOUND", "AMBIGUOUS_VPK"} for s in outcomes):
            row["status"] = "INCOMPLETE_REVIEW"
        elif matches > 1:
            row["status"] = "AMBIGUOUS_VPK"
        elif matches == 1:
            row["status"] = "VPK_FOUND"
        elif not outcomes:
            row["status"] = "NO_ZIP_ASSET"
        else:
            row["status"] = "NO_VPK" if all(s == "NO_VPK" for s in outcomes) else "NO_VALID_VPK"
    while len(entries) > MAX_CACHE:
        entries.pop(next(iter(entries)))
    return {"targets": len(targets), "checked": len(rows), "downloads": downloads,
            "download_budget_bytes": total, "cache_hits": cache_hits, "rounds": rounds,
            "deferred_reasons": dict(deferred_counts), "rows": rows}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--monitor-ref", default="main")
    p.add_argument("--persist", action="store_true")
    p.add_argument("--report", type=Path, default=Path("zip-audit-report/report.json"))
    args = p.parse_args()
    if args.persist and (os.environ.get("GITHUB_REPOSITORY") != "VegettoSan/PSVitaAlive"
                         or os.environ.get("GITHUB_REF") != "refs/heads/main"
                         or os.environ.get("GITHUB_EVENT_NAME") != "repository_dispatch"
                         or not SHA_RE.fullmatch(args.monitor_ref)):
        p.error("Persistent cache writes require trusted main dispatch with immutable monitor SHA")
    root = args.root.resolve()
    cache = load_cache(root)
    previous = json.dumps(cache, ensure_ascii=False, indent=2)
    try:
        rotation = int(os.environ.get("GITHUB_RUN_NUMBER", "1")) - 1
    except ValueError:
        rotation = 0
    report = scan(root, load_snapshot(args.monitor_ref), cache, rotation=rotation)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", "utf-8")
    for row in report["rows"]:
        print("ZIP audit: " + json.dumps({"app_id": row["app_id"], "tag": row["tag"],
                                         "status": row["status"], "assets": len(row["assets"]),
                                         "cache_hits": sum(a["cache_hit"] for a in row["assets"])}), flush=True)
    print(f"ZIP audit targets={report['targets']}, checked={report['checked']}, downloads={report['downloads']}, "
          f"cache_hits={report['cache_hits']}, rounds={report['rounds']}, "
          f"deferred={json.dumps(report['deferred_reasons'], sort_keys=True)}")
    final = json.dumps(cache, ensure_ascii=False, indent=2)
    if args.persist and final != previous:
        path = root / STATE
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(final + "\n", "utf-8")
        git_publish(root, [str(STATE)], "chore: persist ZIP VPK inspection results", validate=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
