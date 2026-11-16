#!/usr/bin/env python3
"""Safely update *canonical* apps/*.json from the release monitor snapshot.

Never writes generated catalog.json. Deliberately conservative:
- GitHub/GitLab releases with strictly newer numeric version only.
- One canonical application explicitly references the upstream repository.
- Exactly one .vpk asset by metadata; never downloads or inspects files.
- Replace ONLY the existing Download named VPK (no extra buttons).
- Complex setups, shared repos and ambiguous cases require manual review.
"""
from __future__ import annotations

import argparse
import json
import re
import urllib.request
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path
from urllib.parse import unquote, urlparse

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
    """Return owner/repo for GitHub, gitlab:group/subgroup/repo for GitLab."""
    if not isinstance(url, str):
        return None
    parsed = urlparse(url.strip())
    if parsed.scheme != "https" or parsed.username or parsed.password or parsed.port:
        return None
    host = (parsed.hostname or "").lower()
    if host not in {"github.com", "www.github.com", "gitlab.com", "www.gitlab.com"}:
        return None
    parts = [unquote(part) for part in parsed.path.split("/") if part]
    if host in {"github.com", "www.github.com"}:
        parts = parts[:2]
        platform = "github"
    else:
        if "-" in parts:
            parts = parts[:parts.index("-")]
        platform = "gitlab"
    if len(parts) < 2:
        return None
    parts[-1] = parts[-1].removesuffix(".git")
    if not all(re.fullmatch(r"[a-zA-Z0-9_.-]+", item) for item in parts):
        return None
    project = "/".join(parts).lower()
    return f"gitlab:{project}" if platform == "gitlab" else project


def app_repositories(app: dict) -> set[str]:
    urls = [app.get("source_url")]
    for link in app.get("links") or []:
        if isinstance(link, dict) and str(link.get("type") or "").lower() == "repository":
            urls.append(link.get("url"))
    return {repo for url in urls if (repo := normalized_repo(url))}


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


def release_asset_is_vpk(asset: dict) -> bool:
    """GitLab may call the asset 'VPK', with its .vpk extension in the URL."""
    if str(asset.get("name") or "").lower().endswith(".vpk"):
        return True
    for url in (asset.get("download_url"), asset.get("original_url")):
        if isinstance(url, str) and unquote(urlparse(url).path).lower().endswith(".vpk"):
            return True
    return False


def release_asset_url_matches(url: object, repo: str, platform: str, tag: str) -> bool:
    """Check only provider/project/tag metadata; do not fetch any VPK."""
    if not isinstance(url, str) or not tag:
        return False
    parsed = urlparse(url.strip())
    if parsed.scheme != "https" or parsed.username or parsed.password or parsed.port:
        return False
    if platform == "github":
        if not download_belongs_to_repo(url, repo):
            return False
        parts = [unquote(part) for part in parsed.path.split("/") if part]
        return (len(parts) == 6 and parts[2:4] == ["releases", "download"]
                and parts[4] == tag and parts[5].lower().endswith(".vpk"))
    if platform == "gitlab":
        if (parsed.hostname or "").lower() not in {"gitlab.com", "www.gitlab.com"}:
            return False
        parts = [unquote(part) for part in parsed.path.split("/") if part]
        expected = repo.lower().split("/")
        if len(expected) < 2 or [p.lower() for p in parts[:len(expected)]] != expected:
            return False
        path = parts[len(expected):]
        # GitLab release permalink (direct_asset_url), whose suffix can be
        # arbitrary; the matching release asset must identify a .vpk.
        if (len(path) >= 5 and path[:2] == ["-", "releases"]
                and path[2] == tag and path[3] == "downloads"):
            return True
        # File attached through this project's GitLab uploads.
        return (len(path) >= 4 and path[:2] == ["-", "uploads"]
                and unquote(parsed.path).lower().endswith(".vpk"))
    return False


def select_current_download(links: list) -> tuple[dict | None, str]:
    """Choose only the existing Download button named VPK, not other variants."""
    if not isinstance(links, list):
        return None, "missing links"
    selected = [
        link for link in links if isinstance(link, dict)
        and str(link.get("type") or "").strip().lower() == "download"
        and str(link.get("name") or "").strip().casefold() == "vpk"
    ]
    if len(selected) != 1:
        return None, "no unique Download button named VPK: manual review"
    return selected[0], "ready"


def release_update(app: dict, item: dict, repo: str) -> tuple[dict | None, str]:
    if app.get("status") not in {"Verified", "Legacy"}:
        return None, "Archive or unsupported status"
    platform = str(item.get("platform") or "").lower()
    if platform not in {"github", "gitlab"} or item.get("prerelease"):
        return None, "not a stable GitHub/GitLab release"
    # Check the canonical catalog version FIRST. When the developer's release
    # is already catalogued, keep every manually preserved Archive.org URL,
    # link order, recommendation and date untouched on later monitor scans.
    # Only a strictly newer version may ever replace the current VPK URL.
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
              if isinstance(asset, dict) and release_asset_is_vpk(asset)]
    if not assets:
        return None, "no .vpk asset: manual update required"
    if len(assets) > 1:
        return None, f"{len(assets)} .vpk assets: manual update required"
    asset = assets[0]
    asset_url = asset.get("download_url")
    if not release_asset_url_matches(asset_url, repo, platform, str(item.get("tag") or "")):
        return None, "unique VPK lacks a trusted GitHub/GitLab release link"
    size = asset.get("size")
    size = size if isinstance(size, int) and not isinstance(size, bool) and size > 0 else None

    current_link, reason = select_current_download(links)
    if current_link is None:
        return None, reason
    current_url = current_link.get("url")
    current_archive = archive_vpk_link(current_url)
    current_source = normalized_repo(current_url)
    # Some catalog records list both GitHub and GitLab sources. The existing
    # VPK link may point to either *registered* source. Never trust a random
    # unrelated download domain.
    if not (current_archive or current_source in app_repositories(app)):
        return None, "current VPK Download is not a registered source or Archive.org"

    proposal = json.loads(json.dumps(app))
    proposal["version"] = str(item["tag"]).lstrip("vV.")
    proposal["version_date"] = when
    if size is not None:
        proposal["size"] = size
    else:
        # Do not report an old version's size for the new VPK.
        proposal.pop("size", None)
    index = links.index(current_link)

    # Keep exactly the same buttons in the same order, no old-version Mirror.
    proposal["links"][index]["url"] = asset_url
    if size is not None:
        if "size" in current_link:
            proposal["links"][index]["size"] = size
    else:
        proposal["links"][index].pop("size", None)
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
        platform = str(release.get("platform") or "").lower()
        if (platform not in {"github", "gitlab"}
                or not re.fullmatch(r"[a-z0-9_.-]+(?:/[a-z0-9_.-]+)+", repo)
                or (platform == "github" and repo.count("/") != 1)):
            continue
        key = f"gitlab:{repo}" if platform == "gitlab" else repo
        if key in seen:
            continue
        seen.add(key)
        matches = repo_index.get(key, [])
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
    # A canonical Homebrew may reference more than one upstream repository.
    # Never overwrite a single apps/*.json twice in one scan, which would make
    # the chosen version dependent on release iteration order.
    path_counts = Counter(path for path, _proposal, _compact in changes)
    conflicted = {path for path, count in path_counts.items() if count > 1}
    if conflicted:
        changes = [candidate for candidate in changes if candidate[0] not in conflicted]
        for path in sorted(conflicted):
            skipped.append(f"{path.name}: multiple upstream release candidates; manual review")
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
    parser.add_argument("--apply", action="store_true",
                        help="write canonical apps/*.json; default dry run")
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
    print(f"Catalog update candidates={len(changes)}, skipped={len(skipped)}, applied={args.apply}; "
          "VPK selection uses release asset metadata only (no VPK downloads)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
