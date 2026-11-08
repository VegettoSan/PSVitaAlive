#!/usr/bin/env python3
"""Reversible PSVitaAlive catalog-only JSON key codec (never transforms values).

Source schemas such as VitaDBtoo are normalized *before* entering this codec.
Source IDs within `sources`, regions within `title_ids`, and translations
within language dictionaries are user/data identifiers, not schema keys.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

# Always add a documented mapping here before migrating a new catalog field.
# The mapping must remain injective. Existing 1-3 character keys are preserved
# if renaming them would not reduce size or would risk historical semantics.
KEYS = {
    "title_id": "tid",
    "title_ids": "tids",
    "name": "n",
    "description": "d",
    "long_description": "ld",
    "author_ids": "aids",
    "author_id": "aid",
    "category_id": "cid",
    "subcategory_ids": "sids",
    "version": "v",
    "version_date": "vd",
    "release_date": "rd",
    "updated_at": "ua",
    "date": "dt",
    "icon": "ic",
    "icon_url": "icu",
    "cover": "cv",
    "cover_url": "cvu",
    "background": "bg",
    "background_url": "bgu",
    "screenshots": "ss",
    "screenshot_urls": "ssu",
    "links": "l",
    "status": "st",
    "source": "so",
    "source_name": "sn",
    "source_id": "si",
    "source_url": "su",
    "source_catalog": "sct",
    "source_labels": "sl",
    "sources": "srcs",
    "release_page": "rp",
    "requirements": "req",
    "changelog": "cl",
    "downloads": "dl",
    "download_url": "dlu",
    "hash": "h",
    "hash2": "h2",
    "score": "sc",
    "size": "sz",
    "data_size": "dsz",
    "data_url": "du",
    "data_extract_path": "dxp",
    "data_size_bytes": "dsb",
    "game_data_languages": "gdl",
    "languages": "langs",
    "language": "lang",
    "extract_path": "xp",
    "installed_paths": "ips",
    "recommended": "rec",
    "type": "t",
    "url": "u",
    "avatar": "av",
    "bio": "b",
    "order": "ord",
    "subcategories": "subs",
    "required_fw": "rfw",
    "pkg_name": "pn",
    "rap": "rap",  # already short; preserved for PSP compatibility
    "content_id": "coid",
    "contentId": "coi",
    "region": "rg",
    "regions": "rgs",
    "zrif": "zr",
    "zRIF": "zR",
    "license": "lic",
    "required": "rq",
    "section": "sec",
    "line": "ln",
    "plugin_install_path": "pip",
    "plugin_config_section": "pcs",
    "plugin_config_line": "pcl",
    "plugins": "plg",
    "conflicts_with": "cwf",
    "replaces": "repl",
    "requires": "requires",  # already short; keep meaning
    "kernel_priority": "kp",
    "reboot_required": "rr",
    "last_seen": "ls",
    "original_name": "on",
    "overview": "ov",
    "install_count": "inct",
    "average_rating": "ar",
    "ratings_count": "rc",
    "likes_count": "lc",
    "comments_count": "cc",
    "user_liked": "ul",
    "user_rating": "ur",
    "dr": "dr",
    "id": "id",
}
# Wildcard keys are data, never changed. E.g. { "sources": {"vitadb": {...}} }.
PASSTHROUGH_CHILDREN = {"sources", "title_ids", "overview"}

def _validate_map() -> None:
    inverse: dict[str, str] = {}
    for long, short in KEYS.items():
        if short in inverse and inverse[short] != long:
            raise ValueError(f"Schema V2 name collision: {inverse[short]} and {long} -> {short}")
        inverse[short] = long
        if short != long and short in KEYS:
            raise ValueError(f"V2 alias {short} collides with V1 key")
_validate_map()
REVERSE = {v: k for k, v in KEYS.items() if v != k}


def _transform(obj: Any, *, compact: bool, parent: str = "", passthrough: bool = False) -> Any:
    if isinstance(obj, list):
        return [_transform(v, compact=compact, parent=parent, passthrough=passthrough) for v in obj]
    if not isinstance(obj, dict):
        return obj
    result: dict[str, Any] = {}
    for key, val in obj.items():
        if passthrough:
            new_key = key
            nested_passthrough = False  # Preserve dynamic key only, not fields inside it.
        else:
            new_key = (KEYS if compact else REVERSE).get(key, key)
            v1_key = key if compact else REVERSE.get(key, key)
            # Source names ('vitadb'), region IDs ('US'), language IDs: data identifiers.
            nested_passthrough = v1_key in PASSTHROUGH_CHILDREN
        if new_key in result:
            raise ValueError(f"Collision at {parent}: {key!r} -> {new_key!r}")
        result[new_key] = _transform(val, compact=compact, parent=parent + "." + new_key,
                                     passthrough=nested_passthrough)
    return result


def to_v2(obj: Any) -> Any:
    return _transform(obj, compact=True)


def to_v1(obj: Any) -> Any:
    return _transform(obj, compact=False)


def assert_lossless(original: Any) -> None:
    converted = to_v2(original)
    restored = to_v1(converted)
    if restored != original:
        raise AssertionError("Schema V2 roundtrip lost data or changed values")


def load_source(path: Path) -> Any:
    with path.open(encoding="utf-8") as fh:
        return to_v1(json.load(fh))


def dump_source(path: Path, data: Any, *, compact: bool = False) -> None:
    path.write_text(
        json.dumps(to_v2(data), ensure_ascii=False,
                   separators=(",", ":") if compact else None,
                   indent=None if compact else 2) + "\n",
        encoding="utf-8",
    )
