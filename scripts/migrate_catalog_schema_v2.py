#!/usr/bin/env python3
"""Convert PSVitaAlive canonical source JSON files to abbreviated schema V2.

Default is SAFE READ-ONLY. Use --write only on a feature branch after review.
Unknown key names fail closed unless explicitly added to catalog_schema_v2.KEYS.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from catalog_schema_v2 import KEYS, PASSTHROUGH_CHILDREN, to_v1, to_v2

ROOT = Path(__file__).resolve().parents[1]
FOLDERS = ("apps", "authors", "categories")


def unknown_names(obj, *, parent="", passthrough=False):
    found = set()
    if isinstance(obj, list):
        for item in obj:
            found.update(unknown_names(item, parent=parent, passthrough=passthrough))
    elif isinstance(obj, dict):
        for key, value in obj.items():
            if passthrough:
                dynamic_key = True
            else:
                dynamic_key = False
                if key not in KEYS:
                    found.add((parent, key))
            # Region and source identifiers are values represented as keys.
            next_passthrough = not dynamic_key and key in PASSTHROUGH_CHILDREN
            found.update(unknown_names(value, parent=f"{parent}.{key}",
                                       passthrough=next_passthrough))
    return found


def migrate(root: Path, *, write: bool = False):
    updated = 0
    total = 0
    failures = []
    unknown = set()
    staged = []
    for folder in FOLDERS:
        d = root / folder
        if not d.is_dir():
            raise FileNotFoundError(d)
        for path in sorted(d.glob("*.json")):
            total += 1
            try:
                original = json.loads(path.read_text(encoding="utf-8"))
                decoded = to_v1(original)
                # Old and new schema must both roundtrip, including nested links.
                encoded = to_v2(decoded)
                if to_v1(encoded) != decoded:
                    raise ValueError("V1->V2->V1 roundtrip differs")
                if to_v2(to_v1(encoded)) != encoded:
                    raise ValueError("V2->V1->V2 roundtrip differs")
                for ctx, key in unknown_names(decoded, parent=folder):
                    unknown.add((ctx, key))
                # Only JSON keys may change; scalar strings/numbers/list order remain.
                serialized = json.dumps(encoded, ensure_ascii=False, indent=2) + "\n"
                if original != encoded or path.read_text(encoding="utf-8") != serialized:
                    staged.append((path, serialized))
            except Exception as exc:
                failures.append(f"{path.relative_to(root)}: {exc}")
    if unknown:
        failures.extend(f"Unmapped field {ctx}.{key}" for ctx, key in sorted(unknown))
    if failures:
        raise RuntimeError("No files changed; migration failed:\n" +
                           "\n".join(failures[:150]) +
                           (f"\n... {len(failures) - 150} more" if len(failures) > 150 else ""))
    if write:
        for path, content in staged:
            path.write_text(content, encoding="utf-8")
            updated += 1
    print(f"Verified {total} JSON sources; candidates={len(staged)}; modified={updated}.")
    return total, len(staged), updated


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=ROOT)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    migrate(args.root, write=args.write)


if __name__ == "__main__":
    main()
