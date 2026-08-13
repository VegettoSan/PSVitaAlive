#!/usr/bin/env python3

import json
import re
import sys
from datetime import datetime
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]


def parse_catalog_date(value):
    if not isinstance(value, str) or not value.strip():
        return datetime.min
    raw = value.strip().replace("Z", "+00:00")
    try:
        return datetime.fromisoformat(raw).replace(tzinfo=None)
    except ValueError:
        return datetime.min


def sort_latest_first(apps):
    """Order applications newest-to-oldest by version date.

    `updated_at` is used only when `version_date` is absent or invalid.
    Original order is kept as the final stable tie-breaker.
    """
    indexed = list(enumerate(apps))

    def key(item):
        index, app = item
        version_date = parse_catalog_date(app.get("version_date"))
        updated_at = parse_catalog_date(app.get("updated_at"))
        return (version_date, updated_at, -index)

    indexed.sort(key=key, reverse=True)
    return [app for _, app in indexed]


def repository_relative_resource(value, source_directory):
    if not isinstance(value, str) or not value.strip():
        return value
    if value.startswith(("http://", "https://")):
        return value
    local = (source_directory / value).resolve()
    try:
        relative = local.relative_to(ROOT.resolve())
    except ValueError as exc:
        raise ValueError(f"resource escapes repository: {value}") from exc
    return relative.as_posix()


def split_media_values(value):
    """Normalize legacy screenshot fields that may contain packed URLs.

    Some external catalogs encode several screenshots in one string separated
    by semicolons or newlines. Keep URLs intact and turn those packed values
    into the normal VitaHub list representation.
    """
    if isinstance(value, list):
        result = []
        for item in value:
            result.extend(split_media_values(item))
        return result
    if not isinstance(value, str) or not value.strip():
        return []
    return [item.strip() for item in re.split(r"[;\r\n]+", value) if item.strip()]


def is_remote_resource(value):
    if not isinstance(value, str):
        return False
    return urlparse(value).scheme in {"http", "https"}


def local_resource_exists(value):
    if not isinstance(value, str) or not value.strip() or is_remote_resource(value):
        return False
    try:
        path = (ROOT / value).resolve()
        path.relative_to(ROOT.resolve())
    except (OSError, ValueError):
        return False
    return path.is_file()


def validate_final(apps, authors, categories):
    author_ids = {item.get("id") for item in authors}
    category_map = {item.get("id"): item for item in categories}
    title_ids = set()
    app_ids = set()
    for app in apps:
        if not app.get("id") or app["id"] in app_ids:
            raise ValueError(f"duplicate/empty application id: {app.get('id')}")
        app_ids.add(app["id"])
        title_id = app.get("title_id")
        if not title_id or title_id in title_ids:
            raise ValueError(f"duplicate/empty title_id: {title_id}")
        title_ids.add(title_id)
        author_list = app.get("author_ids")
        if not isinstance(author_list, list) or not author_list:
            raise ValueError(f"{app['id']}: author_ids must be non-empty")
        missing = [item for item in author_list if item not in author_ids]
        if missing:
            raise ValueError(f"{app['id']}: unknown author_ids: {missing}")
        category = category_map.get(app.get("category_id"))
        if not category:
            raise ValueError(f"{app['id']}: unknown category_id {app.get('category_id')}")
        allowed = {item.get("id") for item in category.get("subcategories", [])}
        for sub_id in app.get("subcategory_ids", []):
            if sub_id not in allowed:
                raise ValueError(f"{app['id']}: invalid subcategory {sub_id}")
        screenshots = app.get("screenshots") or []
        if screenshots and not 1 <= len(screenshots) <= 5:
            raise ValueError(f"{app['id']}: screenshots must contain 1-5 items")
        links = app.get("links") or []
        recommended = sum(1 for link in links if isinstance(link, dict) and link.get("recommended") is True)
        if recommended > 1:
            raise ValueError(f"{app['id']}: more than one recommended link")


def process_media(apps):
    local_paths = {}
    for path in sorted((ROOT / "apps").glob("*.json")):
        try:
            with path.open(encoding="utf-8") as handle:
                data = json.load(handle)
            local_paths[data.get("id")] = path.parent
        except Exception:
            continue

    category_icons = {}
    for path in sorted((ROOT / "categories").glob("*.json")):
        with path.open(encoding="utf-8") as handle:
            category = json.load(handle)
        icon = category.get("icon")
        if isinstance(icon, str) and icon.strip():
            normalized = repository_relative_resource(icon, path.parent)
            if is_remote_resource(normalized) or local_resource_exists(normalized):
                category_icons[category.get("id")] = normalized

    fallback_icon = ""
    for candidate in (
        ROOT / "icon" / "app.png",
        ROOT / "assets" / "icon.png",
        ROOT / "authors" / "icon" / "autoricon.png",
    ):
        if candidate.is_file():
            fallback_icon = candidate.relative_to(ROOT).as_posix()
            break

    for app in apps:
        source_dir = local_paths.get(app.get("id"), ROOT)

        # Icon is the canonical media fallback. If an external source has no
        # usable icon, use the official VitaHub category icon, then the global
        # fallback icon if available.
        icon = app.get("icon")
        if not isinstance(icon, str) or not icon.strip():
            icon = category_icons.get(app.get("category_id"), fallback_icon)
        else:
            icon = repository_relative_resource(icon, source_dir)
            if not is_remote_resource(icon) and not local_resource_exists(icon):
                icon = category_icons.get(app.get("category_id"), fallback_icon)

        if icon:
            app["icon"] = icon

        raw_screenshots = split_media_values(app.get("screenshots") or [])
        normalized = []
        for screenshot in raw_screenshots:
            if is_remote_resource(screenshot):
                normalized.append(screenshot)
                continue

            local_value = repository_relative_resource(screenshot, source_dir)
            if local_resource_exists(local_value):
                normalized.append(local_value)

        # Never emit a broken local screenshot. If no usable screenshot exists,
        # use the application's icon as the first screenshot when available.
        # This preserves the UI expectation that every app has visual media
        # without inventing or downloading assets during catalog generation.
        if not normalized and app.get("icon"):
            normalized = [app["icon"]]

        # Deduplicate while preserving source order and keep the schema limit.
        deduplicated = []
        seen = set()
        for item in normalized:
            if item not in seen:
                seen.add(item)
                deduplicated.append(item)
        app["screenshots"] = deduplicated[:5]

    # The author avatar fallback is required by the current catalog schema.
    default_author_avatar = ROOT / "authors" / "icon" / "autoricon.png"
    if not default_author_avatar.is_file():
        raise ValueError("authors/icon/autoricon.png is required")


def generate():
    from external.aggregate import build

    apps, authors, categories, conflicts = build(ROOT)
    if conflicts:
        report_dir = ROOT / "reports"
        report_dir.mkdir(exist_ok=True)
        with (report_dir / "external_conflicts.json").open("w", encoding="utf-8") as handle:
            json.dump(conflicts, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        print(f"External aggregation reported {len(conflicts)} conflicts", file=sys.stderr)

    process_media(apps)
    apps = sort_latest_first(apps)

    for author in authors:
        avatar = author.get("avatar")
        if not isinstance(avatar, str) or not avatar.strip():
            author["avatar"] = "icon/autoricon.png"

    validate_final(apps, authors, categories)

    with (ROOT / "catalog.json").open("w", encoding="utf-8") as handle:
        json.dump(apps, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    with (ROOT / "authors.json").open("w", encoding="utf-8") as handle:
        json.dump(authors, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    with (ROOT / "categories.json").open("w", encoding="utf-8") as handle:
        json.dump(categories, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    print(f"Applications: {len(apps)}")
    print(f"Authors: {len(authors)}")
    print(f"Categories: {len(categories)}")


if __name__ == "__main__":
    try:
        generate()
    except Exception as exc:
        print(f"VitaHub catalog generation failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(1)
