#!/usr/bin/env python3
"""Read-only Archive.org backup batching preview; never uploads or edits catalog.

Catalog-first VPK backup discovery: apps/*.json is the authoritative source
for the current approved VPK URL and version. Upstream release-monitor events
are not required for backup eligibility. Output is a proposal, not evidence
of actual storage or permission to redistribute a binary.
"""
from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path
from urllib.parse import unquote, urlparse

from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import (
    app_repositories, normalized_repo, release_asset_url_matches,
    select_current_download,
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


def media_archive_id(url: object) -> str | None:
    """Use only direct Archive.org image files, never a game-data or VPK link."""
    if not isinstance(url, str):
        return None
    parsed = urlparse(url)
    if (parsed.scheme != "https" or (parsed.hostname or "").lower()
            not in {"archive.org", "www.archive.org"}
            or parsed.username or parsed.password or parsed.port):
        return None
    pieces = [unquote(x) for x in parsed.path.split("/") if x]
    if (len(pieces) < 3 or pieces[0] != "download"
            or not pieces[-1].lower().endswith((".png", ".jpg", ".jpeg", ".webp"))):
        return None
    return archive_id(url)


def catalog_media_item(app: dict) -> tuple[str | None, str]:
    """Infer the destination from BOTH icon and all supplied screenshots.

    This is only an item *candidate*, not proof of ownership or permission.
    The upload engine must validate metadata.uploader with its private secret.
    """
    icon = media_archive_id(app.get("icon"))
    shots = app.get("screenshots")
    if not icon or not isinstance(shots, list) or not shots:
        return None, "no consistent Archive.org icon and screenshots for candidate item"
    shot_items = [media_archive_id(url) for url in shots]
    if any(item != icon for item in shot_items):
        return None, "catalog icon and screenshots reference different or unsupported items"
    return icon, "catalog-media-candidate"


def resolve_item(app: dict, known: dict[str, set[str]]) -> tuple[str | None, str]:
    """Resolve an existing Archive.org item, not necessarily from a prior upload.

    The historic owned-upload record has priority. Without a record, the icon
    and ALL screenshots can nominate a single existing item. None of those
    public URLs prove account ownership: only a successful private uploader
    metadata check may authorize an upload or reconcile an existing VPK.
    Game Files and plugins are never considered when selecting the item.
    """
    app_id = str(app.get("id") or "")
    logged = known.get(app_id, set())
    if len(logged) > 1:
        return None, "several historical identifiers: manual backup required"
    if logged:
        chosen = next(iter(logged))
        evidence = "historical-upload-log"
    else:
        chosen, evidence = catalog_media_item(app)
        if not chosen:
            return None, evidence

    # Only a conflicting PRIMARY VPK URL is significant. Game Files, plugins
    # and other media links legitimately belong to different IA items.
    current, _ = select_current_download(app.get("links", []))
    primary = archive_id(current.get("url")) if current else None
    if primary and primary != chosen:
        return None, "main VPK link conflicts with candidate own item: manual review"
    return chosen, evidence

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


def catalog_current_asset(app: dict) -> tuple[dict | None, str]:
    """Select the VPK ALREADY APPROVED in apps/, independent of Orbit's feed.

    Only an exact GitHub Releases VPK from a canonical repository is currently
    supported for safe automatic transfer. Other hosts are reported for
    adapters/manual review, NEVER silently treated as missing releases.
    """
    link, reason = select_current_download(app.get("links"))
    if link is None:
        return None, reason
    url = link.get("url")
    if not isinstance(url, str):
        return None, "current Download/VPK link has no URL"
    current_archive = archive_id(url)
    if current_archive:
        return None, "the current VPK already uses Archive.org"
    parsed = urlparse(url)
    repo = normalized_repo(url)
    if not repo or (parsed.hostname or "").lower() != "github.com":
        return None, "catalog VPK source requires a provider-specific download adapter"
    if repo not in app_repositories(app):
        return None, "catalog VPK link does not match a registered upstream repository"
    upstream = normalized_repo(app.get("source_url"))
    if upstream is not None and upstream != repo:
        return None, "catalog VPK link conflicts with the primary repository"
    pieces = [unquote(p) for p in parsed.path.split("/") if p]
    if (len(pieces) != 6 or pieces[2:4] != ["releases", "download"]
            or not release_asset_url_matches(url, repo, "github", pieces[4])):
        return None, "catalog VPK is not a direct verified GitHub Releases asset URL"
    version = app.get("version")
    if not isinstance(version, str) or not version.strip():
        return None, "catalog version is missing"
    size = app.get("size")
    if type(size) is not int or size <= 0:
        return None, "catalog VPK size missing; source metadata check required"
    return {"url": url, "version": version, "size": size}, "catalog Download/VPK"


def candidates(root: Path, snapshot: dict | None = None) -> tuple[list[dict], list[dict]]:
    """Current canonical VPK backfill queue, NOT an upstream version update.

    snapshot is accepted for backward-compatible callers but intentionally
    unused: backup must work even when Orbit has not seen a release or is down.
    """
    known = upload_map(root / "scripts" / "archive_upload_log.csv")
    apps: dict[Path, dict] = {}
    title_ids: Counter = Counter()
    for path in sorted((root / "apps").glob("*.json")):
        app = to_v1(json.loads(path.read_text(encoding="utf-8")))
        apps[path] = app
        title_ids[str(app.get("title_id") or "").upper()] += 1

    result = []
    report = []
    for path, app in apps.items():
        if app.get("status") not in {"Verified", "Legacy", "Archive"}:
            continue
        link, reason = select_current_download(app.get("links"))
        if link is None:
            # Do not flood the backup report with records that do not offer
            # any VPK. They are not current-VPK backup candidates.
            continue

        current_url = link.get("url")
        target, evidence = resolve_item(app, known)
        current_archive = archive_id(current_url)
        if current_archive:
            # A current VPK in our already-managed item is not "pending".
            if current_archive == target and evidence == "historical-upload-log":
                continue
            report.append({"app_id": app.get("id"), "status": "PENDING_MANUAL",
                           "reason": "primary Archive.org VPK is not confirmed as PSVitaAlive-owned"})
            continue

        if title_ids[str(app.get("title_id") or "").upper()] != 1:
            report.append({"app_id": app.get("id"), "status": "PENDING_MANUAL",
                           "reason": "duplicate or missing Title ID"})
            continue

        if target is None or evidence not in {"historical-upload-log", "catalog-media-candidate"}:
            report.append({"app_id": app.get("id"), "status": "PENDING_MANUAL",
                           "reason": evidence})
            continue

        asset, reason = catalog_current_asset(app)
        if asset is None:
            report.append({"app_id": app.get("id"), "status": "SOURCE_ADAPTER_PENDING",
                           "reason": reason})
            continue

        result.append({
            "app_id": app["id"], "title_id": app["title_id"],
            "version": asset["version"], "source_url": asset["url"],
            "size_bytes": asset["size"], "archive_identifier": target,
            "version_date": app.get("version_date"),
            "kind": "CATALOG_BACKFILL",
            "ownership_status": "REQUIRED_BEFORE_UPLOAD",
            "status": "READY_OWNER_PREFLIGHT",
        })

    # Preserve the most recently catalogued versions first. When dates tie
    # or are missing, smaller files go first. A new VPK must not wait behind
    # years-old legacy releases simply because it is larger.
    def priority(item: dict) -> tuple[int, int, str]:
        try:
            day = date.fromisoformat(str(item.get("version_date") or "")[:10]).toordinal()
        except ValueError:
            day = 0
        return (-day, item["size_bytes"], item["app_id"])

    result.sort(key=priority)
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
             "**Catalog-first discovery. No VPK downloaded/uploaded and no JSON changes made. A historical upload log is not proof of ownership; metadata.uploader must still be verified before writing.**", "",
             f"Limits: {limits['max_files']} files, {limits['max_file_mb']} MB per VPK, {limits['max_total_mb']} MB total. Time cap: {limits['max_minutes']} min (future upload step).", "",
             "| Status | Number |", "|---|---:|"]
    lines += [f"| {key} | {count} |" for key, count in sorted(tally.items())]
    lines += ["", "| App | Version | Size MB | Archive item | Selection |", "|---|---|---:|---|---|"]
    for it in items:
        lines.append(f"| `{it['app_id']}` | `{it['version']}` | {it['size_bytes']/1048576:.1f} | `{it['archive_identifier']}` | {it['status']} |")
    lines += ["", "## Current-catalog VPKs requiring review/adapters", ""]
    for it in reviews[:100]:
        lines.append(f"- `{it.get('app_id', 'upstream')}`: {it['reason']}")
    if len(reviews) > 100:
        lines.append(f"- ... and {len(reviews)-100} more; see JSON report")
    return "\n".join(lines) + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--max-files", type=int, default=12)
    p.add_argument("--max-file-mb", type=int, default=200)
    p.add_argument("--max-total-mb", type=int, default=800)
    p.add_argument("--max-minutes", type=int, default=45)
    p.add_argument("--output", type=Path, default=Path("archive-batch-preview.json"))
    p.add_argument("--summary", type=Path, default=Path("archive-batch-preview.md"))
    args = p.parse_args()
    if any(n <= 0 for n in (args.max_files, args.max_file_mb, args.max_total_mb, args.max_minutes)):
        p.error("all limits must be positive")
    queue, reviews = candidates(args.root)
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
