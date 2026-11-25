"""Offline regression tests for the owned-item Archive.org batch backup engine."""
import io
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from scripts.archive_backup_batch import (
    Deferred, expected_filename, source_matches_approved, candidate_proposal,
    save_canonical, existing_file, safe_host, stream_download, run_one, load_state, MIB,
    write_state, public_url, verify_public_download, load_policy, policy_entry, should_stop_batch,
)


def release_app():
    return {
        "id": "fmvita", "title_id": "FMVITA001", "name": "FMVita",
        "version": "0.6.2", "version_date": "2026-11-01", "size": 2071824,
        "status": "Legacy", "source_url": "https://github.com/WolffsRoom/FMVita",
        "links": [
            {"type": "Download", "name": "VPK",
             "url": "https://github.com/WolffsRoom/FMVita/releases/download/v0.6.2/FileManager_v0.6.2.vpk",
             "recommended": True},
            {"type": "Game Files", "name": "Game Files",
             "url": "https://archive.org/download/foreign-third-party/assets.zip"},
            {"type": "Repository", "name": "Repository",
             "url": "https://github.com/WolffsRoom/FMVita"},
        ],
    }


def candidate(kind="SAME_VERSION_BACKFILL"):
    return {
        "app_id": "fmvita", "title_id": "FMVITA001", "version": "0.6.2",
        "kind": kind, "size_bytes": 2071824,
        "archive_identifier": "psvitaalive-fmvita",
        "source_url": release_app()["links"][0]["url"],
    }


def policy():
    return {"fmvita": {"enabled": True, "github_repo": "WolffsRoom/FMVita",
                       "spdx_id": "GPL-3.0"}}


class DownloadFake:
    def __init__(self, status=200, content=b"PK\x03\x04data", location=None):
        self.status_code = status
        self.headers = {"Content-Length": str(len(content))}
        if location:
            self.headers["Location"] = location
        self.content = content
        self.closed = False

    def iter_content(self, chunk_size):
        yield self.content

    def close(self):
        self.closed = True


class BatchTests(unittest.TestCase):
    def test_public_url_and_versioned_name_are_deterministic(self):
        self.assertEqual(expected_filename(candidate()), "fmvita v0.6.2.vpk")
        self.assertEqual(expected_filename(dict(candidate(), version="v.0.8")), "fmvita v.0.8.vpk")
        self.assertEqual(public_url("psvitaalive-fmvita", "fmvita v0.6.2.vpk"),
                         "https://archive.org/download/psvitaalive-fmvita/fmvita%20v0.6.2.vpk")
        bad = dict(candidate(), version="../../foo")
        with self.assertRaises(Deferred):
            expected_filename(bad)

    def test_only_exact_enabled_approved_repo_is_eligible(self):
        self.assertTrue(source_matches_approved(candidate(), policy(), release_app()))
        disallowed = policy()
        disallowed["fmvita"]["enabled"] = False
        self.assertFalse(source_matches_approved(candidate(), disallowed, release_app()))
        foreign = dict(candidate(), source_url="https://github.com/foreign/malware/releases/download/x/a.vpk")
        self.assertFalse(source_matches_approved(foreign, policy(), release_app()))
        self.assertFalse(source_matches_approved(candidate(), policy(),
                        {**release_app(), "source_url": "https://github.com/other/app"}))

    def test_media_owned_item_without_psvitaalive_prefix_is_allowed_only_for_canonical_source(self):
        app = release_app()
        item = dict(candidate(), kind="CATALOG_BACKFILL",
                    archive_identifier="dragon-ball-z-budokai-tenkaichi-3-ps-vita")
        self.assertTrue(source_matches_approved(item, {"*": {"enabled": True}}, app))
        self.assertFalse(source_matches_approved(
            dict(item, archive_identifier="https://archive.org/evil"),
            {"*": {"enabled": True}}, app))
        self.assertFalse(source_matches_approved(
            dict(item, archive_identifier="../escape"),
            {"*": {"enabled": True}}, app))
        self.assertFalse(source_matches_approved(
            dict(item, source_url="https://github.com/foreign/app/releases/download/v1.0/a.vpk"),
            {"*": {"enabled": True}}, app))
        self.assertFalse(source_matches_approved(
            item, {"*": {"enabled": True}, "fmvita": {"enabled": False}}, app))

    def test_target_collision_any_case_never_overwrites(self):
        files = [{"name": "FMVITA V0.6.2.VPK", "size": "0"}]
        with self.assertRaises(Deferred):
            existing_file({"files": files}, "fmvita v0.6.2.vpk")

    def test_archive_and_github_redirects_reject_external_hosts(self):
        self.assertTrue(safe_host("https://ia123.us.archive.org/file", archive=True))
        self.assertFalse(safe_host("https://archive.org.evil.test/file", archive=True))
        self.assertFalse(safe_host("http://ia123.us.archive.org/file", archive=True))
        self.assertTrue(safe_host("https://release-assets.githubusercontent.com/file"))
        self.assertFalse(safe_host("https://127.0.0.1/file"))
        result = []

        class Session:
            def get(self, url, **kwargs):
                result.append(url)
                return DownloadFake(status=302, location="https://evil.test/exfil")

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(Deferred):
                stream_download(candidate()["source_url"], Path(tmp) / "file",
                                5 * 1024 * 1024, session=Session())
        self.assertEqual(len(result), 1, "Never contact an untrusted redirected host")

    def test_size_and_digest_of_public_download_are_checked(self):
        data = b"PK\x03\x04more-archive-content"
        md5 = hashlib.md5(data).hexdigest()
        sha = hashlib.sha256(data).hexdigest()

        class Session:
            def get(self, url, **kwargs):
                return DownloadFake(content=data)

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "asset.vpk"
            self.assertEqual(stream_download("https://archive.org/download/identifier/file.vpk",
                                             target, len(data), archive=True, session=Session()),
                             (len(data), md5, sha))
            with self.assertRaises(Deferred):
                stream_download("https://archive.org/download/identifier/file.vpk",
                                target, len(data) - 1, archive=True, session=Session())

    def test_same_version_catalog_mutation_only_changes_main_vpk(self):
        app = release_app()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            path = root / "apps/fmvita.json"
            path.write_text(json.dumps(app), encoding="utf-8")
            target, proposal, compact = candidate_proposal(root, candidate(), {})
            self.assertEqual(target, path)
            self.assertFalse(compact)
            self.assertEqual(proposal["links"][1], app["links"][1])
            self.assertEqual(proposal["version"], app["version"])
            self.assertTrue(proposal["links"][0]["url"].startswith("https://archive.org/download/psvitaalive-fmvita/"))
            save_canonical(path, proposal, compact)
            after = json.loads(path.read_text("utf-8"))
            self.assertEqual(after["links"][1], app["links"][1])

    def test_catalog_backfill_never_changes_approved_version_or_other_links(self):
        app = release_app()
        app["status"] = "Archive"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            path = root / "apps/fmvita.json"
            path.write_text(json.dumps(app), encoding="utf-8")
            input_candidate = dict(candidate(), kind="CATALOG_BACKFILL")
            output_path, updated, compact = candidate_proposal(root, input_candidate, {})
            self.assertEqual(output_path, path)
            self.assertFalse(compact)
            self.assertEqual(updated["status"], "Archive")
            self.assertEqual(updated["version"], app["version"])
            self.assertEqual(updated["version_date"], app["version_date"])
            self.assertEqual(updated["links"][1:], app["links"][1:])
            self.assertEqual(
                updated["links"][0]["url"],
                public_url("psvitaalive-fmvita", "fmvita v0.6.2.vpk"),
            )

    def test_new_release_cannot_use_unrelated_proposal(self):
        app = release_app()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            path = root / "apps/fmvita.json"
            path.write_text(json.dumps(app), encoding="utf-8")
            new = dict(candidate("NEW_RELEASE"), version="0.6.3")
            with self.assertRaises(Deferred):
                candidate_proposal(root, new, {})
            proposal = json.loads(json.dumps(app))
            proposal["version"], proposal["version_date"], proposal["size"] = "0.6.3", "2026-11-22", 2071824
            proposal["links"][0]["url"] = new["source_url"]
            _, updated, _ = candidate_proposal(root, new, {path: proposal})
            self.assertEqual(updated["version"], "0.6.3")
            self.assertEqual(updated["version_date"], "2026-11-22")
            self.assertEqual(updated["links"][1], app["links"][1])

    def test_processing_404_does_not_abort_other_jobs(self):
        self.assertFalse(should_stop_batch("VERIFYING"))
        self.assertFalse(should_stop_batch("PUBLISHED_VERIFIED"))
        self.assertTrue(should_stop_batch("WAITING_ARCHIVE"))
        self.assertTrue(should_stop_batch("UPLOAD_RESULT_UNCERTAIN"))
        self.assertTrue(should_stop_batch("CATALOG_PUSH_BLOCKED"))

    def test_state_persists_for_recovery(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            state = load_state(root)
            state["inflight"]["fmvita"] = {"version": "0.6.2", "status": "TRANSFER_PREPARED"}
            write_state(root, state)
            self.assertEqual(load_state(root)["inflight"]["fmvita"]["status"], "TRANSFER_PREPARED")

    def test_media_inferred_item_requires_private_owner_check_before_download(self):
        app = release_app()
        app["icon"] = "https://archive.org/download/battle-for-bikini-bottom-vita/icon0.png"
        app["screenshots"] = [
            "https://archive.org/download/battle-for-bikini-bottom-vita/shot0.png",
            "https://archive.org/download/battle-for-bikini-bottom-vita/shot1.png",
        ]
        app["links"].append({
            "type": "Game Files", "name": "Game Files other item",
            "url": "https://archive.org/download/foreign-assets/data.zip",
        })
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "apps/fmvita.json").write_text(json.dumps(app), encoding="utf-8")
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n", encoding="utf-8")
            with patch("scripts.archive_backup_batch.load_policy",
                       return_value={"*": {"enabled": True}}), \
                 patch("scripts.archive_backup_batch.owned_metadata",
                       side_effect=Deferred("PENDING_MANUAL", "wrong uploader")) as owner, \
                 patch("scripts.archive_backup_batch.download_source") as download, \
                 patch("scripts.archive_backup_batch.attempt_upload") as upload:
                with self.assertRaises(Deferred) as caught:
                    run_one(root, dict(candidate(), kind="CATALOG_BACKFILL",
                                       archive_identifier="battle-for-bikini-bottom-vita"), {},
                            {"schema_version": 1, "inflight": {}},
                            ("ACCESS", "SECRET", "owner@example.test"), max_file=10*MIB)
            self.assertEqual(caught.exception.status, "PENDING_MANUAL")
            owner.assert_called_once_with("battle-for-bikini-bottom-vita", "owner@example.test")
            download.assert_not_called()
            upload.assert_not_called()

    def test_unconfirmed_checkpoint_is_read_only_and_does_not_block_others(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "apps/fmvita.json").write_text(json.dumps(release_app()), encoding="utf-8")
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "fmvita,psvitaalive-fmvita,ok,old.vpk,,\n", encoding="utf-8")
            state = {"schema_version": 1, "inflight": {
                "fmvita": {"version": "0.6.2", "filename": "fmvita v0.6.2.vpk",
                          "source_url": candidate()["source_url"],
                          "archive_identifier": candidate()["archive_identifier"]}}}
            mock_md = {"metadata": {"uploader": "owner@example.test"}, "files": []}
            with patch("scripts.archive_backup_batch.load_policy", return_value=policy()), \
                 patch("scripts.archive_backup_batch.owned_metadata", return_value=mock_md), \
                 patch("scripts.archive_backup_batch.download_source",
                       return_value=("a" * 32, "b" * 64)) as download, \
                 patch("scripts.archive_backup_batch.attempt_upload") as upload:
                with self.assertRaises(Deferred) as caught:
                    run_one(root, candidate(), {}, state, ("ACCESS", "SECRET", "owner@example.test"),
                            max_file=10 * MIB)
            # Do not download the GitHub VPK again while the Archive.org
            # manifest has not yet shown the old checkpointed filename.
            self.assertEqual(caught.exception.status, "VERIFYING")
            self.assertIn("read-only", caught.exception.reason)
            download.assert_not_called()
            upload.assert_not_called()
            self.assertFalse(should_stop_batch("VERIFYING"))

    def test_existing_checkpoint_verifies_remote_and_publishes_without_second_put(self):
        app = release_app()
        name = "fmvita v0.6.2.vpk"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            path = root / "apps/fmvita.json"
            path.write_text(json.dumps(app), encoding="utf-8")
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "fmvita,psvitaalive-fmvita,ok,old.vpk,,\n", encoding="utf-8")
            state = {"schema_version": 1, "inflight": {"fmvita": {
                "version": "0.6.2", "filename": name,
                "source_url": candidate()["source_url"],
                "archive_identifier": "psvitaalive-fmvita",
                "status": "TRANSFER_PREPARED",
            }}}
            md = {"metadata": {"uploader": "owner@example.test"},
                  "files": [{"name": name, "size": "2071824",
                             "md5": "a" * 32}]}
            with patch("scripts.archive_backup_batch.load_policy",
                       return_value=policy()), \
                 patch("scripts.archive_backup_batch.owned_metadata",
                       return_value=md), \
                 patch("scripts.archive_backup_batch.download_source",
                       return_value=("a" * 32, "b" * 64)) as download, \
                 patch("scripts.archive_backup_batch.verify_file_metadata") as verify_md, \
                 patch("scripts.archive_backup_batch.verify_public_download") as verify_public, \
                 patch("scripts.archive_backup_batch.attempt_upload") as upload:
                published = []
                status, detail = run_one(
                    root, dict(candidate(), kind="CATALOG_BACKFILL"), {},
                    state, ("ACCESS", "SECRET", "owner@example.test"),
                    max_file=10 * MIB, publish=lambda *args, **kwargs: published.append(args)
                )
            self.assertEqual(status, "PUBLISHED_VERIFIED")
            download.assert_called_once()
            verify_md.assert_called_once()
            verify_public.assert_called_once()
            upload.assert_not_called()
            self.assertNotIn("fmvita", state["inflight"])
            self.assertEqual(len(published), 1)
            updated = json.loads(path.read_text("utf-8"))
            self.assertEqual(updated["version"], app["version"])
            self.assertEqual(updated["version_date"], app["version_date"])
            self.assertEqual(updated["links"][1:], app["links"][1:])
            self.assertEqual(updated["links"][0]["url"], public_url(
                "psvitaalive-fmvita", name))

    def test_newer_version_retires_uncertain_checkpoint_and_preserves_new_vpk(self):
        app = release_app()
        item = dict(candidate(), kind="CATALOG_BACKFILL")
        old = {"version": "0.6.1", "filename": "fmvita v0.6.1.vpk",
               "source_url": item["source_url"].replace("0.6.2", "0.6.1"),
               "archive_identifier": item["archive_identifier"],
               "status": "TRANSFER_PREPARED"}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            path = root / "apps/fmvita.json"
            path.write_text(json.dumps(app), encoding="utf-8")
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "fmvita,psvitaalive-fmvita,ok,old.vpk,,\n", encoding="utf-8")
            state = {"schema_version": 1, "inflight": {"fmvita": old}}
            md = {"files": []}
            snapshots = []
            def publish(*args, **kwargs):
                snapshots.append(load_state(root))
            def upload_new(item, *args):
                self.assertEqual(item["version"], "0.6.2")
                persisted = load_state(root)
                self.assertEqual(persisted["inflight"]["fmvita"]["version"], "0.6.2")
                self.assertEqual(persisted["superseded"]["fmvita"][old["filename"]]["version"], "0.6.1")
            with patch("scripts.archive_backup_batch.load_policy", return_value=policy()), \
                 patch("scripts.archive_backup_batch.owned_metadata", return_value=md), \
                 patch("scripts.archive_backup_batch.download_source",
                       return_value=("a"*32, "b"*64)), \
                 patch("scripts.archive_backup_batch.verify_file_metadata"), \
                 patch("scripts.archive_backup_batch.verify_public_download"), \
                 patch("scripts.archive_backup_batch.attempt_upload", side_effect=upload_new) as upload:
                status, _ = run_one(root, item, {}, state, ("a", "s", "owner"),
                                    max_file=10*MIB, publish=publish)
            self.assertEqual(status, "PUBLISHED_VERIFIED")
            upload.assert_called_once()
            self.assertEqual(len(snapshots), 3)
            self.assertNotIn("fmvita", snapshots[0]["inflight"])
            self.assertEqual(snapshots[0]["superseded"]["fmvita"][old["filename"]]["recovery_status"],
                             "NOT_VISIBLE")
            self.assertNotIn("fmvita", state["inflight"])
            self.assertEqual(json.loads(path.read_text())["version"], "0.6.2")

    def test_superseded_reconciliation_is_read_only_and_does_not_downgrade(self):
        from scripts.archive_backup_batch import reconcile_historical
        app = release_app()
        old = {"version": "0.6.1", "filename": "fmvita v0.6.1.vpk",
               "source_url": candidate()["source_url"].replace("0.6.2", "0.6.1"),
               "archive_identifier": candidate()["archive_identifier"]}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            path = root / "apps/fmvita.json"
            before = json.dumps(app)
            path.write_text(before, encoding="utf-8")
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "fmvita,psvitaalive-fmvita,ok,old.vpk,,\n", encoding="utf-8")
            md = {"files": [{"name": old["filename"], "size": "1234", "md5": "a"*32}]}
            with patch("scripts.archive_backup_batch.load_policy", return_value=policy()), \
                 patch("scripts.archive_backup_batch.owned_metadata", return_value=md), \
                 patch("scripts.archive_backup_batch.download_source",
                       return_value=("a"*32, "b"*64)), \
                 patch("scripts.archive_backup_batch.stream_download",
                       return_value=(1234, "a"*32, "b"*64)), \
                 patch("scripts.archive_backup_batch.attempt_upload") as upload:
                status, _ = reconcile_historical(root, "fmvita", old["filename"], old,
                                                 "owner", max_file=10*MIB)
            self.assertEqual(status, "HISTORICAL_VERIFIED")
            upload.assert_not_called()
            self.assertEqual(path.read_text(), before)

    def test_historical_rechecks_are_bounded_and_preserve_checkpoint(self):
        from scripts.archive_backup_batch import recover_one_historical
        old = {"version": "0.6.1", "recovery_status": "NOT_VISIBLE", "checks": 0}
        state = {"schema_version": 1, "inflight": {},
                 "superseded": {"fmvita": {"fmvita v0.6.1.vpk": old}}}
        with tempfile.TemporaryDirectory() as tmp, \
             patch("scripts.archive_backup_batch.reconcile_historical",
                   side_effect=Deferred("VERIFYING", "not visible")) as check:
            root = Path(tmp)
            publish = lambda *args, **kwargs: None
            for attempt in range(3):
                if attempt:
                    old["last_checked_at"] = "2020-01-01T00:00:00+00:00"
                result = recover_one_historical(root, state, "owner",
                                                max_file=10*MIB, publish=publish)
                self.assertIsNotNone(result)
                if attempt == 0:
                    self.assertIsNone(recover_one_historical(root, state, "owner",
                                                             max_file=10*MIB, publish=publish))
            self.assertEqual(check.call_count, 3)
            self.assertEqual(old["recovery_status"], "PENDING_MANUAL")
            self.assertEqual(load_state(root)["superseded"]["fmvita"]["fmvita v0.6.1.vpk"]["checks"], 3)
            self.assertIsNone(recover_one_historical(root, state, "owner",
                                                     max_file=10*MIB, publish=publish))

    def test_older_or_equal_checkpoint_cannot_replace_active_transfer(self):
        from scripts.archive_backup_batch import retire_older_checkpoint
        old = {"version": "0.6.3", "filename": "fmvita v0.6.3.vpk",
               "archive_identifier": candidate()["archive_identifier"]}
        state = {"schema_version": 1, "inflight": {"fmvita": old}}
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(Deferred):
                retire_older_checkpoint(Path(tmp), candidate(), state, {"files": []})
        self.assertEqual(state["inflight"]["fmvita"], old)

    def test_all_nonexcluded_apps_are_autoeligible_without_spdx(self):
        default_policy = {"*": {"enabled": True}}
        app = release_app()
        self.assertTrue(source_matches_approved(candidate(), default_policy, app))
        another = dict(candidate(), app_id="another-psvita-app")
        self.assertTrue(source_matches_approved(another, default_policy, app))
        disabled = {**default_policy, "fmvita": {"enabled": False}}
        self.assertFalse(source_matches_approved(candidate(), disabled, app))
        foreign = dict(candidate(), source_url="https://github.com/stranger/repo/releases/download/a/file.vpk")
        self.assertFalse(source_matches_approved(foreign, default_policy, app))

    def test_policy_v2_defaults_on_and_explicit_exclusions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "config").mkdir()
            policy_file = root / "config/archive_backup_approved_apps.json"
            policy_file.write_text(json.dumps({
                "schema_version": 2, "default_enabled": True,
                "excluded_apps": ["blocked-game"],
            }), encoding="utf-8")
            rules = load_policy(root)
            self.assertTrue(policy_entry(rules, "fmvita")["enabled"])
            self.assertFalse(policy_entry(rules, "blocked-game")["enabled"])
            policy_file.write_text(json.dumps({
                "schema_version": 2, "default_enabled": True,
                "excluded_apps": ["blocked-game", "blocked-game"],
            }), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_policy(root)



if __name__ == "__main__":
    unittest.main()
