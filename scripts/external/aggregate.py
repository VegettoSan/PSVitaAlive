#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from .identity import canonical_author_id, same_identity
from .merge import merge_group
from .neovitadb import fetch_candidates as fetch_neovita
from .normalizer import canonical_repo, normalize_text, normalize_version
from .overrides import apply_override, load_overrides
from .sources import Candidate, extract_catalog_items, fetch_json, fetch_vitadb, normalize_vitadb, normalize_vitadbtoo

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "sources" / "external_sources.json"
CATEGORY_MAP = ROOT / "sources" / "category_map.json"

SOURCE_NAMES = {
    "vitadb": "VitaDB",
    "vitadbtoo": "VitaDBtoo",
    "neovitadb": "NeoVitaDB",
}

AUTHOR_ICON_FALLBACK = (
    "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png"
)


def load_local():
    apps = []
    authors = {}
    categories = []
    for path in sorted((ROOT / "apps").glob("*.json")):
        with path.open(encoding="utf-8") as f:
            apps.append((path, json.load(f)))
    for path in sorted((ROOT / "authors").glob("*.json")):
        with path.open(encoding="utf-8") as f:
            value = json.load(f)
            authors[value["id"]] = value
    for path in sorted((ROOT / "categories").glob("*.json")):
        with path.open(encoding="utf-8") as f:
            categories.append(json.load(f))
    return apps, authors, categories


def external_config():
    with CONFIG.open(encoding="utf-8") as f:
        return json.load(f)


def external_candidates():
    config = external_config()
    result = []
    source_counts = Counter()
    for source in config.get("sources", []):
        if not source.get("enabled"):
            continue
        source_id = source.get("id")
        before = len(result)
        try:
            if source_id == "vitadb":
                items = fetch_vitadb(source.get("url"))
                for item in items:
                    candidate = normalize_vitadb(item)
                    if candidate.name and candidate.platform == "vita" and candidate.title_id:
                        result.append(candidate)
            elif source_id == "vitadbtoo":
                data = fetch_json(source["url"])
                for item in extract_catalog_items(data, source_id):
                    candidate = normalize_vitadbtoo(item)
                    if candidate.name and candidate.platform == "vita" and candidate.title_id:
                        result.append(candidate)
            elif source_id == "neovitadb":
                for item in fetch_neovita():
                    if item.get("platform", "vita") == "vita":
                        from .neovitadb import normalize
                        candidate = normalize(item)
                        if candidate.name and candidate.title_id:
                            result.append(candidate)
            source_counts[source_id] = len(result) - before
        except Exception as exc:
            source_counts[source_id] = 0
            print(
                f"warning: external source {source_id} unavailable: {type(exc).__name__}: {exc}",
                file=sys.stderr,
            )
    print("External source counts:")
    for source_id, count in source_counts.items():
        print(f"  {source_id}: {count}")
    return result


def group_candidates(candidates):
    groups = []
    for candidate in candidates:
        placed = False
        for group in groups:
            if same_identity(candidate, group[0]):
                group.append(candidate)
                placed = True
                break
        if not placed:
            groups.append([candidate])
    return groups


def category_map():
    if not CATEGORY_MAP.exists():
        return {}
    with CATEGORY_MAP.open(encoding="utf-8") as f:
        return json.load(f).get("mappings", {})


def normalize_categories(candidate: Candidate, categories, local_app: dict | None = None):
    valid = {item.get("id"): item for item in categories}

    if local_app:
        category_id = local_app.get("category_id")
        category = valid.get(category_id)
        if category:
            allowed = {item.get("id") for item in category.get("subcategories", [])}
            subs = [item for item in local_app.get("subcategory_ids", []) if item in allowed]
            return category_id, subs

    mappings = category_map()
    raw = str(candidate.category_raw or "").strip().lower()
    mapping = mappings.get(raw)
    if not mapping:
        mapping = mappings.get("game") if candidate.platform == "vita" else None
    category_id = mapping.get("category_id") if mapping else None
    if category_id not in valid:
        return None, []
    allowed = [item.get("id") for item in valid[category_id].get("subcategories", [])]
    requested = list(mapping.get("subcategory_ids", [])) if mapping else []
    subs = [item for item in requested if item in allowed]
    return category_id, subs or (["other"] if "other" in allowed else ([allowed[0]] if allowed else []))


def author_profile(author_id, name, repo_url=None):
    links = []
    repo = canonical_repo(repo_url)
    if repo:
        owner = repo.split("/", 1)[0]
        links.append({
            "type": "GitHub",
            "name": "GitHub",
            "url": f"https://github.com/{owner}",
            "recommended": True,
        })
        avatar = f"https://github.com/{owner}.png"
    else:
        avatar = ""
    icon = avatar if avatar else AUTHOR_ICON_FALLBACK
    return {
        "id": author_id,
        "name": name or author_id,
        "avatar": avatar if avatar else AUTHOR_ICON_FALLBACK,
        "bio": "",
        "links": links,
        "icon": icon,
    }


def resolve_author_id(name: str, authors: dict, group: list[Candidate]) -> str:
    raw = str(name or "").strip()
    if raw in authors:
        return raw
    normalized = normalize_text(raw)
    for author_id, profile in authors.items():
        if normalize_text(profile.get("name")) == normalized:
            return author_id
    for candidate in group:
        repo = canonical_repo(candidate.repository_url)
        if repo:
            owner = repo.split("/", 1)[0]
            for author_id, profile in authors.items():
                for link in profile.get("links", []) or []:
                    url = str(link.get("url", "")).rstrip("/").lower()
                    if url in {
                        f"https://github.com/{owner}".lower(),
                        f"https://github.com/{owner}/".lower(),
                    }:
                        return author_id
    return canonical_author_id(raw)


def app_id_from_candidate(candidate: Candidate, existing_ids: set[str]) -> str:
    value = normalize_text(candidate.name)
    value = re.sub(r"[^a-z0-9_-]+", "-", value).strip("-") or "external-homebrew"
    if value not in existing_ids:
        return value
    suffix = str(candidate.title_id or "external").strip().lower()
    candidate_id = f"{value}-{suffix}"
    if candidate_id not in existing_ids:
        return candidate_id
    index = 2
    while f"{candidate_id}-{index}" in existing_ids:
        index += 1
    return f"{candidate_id}-{index}"


def write_json_if_changed(path: Path, value: dict) -> bool:
    serialized = json.dumps(value, ensure_ascii=False, indent=2) + "\n"
    if path.exists():
        try:
            current = path.read_text(encoding="utf-8")
            if current == serialized:
                return False
        except OSError:
            pass
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(serialized, encoding="utf-8")
    return True


def persist_source_files(root: Path, final_apps: list[dict], final_authors: dict[str, dict], clean_rebuild: bool = False):
    changed_apps = 0
    changed_authors = 0
    final_ids = {app.get("id") for app in final_apps if app.get("id")}

    if clean_rebuild:
        for path in sorted((root / "apps").glob("*.json")):
            if path.stem not in final_ids:
                path.unlink()
                changed_apps += 1

    for app in sorted(final_apps, key=lambda item: str(item.get("id", ""))):
        app_id = app.get("id")
        if not app_id:
            continue
        path = root / "apps" / f"{app_id}.json"
        if write_json_if_changed(path, app):
            changed_apps += 1

    for author_id, author in sorted(final_authors.items()):
        path = root / "authors" / f"{author_id}.json"
        if "icon" not in author or not author.get("icon"):
            author = dict(author)
            author["icon"] = author.get("avatar") or AUTHOR_ICON_FALLBACK
            if not author.get("avatar"):
                author["avatar"] = AUTHOR_ICON_FALLBACK
            final_authors[author_id] = author
        if not path.exists() and write_json_if_changed(path, author):
            changed_authors += 1

    print("Canonical source persistence:")
    print(f"  Clean rebuild: {'yes' if clean_rebuild else 'no'}")
    print(f"  App JSON files changed/created/deleted: {changed_apps}")
    print(f"  Author JSON files created: {changed_authors}")


def build(root: Path):
    clean_rebuild = os.environ.get("VITAHUB_REBUILD_FROM_EXTERNAL", "").lower() in {"1", "true", "yes"}
    local_apps, authors, categories = load_local()
    if clean_rebuild:
        local_apps = []

    locals_as_candidates = []
    local_by_id = {}
    local_ids = {app.get("id") for _, app in local_apps if app.get("id")}

    for _, app in local_apps:
        repo = next((x.get("url") for x in app.get("links", []) if x.get("type") == "Repository"), None)
        local_candidate = Candidate(
            source_id="local", source_item_id=app.get("id"), title_id=app.get("title_id"), name=app.get("name", ""),
            author_names=list(app.get("author_ids", [])), repository_url=repo, release_page=None,
            version=app.get("version"), version_date=app.get("version_date"), description=app.get("description"),
            long_description=app.get("long_description"), requirements=app.get("requirements"), changelog=app.get("changelog"),
            icon=app.get("icon"), screenshots=list(app.get("screenshots", [])),
            download_url=next((x.get("url") for x in app.get("links", []) if x.get("type") == "Download" and x.get("recommended")), None),
            size=app.get("size"), category_raw=app.get("category_id"), platform="vita",
        )
        locals_as_candidates.append(local_candidate)
        local_by_id[app.get("id")] = app

    external = external_candidates()
    all_candidates = locals_as_candidates + external
    groups = group_candidates(all_candidates)
    overrides = load_overrides(root)
    final_apps = []
    final_authors = dict(authors)
    conflicts = []
    new_external = 0
    merged_external = 0
    override_count = 0
    updated_local = 0
    source_priority = {item.get("id"): int(item.get("priority", 0)) for item in external_config().get("sources", [])}

    for group in groups:
        merged = merge_group(group)
        local = next((x for x in group if x.source_id == "local"), None)
        local_app = local_by_id.get(local.source_item_id) if local else None
        if local and any(x.source_id != "local" for x in group):
            merged_external += 1
        if not local:
            new_external += 1

        app_id = local.source_item_id if local else None
        if not app_id:
            app_id = app_id_from_candidate(merged, local_ids)
            local_ids.add(app_id)

        author_ids = []
        from .sources import _split_author_field
        expanded_names = []
        for author_name in merged.author_names:
            parts = _split_author_field(author_name)
            expanded_names.extend(parts if parts else [author_name])
        for author_name in expanded_names:
            aid = resolve_author_id(author_name, final_authors, group)
            if aid not in author_ids:
                author_ids.append(aid)
            if aid not in final_authors:
                repo_url = next((x.repository_url for x in group if author_name in x.author_names and x.repository_url), None)
                final_authors[aid] = author_profile(aid, author_name, repo_url)

        category_id, subcategory_ids = normalize_categories(merged, categories, local_app)
        if not category_id:
            conflicts.append({"type": "unmapped_category", "app": app_id, "value": merged.category_raw})
            if local_app:
                final_apps.append(dict(local_app))
            continue

        app = dict(local_app) if local_app else {}
        old_version = app.get("version")
        app.update({
            "id": app_id,
            "title_id": merged.title_id or app.get("title_id", ""),
            "name": merged.name or app.get("name", "Unnamed Homebrew"),
            "description": merged.description if merged.description is not None else app.get("description", ""),
            "long_description": merged.long_description if merged.long_description is not None else app.get("long_description", ""),
            "author_ids": list(dict.fromkeys(author_ids)) or app.get("author_ids", []),
            "category_id": category_id,
            "subcategory_ids": subcategory_ids,
            "version": merged.version or app.get("version", "0"),
            "version_date": (merged.version_date or app.get("version_date") or datetime.now(timezone.utc).date().isoformat())[:10],
            "requirements": merged.requirements if merged.requirements is not None else app.get("requirements", ""),
            "size": int(merged.size or app.get("size") or 1),
            "status": app.get("status", "Legacy") if local_app else "Legacy",
            "icon": merged.icon or app.get("icon", ""),
            "screenshots": merged.screenshots or app.get("screenshots", []),
        })
        if local and old_version and app.get("version") != old_version:
            updated_local += 1

        existing_links = list(app.get("links", []))
        existing_urls = {item.get("url") for item in existing_links if isinstance(item, dict)}
        matching_external = []
        target_version = normalize_version(merged.version)
        for candidate in group:
            if not candidate.download_url:
                continue
            if normalize_version(candidate.version) == target_version:
                matching_external.append(candidate)

        matching_external.sort(key=lambda item: source_priority.get(item.source_id, 0), reverse=True)
        for candidate in matching_external:
            url = candidate.download_url
            if not url or url in existing_urls:
                continue
            label = SOURCE_NAMES.get(candidate.source_id, candidate.source_id)
            existing_links.append({
                "type": "Download",
                "name": f"{label} VPK",
                "url": url,
            })
            existing_urls.add(url)

        release_candidates = sorted(
            [item for item in group if item.release_page and normalize_version(item.version) == target_version],
            key=lambda item: source_priority.get(item.source_id, 0),
            reverse=True,
        )
        for candidate in release_candidates:
            url = candidate.release_page
            if url in existing_urls:
                continue
            existing_links.append({
                "type": "Official Website",
                "name": f"{SOURCE_NAMES.get(candidate.source_id, candidate.source_id)} Release Page",
                "url": url,
            })
            existing_urls.add(url)

        if matching_external and not any(item.get("recommended") is True for item in existing_links if isinstance(item, dict)):
            preferred = matching_external[0].download_url
            for item in existing_links:
                if item.get("url") == preferred:
                    item["recommended"] = True
                    break

        repo_candidates = sorted(
            [item for item in group if item.repository_url],
            key=lambda item: source_priority.get(item.source_id, 0),
            reverse=True,
        )
        for candidate in repo_candidates[:1]:
            url = candidate.repository_url
            if url not in existing_urls:
                existing_links.append({"type": "Repository", "name": "Repositorio", "url": url})
                existing_urls.add(url)

        app["links"] = existing_links

        if not local:
            newest_external = max(group, key=lambda item: (normalize_version(item.version), source_priority.get(item.source_id, 0), str(item.version_date or "")))
            app["source_name"] = SOURCE_NAMES.get(newest_external.source_id, newest_external.source_id)
            app["source_id"] = newest_external.source_item_id or newest_external.source_id
            app["source_url"] = newest_external.repository_url or newest_external.release_page or newest_external.download_url or ""
            app["release_page"] = newest_external.release_page or ""
            app["updated_at"] = datetime.now(timezone.utc).isoformat()

        if app_id in overrides:
            app = apply_override(app, overrides[app_id])
            override_count += 1
        final_apps.append(app)

    persist_source_files(root, final_apps, final_authors, clean_rebuild=clean_rebuild)

    print("External aggregation summary:")
    print(f"  Clean rebuild from external sources: {'yes' if clean_rebuild else 'no'}")
    print(f"  Local applications used as input: {len(local_apps)}")
    print(f"  External candidates: {len(external)}")
    print(f"  Deduplicated application groups: {len(groups)}")
    print(f"  New external applications: {new_external}")
    print(f"  Existing applications enriched/merged: {merged_external}")
    print(f"  Existing applications with newer external version: {updated_local}")
    print(f"  Overrides applied: {override_count}")
    print(f"  Conflicts: {len(conflicts)}")
    return final_apps, list(final_authors.values()), categories, conflicts
