#!/usr/bin/env python3
"""Build *experimental* V2 editions of the existing PS Vita, PSP and PS1 catalogs.

The three root catalog_ps*_games.json files are input only and are NEVER
overwritten. The three official homebrew catalogs and the PSVita zRIF sidecar
are likewise untouched.

Usage (repository root):
    python scripts/compact_game_catalogs_v2.py --out-dir /tmp/game-catalogs-v2 \
        --report /tmp/game-catalog-v2-report.json

Default with no --out-dir is an audit/dry run. Unknown JSON field paths are
rejected instead of silently discarding or rewriting them.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

from catalog_schema_v2 import KEYS, to_v1, to_v2

ROOT = Path(__file__).resolve().parents[1]
GAMES = (
    "catalog_psvita_games.json",
    "catalog_psp_games.json",
    "catalog_ps1_games.json",
)
EXPECTED_KEYS = {
    "root[]": {
        "id", "title_ids", "name", "description", "category_id",
        "subcategory_ids", "version", "version_date", "requirements",
        "size", "cover", "icon", "screenshots", "links",
    },
    "root[].links[]": {
        "type", "name", "region", "title_id", "url", "size",
        "recommended", "content_id", "version", "required_fw",
        "pkg_name", "rap",
    },
}


def scan_keys(value, at: str, errors: list[str], counts: dict[str, int]) -> None:
    if isinstance(value, list):
        for item in value:
            scan_keys(item, at + "[]", errors, counts)
        return
    if not isinstance(value, dict):
        return
    for key, child in value.items():
        location = at + "." + key
        counts[location] = counts.get(location, 0) + 1
        if at == "root[].title_ids":
            # Region codes are data identifiers (US, EU, JP, ASIA, INT...).
            if not isinstance(child, str):
                errors.append(f"{location}: expected string Title ID")
        elif key not in EXPECTED_KEYS.get(at, set()):
            errors.append(f"{location}: unexpected game catalog field")
        if at != "root[].title_ids" and key not in KEYS:
            errors.append(f"{location}: no V2 mapping")
        scan_keys(child, location, errors, counts)


def normalize(value) -> bytes:
    # Lossless and order-preserving; also keeps JSON number/bool and UTF-8 text.
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def build(out_dir: Path | None, report_path: Path | None) -> dict:
    if out_dir is not None:
        out_dir = out_dir.resolve()
        if out_dir == ROOT.resolve():
            raise ValueError("Refusing to replace public root catalog files")
        out_dir.mkdir(parents=True, exist_ok=True)

    results = []
    failed = []
    for name in GAMES:
        path = ROOT / name
        original_bytes = path.read_bytes()
        original = json.loads(original_bytes)
        if not isinstance(original, list):
            raise ValueError(f"{name}: root must be an array")
        fields: dict[str, int] = {}
        errors: list[str] = []
        scan_keys(original, "root", errors, fields)
        compact = to_v2(original)
        restored = to_v1(compact)
        if normalize(restored) != normalize(original):
            errors.append("V2 -> V1 roundtrip differs from source")
        if len({str(x["id"]) for x in original}) != len(original):
            errors.append("Duplicate game IDs")
        for index, (old, new) in enumerate(zip(original, restored)):
            if old.get("title_ids") != new.get("title_ids"):
                errors.append(f"record {index}: regional title IDs differ")
            if old.get("links") != new.get("links"):
                errors.append(f"record {index}: link values, order or metadata differ")
        compact_bytes = (json.dumps(compact, ensure_ascii=False,
                                   separators=(",", ":")) + "\n").encode("utf-8")
        original_count = len(original_bytes)
        compressed_count = len(compact_bytes)
        stats = {
            "file": name,
            "records": len(original),
            "source_sha256": hashlib.sha256(original_bytes).hexdigest(),
            "original_bytes": original_count,
            "v2_bytes": compressed_count,
            "bytes_saved": original_count - compressed_count,
            "saving_pct": round(100 * (1 - compressed_count / original_count), 2),
            "paths": len(fields),
            "field_occurrences": sum(fields.values()),
            "field_counts": fields,
            "errors": errors[:100],
            "error_count": len(errors),
        }
        results.append(stats)
        if errors:
            failed.append(name)
        elif out_dir is not None:
            (out_dir / name).write_bytes(compact_bytes)

    results_summary = {
        "files": results,
        "total_records": sum(x["records"] for x in results),
        "original_bytes": sum(x["original_bytes"] for x in results),
        "v2_bytes": sum(x["v2_bytes"] for x in results),
        "bytes_saved": sum(x["bytes_saved"] for x in results),
        "passed": not failed,
        "failed_files": failed,
        "note": "V2 output is experimental. Do not publish in main before client validation.",
    }
    if report_path is not None:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(results_summary, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    for item in results:
        print(f"{item['file']}: {item['records']} games, "
              f"{item['original_bytes']:,} -> {item['v2_bytes']:,} bytes "
              f"({item['saving_pct']}% saved), errors={item['error_count']}")
        for error in item["errors"][:10]:
            print("  ERROR", error)
    print(f"TOTAL: {results_summary['bytes_saved']:,} bytes saved, "
          f"records={results_summary['total_records']}")
    if failed:
        raise RuntimeError("Game V2 audit failed for: " + ", ".join(failed))
    return results_summary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out-dir", type=Path, help="Write test artifacts here, never in repository root")
    ap.add_argument("--report", type=Path, help="Write detailed JSON results")
    args = ap.parse_args()
    build(args.out_dir, args.report)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"Game V2 audit FAILED: {exc}", file=sys.stderr)
        sys.exit(1)
