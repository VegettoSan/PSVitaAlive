#!/usr/bin/env python3
"""Offline smoke tests for the external aggregation primitives."""

from pathlib import Path
import tempfile

from external.identity import canonical_author_id, same_identity
from external.merge import merge_group, select_newest
from external.overrides import load_overrides, apply_override
from external.sources import Candidate, extract_catalog_items, normalize_vitadb, normalize_vitadbtoo, normalize_neovitadb


def candidate(source, version, title="TEST00001", version_date="2026-08-12"):
    return Candidate(
        source_id=source,
        source_item_id=source,
        title_id=title,
        name="Example Homebrew",
        author_names=["ExampleDev"],
        repository_url="https://github.com/example/project",
        release_page=None,
        version=version,
        version_date=version_date,
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

# A local stale date must be repaired by a matching external version date.
local = candidate("local", "1.1", version_date="2026-08-15")
external = candidate("vitadbtoo", "1.1", version_date="2016-07-30")
merged = merge_group([local, external])
assert merged.version == "1.1"
assert merged.version_date == "2016-07-30"

wrapped = extract_catalog_items({"data": [{"name": "Wrapped"}]}, "vitadb")
assert len(wrapped) == 1
assert wrapped[0]["name"] == "Wrapped"

vitadb = normalize_vitadb({
    "id": 123,
    "name": "Example Vita App",
    "icon": "https://example/icon.png",
    "version": "1.10",
    "author": "ExampleDev",
    "type": "1",
    "date": "2026-08-12",
    "titleid": "TEST00001",
    "screenshots": "https://example/1.png,https://example/2.png",
    "long_description": "Long description",
    "downloads": "10",
    "source": "https://github.com/example/project",
    "release_page": "https://github.com/example/project/releases",
    "url": "https://example/app.vpk",
    "size": "12345",
})
assert vitadb.source_id == "vitadb"
assert vitadb.title_id == "TEST00001"
assert vitadb.category_raw == "game"  # type 1 = Original Game (NeoVitaDB/VitaDBtoo)
assert vitadb.version_date == "2026-08-12"
assert len(vitadb.screenshots) == 2
assert vitadb.download_url == "https://example/app.vpk"

vitadb_port = normalize_vitadb({
    "id": 124,
    "name": "Example Port",
    "icon": "https://example/icon.png",
    "version": "1.0",
    "author": "ExampleDev",
    "type": "2",
    "date": "2026-08-12",
    "titleid": "PORT00001",
    "url": "https://example/port.vpk",
    "size": "1",
})
assert vitadb_port.category_raw == "port"  # type 2 = Game Port

vitadb_util = normalize_vitadb({
    "id": 125,
    "name": "Example Utility",
    "icon": "https://example/icon.png",
    "version": "1.0",
    "author": "ExampleDev",
    "type": "4",
    "date": "2026-08-12",
    "titleid": "UTIL00001",
    "url": "https://example/util.vpk",
    "size": "1",
})
assert vitadb_util.category_raw == "utility"  # type 4 = Utility

vitadb_emu = normalize_vitadb({
    "id": 126,
    "name": "Example Emulator",
    "icon": "https://example/icon.png",
    "version": "1.0",
    "author": "ExampleDev",
    "type": "5",
    "date": "2026-08-12",
    "titleid": "EMUL00001",
    "url": "https://example/emu.vpk",
    "size": "1",
})
assert vitadb_emu.category_raw == "emulator"  # type 5 = Emulator

vitadbtoo = normalize_vitadbtoo({
    "id": 122,
    "name": "VitaScreenFlasher",
    "icon": "vitascreen.png",
    "version": "v.1.1",
    "author": "SMOKE",
    "type": "4",
    "date": "2016-07-30",
    "titleid": "SCRENFLSH",
    "url": "https://example/screen.vpk",
    "size": "312844",
})
assert vitadbtoo.version_date == "2016-07-30"

neovitadb = normalize_neovitadb({
    "id": 21,
    "name": "Example NeoVita App",
    "author": "ExampleDev",
    "category": "utility",
    "platform": "vita",
    "titleid": "NEO000001",
    "repo": "example/project",
    "version": "1.2.3",
    "date": "2024-05-06",
    "icon": "example.png",
})
assert neovitadb.version == "1.2.3"
assert neovitadb.version_date == "2024-05-06"

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    directory = root / "catalog_overrides"
    directory.mkdir()
    (directory / "example.json").write_text(
        '{"id":"example","description":"Editorial","links":{"add":[{"type":"Download","name":"Game Data","url":"https://example/data.zip"}]} }\n',
        encoding="utf-8",
    )
    overrides = load_overrides(root)
    result = apply_override({"id":"example","description":"Old","links":[]}, overrides["example"])
    assert result["description"] == "Editorial"
    assert result["links"][0]["name"] == "Game Data"

print("External aggregation smoke tests passed.")
