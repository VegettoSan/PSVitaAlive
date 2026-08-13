#!/usr/bin/env python3

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def repository_relative_resource(value, source_directory):
    if not isinstance(value, str) or not value.strip():
        return value
    if value.startswith("http://") or value.startswith("https://"):
        return value
    local = (source_directory / value).resolve()
    try:
        relative = local.relative_to(ROOT.resolve())
    except ValueError as exc:
        raise ValueError(f"resource escapes repository: {value}") from exc
    return relative.as_posix()


def normalize_final_resource(value, source_directory):
    return repository_relative_resource(value, source_directory)


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
        authors_for_app = app.get("author_ids")
        if not isinstance(authors_for_app, list) or not authors_for_app:
            raise ValueError(f"{app['id']}: author_ids must be non-empty")
        missing = [item for item in authors_for_app if item not in author_ids]
        if missing:
            raise ValueError(f"{app['id']}: unknown author_ids: {missing}")
        category_id = app.get("category_id")
        category = category_map.get(category_id)
        if not category:
            raise ValueError(f"{app['id']}: unknown category_id {category_id}")
        allowed = {item.get("id") for item in category.get("subcategories", [])}
        for sub_id in app.get("subcategory_ids", []):
            if sub_id not in allowed:
                raise ValueError(f"{app['id']}: subcategory {sub_id} is not valid for {category_id}")
        screenshots = app.get("screenshots") or []
        if screenshots and not 1 <= len(screenshots) <= 5:
            raise ValueError(f"{app['id']}: screenshots must contain 1-5 items")
        links = app.get("links") or []
        recommended = sum(1 for link in links if isinstance(link, dict) and link.get("recommended") is True)
        if recommended > 1:
            raise ValueError(f"{app['id']}: more than one recommended link")


def generate():
    from scripts.external.aggregate import build

    apps, authors, categories, conflicts = build(ROOT)
    if conflicts:
        report_dir = ROOT / "reports"
        report_dir.mkdir(exist_ok=True)
        with (report_dir / "external_conflicts.json").open("w", encoding="utf-8") as handle:
            json.dump(conflicts, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        print(f"External aggregation reported {len(conflicts)} conflicts", file=sys.stderr)

    # Resolve local resource references where possible. External URLs remain untouched.
    for app in apps:
        for field in ("icon",):
            if isinstance(app.get(field), str) and not app[field].startswith(("http://", "https://")):
                app[field] = normalize_final_resource(app[field], ROOT / "apps")
        shots = []
        for shot in app.get("screenshots", []):
            if isinstance(shot, str) and not shot.startswith(("http://", "https://")):
                shots.append(normalize_final_resource(shot, ROOT / "apps"))
            else:
                shots.append(shot)
        app["screenshots"] = shots[:5]

    validate_final(apps, authors, categories)
    with (ROOT / "catalog.json").open("w", encoding="utf-8") as f:
        json.dump(apps, f, ensure_ascii=False, indent=2)
        f.write("\n")
    with (ROOT / "authors.json").open("w", encoding="utf-8") as f:
        json.dump(authors, f, ensure_ascii=False, indent=2)
        f.write("\n")
    with (ROOT / "categories.json").open("w", encoding="utf-8") as f:
        json.dump(categories, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(f"Applications: {len(apps)}")
    print(f"Authors: {len(authors)}")
    print(f"Categories: {len(categories)}")


if __name__ == "__main__":
    try:
        generate()
    except Exception as exc:
        print(f"VitaHub catalog generation failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(1)
