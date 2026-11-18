#!/usr/bin/env python3
"""Safely update *canonical* apps/*.json from the release monitor snapshot.

Never writes generated catalog.json. Deliberately conservative:
- Stable releases by default; select newer pre-releases when opt-in, >7 days after the last stable or no stable exists.
- One canonical application explicitly references the upstream repository.
- Exactly one .vpk asset by metadata; never downloads or inspects files.
- Replace ONLY the existing Download named VPK (no extra buttons).
- Extra links (Game Files, Plugin, other Downloads) remain byte-for-byte unchanged.
- Shared repos and ambiguous VPK choices require manual review.
"""
from __future__ import annotations

import argparse
import json
import re
import urllib.request
from collections import Counter, defaultdict
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import quote, unquote, urlparse

try:
    from scripts.catalog_schema_v2 import to_v1, to_v2
except ModuleNotFoundError:
    from catalog_schema_v2 import to_v1, to_v2

MONITOR_REPO = "VegettoSan/PsVita-Homebrews-New-Releases"
MONITOR_SNAPSHOT = "data/upstream_latest.json"
SHA_RE = re.compile(r"^[a-fA-F0-9]{40}$")
# Explicitly reviewed GitHub folder repositories; add others after validating
# their folder README and per-VPK release conventions in the monitor.
FOLDER_SOURCE_REPOS = {"papilenote/vita-ports": "main"}
RELEASE_VERSION = re.compile(
    r"^[vV]?\.?(\d+(?:\.\d+){0,3})"
    r"(?:[-._]?(dev|test|alpha|a|beta|b|pre|preview|rc)(?:[-._]?(\d+))?)?$",
    re.IGNORECASE,
)
PRERELEASE_STAGES = {
    "dev": 0, "test": 0, "alpha": 1, "a": 1, "beta": 2,
    "b": 2, "pre": 3, "preview": 3, "rc": 4,
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
    """Numeric core, then dev/alpha/beta/pre/rc/stable and stage number."""
    if not isinstance(value, str):
        return None
    match = RELEASE_VERSION.fullmatch(value.strip())
    if not match:
        return None
    numbers = tuple(int(n) for n in match.group(1).split("."))
    core = numbers + (0,) * (4 - len(numbers))
    stage = (PRERELEASE_STAGES[match.group(2).lower()]
             if match.group(2) else 5)
    return core + (stage, int(match.group(3) or 0))


def is_prerelease_version(value: object) -> bool:
    parsed = numeric_version(value)
    return parsed is not None and parsed[4] < 5


def newer_version(current: object, proposed: object) -> bool:
    old, new = numeric_version(current), numeric_version(proposed)
    return old is not None and new is not None and new > old


def release_date(value: object) -> str | None:
    raw = str(value or "")[:10]
    try:
        return date.fromisoformat(raw).isoformat()
    except ValueError:
        return None

def release_instant(value: object) -> datetime | None:
    """Strict UTC-aware ISO publication timestamp; never infer missing dates."""
    raw = str(value or "").strip()
    if not raw:
        return None
    try:
        result = datetime.fromisoformat(raw.replace("Z", "+00:00"))
        return result.astimezone(timezone.utc) if result.tzinfo is not None else None
    except (ValueError, OverflowError):
        return None


def automatic_prerelease_allowed(app: dict, pre: dict, stable: dict | None) -> bool:
    """Choose beta automatically only after >7 days or confirmed no stable.

    An explicit app-level opt-in may accept a newer beta sooner, but never
    displaces a stable release published *after* that prerelease.
    """
    if not newer_version(app.get("version"), pre.get("tag")):
        return False
    pre_date = release_instant(pre.get("published"))
    if pre_date is None:
        return False
    if stable is None:
        return (app.get("allow_prereleases") is True
                or (pre.get("platform") == "github"
                    and pre.get("no_stable_release") is True))
    stable_date = release_instant(stable.get("published"))
    if stable_date is None or pre_date <= stable_date:
        return False
    if not newer_version(stable.get("tag"), pre.get("tag")):
        return False
    return (app.get("allow_prereleases") is True
            or pre_date - stable_date > timedelta(days=7))



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


def release_update(app: dict, item: dict, repo: str, *, auto_prerelease: bool = False) -> tuple[dict | None, str]:
    if app.get("status") not in {"Verified", "Legacy"}:
        return None, "Archive or unsupported status"
    platform = str(item.get("platform") or "").lower()
    if platform not in {"github", "gitlab"}:
        return None, "not a GitHub/GitLab release"
    if item.get("prerelease") or is_prerelease_version(item.get("tag")):
        if not (app.get("allow_prereleases") is True or auto_prerelease):
            return None, "prerelease updates disabled for this app"
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
    # Auxiliary downloads and dependencies do not block a VPK update:
    # only the uniquely named Download/VPK entry may be touched. Game Files,
    # Data Files, plugins, mods, and alternative VPK configurations retain
    # their existing URLs and all metadata, regardless of compatibility.

    assets = [asset for asset in (item.get("assets") or [])
              if isinstance(asset, dict) and release_asset_is_vpk(asset)]
    if not assets:
        return None, "no .vpk asset: manual update required"
    if len(assets) > 1:
        return None, f"{len(assets)} .vpk assets: manual update required"
    asset = assets[0]
    asset_url = asset.get("download_url")
    # A GitHub repository may have changed owner/name while its official API
    # still resolves the old catalog URL. The monitor verifies the redirect
    # against the original repository numeric identity, and only then emits
    # these two fields. Never accept an arbitrary host supplied by a release.
    asset_repo = repo
    canonical = item.get("canonical_repo")
    if canonical is not None:
        if (platform != "github" or not isinstance(canonical, str)
                or not re.fullmatch(r"[a-zA-Z0-9_.-]+/[a-zA-Z0-9_.-]+", canonical)
                or type(item.get("repo_id")) is not int or item["repo_id"] <= 0):
            return None, "invalid canonical GitHub repository provenance"
        asset_repo = canonical.lower()
    if not release_asset_url_matches(asset_url, asset_repo, platform, str(item.get("tag") or "")):
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




def registered_folder_paths(app: dict) -> set[tuple[str, str, str]]:
    """Only explicit per-port GitHub tree links are accepted as identities."""
    found = set()
    for link in app.get("links") or []:
        if not isinstance(link, dict) or str(link.get("type") or "").strip().lower() not in {
            "repository", "official website"
        }:
            continue
        url = link.get("url")
        if not isinstance(url, str):
            continue
        parsed = urlparse(url.strip())
        if (parsed.scheme != "https" or parsed.hostname not in {"github.com", "www.github.com"}
                or parsed.username or parsed.password or parsed.port
                or parsed.query or parsed.fragment):
            continue
        parts = [unquote(part) for part in parsed.path.split("/") if part]
        if len(parts) != 5 or parts[2] != "tree":
            continue
        repo = (parts[0] + "/" + parts[1]).lower()
        branch, folder = parts[3], parts[4]
        if (FOLDER_SOURCE_REPOS.get(repo) != branch
                or not folder or folder in {".", ".."}
                or not re.fullmatch(r"[A-Za-z0-9_. -]+", folder)):
            continue
        found.add((repo, branch, folder.casefold()))
    return found


def current_folder_download(url: object, repo: str, folder: str) -> bool:
    """A previous immutable GitHub RAW VPK is a trusted source for this folder."""
    if not isinstance(url, str):
        return False
    parsed = urlparse(url.strip())
    if (parsed.scheme != "https" or parsed.hostname != "raw.githubusercontent.com"
            or parsed.query or parsed.fragment or parsed.username or parsed.password):
        return False
    parts = [unquote(p) for p in parsed.path.split("/") if p]
    return (len(parts) == 5
            and (parts[0] + "/" + parts[1]).lower() == repo
            and bool(SHA_RE.fullmatch(parts[2]))
            and parts[3].casefold() == folder.casefold()
            and parts[4].lower().endswith(".vpk"))


def folder_release_update(app: dict, item: dict, repo: str) -> tuple[dict | None, str]:
    """Promote one folder VPK only after verifying its source and app identity."""
    if app.get("status") not in {"Verified", "Legacy"}:
        return None, "Archive or unsupported status"
    if item.get("platform") != "github_folder" or item.get("branch") != FOLDER_SOURCE_REPOS.get(repo):
        return None, "unknown folder source"
    folder = item.get("folder")
    if not isinstance(folder, str) or (repo, item["branch"], folder.casefold()) not in registered_folder_paths(app):
        return None, "no matching registered folder"
    title = app.get("title_id")
    if (not isinstance(title, str) or title.upper() != item.get("title_id")
            or not re.fullmatch(r"[A-Z0-9]{9}", item.get("title_id") or "")):
        return None, "Title ID mismatch"
    tag = item.get("tag")
    if is_prerelease_version(tag) and app.get("allow_prereleases") is not True:
        return None, "prerelease updates disabled"
    if not newer_version(app.get("version"), tag):
        return None, "version not newer or cannot be compared safely"
    when = release_date(item.get("published"))
    if not when or (release_date(app.get("version_date"))
                    and when < str(app["version_date"])[:10]):
        return None, "invalid or older VPK publication date"
    commit_sha, blob_sha = item.get("commit_sha"), item.get("blob_sha")
    if (not isinstance(commit_sha, str) or not SHA_RE.fullmatch(commit_sha)
            or not isinstance(blob_sha, str) or not SHA_RE.fullmatch(blob_sha)):
        return None, "invalid commit/blob SHA"
    assets = item.get("assets")
    if not isinstance(assets, list) or len(assets) != 1 or not isinstance(assets[0], dict):
        return None, "VPK asset ambiguous"
    asset = assets[0]
    name = asset.get("name")
    size = asset.get("size")
    if (not isinstance(name, str) or not name.lower().endswith(".vpk")
            or not re.fullmatch(r"[^/\\\x00-\x1f]+\.vpk", name, re.I)
            or not isinstance(size, int) or isinstance(size, bool)
            or not 1024 <= size <= 100_000_000):
        return None, "invalid VPK asset metadata"
    # Strict source/commit/folder/file match. No arbitrary host or redirect URL.
    expected = (f"https://raw.githubusercontent.com/{repo}/{commit_sha}/"
                f"{quote(folder, safe='')}/{quote(name, safe='')}")
    if asset.get("download_url") != expected:
        return None, "untrusted or unpinned VPK download URL"
    links = app.get("links")
    current, reason = select_current_download(links)
    if current is None:
        return None, reason
    current_url = current.get("url")
    if not (archive_vpk_link(current_url)
            or current_folder_download(current_url, repo, folder)
            or normalized_repo(current_url) in app_repositories(app)):
        return None, "existing VPK link not a trusted source"
    proposal = json.loads(json.dumps(app))
    proposal["version"] = str(tag).lstrip("vV")
    proposal["version_date"] = when
    proposal["size"] = size
    index = links.index(current)
    proposal["links"][index]["url"] = expected
    if "size" in current:
        proposal["links"][index]["size"] = size
    return proposal, "ready"


def plan_changes(root: Path, snapshot: dict) -> tuple[list[tuple[Path, dict, bool]], list[str]]:
    if not isinstance(snapshot, dict) or snapshot.get("schema_version") != 1:
        raise ValueError("Unsupported upstream snapshot")
    releases = snapshot.get("releases")
    if not isinstance(releases, list):
        raise ValueError("Malformed release list")
    folders = snapshot.get("folder_releases", [])
    if not isinstance(folders, list):
        raise ValueError("Malformed folder release list")

    repo_index: dict[str, list[tuple[Path, dict, bool]]] = defaultdict(list)
    folder_index: dict[tuple[str, str, str], list[tuple[Path, dict, bool]]] = defaultdict(list)
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
        for folder_key in registered_folder_paths(app):
            folder_index[folder_key].append((path, app, compact))

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
        # The monitor keeps its latest release and optionally a previous
        # stable release. Stable-only apps must not be blocked by a beta.
        candidate = release
        if release.get("prerelease") or is_prerelease_version(release.get("tag")):
            stable = release.get("stable_release")
            if isinstance(stable, dict):
                trusted_stable = (
                    str(stable.get("repo") or "").lower() == repo
                    and str(stable.get("platform") or "").lower() == platform
                    and stable.get("prerelease") is False
                    and not is_prerelease_version(stable.get("tag"))
                )
                if not trusted_stable:
                    skipped.append(f"{repo}: invalid stable fallback: manual review")
                    continue
                if (app.get("allow_prereleases") is not True
                        or not newer_version(stable.get("tag"), release.get("tag"))):
                    # Stable wins on the same numeric core and on ambiguity.
                    candidate = stable
            elif app.get("allow_prereleases") is not True:
                if newer_version(app.get("version"), release.get("tag")):
                    skipped.append(f"{repo}: prerelease updates disabled for this app")
                continue

        proposal, reason = release_update(app, candidate, repo)
        if proposal is not None:
            changes.append((path, proposal, compact))
        elif newer_version(app.get("version"), candidate.get("tag")):
            skipped.append(f"{repo}: {reason}")
    # Folder VPKs are per-app records, not conventional GitHub Releases.
    # Repositories shared by many apps are safe only when folder + Title ID
    # identify one canonical JSON, with no duplicates anywhere in apps/.
    # If a snapshot repeats a folder (potentially with conflicting versions),
    # reject every candidate for that folder, including the first occurrence.
    folder_counts = Counter(
        (str(row.get("repo") or "").lower(), row.get("branch"),
         str(row.get("folder") or "").casefold())
        for row in folders if isinstance(row, dict)
        and row.get("platform") == "github_folder"
    )
    seen_folders = set()
    for item in folders:
        if not isinstance(item, dict) or item.get("platform") != "github_folder":
            continue
        repo = str(item.get("repo") or "").lower()
        branch, folder = item.get("branch"), item.get("folder")
        if (FOLDER_SOURCE_REPOS.get(repo) != branch or not isinstance(folder, str)):
            continue
        key = (repo, branch, folder.casefold())
        if key in seen_folders:
            continue
        seen_folders.add(key)
        if folder_counts[key] != 1:
            skipped.append(f"{repo}/{folder}: duplicate folder records; manual review")
            continue
        matches = folder_index.get(key, [])
        if len(matches) != 1:
            if len(matches) > 1:
                skipped.append(f"{repo}/{folder}: multiple catalog apps share folder")
            continue
        path, app, compact = matches[0]
        if title_ids[str(app.get("title_id") or "").upper()] != 1:
            skipped.append(f"{repo}/{folder}: duplicate Title ID")
            continue
        proposal, reason = folder_release_update(app, item, repo)
        if proposal is not None:
            changes.append((path, proposal, compact))
        elif newer_version(app.get("version"), item.get("tag")):
            skipped.append(f"{repo}/{folder}: {reason}")

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
