#!/usr/bin/env python3

import json
import sys
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]

APP_DIR = ROOT / "apps"
AUTHOR_DIR = ROOT / "authors"
CATEGORY_DIR = ROOT / "categories"

CATALOG_FILE = ROOT / "catalog.json"
AUTHORS_FILE = ROOT / "authors.json"
CATEGORIES_FILE = ROOT / "categories.json"


def load_json_directory(directory):
    items = []

    if not directory.exists():
        return items

    for path in sorted(directory.glob("*.json")):
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)

        if not isinstance(data, dict):
            raise ValueError(
                f"{path.relative_to(ROOT)} must contain a JSON object"
            )

        items.append(
            (
                path,
                data,
            )
        )

    return items


def write_json(path, data):
    with path.open(
        "w",
        encoding="utf-8",
    ) as handle:
        json.dump(
            data,
            handle,
            ensure_ascii=False,
            indent=2,
        )
        handle.write("\n")


def is_remote_resource(value):
    if not isinstance(value, str):
        return False

    parsed = urlparse(value)

    return parsed.scheme in {
        "http",
        "https",
    }


def repository_relative_resource(
    value,
    source_directory,
):
    """
    Converts a local resource relative to its source
    JSON file into a path relative to the repository root.

    Remote URLs are returned unchanged.
    """

    if is_remote_resource(value):
        return value

    local = (
        source_directory / value
    ).resolve()

    try:
        relative = local.relative_to(
            ROOT.resolve()
        )

    except ValueError as exc:
        raise ValueError(
            (
                f"resource escapes repository: "
                f"{value}"
            )
        ) from exc

    return relative.as_posix()


def get_category_icons(categories):
    result = {}

    for path, category in categories:
        category_id = category.get("id")

        if not category_id:
            continue

        icon = category.get("icon")

        if not isinstance(
            icon,
            str,
        ) or not icon.strip():
            raise ValueError(
                (
                    f"{path.relative_to(ROOT)} "
                    "has no valid icon"
                )
            )

        result[category_id] = (
            repository_relative_resource(
                icon,
                path.parent,
            )
        )

    return result


def process_application(
    path,
    app,
    category_icons,
):
    """
    Build the final application object.

    All original fields are preserved.

    Only media fallback/normalization is performed.
    """

    result = dict(app)

    category_id = app.get(
        "category_id"
    )

    # -------------------------------------------------
    # ICON
    # -------------------------------------------------

    app_icon = app.get("icon")

    if (
        isinstance(app_icon, str)
        and app_icon.strip()
    ):
        final_icon = (
            repository_relative_resource(
                app_icon,
                path.parent,
            )
        )

    else:
        final_icon = category_icons.get(
            category_id
        )

        if not final_icon:
            raise ValueError(
                (
                    f"{path.relative_to(ROOT)} "
                    "has no icon and its category "
                    "has no icon"
                )
            )

    result["icon"] = final_icon

    # -------------------------------------------------
    # SCREENSHOTS
    # -------------------------------------------------

    screenshots = app.get(
        "screenshots"
    )

    if screenshots is None or screenshots == []:
        result["screenshots"] = [
            final_icon
        ]

    else:
        if not isinstance(
            screenshots,
            list,
        ):
            raise ValueError(
                (
                    f"{path.relative_to(ROOT)} "
                    "'screenshots' must be an array"
                )
            )

        if not 1 <= len(screenshots) <= 5:
            raise ValueError(
                (
                    f"{path.relative_to(ROOT)} "
                    "'screenshots' must contain "
                    "between 1 and 5 images"
                )
            )

        result["screenshots"] = [
            repository_relative_resource(
                screenshot,
                path.parent,
            )
            for screenshot in screenshots
        ]

    return result


def generate():
    print("Generating VitaHub catalogs...")

    app_items = load_json_directory(
        APP_DIR
    )

    author_items = load_json_directory(
        AUTHOR_DIR
    )

    category_items = load_json_directory(
        CATEGORY_DIR
    )

    category_icons = get_category_icons(
        category_items
    )

    # -------------------------------------------------
    # APPLICATIONS
    # -------------------------------------------------

    applications = []

    for path, app in app_items:
        applications.append(
            process_application(
                path,
                app,
                category_icons,
            )
        )

    # -------------------------------------------------
    # AUTHORS
    # -------------------------------------------------

    authors = [
        data
        for _, data in author_items
    ]

    # -------------------------------------------------
    # CATEGORIES
    # -------------------------------------------------

    categories = [
        data
        for _, data in category_items
    ]

    # -------------------------------------------------
    # WRITE GENERATED FILES
    # -------------------------------------------------

    write_json(
        CATALOG_FILE,
        applications,
    )

    write_json(
        AUTHORS_FILE,
        authors,
    )

    write_json(
        CATEGORIES_FILE,
        categories,
    )

    print()
    print(
        "Catalogs generated successfully."
    )
    print()
    print(
        f"Applications: {len(applications)}"
    )
    print(
        f"Authors: {len(authors)}"
    )
    print(
        f"Categories: {len(categories)}"
    )
    print()
    print(
        f"Generated: "
        f"{CATALOG_FILE.relative_to(ROOT)}"
    )
    print(
        f"Generated: "
        f"{AUTHORS_FILE.relative_to(ROOT)}"
    )
    print(
        f"Generated: "
        f"{CATEGORIES_FILE.relative_to(ROOT)}"
    )


if __name__ == "__main__":
    try:
        generate()

    except Exception as exc:
        print()
        print(
            "VitaHub catalog generation failed:"
        )
        print(
            f"- {type(exc).__name__}: {exc}"
        )
        print()

        sys.exit(1)