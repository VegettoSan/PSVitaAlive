#!/usr/bin/env python3
from __future__ import annotations

from .sources import Candidate, fetch_json


CANDIDATE_URLS = [
    "https://robin994.github.io/NeoVitaDB-Catalog/dist/vita.json",
    "https://robin994.github.io/NeoVitaDB-Catalog/vita.json",
]


def fetch_candidates():
    last_error = None
    for url in CANDIDATE_URLS:
        try:
            data = fetch_json(url)
            if isinstance(data, list):
                return data
        except Exception as exc:
            last_error = exc
    raise RuntimeError(f"NeoVitaDB feed unavailable: {last_error}")


def normalize(raw: dict) -> Candidate:
    return Candidate(
        source_id="neovitadb",
        source_item_id=str(raw.get("id")) if raw.get("id") is not None else None,
        title_id=raw.get("titleid") or raw.get("title_id"),
        name=str(raw.get("name") or "").strip(),
        author_names=[str(raw.get("author"))] if raw.get("author") else [],
        repository_url=raw.get("repo") or raw.get("source"),
        release_page=raw.get("release_page"),
        version=raw.get("version"),
        version_date=raw.get("date") or raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=raw.get("icon") or raw.get("icon_url"),
        screenshots=raw.get("screenshots") or raw.get("screenshot_urls") or [],
        download_url=raw.get("url") or raw.get("download_url"),
        size=raw.get("size") or raw.get("size_bytes"),
        category_raw=raw.get("category") or raw.get("type"),
        platform=raw.get("platform") or "vita",
    )
