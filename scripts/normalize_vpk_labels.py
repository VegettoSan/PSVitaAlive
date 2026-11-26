#!/usr/bin/env python3
"""Audit canonical Homebrew VPK labels and normalize only unambiguous ones.

No network, no generated catalog edits, no URL/type/recommendation changes.
Keep explicit variants, multiple downloads and unknown labels for review.
Works with both canonical JSON schemas.
"""
from __future__ import annotations

import argparse
import json
import os
import re
from collections import Counter
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
VARIANT = re.compile(r"\b(?:unsafe|safe|lite|full|demo|debug|nightly|experimental|alternate|alternative|patched|modded|uncensored|censored|english|japanese|eur|usa|jpn|undub|dubbed|test|beta|alpha|stable|legacy|no[-_ ]?data|with[-_ ]?data)\b", re.I)


def text(value):
    return value.strip() if isinstance(value, str) else ""


def tokens(value):
    return re.sub(r"[\W_]+", "", text(value).casefold())


def filename(url):
    try:
        u = urlsplit(text(url))
        if (u.scheme not in {"http", "https"} or not u.hostname
                or u.username or u.password or u.port not in (None, 80, 443)):
            return ""
        name = unquote(u.path.rsplit("/", 1)[-1])
        if "/" in name or "\\" in name or not name.lower().endswith(".vpk"):
            return ""
        return name
    except ValueError:
        return ""


def normalizable(label, app, asset_name):
    name = text(label)
    normalized = tokens(name)
    if not normalized:
        return False
    if normalized in {"vpk", "downloadvpk", "vpkdownload", "download"}:
        return True
    version = text(app.get("v", app.get("version")))
    if version:
        # Exact current version, including Alpha 0.1.0; not another build.
        version_labels = {tokens(version), tokens("v" + version)}
        version_labels |= {x + "vpk" for x in tuple(version_labels)}
        if normalized in version_labels:
            return True
    names = {tokens(app.get("n", app.get("name"))), tokens(app.get("id"))}
    names.discard("")
    if normalized in names or normalized in {x + "vpk" for x in names}:
        return True
    # Do not erase meaningful variant labels simply because they appear in
    # the filename. Explicit alternate configurations stay human-readable.
    if VARIANT.search(name.replace("_", " ")):
        return False
    stem = tokens(asset_name[:-4])
    return bool(stem and normalized in {stem, stem + "vpk"})


def inspect_app(app):
    """Return status, index to edit (if any), and a human-readable reason."""
    if not isinstance(app, dict):
        raise ValueError("App record is not an object")
    if "l" in app and "links" in app:
        raise ValueError("Mixed link schema")
    links = app.get("l", app.get("links"))
    if not isinstance(links, list):
        return "NO_DIRECT_VPK", None, "No link array"
    download, direct, named = [], [], []
    for i, link in enumerate(links):
        if not isinstance(link, dict):
            continue
        if any(k in link and long in link for k, long in (("t", "type"), ("n", "name"), ("u", "url"))):
            raise ValueError("Mixed link field schema")
        kind = text(link.get("t", link.get("type"))).casefold()
        label = text(link.get("n", link.get("name")))
        asset = filename(link.get("u", link.get("url")))
        if kind == "download":
            download.append(i)
            if label.casefold() == "vpk":
                named.append(i)
        if asset:
            direct.append((i, asset, kind))
    if len(named) > 1:
        return "REVIEW", None, "Multiple Download/VPK buttons; no change"
    if named:
        i = named[0]
        link = links[i]
        label = link.get("n", link.get("name"))
        if label == "VPK":
            return "ALREADY_STANDARD", None, "Existing principal VPK label retained"
        # Case/spacing-only cleanup preserves exactly the existing selector.
        return "NORMALIZE", i, "Canonical capitalization/spacing"
    if not direct:
        return "NO_DIRECT_VPK", None, "No direct .vpk URL"
    if len(download) != 1 or len(direct) != 1 or direct[0][0] != download[0]:
        return "REVIEW", None, "Multiple or non-Download VPK candidates"
    i, asset, _ = direct[0]
    link = links[i]
    label = link.get("n", link.get("name"))
    if not normalizable(label, app, asset):
        return "REVIEW", None, "Nonstandard label may describe a variant"
    return "NORMALIZE", i, "Unique direct Download VPK"


def audit(root, apply=False):
    rows, changes = [], []
    for path in sorted((root / "apps").glob("*.json")):
        raw = path.read_text("utf-8")
        app = json.loads(raw)
        status, i, reason = inspect_app(app)
        row = {"app_id": app.get("id", path.stem), "status": status, "reason": reason}
        if i is not None:
            links = app.get("l", app.get("links"))
            link = links[i]
            key = "n" if "n" in link else "name"
            row["old_label"] = link[key]
            row["new_label"] = "VPK"
            link[key] = "VPK"
            fmt = {"separators": (",", ":")} if raw.count("\n") <= 1 else {"indent": 2}
            changes.append((path, json.dumps(app, ensure_ascii=False, **fmt) + "\n"))
        if status == "REVIEW":
            links = app.get("l", app.get("links")) or []
            row["labels"] = [text(l.get("n", l.get("name"))) for l in links
                             if isinstance(l, dict) and
                             (text(l.get("t", l.get("type"))).casefold() == "download"
                              or filename(l.get("u", l.get("url"))))]
        rows.append(row)
    # Parse/inspect every source before any write.
    if apply:
        for path, payload in changes:
            path.write_text(payload, "utf-8")
    counts = dict(Counter(r["status"] for r in rows))
    report = {"mode": "APPLY" if apply else "DRY_RUN", "scanned": len(rows), "counts": counts,
              "rows": rows}
    for row in rows:
        if row["status"] in {"NORMALIZE", "REVIEW"}:
            print(json.dumps(row, ensure_ascii=True), flush=True)
    print("VPK label audit: " + json.dumps({"scanned": len(rows), "counts": counts}), flush=True)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--apply", action="store_true")
    p.add_argument("--report", type=Path)
    args = p.parse_args()
    report = audit(args.root, args.apply)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", "utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write("## Canonical VPK label audit\n\n")
            f.write(f"Applications scanned: {report['scanned']}\n\n")
            for status, count in sorted(report["counts"].items()):
                f.write(f"- {status}: {count}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
