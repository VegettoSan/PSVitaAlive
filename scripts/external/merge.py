#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import asdict
from .identity import canonical_author_id
from .normalizer import normalize_version
from .sources import Candidate


def version_key(candidate: Candidate):
    version = normalize_version(candidate.version)
    date = str(candidate.version_date or "")
    stable = 0 if any(marker in str(candidate.version or "").lower() for marker in ("beta", "alpha", "rc", "nightly", "dev")) else 1
    return (version, stable, date)


def select_newest(candidates: list[Candidate]) -> Candidate:
    return max(candidates, key=version_key)


def select_release_candidate(candidates: list[Candidate], local: Candidate | None) -> Candidate:
    """Select release metadata without turning a source refresh into an update."""
    if not local:
        return select_newest(candidates)

    local_version = normalize_version(local.version)
    external = [item for item in candidates if item.source_id != "local"]
    newer = [
        item
        for item in external
        if normalize_version(item.version) > local_version
    ]

    if newer:
        return select_newest(newer)

    return local


def merge_group(candidates: list[Candidate]) -> Candidate:
    local = next((item for item in candidates if item.source_id == "local"), None)
    release = select_release_candidate(candidates, local)

    def first_value(field):
        if local:
            value = getattr(local, field)
            if value not in (None, "", []):
                return value
        value = getattr(release, field)
        if value not in (None, "", []):
            return value
        for item in candidates:
            value = getattr(item, field)
            if value not in (None, "", []):
                return value
        return None

    authors = []
    for item in candidates:
        for author in item.author_names:
            if author and canonical_author_id(author) not in {canonical_author_id(x) for x in authors}:
                authors.append(author)

    screenshots = []
    for item in ([local] if local else []) + sorted(candidates, key=version_key, reverse=True):
        if not item:
            continue
        for url in item.screenshots:
            if url and url not in screenshots:
                screenshots.append(url)

    icon = first_value("icon")
    if not screenshots and icon and any(item.source_id != "local" for item in candidates):
        screenshots = [icon]

    return Candidate(
        source_id="merged",
        source_item_id=local.source_item_id if local else release.source_item_id,
        title_id=first_value("title_id"),
        name=first_value("name") or "Unnamed Homebrew",
        author_names=authors,
        repository_url=first_value("repository_url"),
        release_page=first_value("release_page"),
        version=release.version or first_value("version"),
        version_date=release.version_date or first_value("version_date"),
        description=first_value("description"),
        long_description=first_value("long_description"),
        requirements=first_value("requirements"),
        changelog=first_value("changelog"),
        icon=icon,
        screenshots=screenshots[:5],
        download_url=release.download_url or first_value("download_url"),
        size=release.size or first_value("size"),
        category_raw=local.category_raw if local and local.category_raw else first_value("category_raw"),
        platform="vita",
    )
