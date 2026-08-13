#!/usr/bin/env python3

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


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
            category_icons[category.get("id")] = repository_relative_resource(icon, path.parent)

    default_author_avatar = ROOT / "authors" / "icon" / "autoricon.png"

    for app in apps:
        source_dir = local_paths.get(app.get("id"), ROOT)
        icon = app.get("icon")
        if not isinstance(icon, str) or not icon.strip():
            icon = category_icons.get(app.get("category_id"), "")
        if icon:
            app["icon"] = repository_relative_resource(icon, source_dir)

        screenshots = app.get("screenshots") or []
        if not screenshots and app.get("icon"):
            screenshots = [app["icon"]]
        normalized = []
        for screenshot in screenshots:
            if isinstance(screenshot, str) and screenshot.startswith(("http://", "https://")):
                normalized.append(screenshot)
            else:
                normalized.append(repository_relative_resource(screenshot, source_dir))
        app["screenshots"] = normalized[:5]

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
