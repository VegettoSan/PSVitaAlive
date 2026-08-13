#!/usr/bin/env python3
from __future__ import annotations

from .sources import Candidate, fetch_json, normalize_neovitadb


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
    return normalize_neovitadb(raw)
