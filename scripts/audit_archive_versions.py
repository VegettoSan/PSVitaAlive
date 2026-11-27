#!/usr/bin/env python3
"""Read-only audit of canonical version labels; never edits apps or generated catalogs."""
import argparse
import json
from collections import Counter
from pathlib import Path

from scripts.archive_backup_batch import ROOT, VALID_VERSION, expected_filename, Deferred
from scripts.catalog_schema_v2 import to_v1
from scripts.sync_release_updates import version_order, select_current_download, app_repositories, archive_vpk_link


def audit(root):
    rows = []
    totals = Counter()
    for path in sorted((root / "apps").glob("*.json")):
        app = to_v1(json.loads(path.read_text("utf-8")))
        version = app.get("version")
        old_name = isinstance(version, str) and bool(VALID_VERSION.fullmatch(version))
        try:
            name = expected_filename({"app_id": app.get("id"), "version": version})
        except Deferred:
            name = None
        order = version_order(version)
        totals["scanned"] += 1
        totals["previous_filename_block"] += int(not old_name)
        totals["safe_filename"] += int(name is not None)
        totals["comparison_requires_review"] += int(order is None)
        if old_name and name is not None and order is not None:
            continue
        link, _ = select_current_download(app.get("links"))
        url = link.get("url") if link else None
        rows.append({"app_id": app.get("id"), "version": version,
                     "status": app.get("status"), "previous_filename_block": not old_name,
                     "backup_filename": name, "comparison_family": order[0] if order else None,
                     "has_archive_vpk": archive_vpk_link(url),
                     "has_registered_repository": bool(app_repositories(app))})
    return {"counts": dict(totals), "rows": rows}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--report", type=Path, default=Path("archive-batch-report/version-label-audit.json"))
    args = p.parse_args()
    report = audit(args.root)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", "utf-8")
    print("Version-label audit: " + json.dumps(report["counts"]), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
