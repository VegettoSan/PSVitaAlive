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
            {"type": "Download", "name": "VPK", "url": OLD, "recommended": True},
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
    def test_extended_version_labels_compare_only_with_proven_order(self):
        from scripts.sync_release_updates import is_prerelease_version, effective_release_version
        for old, new in [
            ("Alpha 0.1.0", "0.1.0"), ("Alpha 0.1.0", "0.2.0"),
            ("1.0 rc3", "1.0"), ("v.0.2.3.1 alpha", "0.2.3.1-beta"),
            ("v1.0.0-stable", "1.0.1"), ("beta-1", "beta-2"),
            ("Beta Build 40", "Beta Build 41"), ("r4", "r5"),
            ("2026-08-06", "2026-08-07"),
        ]:
            self.assertTrue(newer_version(old, new), (old, new))
            self.assertFalse(newer_version(new, old), (old, new))
        for old, new in [
            ("Beta 2", "2.0"), ("r4", "4.1"), ("Nightly", "1.2"),
            ("Demo", "Final"), ("1.0-hotfix", "1.1"), ("beta-2", "alpha-3"),
            ("24-07-2025", "2026-01-01"), ("Beta Build 40", "1.0"),
        ]:
            self.assertFalse(newer_version(old, new), (old, new))
        self.assertTrue(is_prerelease_version("Alpha 0.1.0"))
        self.assertTrue(is_prerelease_version("Beta Build 40"))
        self.assertEqual(effective_release_version(release(tag="beta-2")), "beta-2")
        self.assertFalse(newer_version("1.0", "v1.0-stable"))

    def test_prefix_alpha_can_update_to_stable_same_core(self):
        source = app()
        source["version"] = "Alpha 0.1.0"
        item = release(tag="0.1.0", assets=[{
            "name": "vita.vpk", "download_url":
                "https://github.com/example/vita-app/releases/download/0.1.0/vita.vpk",
            "size": 400}])
        result, reason = release_update(source, item, REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(result["version"], "0.1.0")
        self.assertEqual(source["version"], "Alpha 0.1.0")

    def test_vagachat_same_release_tag_does_not_relabel_alpha(self):
        source = app()
        source["version"] = "Alpha 0.1.0"
        source["version_date"] = "2026-11-09"
        url = "https://github.com/example/vita-app/releases/download/0.1.0/vita.vpk"
        source["links"][0]["url"] = url
        item = release(tag="0.1.0", assets=[{"name": "vita.vpk", "download_url": url, "size": 400}])
        self.assertIn("same immutable release", release_update(source, item, REPO)[1])
        source["links"][0]["url"] = "https://archive.org/download/vita-app/Alpha.vpk"
        self.assertIn("same-day archived", release_update(source, item, REPO)[1])
        item["published"] = "2026-11-10T00:00:00Z"
        updated, reason = release_update(source, item, REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "0.1.0")

    def test_prereleases_need_opt_in_and_are_semantically_ordered(self):
        old = app()
        old["version"] = "1.1"
        beta = release(tag="v1.2-beta2", prerelease=True, assets=[{
            "name": "vita.vpk",
            "download_url": "https://github.com/example/vita-app/releases/download/v1.2-beta2/vita.vpk",
            "size": 401,
        }])
        self.assertIn("prerelease updates disabled", release_update(old, beta, REPO)[1])
        old["allow_prereleases"] = True
        updated, reason = release_update(old, beta, REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.2-beta2")
        self.assertEqual(updated["links"][0]["url"], beta["assets"][0]["download_url"])
        for lower, higher in [
            ("1.2-beta2", "v1.2-beta3"),
            ("1.2-beta3", "1.2-rc1"),
            ("1.2-rc1", "1.2"),
            ("1.2.0a", "1.2.0-beta1"),
        ]:
            self.assertTrue(newer_version(lower, higher), (lower, higher))
        for lower, higher in [
            ("1.2", "1.2-beta3"),
            ("1.2", "v1.2.0"),
            ("1.2", "continuous"),
            ("1.2", "1.2-multicore2"),
        ]:
            self.assertFalse(newer_version(lower, higher), (lower, higher))

    def test_stable_fallback_does_not_force_beta_on_stable_apps(self):
        latest = release(tag="v1.3-beta2", prerelease=True, assets=[{
            "name": "vita.vpk",
            "download_url": "https://github.com/example/vita-app/releases/download/v1.3-beta2/vita.vpk",
            "size": 502,
        }])
        latest["stable_release"] = release(tag="v1.2", prerelease=False,
                                             published="2026-11-07T00:00:00Z", assets=[{
            "name": "vita.vpk",
            "download_url": "https://github.com/example/vita-app/releases/download/v1.2/vita.vpk",
            "size": 501,
        }])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            file = root / "apps" / "vita-app.json"
            for allowed, expected in [(False, "1.2"), (True, "1.3-beta2")]:
                source = app()
                source["allow_prereleases"] = allowed
                file.write_text(json.dumps(to_v2(source)), encoding="utf-8")
                changes, skipped = plan_changes(root, {
                    "schema_version": 1, "releases": [latest]})
                self.assertEqual(skipped, [])
                self.assertEqual(len(changes), 1)
                self.assertEqual(changes[0][1]["version"], expected)

    def test_prereleases_automatically_eligible_after_more_than_seven_days(self):
        from scripts.sync_release_updates import automatic_prerelease_allowed
        source = app()
        stable = release(tag="v1.1", published="2026-11-01T00:00:00Z")
        pre = release(
            tag="v1.2-beta3", prerelease=True,
            published="2026-11-08T00:00:00Z",
            assets=[{"name": "vita.vpk", "size": 401,
                     "download_url": "https://github.com/example/vita-app/releases/download/v1.2-beta3/vita.vpk"}],
        )
        self.assertFalse(automatic_prerelease_allowed(source, pre, stable),
                         "Exactly seven days is not more than a week")
        pre["published"] = "2026-11-08T00:00:01Z"
        self.assertTrue(automatic_prerelease_allowed(source, pre, stable))
        for bad_time in ["2026-11-01T00:00:00Z", "garbage", ""]:
            pre["published"] = bad_time
            self.assertFalse(automatic_prerelease_allowed(source, pre, stable))
        pre["published"] = "2026-11-08T00:00:01Z"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps/vita-app.json").write_text(
                json.dumps(to_v2(source)), encoding="utf-8"
            )
            release_item = {**pre, "stable_release": stable}
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [release_item],
            })
            self.assertEqual(skipped, [])
            self.assertEqual(len(changes), 1)
            self.assertEqual(changes[0][1]["version"], "1.2-beta3")

    def test_prerelease_auto_if_no_stable_confirmed_but_not_when_unknown(self):
        from scripts.sync_release_updates import automatic_prerelease_allowed
        source = app()
        pre = release(
            tag="v1.3-beta2", prerelease=True,
            published="2026-11-09T00:00:00Z",
            assets=[{"name": "vita.vpk", "size": 900,
                     "download_url": "https://github.com/example/vita-app/releases/download/v1.3-beta2/vita.vpk"}],
        )
        self.assertFalse(automatic_prerelease_allowed(source, pre, None))
        pre["no_stable_release"] = True
        self.assertTrue(automatic_prerelease_allowed(source, pre, None))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps/vita-app.json").write_text(
                json.dumps(to_v2(source)), encoding="utf-8")
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [pre],
            })
            self.assertEqual(skipped, [])
            self.assertEqual(len(changes), 1)
            self.assertEqual(changes[0][1]["version"], "1.3-beta2")
            pre["no_stable_release"] = False
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [pre],
            })
            self.assertEqual(changes, [])
            self.assertTrue(any("not eligible" in text for text in skipped))

    def test_prerelease_age_still_cannot_downgrade_stable(self):
        from scripts.sync_release_updates import automatic_prerelease_allowed
        source = app()
        source["version"] = "1.3"
        stable = release(tag="v1.3", published="2026-11-01T00:00:00Z")
        pre = release(tag="v1.3-beta9", prerelease=True,
                      published="2026-11-17T00:00:00Z")
        self.assertFalse(automatic_prerelease_allowed(source, pre, stable))
        source["version"] = "1.0"
        self.assertFalse(automatic_prerelease_allowed(source, pre, stable))

    def test_renamed_github_repo_sonic_r_can_update_verified_assets(self):
        source = app()
        source["version"] = "2.0"
        source["version_date"] = "2026-10-12"
        source["title_id"] = "SONICR001"
        source["source_url"] = "https://github.com/fr3dsp/sonic-r-vita"
        source["links"][0]["url"] = (
            "https://archive.org/download/sonic-r-ps-vita/sonicr_vita%20v2.0.vpk"
        )
        source["links"][2]["url"] = source["source_url"]
        new_url = (
            "https://github.com/PupueStudios/sonic-r-vita/releases/download/v2.1/sonicr_vita.vpk"
        )
        item = {
            "platform": "github", "repo": "fr3dsp/sonic-r-vita",
            "canonical_repo": "PupueStudios/sonic-r-vita",
            "repo_id": 1378201322, "tag": "v2.1",
            "published": "2026-11-17T17:15:58Z", "prerelease": False,
            "assets": [{"name": "sonicr_vita.vpk", "size": 4363896,
                        "download_url": new_url}],
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps/sonic-r.json").write_text(
                json.dumps(to_v2(source)), encoding="utf-8"
            )
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [item],
            })
            self.assertEqual(skipped, [])
            self.assertEqual(len(changes), 1)
            updated = changes[0][1]
            self.assertEqual(updated["version"], "2.1")
            self.assertEqual(updated["version_date"], "2026-11-17")
            self.assertEqual(updated["links"][0]["url"], new_url)
            self.assertEqual(updated["links"][1], source["links"][1])
        for change in [
            {"canonical_repo": "Attacker/other"},
            {"repo_id": 0},
            {"repo_id": "1378201322"},
            {"assets": [{**item["assets"][0],
                         "download_url": "https://evil.example/sonicr_vita.vpk"}]},
        ]:
            altered, reason = release_update(source, {**item, **change}, "fr3dsp/sonic-r-vita")
            self.assertIsNone(altered, (change, reason))
        unverified = dict(item)
        del unverified["canonical_repo"]
        del unverified["repo_id"]
        self.assertIsNone(release_update(source, unverified, "fr3dsp/sonic-r-vita")[0])

    def test_golden_balloon_rolling_release_is_detected_but_draft_is_not_promoted(self):
        from scripts.sync_release_updates import effective_release_version
        source = app()
        source["id"] = "golden-balloon"
        source["title_id"] = "GBLN00001"
        source["version"] = "1.7.2"
        source["version_date"] = "2026-09-17"
        source["source_url"] = "https://github.com/zm2283145/goldenballoon-vita-port"
        source["links"][0]["url"] = (
            "https://archive.org/download/golden-balloon-psvita/GoldenBalloon-Vita-v1.7.2.vpk"
        )
        source["links"][1] = {
            "type": "Game Files", "name": "Game Files",
            "url": "https://archive.org/download/golden-balloon-psvita/Game%20Files%20goldenballoon.zip",
            "extract_path": "ux0:data/",
        }
        source["links"][2]["url"] = source["source_url"]
        vpk = "GoldenBalloon-Vita-v1.7.3-44079be4-44209555230-1.vpk"
        updated_url = (
            "https://github.com/zm2283145/goldenballoon-vita-port/releases/download/vita-next/" + vpk
        )
        rolling = {
            "platform": "github",
            "repo": "zm2283145/goldenballoon-vita-port",
            "tag": "vita-next",
            "version": "1.7.3",
            "release_channel": "rolling",
            "review_required": True,
            "prerelease": True,
            "published": "2026-11-18T03:49:45Z",
            "assets": [{"name": vpk, "size": 4799043, "download_url": updated_url}],
            "stable_release": {
                "platform": "github", "repo": "zm2283145/goldenballoon-vita-port",
                "tag": "v1.7.2", "prerelease": False,
                "published": "2026-09-17T23:04:32Z",
                "assets": [{
                    "name": "GoldenBalloon-Vita-v1.7.2.vpk", "size": 4746335,
                    "download_url": "https://github.com/zm2283145/goldenballoon-vita-port/releases/download/v1.7.2/GoldenBalloon-Vita-v1.7.2.vpk",
                }],
            },
        }
        self.assertEqual(effective_release_version(rolling), "1.7.3")
        self.assertTrue(newer_version("1.7.2", effective_release_version(rolling)))
        self.assertIsNone(release_update(
            {**source, "allow_prereleases": True}, rolling,
            "zm2283145/goldenballoon-vita-port", auto_prerelease=True,
        )[0], "Even early-access opt-in must not promote an unvalidated draft")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            target = root / "apps/golden-balloon.json"
            target.write_text(json.dumps(to_v2(source)), encoding="utf-8")
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [rolling],
            })
            self.assertEqual(changes, [])
            self.assertTrue(any("draft/untested" in msg for msg in skipped))
            published_build = {**rolling, "review_required": False}
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [published_build],
            })
            self.assertEqual(skipped, [])
            self.assertEqual(len(changes), 1)
            updated = changes[0][1]
            self.assertEqual(updated["version"], "1.7.3")
            self.assertEqual(updated["version_date"], "2026-11-18")
            self.assertEqual(updated["size"], 4799043)
            self.assertEqual(updated["links"][0]["url"], updated_url)
            self.assertEqual(updated["links"][1], source["links"][1])
            mismatched_vpk = {**published_build, "assets": [
                {**rolling["assets"][0],
                 "name": "GoldenBalloon-Vita-v1.7.2-44079be4.vpk"}
            ]}
            self.assertIsNone(effective_release_version(mismatched_vpk))
            self.assertIsNone(release_update(
                source, mismatched_vpk, "zm2283145/goldenballoon-vita-port",
                auto_prerelease=True,
            )[0])

    def test_stable_replaces_same_core_beta_but_never_downgrades(self):
        source = app()
        source["version"] = "1.2-beta2"
        source["allow_prereleases"] = True
        stable = release(tag="v1.2", prerelease=False, assets=[{
            "name": "vita.vpk",
            "download_url": "https://github.com/example/vita-app/releases/download/v1.2/vita.vpk",
            "size": 501,
        }])
        updated, reason = release_update(source, stable, REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.2")
        updated["links"][0]["url"] = "https://archive.org/download/vita-app/v1.2.vpk"
        prerelease = release(tag="v1.2-beta5", prerelease=True)
        ignored, reason = release_update(updated, prerelease, REPO)
        self.assertIsNone(ignored)
        self.assertIn("version not newer", reason)

    def test_github_folder_can_update_two_apps_from_same_repository(self):
        from scripts.sync_release_updates import registered_folder_paths
        repository = "papilenote/vita-ports"
        commit = "a" * 40
        blob = "b" * 40
        def folder_app(app_id, tid, folder):
            record = app()
            record["id"] = app_id
            record["title_id"] = tid
            record["version"] = "1.0"
            record["version_date"] = "2026-10-01"
            record["links"][0]["url"] = "https://archive.org/download/" + app_id + "/old.vpk"
            record["links"][1] = {"type": "Game Files", "name": "Game Files",
                                   "url": "https://archive.org/download/" + app_id + "/data.zip"}
            record["links"][2] = {"type": "Repository", "name": "GitHub",
                "url": "https://github.com/" + repository + "/tree/main/" + folder.replace(" ", "%20")}
            return record
        def folder_release(folder, tid, version, filename):
            url = ("https://raw.githubusercontent.com/" + repository + "/" + commit
                   + "/" + folder.replace(" ", "%20") + "/" + filename)
            return {
                "platform": "github_folder", "repo": repository, "branch": "main",
                "folder": folder, "title_id": tid, "tag": version,
                "published": "2026-11-16T11:00:00Z",
                "commit_sha": commit, "blob_sha": blob,
                "assets": [{"name": filename, "download_url": url, "size": 1234567}],
            }
        first = folder_app("tap", "TPSN00001", "Tap Sonic")
        second = folder_app("marvel", "MVSC00001", "Marvel VS Capcom 2")
        self.assertIn((repository, "main", "tap sonic"), registered_folder_paths(first))
        snapshot = {
            "schema_version": 1, "releases": [],
            "folder_releases": [
                folder_release("Tap Sonic", "TPSN00001", "1.1", "tapsonic.vpk"),
                folder_release("Marvel VS Capcom 2", "MVSC00001", "1.2", "mvc2_v1.2.vpk"),
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            for record in (first, second):
                (root / "apps" / (record["id"] + ".json")).write_text(
                    json.dumps(to_v2(record)), encoding="utf-8")
            changes, skipped = plan_changes(root, snapshot)
            self.assertEqual(skipped, [])
            self.assertEqual(len(changes), 2)
            output = {proposal["id"]: proposal for _path, proposal, _compact in changes}
            self.assertEqual(output["tap"]["version"], "1.1")
            self.assertEqual(output["marvel"]["version"], "1.2")
            for record in (first, second):
                updated = output[record["id"]]
                self.assertTrue(updated["links"][0]["url"].startswith(
                    "https://raw.githubusercontent.com/" + repository + "/" + commit + "/"))
                self.assertEqual(updated["links"][1], record["links"][1])
                self.assertEqual(updated["links"][2], record["links"][2])
                self.assertEqual(updated["version_date"], "2026-11-16")
                self.assertEqual(updated["size"], 1234567)

    def test_github_folder_rejects_wrong_title_sha_branch_and_unpinned_url(self):
        from scripts.sync_release_updates import folder_release_update
        candidate = app()
        candidate["title_id"] = "TPSN00001"
        candidate["links"][0]["url"] = "https://archive.org/download/tap/old.vpk"
        candidate["links"][2]["url"] = "https://github.com/papilenote/vita-ports/tree/main/Tap%20Sonic"
        repo = "papilenote/vita-ports"
        sha = "a" * 40
        asset = {
            "name": "tapsonic.vpk",
            "size": 1113939,
            "download_url": f"https://raw.githubusercontent.com/{repo}/{sha}/Tap%20Sonic/tapsonic.vpk",
        }
        record = {
            "platform": "github_folder", "repo": repo, "branch": "main",
            "folder": "Tap Sonic", "title_id": "TPSN00001", "tag": "1.1",
            "published": "2026-11-16T16:00:00Z",
            "commit_sha": sha, "blob_sha": "b" * 40, "assets": [asset],
        }
        ok, reason = folder_release_update(candidate, record, repo)
        self.assertEqual(reason, "ready")
        self.assertEqual(ok["links"][0]["url"], asset["download_url"])
        for change in [
            {"title_id": "OTH000001"},
            {"folder": "Marvel VS Capcom 2"},
            {"branch": "dev"},
            {"commit_sha": "main"},
            {"blob_sha": "notsha"},
            {"assets": [{**asset, "download_url": asset["download_url"].replace(sha, "main")}]},
            {"assets": [asset, asset]},
        ]:
            failed, reason = folder_release_update(candidate, {**record, **change}, repo)
            self.assertIsNone(failed, (change, reason))
        candidate["version"] = "1.1"
        failed, reason = folder_release_update(candidate, record, repo)
        self.assertIsNone(failed)
        self.assertIn("version not newer", reason)

    def test_github_folder_does_not_override_equal_version_archive_backup(self):
        from scripts.sync_release_updates import folder_release_update
        record = app()
        record["title_id"] = "TPSN00001"
        record["version"] = "1.1"
        record["links"][0]["url"] = "https://archive.org/download/tap/1.1.vpk"
        record["links"][2]["url"] = "https://github.com/papilenote/vita-ports/tree/main/Tap%20Sonic"
        remote = {
            "platform": "github_folder", "repo": "papilenote/vita-ports",
            "branch": "main", "folder": "Tap Sonic", "title_id": "TPSN00001",
            "tag": "1.1", "published": "2026-11-16T16:00:00Z",
            "commit_sha": "a" * 40, "blob_sha": "b" * 40,
            "assets": [{
                "name": "tapsonic.vpk", "size": 1113939,
                "download_url": "https://raw.githubusercontent.com/papilenote/vita-ports/" +
                                ("a" * 40) + "/Tap%20Sonic/tapsonic.vpk"
            }],
        }
        updated, reason = folder_release_update(record, remote, "papilenote/vita-ports")
        self.assertIsNone(updated)
        self.assertEqual(record["links"][0]["url"], "https://archive.org/download/tap/1.1.vpk")

    def test_duplicate_folder_records_are_rejected_before_applying(self):
        source = app()
        source["title_id"] = "TPSN00001"
        source["links"][0]["url"] = "https://archive.org/download/tap/old.vpk"
        source["links"][2]["url"] = "https://github.com/papilenote/vita-ports/tree/main/Tap%20Sonic"
        sha = "a" * 40
        item = {
            "platform": "github_folder", "repo": "papilenote/vita-ports",
            "branch": "main", "folder": "Tap Sonic", "title_id": "TPSN00001",
            "tag": "1.1", "published": "2026-11-16T16:00:00Z",
            "commit_sha": sha, "blob_sha": "b" * 40,
            "assets": [{
                "name": "tapsonic.vpk", "size": 1113939,
                "download_url": "https://raw.githubusercontent.com/papilenote/vita-ports/" +
                                sha + "/Tap%20Sonic/tapsonic.vpk",
            }],
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "apps/tap.json").write_text(json.dumps(to_v2(source)), encoding="utf-8")
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [],
                "folder_releases": [item, {**item, "tag": "1.2"}],
            })
            self.assertEqual(changes, [])
            self.assertTrue(any("duplicate folder records" in message for message in skipped))

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
        self.assertIn("trusted GitHub/GitLab release link", reason)

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

    def test_extra_game_files_and_plugins_do_not_block_only_vpk(self):
        source = app()
        source["links"][0]["url"] = "https://archive.org/download/vita-app/old.vpk"
        source["links"][0]["size"] = 100
        source["links"][0]["recommended"] = False
        source["links"][1]["recommended"] = True
        source["links"].extend([
            {
                "type": "Game Files", "name": "Game Data",
                "url": "https://archive.org/download/vita-app/game-data.zip",
                "size": 999999, "install_path": "ux0:data/example",
                "required": True,
            },
            {
                "type": "Plugin", "name": "CapUnlocker",
                "url": "https://archive.org/download/vita-app/capunlocker.skprx",
                "recommended": True,
            },
            {
                "type": "Data Files", "name": "Expansion",
                "url": "https://example.org/files/expansion.zip",
            },
            {
                "type": "Download", "name": "VPK Lite",
                "url": "https://archive.org/download/vita-app/lite.vpk",
            },
            {
                "type": "Download", "name": "VPK Debug",
                "url": "https://archive.org/download/vita-app/debug.vpk",
            },
            {
                "type": "Mirror", "name": "Other source",
                "url": "https://archive.org/download/vita-app/previous.vpk",
            },
        ])
        source["requirements"] = "Own game files required"
        source["data_size"] = 999999
        before = json.loads(json.dumps(source))
        updated, reason = release_update(source, release(), REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.1")
        self.assertEqual(updated["version_date"], "2026-11-09")
        self.assertEqual(updated["size"], 400)
        self.assertEqual(len(updated["links"]), len(before["links"]))
        self.assertEqual(updated["links"][0], {
            **before["links"][0], "url": NEW, "size": 400,
        })
        self.assertEqual(updated["links"][1:], before["links"][1:])
        for key, value in before.items():
            if key not in {"version", "version_date", "size", "links"}:
                self.assertEqual(updated[key], value, key)
        self.assertEqual(source, before)

        # Once this version is catalogued, a manual Archive.org replacement
        # must never be overwritten by another scan for the same version.
        updated["links"][0]["url"] = "https://archive.org/download/vita-app/new-v1.1.vpk"
        stable = json.loads(json.dumps(updated))
        again, reason = release_update(updated, release(), REPO)
        self.assertIsNone(again)
        self.assertEqual(updated, stable)

    def test_compact_v2_plan_updates_only_vpk_with_auxiliary_links(self):
        source = app()
        source["links"][0]["url"] = "https://archive.org/download/vita-app/old.vpk"
        source["links"].extend([
            {"type": "Game Files", "name": "Optional data",
             "url": "https://archive.org/download/vita-app/data.zip"},
            {"type": "Plugin", "name": "Optional plugin",
             "url": "https://archive.org/download/vita-app/plugin.skprx"},
        ])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            path = root / "apps" / "vita-app.json"
            original = to_v2(source)
            path.write_text(json.dumps(original, separators=(",", ":")) + "\n", encoding="utf-8")
            before = path.read_bytes()
            with patch("urllib.request.urlopen", side_effect=AssertionError("no network/VPK downloads")):
                changes, skipped = plan_changes(root, {
                    "schema_version": 1, "releases": [release()],
                })
            self.assertEqual(len(changes), 1)
            self.assertEqual(skipped, [])
            _path, proposal, compact = changes[0]
            self.assertTrue(compact)
            self.assertEqual(proposal["links"][0]["url"], NEW)
            self.assertEqual(proposal["links"][1:], source["links"][1:])
            self.assertEqual(path.read_bytes(), before)

    def test_extra_links_do_not_override_vpk_asset_count_and_origin_checks(self):
        source = app()
        source["links"].append({
            "type": "Game Files", "name": "Game Files",
            "url": "https://archive.org/download/vita-app/data.zip",
        })
        source["links"].append({
            "type": "Plugin", "name": "Plugin",
            "url": "https://archive.org/download/vita-app/plugin.skprx",
        })
        for candidate in [
            release(assets=[]),
            release(assets=[
                {"name": "one.vpk", "download_url": NEW},
                {"name": "two.vpk", "download_url": NEW},
            ]),
            release(prerelease=True),
            release(tag="nightly"),
        ]:
            with self.subTest(candidate=candidate):
                self.assertIsNone(release_update(source, candidate, REPO)[0])
        source["links"].append({
            "type": "Download", "name": "vPk",
            "url": "https://archive.org/download/vita-app/alternative.vpk",
        })
        self.assertIsNone(release_update(source, release(), REPO)[0])

    def test_rejects_unrelated_downloads(self):
        self.assertFalse(download_belongs_to_repo(
            "https://github.com/example/other/releases/download/v1.1/vita.vpk", REPO))
        item = app()
        item["links"][0]["url"] = "https://example.com/unknown-download.vpk"
        self.assertIsNone(release_update(item, release(), REPO)[0])

    def test_archive_update_replaces_vpk_url_without_creating_old_version_button(self):
        source = app()
        source["version"] = "v.1.0"
        old_archive = "https://archive.org/download/psvitaalive-vita-app/vita.vpk"
        source["links"][0]["url"] = old_archive
        original = json.loads(json.dumps(source))
        updated, reason = release_update(source, release(), REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.1")
        self.assertEqual(updated["links"][0]["url"], NEW)
        self.assertEqual(updated["links"][0]["name"], "VPK")
        self.assertEqual(len(updated["links"]), len(original["links"]))
        self.assertEqual(updated["links"][1:], original["links"][1:])
        self.assertFalse(any(link.get("url") == old_archive for link in updated["links"]))
        self.assertEqual(updated["title_id"], original["title_id"])
        self.assertEqual(updated["author_ids"], original["author_ids"])
        self.assertEqual(source, original)
        self.assertIsNone(release_update(updated, release(), REPO)[0])

    def test_alternative_vpk_buttons_remain_untouched(self):
        source = app()
        source["links"].insert(0, {
            "type": "Download", "name": "VPK Lite",
            "url": "https://example.com/lite-v1.0.vpk", "recommended": True,
        })
        source["links"].append({
            "type": "Download", "name": "VPK configuracion debug",
            "url": "https://example.com/debug-v1.0.vpk",
        })
        original = json.loads(json.dumps(source))
        updated, reason = release_update(source, release(), REPO)
        self.assertEqual(reason, "ready")
        self.assertEqual(len(updated["links"]), len(original["links"]))
        self.assertEqual(updated["links"][1]["url"], NEW)
        self.assertEqual(updated["links"][1]["name"], "VPK")
        self.assertEqual(updated["links"][0], original["links"][0])
        self.assertEqual(updated["links"][2:], original["links"][2:])

    def test_ambiguous_or_missing_vpk_button_requires_manual_review(self):
        source = app()
        source["links"][0]["name"] = "VPK version Lite"
        self.assertIsNone(release_update(source, release(), REPO)[0])
        source = app()
        source["links"].append({
            "type": "Download", "name": "vPk",
            "url": "https://archive.org/download/other/second.vpk",
        })
        self.assertIsNone(release_update(source, release(), REPO)[0])

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

    def test_next_new_version_replaces_same_vpk_button_again(self):
        current = app()
        current["version"] = "1.1"
        current["version_date"] = "2026-11-09"
        current["links"][0]["url"] = (
            "https://archive.org/download/psvitaalive-vita-app/vita-v1.1.vpk"
        )
        old_links = json.loads(json.dumps(current["links"]))
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
        self.assertEqual(updated["links"][0]["name"], "VPK")
        self.assertEqual(len(updated["links"]), len(old_links))
        self.assertEqual(updated["links"][1:], old_links[1:])
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

    def test_gitlab_subgroup_release_updates_existing_vpk_button(self):
        project = "vita/homebrew/awesome-app"
        url = ("https://gitlab.com/vita/homebrew/awesome-app/-/releases/"
               "v1.1/downloads/builds/awesome.vpk")
        source = app()
        source["source_url"] = "https://gitlab.com/vita/homebrew/awesome-app"
        source["links"][2]["url"] = source["source_url"]
        source["links"][0]["url"] = "https://archive.org/download/awesome/awesome.vpk"
        candidate = release(platform="gitlab", repo=project, assets=[
            {"name": "awesome.vpk", "download_url": url, "size": 0},
            {"name": "source.zip", "download_url": (
                "https://gitlab.com/vita/homebrew/awesome-app/-/archive/v1.1/src.zip"
            )},
        ])
        updated, reason = release_update(source, candidate, project)
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["version"], "1.1")
        self.assertEqual(updated["version_date"], "2026-11-09")
        self.assertEqual(updated["links"][0]["url"], url)
        self.assertEqual(len(updated["links"]), len(source["links"]))
        self.assertEqual(updated["links"][1:], source["links"][1:])
        self.assertNotIn("size", updated)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps" / "vita-app.json").write_text(
                json.dumps(to_v2(source)), encoding="utf-8"
            )
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [candidate],
            })
            self.assertEqual(len(changes), 1)
            self.assertEqual(skipped, [])
            self.assertEqual(changes[0][1]["links"][0]["url"], url)

    def test_gitlab_uses_original_vpk_extension_when_permalink_is_generic(self):
        project = "team/vita"
        asset = {
            "name": "Installer",
            "download_url": "https://gitlab.com/team/vita/-/releases/v1.1/downloads/release-installer",
            "original_url": "https://gitlab.com/team/vita/-/uploads/abcd/vita.vpk",
        }
        source = app()
        source["source_url"] = "https://gitlab.com/team/vita"
        source["links"][2]["url"] = source["source_url"]
        source["links"][0]["url"] = "https://archive.org/download/vita/vita.vpk"
        updated, reason = release_update(
            source, release(platform="gitlab", repo=project, assets=[asset]), project
        )
        self.assertEqual(reason, "ready")
        self.assertEqual(updated["links"][0]["url"], asset["download_url"])

    def test_gitlab_zero_multiple_or_external_vpk_requires_review(self):
        project = "team/vita"
        source = app()
        source["source_url"] = "https://gitlab.com/team/vita"
        source["links"][2]["url"] = source["source_url"]
        source["links"][0]["url"] = "https://archive.org/download/vita/vita.vpk"
        good = {"name": "vita.vpk", "download_url": (
            "https://gitlab.com/team/vita/-/releases/v1.1/downloads/vita.vpk"
        )}
        candidates = [
            ([], "no .vpk"),
            ([good, dict(good)], "2 .vpk"),
            ([{"name": "vita.vpk", "download_url": "https://evil.com/vita.vpk"}], "trusted"),
            ([{"name": "vita.vpk", "download_url": (
                "https://gitlab.com/other/vita/-/releases/v1.1/downloads/vita.vpk"
            )}], "trusted"),
        ]
        for assets, expected in candidates:
            with self.subTest(assets=assets):
                updated, reason = release_update(
                    source, release(platform="gitlab", repo=project, assets=assets), project
                )
                self.assertIsNone(updated)
                self.assertIn(expected, reason)

    def test_conflicting_github_and_gitlab_sources_do_not_write_same_json_twice(self):
        source = app()
        source["links"].append({
            "type": "Repository", "name": "GitLab mirror",
            "url": "https://gitlab.com/team/vita",
        })
        candidate = release(
            platform="gitlab", repo="team/vita", assets=[{
                "name": "vita.vpk",
                "download_url": "https://gitlab.com/team/vita/-/releases/v1.1/downloads/vita.vpk",
            }]
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "apps").mkdir()
            (root / "apps" / "vita-app.json").write_text(
                json.dumps(to_v2(source)), encoding="utf-8"
            )
            changes, skipped = plan_changes(root, {
                "schema_version": 1, "releases": [release(), candidate],
            })
            self.assertEqual(changes, [])
            self.assertTrue(any("multiple upstream" in msg for msg in skipped))

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
