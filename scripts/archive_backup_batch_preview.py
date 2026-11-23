#!/usr/bin/env python3
"""Read-only Archive.org backup batching preview; never uploads or edits catalog.

Integrates with the existing PSVitaAlive release validator; new releases use
plan_changes(), and same-version GitHub/GitLab downloads require a strict asset
match. Output is a proposal, not proof of storage or redistribution permission.
"""
from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote, urlparse

from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import (
    app_repositories, effective_release_version, load_snapshot, normalized_repo,
    numeric_version, plan_changes, release_asset_is_vpk,
    release_asset_url_matches, select_current_download,
)

DEFAULT_LIMIT_BYTES = 200 * 1024 * 1024


def archive_id(url: object) -> str | None:
    if not isinstance(url, str):
        return None
    u = urlparse(url)
    if u.scheme != "https" or (u.hostname or "").lower() not in {"archive.org", "www.archive.org"}:
        return None
    parts = [unquote(s) for s in u.path.split("/") if s]
    if len(parts) < 2 or parts[0] not in {"download", "details"}:
        return None
    return parts[1] if all(ch.isalnum() or ch in "-_." for ch in parts[1]) else None


def upload_map(path: Path) -> dict[str, set[str]]:
    result: dict[str, set[str]] = defaultdict(set)
    if not path.exists():
        return result
    with path.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            app_id, ident = (row.get("app_id") or "").strip(), (row.get("identifier") or "").strip()
            if (row.get("status") or "").lower() == "ok" and app_id and ident:
                result[app_id].add(ident)
    return result


def resolve_item(app: dict, known: dict[str, set[str]]) -> tuple[str | None, str]:
    """Only a historical *successful own upload* may nominate a destination.

    Third-party Archive.org screenshots, icons and Game Files are never proof
    of account ownership. The historical log is a hint, NOT final permission:
    a future uploader MUST verify metadata.uploader against the private
    configured Archive.org account before it writes anything.
    """
    app_id = str(app.get("id") or "")
    logged = known.get(app_id, set())
    if len(logged) > 1:
        return None, "several historical identifiers: manual backup required"
    if not logged:
        return None, "no previously managed Archive.org item: manual backup required"
    chosen = next(iter(logged))
    # Only a conflicting PRIMARY VPK URL is significant. Screenshots, icon,
    # Game Files and data packs legitimately live in other people's items.
    current, _ = select_current_download(app.get("links", []))
    primary = archive_id(current.get("url")) if current else None
    if primary and primary != chosen:
        return None, "main VPK link conflicts with historical own item: manual backup required"
    return chosen, "historical-upload-log"


def item_owned_by_account(metadata: object, expected_uploader: str) -> tuple[bool, str]:
    """Mandatory pre-upload ownership gate. Never report the account's email.

    expected_uploader comes from a GitHub Actions secret, never the public
    catalog or this module. This does not test upload permissions; a future
    transfer must still handle 403/permission errors as PENDING_MANUAL.
    """
    if not isinstance(expected_uploader, str) or not expected_uploader.strip():
        return False, "expected Archive.org uploader not configured"
    if not isinstance(metadata, dict) or not isinstance(metadata.get("metadata"), dict):
        return False, "Archive.org item metadata unavailable"
    actual = metadata["metadata"].get("uploader")
    if not isinstance(actual, str) or not actual.strip():
        return False, "Archive.org uploader metadata missing"
    if actual.strip().casefold() != expected_uploader.strip().casefold():
        return False, "Archive.org item managed by a different account"
    return True, "uploader verified"


def same_version_asset(app: dict, snapshot: dict, repo_usage: Counter) -> dict | None:
    """Candidate for backfilling *current* GitHub/GitLab VPK without version bump.

    Only matches the exact download URL and a unique trusted release asset.
    It never implies the binary is absent from Archive.org.
    """
    if app.get("status") not in {"Verified", "Legacy"}:
        return None
    link, _ = select_current_download(app.get("links") or [])
    if link is None:
        return None
    url = link.get("url")
    source_key = normalized_repo(url)
    if source_key is None or source_key not in app_repositories(app) or repo_usage[source_key] != 1:
        return None
    matches = []
    for rel in snapshot.get("releases", []):
        if not isinstance(rel, dict):
            continue
        platform = str(rel.get("platform") or "").lower()
        repo = str(rel.get("repo") or "").lower()
        if platform not in {"github", "gitlab"}:
            continue
        key = f"gitlab:{repo}" if platform == "gitlab" else repo
        if key != source_key or rel.get("review_required") is True:
            continue
        for item in (rel, rel.get("stable_release")):
            if not isinstance(item, dict) or item.get("review_required") is True:
                continue
            if numeric_version(effective_release_version(item)) != numeric_version(app.get("version")):
                continue
            assets = [a for a in (item.get("assets") or []) if isinstance(a, dict) and release_asset_is_vpk(a)]
            if len(assets) != 1:
                continue
            asset = assets[0]
            asset_repo = str(item.get("canonical_repo") or repo).lower()
            if item.get("canonical_repo") is not None and (item.get("platform") != "github" or not isinstance(item.get("repo_id"), int) or item["repo_id"] <= 0):
                continue
            if asset.get("download_url") != url or not release_asset_url_matches(url, asset_repo, platform, str(item.get("tag") or "")):
                continue
            size = asset.get("size")
            if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
                # Do not use app size as if it came from release; could be stale.
                continue
            matches.append({"url": url, "size": size, "version": app["version"]})
    return matches[0] if len(matches) == 1 else None


def candidates(root: Path, snapshot: dict) -> tuple[list[dict], list[dict]]:
    known = upload_map(root / "scripts" / "archive_upload_log.csv")
    apps: dict[Path, dict] = {}
    title_ids: Counter = Counter()
    repo_usage: Counter = Counter()
    for path in sorted((root / "apps").glob("*.json")):
        app = to_v1(json.loads(path.read_text(encoding="utf-8")))
        apps[path] = app
        title_ids[str(app.get("title_id") or "").upper()] += 1
        repo_usage.update(app_repositories(app))

    proposals, reviews = plan_changes(root, snapshot)
    report = [{"reason": msg, "status": "REVIEW"} for msg in reviews]
    planned: dict[Path, dict] = {}
    for path, proposed, _compact in proposals:
        app = apps[path]
        target, evidence = resolve_item(app, known)
        link, _ = select_current_download(proposed.get("links"))
        size = proposed.get("size")
        if (not target or evidence != "historical-upload-log"
                or not isinstance(size, int) or isinstance(size, bool) or size <= 0 or link is None):
            report.append({"app_id": app.get("id"), "status": "PENDING_MANUAL" if not target or evidence != "historical-upload-log" else "REVIEW", "reason": evidence if not target or evidence != "historical-upload-log" else "no trustworthy release asset size"})
            continue
        planned[path] = {"app_id": app["id"], "title_id": app["title_id"], "version": proposed["version"],
                         "source_url": link["url"], "size_bytes": size, "archive_identifier": target,
                         "kind": "NEW_RELEASE", "status": "READY_OWNER_PREFLIGHT"}

    for path, app in apps.items():
        if path in planned or app.get("status") not in {"Verified", "Legacy"}:
            continue
        if title_ids[str(app.get("title_id") or "").upper()] != 1:
            continue
        matched = same_version_asset(app, snapshot, repo_usage)
        if not matched:
            continue
        target, evidence = resolve_item(app, known)
        if not target or evidence != "historical-upload-log":
            report.append({"app_id": app.get("id"), "status": "PENDING_MANUAL", "reason": evidence})
            continue
        planned[path] = {"app_id": app["id"], "title_id": app["title_id"], "version": matched["version"],
                         "source_url": matched["url"], "size_bytes": matched["size"],
                         "archive_identifier": target, "kind": "SAME_VERSION_BACKFILL", "status": "READY_OWNER_PREFLIGHT"}

    # Stable reproducible ordering. Large jobs not allowed to starve smaller jobs.
    result = sorted(planned.values(), key=lambda a: (a["size_bytes"], a["app_id"]))
    return result, report


def select_batch(items: list[dict], *, max_files: int, max_file_bytes: int, max_total_bytes: int) -> list[dict]:
    used = 0
    selected = 0
    result = []
    for item in items:
        entry = dict(item)
        size = entry["size_bytes"]
        if size > max_file_bytes:
            entry["status"] = "TOO_LARGE_PENDING"
        elif selected >= max_files or used + size > max_total_bytes:
            entry["status"] = "DEFERRED"
        else:
            entry["status"] = "SELECTED_PREVIEW"
            selected += 1
            used += size
        result.append(entry)
    return result


def report_markdown(items: list[dict], reviews: list[dict], *, limits: dict) -> str:
    tally = Counter(r["status"] for r in items + reviews)
    lines = ["# Archive.org backup batch — READ-ONLY preview", "",
             "**No VPK downloaded/uploaded and no catalog changes made. A historical upload log is not proof of ownership; verify metadata.uploader with a private expected account before enabling any upload.**", "",
             f"Limits: {limits['max_files']} files, {limits['max_file_mb']} MB per VPK, {limits['max_total_mb']} MB total. Time cap: {limits['max_minutes']} min (future upload step).", "",
             "| Status | Number |", "|---|---:|"]
    lines += [f"| {key} | {count} |" for key, count in sorted(tally.items())]
    lines += ["", "| App | Version | Size MB | Archive item | Selection |", "|---|---|---:|---|---|"]
    for it in items:
        lines.append(f"| `{it['app_id']}` | `{it['version']}` | {it['size_bytes']/1048576:.1f} | `{it['archive_identifier']}` | {it['status']} |")
    lines += ["", "## Requires review", ""]
    for it in reviews[:100]:
        lines.append(f"- `{it.get('app_id', 'upstream')}`: {it['reason']}")
    if len(reviews) > 100:
        lines.append(f"- ... and {len(reviews)-100} more; see JSON report")
    return "\n".join(lines) + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--monitor-ref", default="main")
    p.add_argument("--snapshot-file", type=Path)
    p.add_argument("--max-files", type=int, default=12)
    p.add_argument("--max-file-mb", type=int, default=200)
    p.add_argument("--max-total-mb", type=int, default=800)
    p.add_argument("--max-minutes", type=int, default=45)
    p.add_argument("--output", type=Path, default=Path("archive-batch-preview.json"))
    p.add_argument("--summary", type=Path, default=Path("archive-batch-preview.md"))
    args = p.parse_args()
    if any(n <= 0 for n in (args.max_files, args.max_file_mb, args.max_total_mb, args.max_minutes)):
        p.error("all limits must be positive")
    snapshot = json.loads(args.snapshot_file.read_text(encoding="utf-8")) if args.snapshot_file else load_snapshot(args.monitor_ref)
    queue, reviews = candidates(args.root, snapshot)
    items = select_batch(queue, max_files=args.max_files, max_file_bytes=args.max_file_mb*1024*1024,
                         max_total_bytes=args.max_total_mb*1024*1024)
    limits = {"max_files": args.max_files, "max_file_mb": args.max_file_mb,
              "max_total_mb": args.max_total_mb, "max_minutes": args.max_minutes}
    output = {"mode": "DRY_RUN", "limits": limits, "jobs": items, "reviews": reviews}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    args.summary.write_text(report_markdown(items, reviews, limits=limits), encoding="utf-8")
    print(report_markdown(items, reviews, limits=limits))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
