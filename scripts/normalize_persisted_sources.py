#!/usr/bin/env python3
"""Normalize persisted canonical source JSON before CI validation.

The catalog generator enriches records in memory after external aggregation has
already persisted apps/authors. This pass mirrors the final normalization onto
the actual apps/*.json and authors/*.json files so the source layer and the
generated catalogs cannot diverge.

All media persisted by this pass is an absolute URL. Repository-local media is
converted to the public raw GitHub URL so GitHub Pages and the Vita client do
not depend on a relative filesystem path.

This pass also repairs legacy compound author records. External catalogs often
write several developers into one author field (for example "Aurora &
Jackiepooh & Rinnegatamante"). VitaHub requires one author JSON/profile per
developer, so compound profiles are expanded before the generated registry is
validated.
"""
from __future__ import annotations

import json
import re
import urllib.error
import urllib.request
from pathlib import Path
from urllib.parse import quote, urljoin, urlparse

ROOT = Path(__file__).resolve().parents[1]
RAW_ROOT = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/"
AUTHOR_FALLBACK = RAW_ROOT + "authors/icon/autoricon.png"

MEDIA_ROOTS = {
    "vitadbtoo": {
        "base": "https://raw.githubusercontent.com/DrDecki/VitaDBtoo-db/main/",
        "icon": "icons/",
        "screenshot": "screenshots/",
    },
    "vitadb": {
        "base": "https://www.rinnegatamante.eu/vitadb/",
        "icon": "icons/",
        "screenshot": "screenshots/",
    },
    "neovitadb": {
        "base": "https://raw.githubusercontent.com/robin994/NeoVitaDB-Catalog/main/",
        "icon": "icons_vita/",
        "screenshot": None,
    },
}


def split_values(value):
    if isinstance(value, list):
        result = []
        for item in value:
            result.extend(split_values(item))
        return result
    if not isinstance(value, str) or not value.strip():
        return []
    return [item.strip() for item in re.split(r"[;\r\n]+", value) if item.strip()]


def split_author_names(value):
    """Split human-readable compound author names into individual names."""
    if isinstance(value, list):
        result = []
        seen = set()
        for item in value:
            for name in split_author_names(item):
                key = name.casefold()
                if key and key not in seen:
                    seen.add(key)
                    result.append(name)
        return result
    if not isinstance(value, str) or not value.strip():
        return []
    text = re.sub(r"\s+", " ", value.replace("\u00a0", " ")).strip()
    text = re.sub(r"\s*&\s*", ",", text)
    text = re.sub(r"\s+\band\b\s+", ",", text, flags=re.I)
    text = re.sub(r"\s*\+\s*", ",", text)
    text = re.sub(r"[;\n\r]+", ",", text)
    result = []
    seen = set()
    for part in text.split(","):
        name = part.strip(" \t,;|")
        if not name:
            continue
        key = name.casefold()
        if key not in seen:
            seen.add(key)
            result.append(name)
    return result


def is_remote(value):
    return isinstance(value, str) and urlparse(value).scheme in {"http", "https"}


def remote_ok(url):
    headers = {"User-Agent": "VitaHub-Catalog-Generator/1.0"}
    request = urllib.request.Request(url, method="HEAD", headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return getattr(response, "status", 200) < 400
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return False
    except Exception:
        return True

    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return getattr(response, "status", 200) < 400
    except urllib.error.HTTPError as exc:
        return exc.code != 404
    except Exception:
        return True


def source_from_app(app):
    source = str(app.get("source_id") or app.get("source_name") or "").lower()
    if "vitadbtoo" in source:
        return "vitadbtoo"
    if source == "vitadb" or source.startswith("vitadb "):
        return "vitadb"
    if "neovitadb" in source:
        return "neovitadb"
    for link in app.get("links") or []:
        url = link.get("url") if isinstance(link, dict) else None
        if not isinstance(url, str):
            continue
        lower = url.lower()
        if "drdecki/vitadBtoo-db".lower() in lower or "drdecki.github.io/vitadBtoo-db".lower() in lower:
            return "vitadbtoo"
        if "rinnegatamante.eu/vitadb" in lower:
            return "vitadb"
        if "neovitadb-catalog" in lower:
            return "neovitadb"
    return ""


def resolve_source_media(value, source_hint, kind):
    """Resolve a source-relative media value and return only a usable URL."""
    if not isinstance(value, str) or not value.strip():
        return None
    clean = value.strip().lstrip("./").replace("\\", "/")

    if is_remote(clean):
        return clean if remote_ok(clean) else None

    config = MEDIA_ROOTS.get(source_hint, {})
    base = config.get("base")
    folder = config.get(kind)
    if not base or folder is None:
        return None

    prefix = folder.rstrip("/") + "/"
    relative = clean if clean.startswith(prefix) else prefix + clean
    encoded = "/".join(quote(part, safe="@:+,;=-._~") for part in relative.split("/"))
    url = urljoin(base, encoded)
    return url if remote_ok(url) else None


def resolve_local_raw(value):
    """Convert an existing repository-relative resource to its raw GitHub URL."""
    clean = value.strip().lstrip("./").replace("\\", "/")
    if is_remote(clean):
        return clean if remote_ok(clean) else None

    candidate = ROOT / clean
    try:
        candidate.resolve().relative_to(ROOT.resolve())
    except ValueError:
        return None

    if candidate.is_file():
        relative = candidate.relative_to(ROOT).as_posix()
        return RAW_ROOT + "/".join(quote(part, safe="@:+,;=-._~") for part in relative.split("/"))
    return None


def resolve_relative_screenshot(value, source_hint):
    clean = value.strip().lstrip("./").replace("\\", "/")
    if is_remote(clean):
        return clean if remote_ok(clean) else None

    local_raw = resolve_local_raw(clean)
    if local_raw and remote_ok(local_raw):
        return local_raw

    source_url = resolve_source_media(clean, source_hint, "screenshot")
    if source_url:
        return source_url

    for candidate_source in MEDIA_ROOTS:
        if candidate_source == source_hint:
            continue
        source_url = resolve_source_media(clean, candidate_source, "screenshot")
        if source_url:
            return source_url
    return None


def category_icon_url(category_id):
    """Return a validated VitaHub category icon URL for an application fallback."""
    if not isinstance(category_id, str) or not category_id.strip():
        return None
    category = category_id.strip().lower()
    if not re.fullmatch(r"[a-z0-9_-]+", category):
        return None
    url = RAW_ROOT + f"categories/icons/{quote(category, safe='_-')}.png"
    return url if remote_ok(url) else None


def normalize_app_icon(app, source_hint):
    """Normalize an app icon, falling back to the VitaHub category icon."""
    value = app.get("icon")
    if isinstance(value, str) and value.strip():
        if is_remote(value):
            if remote_ok(value):
                return value.strip()
        else:
            local_raw = resolve_local_raw(value)
            if local_raw and remote_ok(local_raw):
                return local_raw
            source_url = resolve_source_media(value, source_hint, "icon")
            if source_url:
                return source_url

    return category_icon_url(app.get("category_id"))


def normalize_apps():
    apps = []
    for path in sorted((ROOT / "apps").glob("*.json")):
        try:
            app = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue

        source_hint = source_from_app(app)

        requirements = app.get("requirements")
        if not isinstance(requirements, str) or not requirements.strip():
            app["requirements"] = "Not specified"

        icon = normalize_app_icon(app, source_hint)
        if icon:
            app["icon"] = icon
        else:
            app["icon"] = ""

        screenshots = []
        for item in split_values(app.get("screenshots") or []):
            resolved = resolve_relative_screenshot(item, source_hint)
            if resolved and resolved not in screenshots:
                screenshots.append(resolved)
            if len(screenshots) == 5:
                break

        if not screenshots and app.get("icon"):
            screenshots = [app["icon"]]

        app["screenshots"] = screenshots[:5]
        path.write_text(json.dumps(app, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        apps.append(app)
    return apps


def normalize_author_icon(author):
    """Normalize author avatar/icon and always provide the VitaHub fallback."""
    candidates = [author.get("avatar"), author.get("icon")]
    for value in candidates:
        if not isinstance(value, str) or not value.strip():
            continue
        value = value.strip()
        if is_remote(value):
            if remote_ok(value):
                return value
        else:
            local_raw = resolve_local_raw(value)
            if local_raw and remote_ok(local_raw):
                return local_raw
            clean = value.lstrip("./").replace("\\", "/")
            if not clean.startswith("authors/"):
                candidate = RAW_ROOT + "authors/icon/" + "/".join(
                    quote(part, safe="@:+,;=-._~") for part in clean.split("/")
                )
                if remote_ok(candidate):
                    return candidate

    return AUTHOR_FALLBACK


def normalize_author_links(links):
    """Deduplicate author links and allow at most one recommended link."""
    if not isinstance(links, list):
        return []

    normalized = []
    seen_urls = set()
    recommended_seen = False

    for link in links:
        if not isinstance(link, dict):
            continue
        url = link.get("url")
        if not isinstance(url, str) or not url.strip():
            continue
        url = url.strip()
        if url in seen_urls:
            continue

        item = dict(link)
        item["url"] = url
        recommended = bool(item.get("recommended", False))
        if recommended:
            if recommended_seen:
                item["recommended"] = False
            else:
                recommended_seen = True
        normalized.append(item)
        seen_urls.add(url)

    return normalized


def canonicalize_compound_authors(apps, authors):
    """Replace compound author IDs with one canonical ID per developer.

    Existing compound author files are deliberately retained for preservation,
    but applications stop referencing them. New individual profiles inherit
    useful metadata when available and always receive a valid fallback avatar.
    """
    by_name = {}
    for author_id, author in authors.items():
        name = author.get("name")
        if isinstance(name, str) and name.strip():
            by_name[name.casefold()] = author_id

    changed = 0
    created = 0

    for app in apps:
        raw_ids = app.get("author_ids")
        if not isinstance(raw_ids, list):
            raw_ids = [app.get("author_id")] if app.get("author_id") else []

        new_ids = []
        for raw_id in raw_ids:
            if not isinstance(raw_id, str) or not raw_id.strip():
                continue
            raw_id = raw_id.strip()
            profile = authors.get(raw_id)
            display = profile.get("name") if isinstance(profile, dict) else raw_id
            names = split_author_names(display)
            if len(names) <= 1:
                aid = raw_id
                if names:
                    name_key = names[0].casefold()
                    aid = by_name.get(name_key, aid)
                if aid not in new_ids:
                    new_ids.append(aid)
                continue

            for name in names:
                name_key = name.casefold()
                aid = by_name.get(name_key)
                if not aid:
                    # Use the same stable ID convention as the external identity
                    # layer without introducing a dependency cycle.
                    normalized = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
                    aid = normalized or "unknown-author"
                    base = aid
                    index = 2
                    while aid in authors and str(authors[aid].get("name", "")).casefold() != name_key:
                        aid = f"{base}-{index}"
                        index += 1
                    by_name[name_key] = aid

                if aid not in authors:
                    source_profile = profile if isinstance(profile, dict) else {}
                    authors[aid] = {
                        "id": aid,
                        "name": name,
                        "avatar": source_profile.get("avatar") or source_profile.get("icon") or AUTHOR_FALLBACK,
                        "bio": "",
                        "links": [],
                        "icon": source_profile.get("icon") or source_profile.get("avatar") or AUTHOR_FALLBACK,
                    }
                    created += 1
                if aid not in new_ids:
                    new_ids.append(aid)

        if new_ids != raw_ids or "author_id" in app:
            app["author_ids"] = new_ids
            app.pop("author_id", None)
            changed += 1

    return changed, created


def normalize_authors(apps):
    authors = {}
    for path in sorted((ROOT / "authors").glob("*.json")):
        try:
            author = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        if author.get("id"):
            authors[author["id"]] = author

    changed_apps, created_authors = canonicalize_compound_authors(apps, authors)

    by_author = {}
    for app in apps:
        links = app.get("links") if isinstance(app.get("links"), list) else []
        for author_id in app.get("author_ids") or []:
            bucket = by_author.setdefault(author_id, [])
            for link in links:
                if not isinstance(link, dict):
                    continue
                url = link.get("url")
                if not isinstance(url, str) or not url.strip():
                    continue
                if any(existing.get("url") == url for existing in bucket):
                    continue
                bucket.append(dict(link))

    for author_id, author in authors.items():
        links = author.get("links")
        if not isinstance(links, list) or not links:
            author["links"] = by_author.get(author_id, [])[:5]
        resolved_avatar = normalize_author_icon(author)
        author["avatar"] = resolved_avatar
        author["icon"] = resolved_avatar

        path = ROOT / "authors" / f"{author_id}.json"
        path.write_text(json.dumps(author, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    return changed_apps, created_authors


def main():
    apps = normalize_apps()
    changed_apps, created_authors = normalize_authors(apps)

    # Persist repaired application author_ids as part of the canonical source
    # layer. The workflow regenerates authors.json after this pass.
    for app in apps:
        path = ROOT / "apps" / f"{app.get('id')}.json"
        if app.get("id"):
            path.write_text(json.dumps(app, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(f"Persisted source normalization: {len(apps)} applications processed")
    print(f"Compound author groups repaired: {changed_apps}")
    print(f"Individual author profiles created: {created_authors}")


if __name__ == "__main__":
    main()
