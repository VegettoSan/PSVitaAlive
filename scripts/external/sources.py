#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import urllib.request
from dataclasses import dataclass
from pathlib import Path


USER_AGENT = "PSVitaAlive-ExternalCatalog/1.3"


@dataclass
class Candidate:
    source_id: str
    source_item_id: str | None
    title_id: str | None
    name: str
    author_names: list[str]
    repository_url: str | None
    release_page: str | None
    version: str | None
    version_date: str | None
    description: str | None
    long_description: str | None
    requirements: str | None
    changelog: str | None
    icon: str | None
    screenshots: list[str]
    download_url: str | None
    size: int | None
    category_raw: str | None
    platform: str | None


def fetch_json(url: str):
    request = urllib.request.Request(
        url,
        headers={"User-Agent": USER_AGENT, "Accept": "application/json, text/plain, */*"},
    )
    with urllib.request.urlopen(request, timeout=45) as response:
        return json.load(response)


def extract_catalog_items(data, source_id: str) -> list[dict]:
    """Accept list feeds and common API wrapper/object shapes."""
    if isinstance(data, list):
        return [item for item in data if isinstance(item, dict)]
    if isinstance(data, dict):
        for key in ("apps", "homebrew", "homebrews", "items", "results", "data", "entries"):
            value = data.get(key)
            if isinstance(value, list):
                return [item for item in value if isinstance(item, dict)]
        values = list(data.values())
        if values and all(isinstance(item, dict) for item in values):
            if any(item.get("titleid") or item.get("title_id") or item.get("name") for item in values):
                return values
    raise ValueError(f"{source_id}: unsupported JSON feed shape ({type(data).__name__})")


def _as_list(value):
    if isinstance(value, list):
        return [str(item).strip() for item in value if str(item).strip()]
    if isinstance(value, str) and value.strip():
        return [value.strip()]
    return []


def _split_csvish(value):
    """Split legacy catalog fields without corrupting normal URLs.

    VitaDB legacy data commonly stores screenshots/authors as semicolon or
    comma separated strings. URLs themselves are kept intact; only explicit
    separators between values are split.
    """
    if isinstance(value, list):
        result = []
        for item in value:
            result.extend(_split_csvish(item))
        return result
    if not isinstance(value, str) or not value.strip():
        return []
    return [
        item.strip()
        for item in re.split(r"[;\n,]+", value.replace("\r", "\n"))
        if item.strip()
    ]


def _first_url(value):
    """Return the first usable URL from a scalar/list catalog field."""
    values = _split_csvish(value)
    for item in values:
        if item.startswith(("http://", "https://")):
            return item
    if isinstance(value, str) and value.strip():
        return value.strip()
    return None


def _int_or_none(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def normalize_vitadb(raw: dict) -> Candidate:
    """Normalize the official VitaDB list_hbs_json.php format."""
    type_value = str(raw.get("type") if raw.get("type") is not None else "").strip().lower()
    type_map = {
        "0": "game",
        "1": "port",
        "2": "utility",
        "3": "emulator",
        "4": "plugin",
        "game": "game",
        "port": "port",
        "utility": "utility",
        "emulator": "emulator",
        "plugin": "plugin",
    }
    category_raw = type_map.get(type_value, type_value or None)
    screenshots = _split_csvish(raw.get("screenshots"))
    authors = _split_csvish(raw.get("author") or raw.get("authors"))
    return Candidate(
        source_id="vitadb",
        source_item_id=str(raw.get("id")) if raw.get("id") is not None else None,
        title_id=raw.get("titleid") or raw.get("title_id"),
        name=str(raw.get("name") or "").strip(),
        author_names=authors,
        repository_url=_first_url(raw.get("source")),
        release_page=_first_url(raw.get("release_page")),
        version=raw.get("version"),
        version_date=raw.get("date") or raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=_first_url(raw.get("icon")),
        screenshots=screenshots,
        download_url=_first_url(raw.get("url") or raw.get("download_url") or raw.get("download")),
        size=_int_or_none(raw.get("size")),
        category_raw=category_raw,
        platform="vita",
    )


def normalize_vitadbtoo(raw: dict) -> Candidate:
    screenshots = _split_csvish(raw.get("screenshots"))
    authors = _as_list(raw.get("author") or raw.get("authors"))
    return Candidate(
        source_id="vitadbtoo",
        source_item_id=str(raw.get("id")) if raw.get("id") is not None else None,
        title_id=raw.get("titleid") or raw.get("title_id"),
        name=str(raw.get("name") or "").strip(),
        author_names=authors,
        repository_url=_first_url(raw.get("source")),
        release_page=_first_url(raw.get("release_page")),
        version=raw.get("version"),
        version_date=raw.get("date") or raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=_first_url(raw.get("icon")),
        screenshots=screenshots,
        download_url=_first_url(raw.get("url") or raw.get("download_url") or raw.get("download")),
        size=_int_or_none(raw.get("size")),
        category_raw=raw.get("type") or raw.get("category"),
        platform="vita",
    )


def normalize_psvitaalive(raw: dict) -> Candidate:
    authors = [str(author_id) for author_id in raw.get("author_ids", [])]
    links = raw.get("links", []) if isinstance(raw.get("links"), list) else []
    downloads = [item for item in links if item.get("type") == "Download"]
    repo = next((item.get("url") for item in links if item.get("type") == "Repository"), None)
    release = next((item.get("url") for item in links if item.get("type") in {"Official Website", "Repository"}), None)
    download = next((item.get("url") for item in downloads if item.get("recommended")), None)
    if not download and downloads:
        download = downloads[0].get("url")
    return Candidate(
        source_id="local",
        source_item_id=raw.get("id"),
        title_id=raw.get("title_id"),
        name=raw.get("name", ""),
        author_names=authors,
        repository_url=repo,
        release_page=release,
        version=raw.get("version"),
        version_date=raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=raw.get("icon"),
        screenshots=list(raw.get("screenshots", [])),
        download_url=download,
        size=_int_or_none(raw.get("size")),
        category_raw=raw.get("category_id"),
        platform="vita",
    )
