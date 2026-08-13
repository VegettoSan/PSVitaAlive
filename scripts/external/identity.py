#!/usr/bin/env python3
from __future__ import annotations

import re

from .normalizer import canonical_repo, normalize_text
from .sources import Candidate


def canonical_author_id(name: str) -> str:
    value = normalize_text(name)
    value = re.sub(r"[^a-z0-9_-]+", "-", value).strip("-")
    return value or "unknown-author"


def identity_keys(candidate: Candidate) -> list[tuple[str, str]]:
    keys = []
    if candidate.title_id:
        keys.append(("title_id", str(candidate.title_id).strip().upper()))
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
    left_keys = set(identity_keys(left))
    right_keys = set(identity_keys(right))
    strong_left = {item for item in left_keys if item[0] in {"title_id", "repo"}}
    strong_right = {item for item in right_keys if item[0] in {"title_id", "repo"}}
    return bool(strong_left & strong_right) or bool(left_keys & right_keys and not strong_left and not strong_right)


def choose_existing_id(candidates: list[tuple[str, Candidate]]) -> str | None:
    for source_id, candidate in candidates:
        if source_id == "local" and candidate.source_item_id:
            return str(candidate.source_item_id)
    return None
