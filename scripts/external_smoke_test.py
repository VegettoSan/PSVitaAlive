#!/usr/bin/env python3
"""Offline smoke tests for the external aggregation primitives."""

from pathlib import Path
import tempfile

from scripts.external.identity import canonical_author_id, same_identity
from scripts.external.merge import select_newest
from scripts.external.overrides import load_overrides, apply_override
from scripts.external.sources import Candidate


def candidate(source, version, title="TEST00001"):
    return Candidate(
        source_id=source,
        source_item_id=source,
        title_id=title,
        name="Example Homebrew",
        author_names=["ExampleDev"],
        repository_url="https://github.com/example/project",
        release_page=None,
        version=version,
        version_date="2026-08-12",
        description="description",
        long_description="long",
        requirements="",
        changelog="",
        icon="https://example/icon.png",
        screenshots=["https://example/1.png"],
        download_url="https://example/app.vpk",
        size=123,
        category_raw="game",
        platform="vita",
    )


a = candidate("a", "1.9")
b = candidate("b", "1.10")
assert select_newest([a, b]).version == "1.10"
assert same_identity(a, b)
assert canonical_author_id("Example Developer") == "example-developer"

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    directory = root / "catalog_overrides"
    directory.mkdir()
    (directory / "example.json").write_text(
        '{"id":"example","description":"Editorial","links":{"add":[{"type":"Download","name":"Game Data","url":"https://example/data.zip"}]}}\n',
        encoding="utf-8",
    )
    overrides = load_overrides(root)
    result = apply_override({"id":"example","description":"Old","links":[]}, overrides["example"])
    assert result["description"] == "Editorial"
    assert result["links"][0]["name"] == "Game Data"

print("External aggregation smoke tests passed.")
