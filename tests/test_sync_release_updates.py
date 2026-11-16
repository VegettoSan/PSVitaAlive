#!/usr/bin/env python3
"""Regression tests for unattended PSVitaAlive release synchronization."""
import json
import tempfile
import unittest
from pathlib import Path

from scripts.catalog_schema_v2 import to_v1, to_v2
from scripts.sync_release_updates import (
    download_belongs_to_repo, newer_version, plan_changes, release_update
)

REPO = "example/vita-app"
OLD = "https://github.com/example/vita-app/releases/download/v1.0/vita.vpk"
NEW = "https://github.com/example/vita-app/releases/download/v1.1/vita.vpk"


def app():
    return {
        "id": "vita-app",
        "title_id": "VITA00001",
        "name": "Vita App",
        "version": "1.0",
        "version_date": "2026-10-01",
        "status": "Legacy",
        "size": 100,
        "author_ids": ["dev"],
        "source_url": "https://github.com/example/vita-app",
        "links": [
            {"type": "Download", "name": "VPK oficial", "url": OLD, "recommended": True},
            {"type": "Mirror", "name": "Preservación", "url": "https://archive.org/details/vita-app"},
            {"type": "Repository", "name": "Fuente", "url": "https://github.com/example/vita-app"},
        ],
    }


def release(**overrides):
    base = {
        "platform": "github", "repo": "example/vita-app", "tag": "v1.1",
        "published": "2026-11-09T00:00:00Z", "prerelease": False,
        "assets": [{"name": "vita.vpk", "download_url": NEW, "size": 400}],
    }
    base.update(overrides)
    return base


class TestReleaseSynchronization(unittest.TestCase):
    def test_only_promotes_unambiguous_official_download(self):
        original = app()
        updated, reason = release_update(original, release(), REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.1")
        self.assertEqual(updated["version_date"], "2026-11-09")
        self.assertEqual(updated["links"][0]["url"], NEW)
        self.assertEqual(updated["size"], 400)
        self.assertEqual(updated["title_id"], original["title_id"])
        self.assertEqual(updated["author_ids"], original["author_ids"])
        self.assertEqual(updated["links"][1], original["links"][1])
        self.assertEqual(original["links"][0]["url"], OLD)

    def test_never_automatically_updates_archive(self):
        item = app()
        item["status"] = "Archive"
        self.assertIsNone(release_update(item, release(), REPO)[0])

    def test_rejects_prereleases_uncomparable_and_downgrades(self):
        self.assertIsNone(release_update(app(), release(prerelease=True), REPO)[0])
        self.assertIsNone(release_update(app(), release(tag="nightly-2026-11-09"), REPO)[0])
        self.assertFalse(newer_version("2.0", "v1.1"))
        self.assertFalse(newer_version("1.0-hotfix", "1.1"))

    def test_rejects_multiple_vpks(self):
        item = release()
        item["assets"].append({"name": "other.vpk", "size": 200, "download_url": NEW})
        self.assertIsNone(release_update(app(), item, REPO)[0])

    def test_rejects_complex_installers_and_outside_recommended(self):
        item = app()
        item["links"].append({"type": "Game Files", "name": "Data", "url": "https://example.org/data.zip"})
        self.assertIsNone(release_update(item, release(), REPO)[0])
        item = app()
        item["links"][0]["recommended"] = False
        item["links"][1]["recommended"] = True
        self.assertIsNone(release_update(item, release(), REPO)[0])

    def test_rejects_unrelated_downloads(self):
        self.assertFalse(download_belongs_to_repo(
            "https://github.com/example/other/releases/download/v1.1/vita.vpk", REPO))
        item = app()
        item["links"][0]["url"] = "https://archive.org/download/vita/vita.vpk"
        self.assertIsNone(release_update(item, release(), REPO)[0])

    def test_ambiguity_detection_and_compact_v2(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps" / "vita-app.json").write_text(
                json.dumps(to_v2(app())), encoding="utf-8")
            snapshot = {"schema_version": 1, "releases": [release()]}
            changes, skipped = plan_changes(root, snapshot)
            self.assertEqual(len(changes), 1)
            path, proposed, compact = changes[0]
            self.assertEqual(path.name, "vita-app.json")
            self.assertTrue(compact)
            self.assertEqual(to_v1(to_v2(proposed)), proposed)
            duplicate = app()
            duplicate["id"] = "vita-app-copy"
            duplicate["title_id"] = "VITA00002"
            (root / "apps" / "copy.json").write_text(json.dumps(to_v2(duplicate)), encoding="utf-8")
            changes, skipped = plan_changes(root, snapshot)
            self.assertEqual(changes, [])
            self.assertTrue(any("shared repository" in issue for issue in skipped))


if __name__ == "__main__":
    unittest.main()
