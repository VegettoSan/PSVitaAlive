#!/usr/bin/env python3
from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path

from .identity import canonical_author_id, same_identity
from .merge import merge_group
from .neovitadb import fetch_candidates as fetch_neovita
from .overrides import apply_override, load_overrides
from .sources import Candidate, fetch_json, normalize_vitadbtoo
from .normalizer import canonical_repo

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "sources" / "external_sources.json"
CATEGORY_MAP = ROOT / "sources" / "category_map.json"


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


def external_candidates():
    with CONFIG.open(encoding="utf-8") as f:
        config = json.load(f)
    result = []
    for source in config.get("sources", []):
        if not source.get("enabled"):
            continue
        try:
            if source["id"] == "vitadbtoo":
                data = fetch_json(source["url"])
                if isinstance(data, list):
                    for item in data:
                        candidate = normalize_vitadbtoo(item)
                        if candidate.name and candidate.platform == "vita" and candidate.title_id:
                            result.append(candidate)
            elif source["id"] == "neovitadb":
                for item in fetch_neovita():
                    if item.get("platform", "vita") == "vita":
                        from .neovitadb import normalize
                        candidate = normalize(item)
                        if candidate.name and candidate.title_id:
                            result.append(candidate)
        except Exception as exc:
            print(f"warning: external source {source.get('id')} unavailable: {exc}", file=sys.stderr)
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


def normalize_categories(candidate: Candidate, categories):
    mappings = category_map()
    raw = str(candidate.category_raw or "").strip().lower()
    mapping = mappings.get(raw)
    if not mapping:
        # Safe fallback for old catalogs that use VitaDB-style generic game tags.
        mapping = mappings.get("game") if candidate.platform == "vita" else None
    valid = {item.get("id"): item for item in categories}
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
        links.append({"type": "GitHub", "name": "GitHub", "url": f"https://github.com/{owner}", "recommended": True})
        avatar = f"https://github.com/{owner}.png"
    else:
        avatar = ""
    return {"id": author_id, "name": name or author_id, "avatar": avatar, "bio": "", "links": links}


def build(root: Path):
    local_apps, authors, categories = load_local()
    locals_as_candidates = []
    local_by_id = {}
    for _, app in local_apps:
        repo = next((x.get("url") for x in app.get("links", []) if x.get("type") == "Repository"), None)
        local_candidate = Candidate(
            source_id="local", source_item_id=app.get("id"), title_id=app.get("title_id"), name=app.get("name", ""),
            author_names=list(app.get("author_ids", [])), repository_url=repo, release_page=None,
            version=app.get("version"), version_date=app.get("version_date"), description=app.get("description"),
            long_description=app.get("long_description"), requirements=app.get("requirements"), changelog=app.get("changelog"),
            icon=app.get("icon"), screenshots=list(app.get("screenshots", [])), download_url=next((x.get("url") for x in app.get("links", []) if x.get("type") == "Download" and x.get("recommended")), None),
            size=app.get("size"), category_raw=app.get("category_id"), platform="vita")
        locals_as_candidates.append(local_candidate)
        local_by_id[app.get("id")] = app
    all_candidates = locals_as_candidates + external_candidates()
    groups = group_candidates(all_candidates)
    overrides = load_overrides(root)
    final_apps, final_authors = [], dict(authors)
    conflicts = []
    for group in groups:
        merged = merge_group(group)
        local = next((x for x in group if x.source_id == "local"), None)
        app_id = local.source_item_id if local else None
        if not app_id:
            slug = canonical_author_id(merged.name)
            suffix = merged.title_id.lower() if merged.title_id else "external"
            app_id = f"{slug}-{suffix}".strip("-")
        author_ids = []
        for author_name in merged.author_names:
            aid = author_name if author_name in authors else canonical_author_id(author_name)
            author_ids.append(aid)
            if aid not in final_authors:
                repo_url = next((x.repository_url for x in group if author_name in x.author_names and x.repository_url), None)
                final_authors[aid] = author_profile(aid, author_name, repo_url)
        category_id, subcategory_ids = normalize_categories(merged, categories)
        if not category_id:
            conflicts.append({"type": "unmapped_category", "app": app_id, "value": merged.category_raw})
            continue
        app = dict(local_by_id.get(app_id, {})) if app_id in local_by_id else {}
        app.update({
            "id": app_id, "title_id": merged.title_id or "", "name": merged.name,
            "description": merged.description or "", "long_description": merged.long_description or "",
            "author_ids": list(dict.fromkeys(author_ids)), "category_id": category_id, "subcategory_ids": subcategory_ids,
            "version": merged.version or app.get("version", "0"), "version_date": (merged.version_date or app.get("version_date") or datetime.utcnow().date().isoformat())[:10],
            "requirements": merged.requirements or app.get("requirements", ""), "size": int(merged.size or app.get("size") or 1),
            "status": app.get("status", "Legacy"), "icon": merged.icon or app.get("icon", ""),
            "screenshots": merged.screenshots or app.get("screenshots", []),
        })
        existing_links = list(app.get("links", []))
        if merged.download_url and not any(item.get("url") == merged.download_url for item in existing_links if isinstance(item, dict)):
            existing_links.append({"type": "Download", "name": "External VPK", "url": merged.download_url})
        if existing_links and not any(item.get("recommended") is True for item in existing_links if isinstance(item, dict)):
            for item in existing_links:
                if item.get("type") == "Download":
                    item["recommended"] = True
                    break
        if merged.repository_url and not any(item.get("url") == merged.repository_url for item in existing_links if isinstance(item, dict)):
            existing_links.append({"type": "Repository", "name": "Repositorio", "url": merged.repository_url})
        app["links"] = existing_links
        if app_id in overrides:
            app = apply_override(app, overrides[app_id])
        final_apps.append(app)
    return final_apps, list(final_authors.values()), categories, conflicts
