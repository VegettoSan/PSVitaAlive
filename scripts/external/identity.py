#!/usr/bin/env python3
from __future__ import annotations

import re

from .normalizer import canonical_repo, normalize_text
from .sources import Candidate


def canonical_author_id(name: str) -> str:
    value = normalize_text(name)
    # Normalize separators so "Danil_linaD" and "danil-linad" share one id.
    value = re.sub(r"[^a-z0-9]+", "-", value).strip("-")
    return value or "unknown-author"


def canonical_title_id(value: str | None) -> str:
    """Return the canonical Vita title ID used for cross-catalog identity."""
    raw = str(value or "").strip().upper()
    return re.sub(r"[^A-Z0-9]", "", raw)


def identity_keys(candidate: Candidate) -> list[tuple[str, str]]:
    keys = []
    title_id = canonical_title_id(candidate.title_id)
    if title_id:
        keys.append(("title_id", title_id))
    repo = canonical_repo(candidate.repository_url)
    if repo:
        keys.append(("repo", repo))
    if candidate.download_url:
        keys.append(("download", candidate.download_url.split("?")[0].split("#")[0].rstrip("/")))
    name = normalize_text(candidate.name)
    authors = ",".join(sorted(canonical_author_id(item) for item in candidate.author_names))
    if name and authors:
        keys.append(("weak", f"{name}|{authors}"))
    return keys


def same_identity(left: Candidate, right: Candidate) -> bool:
    """Title ID is authoritative across all external catalogs."""
    left_title = canonical_title_id(left.title_id)
    right_title = canonical_title_id(right.title_id)
    if left_title and right_title:
        return left_title == right_title

    left_repo = canonical_repo(left.repository_url)
    right_repo = canonical_repo(right.repository_url)
    if left_repo and right_repo:
        return left_repo == right_repo

    left_keys = set(identity_keys(left))
    right_keys = set(identity_keys(right))
    return bool(left_keys & right_keys and not left_repo and not right_repo)


def choose_existing_id(candidates: list[tuple[str, Candidate]]) -> str | None:
    for source_id, candidate in candidates:
        if source_id == "local" and candidate.source_item_id:
            return str(candidate.source_item_id)
    return None


def split_author_names(value) -> list[str]:
    """Split compound author fields into individual display names.

    Handles separators commonly used by VitaDB-family feeds:
    &, +, comma, semicolon, newline, and the word "and".
    Conservative: keeps single legitimate names intact.
    """
    if isinstance(value, list):
        result: list[str] = []
        seen: set[str] = set()
        for item in value:
            for part in split_author_names(item):
                key = normalize_text(part)
                if key and key not in seen:
                    seen.add(key)
                    result.append(part)
        return result

    if not isinstance(value, str) or not value.strip():
        return []

    text_value = re.sub(r"\s+", " ", value.replace("\u00a0", " ")).strip()
    text_value = re.sub(r"\s*&\s*", ",", text_value)
    text_value = re.sub(r"\s+\band\b\s+", ",", text_value, flags=re.I)
    text_value = re.sub(r"\s*\+\s*", ",", text_value)
    text_value = re.sub(r"[;\n\r]+", ",", text_value)

    result = []
    seen = set()
    for part in text_value.split(","):
        name = part.strip(" \t,;|")
        if not name:
            continue
        key = normalize_text(name)
        if key and key not in seen:
            seen.add(key)
            result.append(name)
    return result
