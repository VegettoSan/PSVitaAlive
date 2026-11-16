#!/usr/bin/env python3
"""Safely update *canonical* apps/*.json from the release monitor snapshot.

Never writes generated catalog.json. Deliberately conservative:
- Github stable releases and numeric higher versions only.
- One canonical application must explicitly reference the upstream repository.
- One VPK release asset and one existing matching GitHub Download link.
- Standalone VPK apps only: complicated Game Files / plugin setups need review.
- Never changes Title ID, author, category, extra links or app status.
"""
from __future__ import annotations

import argparse
import json
import re
import urllib.request
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path
from urllib.parse import urlparse

try:
    from scripts.catalog_schema_v2 import to_v1, to_v2
except ModuleNotFoundError:
    from catalog_schema_v2 import to_v1, to_v2

MONITOR_REPO = "VegettoSan/PsVita-Homebrews-New-Releases"
MONITOR_SNAPSHOT = "data/upstream_latest.json"
SHA_RE = re.compile(r"^[a-fA-F0-9]{40}$")
NUMERIC_VERSION = re.compile(r"^[vV]?(\d+(?:\.\d+){0,3})$")
COMPLEX_LINK_TYPES = {
    "game files", "data files", "mod", "mod pack", "patch", "plugin",
    "pkg", "dlc", "update",
}


def normalized_repo(url: object) -> str | None:
    if not isinstance(url, str):
        return None
    parsed = urlparse(url.strip())
    if parsed.scheme != "https" or parsed.netloc.lower() not in {"github.com", "www.github.com"}:
        return None
    parts = [part for part in parsed.path.split("/") if part]
    if len(parts) < 2:
        return None
    owner, project = parts[:2]
    project = project.removesuffix(".git")
    if not re.fullmatch(r"[a-zA-Z0-9_.-]+", owner) or not re.fullmatch(r"[a-zA-Z0-9_.-]+", project):
        return None
    return f"{owner}/{project}".lower()


def app_repositories(app: dict) -> set[str]:
    urls = [app.get("source_url")]
    for item in app.get("links") or []:
        if isinstance(item, dict) and str(item.get("type", "")).lower() == "repository":
            urls.append(item.get("url"))
    return {repo for value in urls if (repo := normalized_repo(value))}


def numeric_version(value: object) -> tuple[int, ...] | None:
    match = NUMERIC_VERSION.fullmatch(str(value or "").strip())
    if not match:
        return None
    numbers = tuple(int(n) for n in match.group(1).split("."))
    return numbers + (0,) * (4 - len(numbers))


def newer_version(current: object, proposed: object) -> bool:
    old, new = numeric_version(current), numeric_version(proposed)
    return old is not None and new is not None and new > old


def release_date(value: object) -> str | None:
    raw = str(value or "")[:10]
    try:
        return date.fromisoformat(raw).isoformat()
    except ValueError:
        return None


def download_belongs_to_repo(url: object, repo: str) -> bool:
    if normalized_repo(url) != repo:
        return False
    parsed = urlparse(str(url))
    parts = parsed.path.split("/")
    # /owner/repository/releases/download/TAG/asset.vpk
    return len(parts) >= 7 and [p.lower() for p in parts[3:5]] == ["releases", "download"]


def release_update(app: dict, item: dict, repo: str) -> tuple[dict | None, str]:
    if app.get("status") not in {"Verified", "Legacy"}:
        return None, "Archive or unsupported status"
    if item.get("platform") != "github" or item.get("prerelease"):
        return None, "not a stable GitHub release"
    if not newer_version(app.get("version"), item.get("tag")):
        return None, "version not newer or cannot be compared safely"
    when = release_date(item.get("published"))
    if not when:
        return None, "invalid release publication date"
    if release_date(app.get("version_date")) and when < str(app["version_date"])[:10]:
        return None, "upstream release date predates catalog version"

    links = app.get("links")
    if not isinstance(links, list):
        return None, "missing links"
    if any(str(link.get("type", "")).lower() in COMPLEX_LINK_TYPES
           for link in links if isinstance(link, dict)):
        return None, "complex install with extra files or plugins"

    assets = [asset for asset in (item.get("assets") or [])
              if isinstance(asset, dict) and str(asset.get("name") or "").lower().endswith(".vpk")]
    if len(assets) != 1:
        return None, "missing or ambiguous VPK assets"
    asset = assets[0]
    asset_url = asset.get("download_url")
    if not download_belongs_to_repo(asset_url, repo):
        return None, "VPK is not a GitHub release download from this repository"
    size = asset.get("size")
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
        return None, "unknown VPK asset size"

    matches = [link for link in links
               if isinstance(link, dict) and str(link.get("type") or "").lower() == "download"
               and download_belongs_to_repo(link.get("url"), repo)]
    if len(matches) != 1:
        return None, "no single existing official GitHub VPK Download link"
    current_link = matches[0]
    if not str(current_link.get("url") or "").lower().endswith(".vpk"):
        return None, "existing official download is not a direct VPK"
    if any(link is not current_link and link.get("recommended") is True
           for link in links if isinstance(link, dict)):
        return None, "another link is recommended; requires human review"

    proposal = json.loads(json.dumps(app))
    proposal["version"] = str(item["tag"]).lstrip("vV")
    proposal["version_date"] = when
    proposal["size"] = size
    index = links.index(current_link)
    proposal["links"][index]["url"] = asset_url
    if "size" in current_link:
        proposal["links"][index]["size"] = size
    # Name/recommended/extract_path remain unchanged, so UI and download
    # preference behave exactly as before the update.
    return proposal, "ready"


def plan_changes(root: Path, snapshot: dict) -> tuple[list[tuple[Path, dict, bool]], list[str]]:
    if not isinstance(snapshot, dict) or snapshot.get("schema_version") != 1:
        raise ValueError("Unsupported upstream snapshot")
    releases = snapshot.get("releases")
    if not isinstance(releases, list):
        raise ValueError("Malformed release list")

    repo_index: dict[str, list[tuple[Path, dict, bool]]] = defaultdict(list)
    title_ids = Counter()
    for path in sorted((root / "apps").glob("*.json")):
        raw = path.read_text(encoding="utf-8")
        source = json.loads(raw)
        app = to_v1(source)
        compact = "tid" in source and "title_id" not in source
        if isinstance(app.get("title_id"), str):
            title_ids[app["title_id"].upper()] += 1
        for repo in app_repositories(app):
            repo_index[repo].append((path, app, compact))

    changes = []
    skipped = []
    seen = set()
    for release in releases:
        if not isinstance(release, dict):
            continue
        repo = str(release.get("repo") or "").lower()
        if not re.fullmatch(r"[a-z0-9_.-]+/[a-z0-9_.-]+", repo):
            continue
        if release.get("platform") != "github" or repo in seen:
            continue
        seen.add(repo)
        matches = repo_index.get(repo, [])
        if len(matches) != 1:
            if len(matches) > 1:
                skipped.append(f"{repo}: shared repository ({len(matches)} apps), manual review required")
            continue
        path, app, compact = matches[0]
        if title_ids[str(app.get("title_id", "")).upper()] != 1:
            skipped.append(f"{repo}: duplicate/ambiguous Title ID")
            continue
        proposal, reason = release_update(app, release, repo)
        if proposal is not None:
            changes.append((path, proposal, compact))
        elif newer_version(app.get("version"), release.get("tag")):
            skipped.append(f"{repo}: {reason}")
    return changes, skipped


def load_snapshot(ref: str) -> dict:
    if ref != "main" and not SHA_RE.fullmatch(ref):
        raise ValueError("monitor ref must be an immutable 40-character SHA or main")
    url = f"https://raw.githubusercontent.com/{MONITOR_REPO}/{ref}/{MONITOR_SNAPSHOT}"
    request = urllib.request.Request(url, headers={"User-Agent": "PSVitaAlive-Catalog-Updater/1.0"})
    with urllib.request.urlopen(request, timeout=35) as response:
        return json.load(response)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--monitor-ref", default="main")
    parser.add_argument("--apply", action="store_true", help="write canonical apps/*.json; default dry run")
    args = parser.parse_args()

    changes, skipped = plan_changes(args.root, load_snapshot(args.monitor_ref))
    for path, app, compact in changes:
        print(f"{'UPDATE' if args.apply else 'DRY-RUN'} {path.relative_to(args.root)} → {app['version']}")
        if args.apply:
            serialized = to_v2(app) if compact else app
            raw = path.read_text(encoding="utf-8")
            formatting = {"separators": (",", ":")} if raw.count("\n") <= 1 else {"indent": 2}
            path.write_text(json.dumps(serialized, ensure_ascii=False, **formatting) + "\n",
                            encoding="utf-8")
    for message in skipped:
        print("REVIEW", message)
    print(f"Catalog update candidates={len(changes)}, skipped={len(skipped)}, applied={args.apply}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
