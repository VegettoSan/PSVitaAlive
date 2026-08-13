#!/usr/bin/env python3
"""Normalize persisted canonical source JSON before CI validation.

The catalog generator enriches records in memory after external aggregation has
already persisted apps/authors. This pass mirrors the final normalization onto
the actual apps/*.json and authors/*.json files so the source layer and the
generated catalogs cannot diverge.
"""
from __future__ import annotations

import json
import re
import urllib.error
import urllib.request
from pathlib import Path
from urllib.parse import quote, urljoin, urlparse

ROOT = Path(__file__).resolve().parents[1]

MEDIA_ROOTS = {
    "vitadbtoo": ("https://raw.githubusercontent.com/DrDecki/VitaDBtoo-db/main/", "screenshots/"),
    "vitadb": ("https://www.rinnegatamante.eu/vitadb/", "screenshots/"),
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
    for link in app.get("links") or []:
        url = link.get("url") if isinstance(link, dict) else None
        if not isinstance(url, str):
            continue
        if "DrDecki/VitaDBtoo-db" in url or "drdecki.github.io/VitaDBtoo-db" in url:
            return "vitadbtoo"
        if "rinnegatamante.eu/vitadb" in url:
            return "vitadb"
    return ""


def resolve_relative_screenshot(value, source_hint):
    clean = value.strip().lstrip("./").replace("\\", "/")
    if is_remote(clean):
        return clean if remote_ok(clean) else None

    # Local repository resource, accepting both repo-root and apps-relative paths.
    for candidate in (ROOT / clean, ROOT / "screenshots" / Path(clean).name):
        try:
            candidate.resolve().relative_to(ROOT.resolve())
        except ValueError:
            continue
        if candidate.is_file():
            return candidate.relative_to(ROOT).as_posix()

    roots = []
    if source_hint in MEDIA_ROOTS:
        roots.append(MEDIA_ROOTS[source_hint])
    for key, root in MEDIA_ROOTS.items():
        if key != source_hint:
            roots.append(root)

    for base, folder in roots:
        relative = clean
        prefix = folder.rstrip("/") + "/"
        if not relative.startswith(prefix):
            relative = prefix + relative
        encoded = "/".join(quote(part, safe="@:+,;=-._~") for part in relative.split("/"))
        url = urljoin(base, encoded)
        if remote_ok(url):
            return url
    return None


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

        icon = app.get("icon")
        if isinstance(icon, str) and icon.strip() and is_remote(icon) and not remote_ok(icon):
            app["icon"] = ""
            icon = ""

        screenshots = []
        for item in split_values(app.get("screenshots") or []):
            resolved = resolve_relative_screenshot(item, source_hint)
            if resolved and resolved not in screenshots:
                screenshots.append(resolved)
            if len(screenshots) == 5:
                break

        if not screenshots:
            icon = app.get("icon")
            if isinstance(icon, str) and icon.strip():
                screenshots = [icon]
            else:
                fallback = ROOT / "icon" / "app.png"
                if fallback.is_file():
                    app["icon"] = fallback.relative_to(ROOT).as_posix()
                    screenshots = [app["icon"]]

        app["screenshots"] = screenshots[:5]
        path.write_text(json.dumps(app, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        apps.append(app)
    return apps


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


def normalize_authors(apps):
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

    for path in sorted((ROOT / "authors").glob("*.json")):
        try:
            author = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        links = author.get("links")
        if not isinstance(links, list) or not links:
            links = by_author.get(author.get("id"), [])[:5]
        author["links"] = normalize_author_links(links)
        path.write_text(json.dumps(author, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    apps = normalize_apps()
    normalize_authors(apps)
    print(f"Persisted source normalization: {len(apps)} applications processed")


if __name__ == "__main__":
    main()
