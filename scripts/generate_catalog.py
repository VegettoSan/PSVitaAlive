#!/usr/bin/env python3

import json
import sys
from pathlib import Path


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

        items.append(data)

    return items


def write_json(path, data):
    with path.open("w", encoding="utf-8") as handle:
        json.dump(
            data,
            handle,
            ensure_ascii=False,
            indent=2,
        )
        handle.write("\n")


def generate():
    print("Generating VitaHub catalogs...")

    apps = load_json_directory(APP_DIR)
    authors = load_json_directory(AUTHOR_DIR)
    categories = load_json_directory(CATEGORY_DIR)

    write_json(CATALOG_FILE, apps)
    write_json(AUTHORS_FILE, authors)
    write_json(CATEGORIES_FILE, categories)

    print()
    print("Catalogs generated successfully.")
    print()
    print(f"Applications: {len(apps)}")
    print(f"Authors: {len(authors)}")
    print(f"Categories: {len(categories)}")
    print()
    print(f"Generated: {CATALOG_FILE.relative_to(ROOT)}")
    print(f"Generated: {AUTHORS_FILE.relative_to(ROOT)}")
    print(f"Generated: {CATEGORIES_FILE.relative_to(ROOT)}")


if __name__ == "__main__":
    try:
        generate()

    except Exception as exc:
        print()
        print("VitaHub catalog generation failed:")
        print(f"- {type(exc).__name__}: {exc}")
        print()
        sys.exit(1)