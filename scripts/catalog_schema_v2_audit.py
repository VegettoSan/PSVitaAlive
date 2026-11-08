#!/usr/bin/env python3
"""Read-only inventory of JSON field names in PSVitaAlive source catalogs.

Usage: python scripts/catalog_schema_v2_audit.py [--output report.json]
No source file or generated catalog is modified.
"""
from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = ("apps", "authors", "categories")


def shape(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, int):
        return "integer"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    return "object"


def collect(value, path, counts, types, appearances):
    if isinstance(value, dict):
        for key, child in value.items():
            if not isinstance(key, str):
                raise ValueError(f"Non-string JSON field at {path!r}")
            entry_path = f"{path}.{key}"
            counts[entry_path] += 1
            types[entry_path].add(shape(child))
            appearances[key].add(path)
            collect(child, entry_path, counts, types, appearances)
    elif isinstance(value, list):
        for child in value:
            collect(child, f"{path}[]", counts, types, appearances)


def inventory(root):
    counts = Counter()
    types = defaultdict(set)
    appearances = defaultdict(set)
    files = Counter()
    for name in SOURCE_DIRS:
        folder = root / name
        if not folder.is_dir():
            raise FileNotFoundError(folder)
        for path in sorted(folder.glob("*.json")):
            with path.open(encoding="utf-8") as stream:
                data = json.load(stream)
            if not isinstance(data, dict):
                raise ValueError(f"Expected JSON object: {path}")
            files[name] += 1
            collect(data, name, counts, types, appearances)
    return {
        "source_files": dict(sorted(files.items())),
        "unique_key_paths": len(counts),
        "unique_field_names": len(appearances),
        "paths": [
            {"path": key, "occurrences": counts[key], "types": sorted(types[key])}
            for key in sorted(counts)
        ],
        "keys": [
            {"name": key, "contexts": sorted(appearances[key])}
            for key in sorted(appearances)
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="Project root (test fixtures or alternate checkout)")
    parser.add_argument("--output", type=Path,
                        help="Write a report to this path; sources remain unchanged")
    args = parser.parse_args()
    report = inventory(args.root)
    data = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data, encoding="utf-8")
    else:
        print(data, end="")


if __name__ == "__main__":
    main()
