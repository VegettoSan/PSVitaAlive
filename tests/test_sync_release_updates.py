#!/usr/bin/env python3
"""Regression tests for unattended PSVitaAlive release synchronization."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

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

    def test_no_vpk_is_manual_even_if_zip_is_available(self):
        item = release()
        item["assets"] = [
            {"name": "source.zip", "size": 400,
             "download_url": "https://github.com/example/vita-app/releases/download/v1.1/source.zip"}
        ]
        proposal, reason = release_update(app(), item, REPO)
        self.assertIsNone(proposal)
        self.assertIn("no .vpk", reason)

    def test_rejects_vpk_from_an_unrelated_release_tag(self):
        item = release()
        item["assets"][0]["download_url"] = OLD
        proposal, reason = release_update(app(), item, REPO)
        self.assertIsNone(proposal)
        self.assertIn("different GitHub release tag", reason)

    def test_metadata_only_never_downloads_the_vpk(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps" / "vita-app.json").write_text(
                json.dumps(to_v2(app())), encoding="utf-8"
            )
            snapshot = {"schema_version": 1, "releases": [release()]}
            with patch("urllib.request.urlopen", side_effect=AssertionError("must not download")):
                changes, skipped = plan_changes(root, snapshot)
            self.assertEqual(len(changes), 1)
            self.assertEqual(skipped, [])
            self.assertEqual(changes[0][1]["version"], "1.1")

    def test_zero_or_multiple_vpks_stay_pending_for_manual_update(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps" / "vita-app.json").write_text(
                json.dumps(to_v2(app())), encoding="utf-8"
            )
            no_vpk = release(assets=[{"name": "bundle.zip", "size": 100}])
            multiple = release(assets=[
                {"name": "vita-a.vpk", "size": 200, "download_url": NEW},
                {"name": "vita-b.vpk", "size": 250, "download_url": NEW},
            ])
            for candidate in (no_vpk, multiple):
                with self.subTest(assets=candidate["assets"]):
                    changes, skipped = plan_changes(
                        root, {"schema_version": 1, "releases": [candidate]}
                    )
                    self.assertEqual(changes, [])
                    self.assertTrue(any("manual update required" in message for message in skipped))

    def test_unknown_asset_size_does_not_block_single_vpk(self):
        item = release()
        item["assets"][0].pop("size")
        original = app()
        original["links"][0]["size"] = 100
        updated, reason = release_update(original, item, REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version_date"], "2026-11-09")
        self.assertNotIn("size", updated)
        self.assertNotIn("size", updated["links"][0])

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
        item["links"][0]["url"] = "https://example.com/unknown-download.vpk"
        self.assertIsNone(release_update(item, release(), REPO)[0])

    def test_archive_version_upgrades_to_github_and_keeps_old_preserved_vpk(self):
        source = app()
        source["version"] = "v.1.0"
        old_archive = "https://archive.org/download/psvitaalive-vita-app/vita.vpk"
        source["links"][0]["url"] = old_archive
        original = json.loads(json.dumps(source))

        updated, reason = release_update(source, release(), REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.1")
        self.assertEqual(updated["links"][0]["url"], NEW)
        self.assertTrue(updated["links"][0]["recommended"])
        historical = [
            link for link in updated["links"]
            if link.get("url") == old_archive and link.get("type") == "Mirror"
        ]
        self.assertEqual(len(historical), 1)
        self.assertIn("anterior", historical[0]["name"])
        self.assertIn("v.1.0", historical[0]["name"])
        self.assertFalse(historical[0]["recommended"])
        self.assertEqual(historical[0]["size"], original["size"])
        self.assertEqual(updated["title_id"], original["title_id"])
        self.assertEqual(updated["author_ids"], original["author_ids"])
        self.assertEqual(source, original)

        # Idempotence: the same release must not trigger another edit.
        self.assertIsNone(release_update(updated, release(), REPO)[0])

    def test_catalog_already_current_never_overwrites_manual_archive_url(self):
        """A maintainer can swap GitHub URL for Archive.org after auto-updating."""
        current = app()
        current["version"] = "1.1"
        current["version_date"] = "2026-11-09"
        current["links"][0]["url"] = (
            "https://archive.org/download/psvitaalive-vita-app/vita-v1.1.vpk"
        )
        current["links"][0]["name"] = "Respaldo actual en Archive.org"
        # Preserve arbitrary user edits, not only links.
        current["links"][0]["size"] = 400
        current["links"].append({
            "type": "Mirror",
            "name": "Respaldo historico 1.0",
            "url": "https://archive.org/download/psvitaalive-vita-app/vita-v1.0.vpk",
            "recommended": False,
        })
        original = json.loads(json.dumps(current))
        same_release = release()
        result, reason = release_update(current, same_release, REPO)
        self.assertIsNone(result)
        self.assertIn("version not newer", reason)
        self.assertEqual(current, original)

        # Even if upstream publishes another release with an older tag,
        # a later monitor run must not roll back the preserved URL.
        older_release = release(tag="v1.0", published="2026-10-01T00:00:00Z")
        self.assertIsNone(release_update(current, older_release, REPO)[0])
        self.assertEqual(current, original)

    def test_catalog_scan_is_noop_after_manual_archive_backup(self):
        """The V2 canonical JSON stays byte-for-byte intact on repeated scans."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            source = app()
            source["version"] = "v.1.1"
            source["version_date"] = "2026-11-09"
            source["links"][0]["url"] = (
                "https://archive.org/download/psvitaalive-vita-app/vita-v1.1.vpk"
            )
            path = root / "apps" / "vita-app.json"
            path.write_text(
                json.dumps(to_v2(source), separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            original_bytes = path.read_bytes()
            snapshot = {"schema_version": 1, "releases": [release()]}
            for _ in range(3):
                changes, skipped = plan_changes(root, snapshot)
                self.assertEqual(changes, [])
                self.assertEqual(skipped, [])
                self.assertEqual(path.read_bytes(), original_bytes)

    def test_next_new_version_can_promote_again_and_keep_archive_history(self):
        current = app()
        current["version"] = "1.1"
        current["version_date"] = "2026-11-09"
        current["links"][0]["url"] = (
            "https://archive.org/download/psvitaalive-vita-app/vita-v1.1.vpk"
        )
        new_vpk = "https://github.com/example/vita-app/releases/download/v1.2/vita-v1.2.vpk"
        newer = release(
            tag="v1.2",
            published="2026-11-16T12:00:00Z",
            assets=[{"name": "vita-v1.2.vpk", "download_url": new_vpk, "size": 500}],
        )
        updated, reason = release_update(current, newer, REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.2")
        self.assertEqual(updated["version_date"], "2026-11-16")
        self.assertEqual(updated["links"][0]["url"], new_vpk)
        archives = [
            link for link in updated["links"]
            if str(link.get("url") or "").endswith("vita-v1.1.vpk")
        ]
        self.assertEqual(len(archives), 1)
        self.assertEqual(archives[0]["type"], "Mirror")
        self.assertFalse(archives[0]["recommended"])
        self.assertIsNone(release_update(updated, newer, REPO)[0])

    def test_no_change_when_old_archive_is_only_in_a_mirror(self):
        source = app()
        source["links"][1]["url"] = "https://archive.org/download/vita/old.vpk"
        updated, reason = release_update(source, release(), REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(len(updated["links"]), len(source["links"]))
        self.assertEqual(updated["links"][1], source["links"][1])

    def test_archive_cannot_be_used_to_prove_project_ownership(self):
        source = app()
        source["source_url"] = "https://github.com/other/repo"
        source["links"] = [
            {"type": "Download", "url": "https://archive.org/download/some/old.vpk", "recommended": True}
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps" / "vita-app.json").write_text(
                json.dumps(to_v2(source)), encoding="utf-8"
            )
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [release()]
            })
            self.assertEqual(changes, [])

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
