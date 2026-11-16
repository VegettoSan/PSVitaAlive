#!/usr/bin/env python3
"""Safely update *canonical* apps/*.json from the release monitor snapshot.

Never writes generated catalog.json. Deliberately conservative:
- Github stable releases and numeric higher versions only.
- One canonical application must explicitly reference the upstream repository.
- One VPK release asset and a unique GitHub or Archive.org VPK Download link.
- Standalone VPK apps only: complicated Game Files / plugin setups need review.
- Never changes Title ID, author, category, extra links or app status.
- Keeps the earlier Archive.org VPK as a clearly labelled old-version Mirror.
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
NUMERIC_VERSION = re.compile(r"^[vV]?\.?(\d+(?:\.\d+){0,3})$")
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


def archive_vpk_link(url: object) -> bool:
    if not isinstance(url, str):
        return False
    parsed = urlparse(url.strip())
    return (parsed.scheme == "https"
            and parsed.hostname in {"archive.org", "www.archive.org"}
            and parsed.path.startswith("/download/")
            and len(parsed.path.split("/")) >= 4
            and parsed.path.lower().endswith(".vpk"))


def select_current_download(links: list) -> tuple[dict | None, str]:
    recommended = [link for link in links
                   if isinstance(link, dict) and link.get("recommended") is True]
    if len(recommended) > 1:
        return None, "multiple recommended links"
    if recommended:
        chosen = recommended[0]
        if str(chosen.get("type") or "").lower() != "download":
            return None, "recommended link is not a VPK Download"
        return chosen, "ready"
    download_links = [link for link in links if isinstance(link, dict)
                      and str(link.get("type") or "").lower() == "download"]
    if len(download_links) != 1:
        return None, "no unique current Download link"
    return download_links[0], "ready"


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

    current_link, reason = select_current_download(links)
    if current_link is None:
        return None, reason
    current_url = current_link.get("url")
    current_archive = archive_vpk_link(current_url)
    current_github = download_belongs_to_repo(current_url, repo)
    if not (current_archive or current_github):
        return None, "current Download is neither official GitHub nor Archive.org VPK"
    if not urlparse(str(current_url)).path.lower().endswith(".vpk"):
        return None, "current Download is not a direct VPK"

    proposal = json.loads(json.dumps(app))
    proposal["version"] = str(item["tag"]).lstrip("vV.")
    proposal["version_date"] = when
    proposal["size"] = size
    index = links.index(current_link)

    # Preserve previous Archive.org payload as a clearly labelled old Mirror.
    # The new GitHub VPK is the only recommended current Download, and thus
    # remains unbacked until its current primary URL switches to Archive.org.
    if current_archive:
        old_link = proposal["links"][index]
        old_archive_url = old_link.get("url")
        archive_exists = any(
            isinstance(link, dict)
            and link.get("url") == old_archive_url
            and str(link.get("type") or "").lower() == "mirror"
            for link in proposal["links"]
        )
        if not archive_exists:
            previous = dict(old_link)
            previous["type"] = "Mirror"
            previous["name"] = f"Archive.org - respaldo anterior ({app['version']})"
            previous["recommended"] = False
            if "size" not in previous and isinstance(app.get("size"), int):
                previous["size"] = app["size"]
            proposal["links"].append(previous)

    proposal["links"][index]["url"] = asset_url
    if "size" in current_link:
        proposal["links"][index]["size"] = size
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
