#!/usr/bin/env python3
from __future__ import annotations

from .identity import canonical_author_id
from .normalizer import normalize_version
from .sources import Candidate


# External source authority for release-date data. Lower numeric priority is
# weaker. VitaDB is the primary source; VitaHomebrewDB is the secondary fallback.
SOURCE_PRIORITY = {
    "vitadb": 110,
    "vitadbtoo": 90,
}


def source_priority(candidate: Candidate) -> int:
    return SOURCE_PRIORITY.get(candidate.source_id, 0)


def version_key(candidate: Candidate):
    version = normalize_version(candidate.version)
    date = str(candidate.version_date or "")
    stable = 0 if any(marker in str(candidate.version or "").lower() for marker in ("beta", "alpha", "rc", "nightly", "dev")) else 1
    return (version, stable, date)


def select_newest(candidates: list[Candidate]) -> Candidate:
    return max(candidates, key=version_key)


def select_release_candidate(candidates: list[Candidate], local: Candidate | None) -> Candidate:
    """Select release metadata without turning a source refresh into an update.

    A local app and an external catalog can describe the same version. For the
    same version, source authority is used before the date: VitaDB is primary
    and VitaHomebrewDB is the fallback. A newer external version can still replace
    the local version normally.
    """
    external = [item for item in candidates if item.source_id != "local"]
    if not local:
        if not external:
            return select_newest(candidates)
        return max(external, key=lambda item: (normalize_version(item.version), 1, source_priority(item), str(item.version_date or "")))

    local_version = normalize_version(local.version)
    newer = [
        item
        for item in external
        if normalize_version(item.version) > local_version
    ]
    if newer:
        return max(newer, key=lambda item: (normalize_version(item.version), 1, source_priority(item), str(item.version_date or "")))

    same_version_with_date = [
        item
        for item in external
        if normalize_version(item.version) == local_version
        and isinstance(item.version_date, str)
        and item.version_date.strip()
    ]
    if same_version_with_date:
        return max(same_version_with_date, key=lambda item: (source_priority(item), str(item.version_date or "")))

    return local


def _select_version_date(candidates: list[Candidate], release: Candidate, local: Candidate | None) -> str | None:
    """Resolve version_date using source authority, never apparent recency.

    For a given version, VitaDB is authoritative when it provides a date.
    VitaHomebrewDB is used only when VitaDB does not provide one. A local date is
    used only when neither external source has a date for that version.
    """
    target_version = normalize_version(release.version)
    dated_external = [
        item
        for item in candidates
        if item.source_id in SOURCE_PRIORITY
        and normalize_version(item.version) == target_version
        and isinstance(item.version_date, str)
        and item.version_date.strip()
    ]

    if dated_external:
        # Pick the highest-authority source first. Within one source, if there
        # are duplicate records for the same version, use the newest dated one.
        return max(
            dated_external,
            key=lambda item: (source_priority(item), str(item.version_date or "")),
        ).version_date

    if local and normalize_version(local.version) == target_version and local.version_date:
        return local.version_date

    if release.version_date:
        return release.version_date

    return None


def merge_group(candidates: list[Candidate]) -> Candidate:
    local = next((item for item in candidates if item.source_id == "local"), None)
    release = select_release_candidate(candidates, local)
    version_date = _select_version_date(candidates, release, local)

    # version_date is a required VitaHub field. Never manufacture a date from
    # the current day (or any other unrelated timestamp). A missing date must
    # fail validation so the source can be reviewed/corrected instead.
    if not isinstance(version_date, str) or not version_date.strip():
        raise ValueError(
            f"Missing version_date for {release.name!r} "
            f"(title_id={release.title_id!r}, version={release.version!r})"
        )

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
        version_date=version_date,
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
