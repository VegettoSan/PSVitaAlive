#!/usr/bin/env python3
"""Compare ALL V2 catalog sources against production main without changing values.

Run on GitHub Actions with origin/main fetched:
  python scripts/catalog_schema_v2_compare.py --base origin/main \
      --v1-dir /tmp/catalog-v1 --v2-dir . --output catalog-v2-comparison.json

Checks all 1,892 editable JSON files, all generated catalog records, every
nested field and value (including list order), and reports sizes. Never writes
catalog/source files, pushes commits, or touches main.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from collections import Counter
from pathlib import Path

from catalog_schema_v2 import to_v1, to_v2, KEYS, PASSTHROUGH_CHILDREN

ROOT = Path(__file__).resolve().parents[1]
FOLDERS = ("apps", "authors", "categories")
CATALOGS = ("catalog.json", "authors.json", "categories.json")


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", *args], cwd=ROOT, stderr=subprocess.PIPE)


def source_paths(ref: str, folder: str) -> set[str]:
    output = git("ls-tree", "-r", "--name-only", ref, "--", folder).decode("utf-8")
    # Canonical JSON documents are directly under each root. Avoid treating
    # nested asset/fixtures or external originals as source catalog records.
    return {p for p in output.splitlines()
            if p.startswith(folder + "/") and p.endswith(".json")
            and p.count("/") == 1}


def key_paths(value, root: str, out: Counter, *, dynamic: bool = False):
    if isinstance(value, list):
        for item in value:
            key_paths(item, root + "[]", out, dynamic=dynamic)
    elif isinstance(value, dict):
        for key, item in value.items():
            out[f"{root}.{key}"] += 1
            key_paths(item, f"{root}.{key}", out,
                      dynamic=(key in PASSTHROUGH_CHILDREN and not dynamic))


def differences(left, right, path: str, result: list[str], max_items: int = 40):
    if len(result) >= max_items:
        return
    if type(left) is not type(right):
        result.append(f"{path}: type {type(left).__name__} != {type(right).__name__}")
    elif isinstance(left, dict):
        for k in left.keys() - right.keys():
            if len(result) < max_items:
                result.append(f"{path}: missing key {k!r}")
        for k in right.keys() - left.keys():
            if len(result) < max_items:
                result.append(f"{path}: new key {k!r}")
        for k in left.keys() & right.keys():
            differences(left[k], right[k], f"{path}.{k}", result, max_items)
    elif isinstance(left, list):
        if len(left) != len(right):
            result.append(f"{path}: list len {len(left)} != {len(right)}")
        for i, (a, b) in enumerate(zip(left, right)):
            differences(a, b, f"{path}[{i}]", result, max_items)
    elif left != right:
        result.append(f"{path}: scalar mismatch")


def compare_source_files(base: str):
    issues = []
    counts = Counter()
    key_counts = Counter()
    size = Counter()
    for folder in FOLDERS:
        before_paths = source_paths(base, folder)
        after_paths = {p.relative_to(ROOT).as_posix()
                       for p in (ROOT / folder).glob("*.json")}
        added = sorted(after_paths - before_paths)
        removed = sorted(before_paths - after_paths)
        if added or removed:
            issues.append({"folder": folder, "type": "source file set",
                           "added": added[:15], "removed": removed[:15],
                           "added_count": len(added), "removed_count": len(removed)})
        for rel in sorted(before_paths & after_paths):
            oldbytes = git("show", f"{base}:{rel}")
            newbytes = (ROOT / rel).read_bytes()
            old = json.loads(oldbytes.decode("utf-8"))
            compact = json.loads(newbytes.decode("utf-8"))
            restored = to_v1(compact)
            counts[folder] += 1
            size[f"{folder}_v1_bytes"] += len(oldbytes)
            size[f"{folder}_v2_bytes"] += len(newbytes)
            key_paths(old, folder, key_counts)
            if restored != old:
                diff = []
                differences(old, restored, rel, diff)
                issues.append({"file": rel, "type": "semantic divergence",
                               "example_differences": diff})
            elif json.dumps(restored, ensure_ascii=False, separators=(",", ":")) != \
                 json.dumps(old, ensure_ascii=False, separators=(",", ":")):
                issues.append({"file": rel, "type": "key ordering drift"})
            if to_v2(restored) != compact:
                issues.append({"file": rel, "type": "noncanonical V2 encoding"})
    return {
        "file_counts": dict(counts),
        "raw_bytes": dict(size),
        "field_paths": len(key_counts),
        "field_occurrences": sum(key_counts.values()),
        "unmapped_source_keys": sorted({
            path.rsplit(".", 1)[1] for path in key_counts
            if path.rsplit(".", 1)[1] not in KEYS
            # Dynamic source names, regions etc. are not schema fields.
            and not any(f".{k}." in path for k in PASSTHROUGH_CHILDREN)
        }),
        "issue_count": len(issues),
        "issues": issues[:150],
    }


def catalog_snapshot_comparison(base: str, filename: str, new_data):
    old = json.loads(git("show", f"{base}:{filename}").decode("utf-8"))
    # Compare by record id to separate ordering changes from data changes.
    if not isinstance(old, list) or not isinstance(new_data, list):
        return {"error": "expected arrays", "matches": False}
    def keyed(records):
        return {str(item.get("id")): item for item in records}
    lhs, rhs = keyed(old), keyed(new_data)
    added = sorted(rhs.keys() - lhs.keys())
    removed = sorted(lhs.keys() - rhs.keys())
    changed = sorted(k for k in lhs.keys() & rhs.keys() if lhs[k] != rhs[k])
    samples = {}
    for key in changed[:12]:
        d = []
        differences(lhs[key], rhs[key], filename + "/" + key, d, max_items=8)
        samples[key] = d
    return {
        "main_rows": len(old),
        "generated_v1_rows": len(new_data),
        "main_duplicate_ids": len(old) - len(lhs),
        "generated_duplicate_ids": len(new_data) - len(rhs),
        "added_ids": added[:25],
        "removed_ids": removed[:25],
        "changed_ids": changed[:25],
        "added_count": len(added), "removed_count": len(removed),
        "changed_count": len(changed),
        "order_matches": [x.get("id") for x in old] ==
                         [x.get("id") for x in new_data],
        "sample_differences": samples,
        "matches": old == new_data,
    }


def compare_catalogs(base: str, v1dir: Path, v2dir: Path):
    per = {}
    for file in CATALOGS:
        raw1 = (v1dir / file).read_bytes()
        raw2 = (v2dir / file).read_bytes()
        v1 = json.loads(raw1)
        v2 = json.loads(raw2)
        restored = to_v1(v2)
        changes = []
        differences(v1, restored, file, changes)
        per[file] = {
            "rows": len(v1),
            "v1_bytes": len(raw1),
            "v2_bytes": len(raw2),
            "saved_bytes": len(raw1) - len(raw2),
            "saved_percent": round(100 * (1 - len(raw2) / len(raw1)), 2)
                             if raw1 else 0,
            "lossless": not changes,
            "difference_examples": changes,
            "vs_published_main": catalog_snapshot_comparison(base, file, v1),
        }
    return per


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--v1-dir", type=Path, default=Path("/tmp/catalog-v1"))
    ap.add_argument("--v2-dir", type=Path, default=ROOT)
    ap.add_argument("--output", type=Path, default=Path("catalog-v2-comparison.json"))
    args = ap.parse_args()
    files = compare_source_files(args.base)
    catalogs = compare_catalogs(args.base, args.v1_dir, args.v2_dir)
    errors = []
    if files["issue_count"]:
        errors.append(f"{files['issue_count']} source JSON discrepancies against {args.base}")
    if files["unmapped_source_keys"]:
        errors.append("Unexpected unmapped source fields: " + ", ".join(files["unmapped_source_keys"]))
    for name, info in catalogs.items():
        if not info["lossless"]:
            errors.append(name + ": V1/V2 generated data differ")
        if not info["vs_published_main"]["matches"]:
            errors.append(name + ": generated V1 differs from currently published main")
    result = {"baseline": args.base, "source": files, "catalogs": catalogs,
              "passed": not errors, "errors": errors}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print("=== PSVitaAlive V2 FULL INTEGRITY AUDIT ===")
    print("Source JSONs:", files["file_counts"])
    print("Field paths:", files["field_paths"], "Occurrences:", files["field_occurrences"])
    print("Source discrepancies:", files["issue_count"],
          "| Unmapped fields:", files["unmapped_source_keys"])
    for name, info in catalogs.items():
        main = info["vs_published_main"]
        print(f"{name}: {info['rows']} records | V1={info['v1_bytes']:,} bytes, "
              f"V2={info['v2_bytes']:,} bytes | saved={info['saved_percent']}% | "
              f"roundtrip={info['lossless']} | published-main-equal={main['matches']}")
        if not main["matches"]:
            print("  published differences: added", main["added_count"],
                  "removed", main["removed_count"], "changed", main["changed_count"],
                  "order", main["order_matches"])
            print("  changed IDs:", main["changed_ids"][:20])
            for record_id, diffs in main["sample_differences"].items():
                print("  ", record_id, "->", "; ".join(diffs[:8]))
    if errors:
        print("ERRORS:")
        for error in errors:
            print("  -", error)
        raise SystemExit(1)
    print("PASS: all source records and all three generated catalogs match.")


if __name__ == "__main__":
    main()
