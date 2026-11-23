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

    def test_unconfirmed_checkpoint_prevents_second_upload(self):
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
                       return_value=("a" * 32, "b" * 64)), \
                 patch("scripts.archive_backup_batch.attempt_upload") as upload:
                with self.assertRaises(Deferred) as caught:
                    run_one(root, candidate(), {}, state, ("ACCESS", "SECRET", "owner@example.test"),
                            max_file=10 * MIB)
            self.assertEqual(caught.exception.status, "UPLOAD_RESULT_UNCERTAIN")
            upload.assert_not_called()

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
