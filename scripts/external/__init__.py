"""External catalog aggregation helpers for PSVitaAlive.

This package installs a small source-level protection hook before
``aggregate.py`` imports the normalizers. The hook reads
``registry/retired_ids.json`` and prevents externally rediscovered retired
Title IDs from entering the candidate set. This keeps author-requested
removals protected without changing the merge/generation architecture.
"""

from __future__ import annotations

import json
from pathlib import Path

from . import sources as _sources

ROOT = Path(__file__).resolve().parents[2]
REGISTRY_PATH = ROOT / "registry" / "retired_ids.json"


def _load_retired_title_ids() -> set[str]:
    try:
        with REGISTRY_PATH.open(encoding="utf-8") as handle:
            registry = json.load(handle)
    except (OSError, ValueError, TypeError):
        return set()

    values = registry.get("apps", {}).get("title_ids", []) if isinstance(registry, dict) else []
    if not isinstance(values, list):
        return set()
    return {
        value.strip().upper()
        for value in values
        if isinstance(value, str) and value.strip()
    }


def _install_retired_source_filter() -> None:
    if getattr(_sources, "_PSVITAALIVE_RETIRED_FILTER_INSTALLED", False):
        return

    normalizers = (
        "normalize_vitadb",
        "normalize_vitadbtoo",
        "normalize_neovitadb",
    )

    for function_name in normalizers:
        original = getattr(_sources, function_name, None)
        if original is None:
            continue

        def guarded_normalizer(raw, _original=original):
            candidate = _original(raw)
            retired = _load_retired_title_ids()
            title_id = str(getattr(candidate, "title_id", "") or "").strip().upper()
            if title_id and title_id in retired:
                print(
                    f"Protected retired Title ID skipped from external import: {title_id}"
                )
                candidate.name = ""
            return candidate

        setattr(_sources, function_name, guarded_normalizer)

    _sources._PSVITAALIVE_RETIRED_FILTER_INSTALLED = True


_install_retired_source_filter()
