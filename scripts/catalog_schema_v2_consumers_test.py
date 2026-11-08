#!/usr/bin/env python3
"""Fail fast when Python, web JS and VitaSDK key readers diverge.

This is intentionally static: the VitaSDK build checks C++ syntax separately;
this check guarantees JSON wire-format contract coverage before that build.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

from catalog_schema_v2 import KEYS, to_v1, to_v2

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web/js/catalog-schema-v2.js"
VITA = ROOT / "Client PSVitaAlive/source/catalog/catalog_parser.cpp"


def fail(problem: str) -> None:
    raise AssertionError("Schema V2 consumer mismatch: " + problem)


def check() -> None:
    js = WEB.read_text(encoding="utf-8")
    prefix = "const fields = Object.freeze("
    if prefix not in js:
        fail("web key map not found")
    start = js.index(prefix) + len(prefix)
    end = js.find(");", start)
    if end < start:
        fail("web key map not closed")
    js_map = json.loads(js[start:end])
    if js_map != KEYS:
        missing = {k: KEYS[k] for k in KEYS.keys() - js_map.keys()}
        extra = {k: js_map[k] for k in js_map.keys() - KEYS.keys()}
        divergent = {k: (KEYS[k], js_map[k])
                     for k in KEYS.keys() & js_map.keys() if KEYS[k] != js_map[k]}
        fail(f"Python/JS aliases mismatch: missing={missing}, extra={extra}, changed={divergent}")

    cpp = VITA.read_text(encoding="utf-8")
    match = re.search(r'static const struct\s*\{.*?\}\s*aliases\[\]\s*=\s*\{(.*?)\n\s*\};',
                      cpp, re.S)
    if not match:
        fail("Vita C++ field alias table not found")
    cpp_map = dict(re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', match.group(1)))
    for old, short in cpp_map.items():
        if KEYS.get(old) != short:
            fail(f"C++ alias {old} -> {short} differs from Python alias {KEYS.get(old)}")

    accessors = re.findall(
        r'\b(?:getString|getUnsigned|firstArrayString|parseStringArray|getCatalogField)'
        r'\s*\(\s*[A-Za-z_][\w]*\s*,\s*"([^"]+)"\s*\)', cpp)
    for old in set(accessors):
        short = KEYS.get(old, old)
        if short != old and cpp_map.get(old) != short:
            fail(f"C++ reads {old} but cannot read V2 alias {short}")

    # Mixed values exercise all error-prone nested metadata that must remain
    # stable as application data (not be mistaken for schema names).
    fixture = [{
        "id": "test", "title_ids": {"US": "PCSE00001", "ASIA": "PCSH00002"},
        "name": "Café — 日本語",
        "links": [
            {"type": "Download", "name": "Base", "region": "ASIA",
             "url": "https://example.invalid/test.pkg?x=1&y=2",
             "content_id": "TEST-123", "recommended": False,
             "required_fw": "3.65", "pkg_name": "game.pkg", "rap": "xyz"},
            {"type": "DLC", "recommended": True, "size": 0,
             "url": "https://example.invalid/dlc.pkg"},
        ],
        "sources": {"vitadb": {"name": "Source Name", "source_id": 123}},
        "overview": {"ja": "説明", "en": "Description"},
    }]
    compact = to_v2(fixture)
    if to_v1(compact) != fixture:
        fail("nested region/source/links fixture not reversible")
    if list(compact[0]["tids"]) != ["US", "ASIA"]:
        fail("Title ID region keys were changed")
    if compact[0]["l"][0]["rec"] is not False:
        fail("False boolean for recommended was lost")
    if compact[0]["l"][0]["rap"] != "xyz":
        fail("RAP string was changed")

    print(f"PASS: {len(KEYS)} Python/JS aliases match, "
          f"{len(cpp_map)} C++ aliases match Python, "
          f"{len(set(accessors))} distinct C++ field reads covered.")


if __name__ == "__main__":
    check()
