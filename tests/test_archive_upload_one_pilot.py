"""Offline tests for the single, fail-closed Archive.org upload pilot."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.archive_upload_one_pilot import (
    APP, ITEM, REMOTE, EXPECTED_BYTES, PilotStop, has_remote_name,
    preflight, safe_redirect, validate_catalog, verify_new_file,
)


def fake_metadata(owner="owner@example.test", files=()):
    return {"metadata": {"identifier": ITEM, "uploader": owner}, "files": list(files)}


class SingleVpkPilotTests(unittest.TestCase):
    def test_owned_item_may_proceed_when_target_missing(self):
        preflight("owner@example.test", fetch=lambda _: fake_metadata())

    def test_third_party_never_uploaded(self):
        with self.assertRaises(PilotStop) as result:
            preflight("owner@example.test", fetch=lambda _: fake_metadata("another@example.test"))
        self.assertEqual(result.exception.status, "PENDING_MANUAL")

    def test_same_file_name_no_overwrite(self):
        file_records = [{"name": REMOTE.upper(), "size": "2"}]
        self.assertTrue(has_remote_name(fake_metadata(files=file_records), REMOTE))
        with self.assertRaises(PilotStop) as result:
            preflight("owner@example.test", fetch=lambda _: fake_metadata(files=file_records))
        self.assertEqual(result.exception.status, "ALREADY_EXISTS_REVIEW")

    def test_outage_waits(self):
        def fail(_):
            raise TimeoutError("unavailable")
        with self.assertRaises(PilotStop) as result:
            preflight("owner@example.test", fetch=fail)
        self.assertEqual(result.exception.status, "WAITING_ARCHIVE")

    def test_untrusted_upstream_redirect_rejected(self):
        self.assertTrue(safe_redirect("https://github.com/example/file.vpk"))
        self.assertTrue(safe_redirect("https://release-assets.githubusercontent.com/a?x=y"))
        for url in (
            "https://evil.example.com/payload.vpk", "http://github.com/file.vpk",
            "https://github.com.evil.tld/file", "file:///etc/passwd",
            "https://a@github.com/file",
        ):
            self.assertFalse(safe_redirect(url))

    def test_check_archive_md5_size(self):
        good = {"name": REMOTE, "size": str(EXPECTED_BYTES), "md5": "a" * 32}
        verify_new_file("a" * 32, fetch=lambda _: fake_metadata(files=[good]),
                        sleep=lambda _: self.fail("No sleep needed"))
        bad = dict(good, md5="b" * 32)
        with self.assertRaises(PilotStop) as result:
            verify_new_file("a" * 32, fetch=lambda _: fake_metadata(files=[bad]),
                            sleep=lambda _: self.fail("No sleep needed"))
        self.assertEqual(result.exception.status, "PENDING_MANUAL")

    def test_canonical_catalog_must_match_hardcoded_pilot(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                f"{APP},{ITEM},ok,old.vpk,,\n", encoding="utf-8")
            official = {
                "id": APP, "title_id": "BHBB00001", "version": "1.46",
                "status": "Legacy", "size": EXPECTED_BYTES,
                "source_url": "https://github.com/Ibrahim778/BetterHomebrewBrowser",
                "links": [
                    {"type": "Download", "name": "VPK",
                     "url": "https://github.com/Ibrahim778/BetterHomebrewBrowser/releases/download/V1.46/BetterHomebrewBrowser.vpk",
                     "recommended": True},
                    {"type": "Repository", "name": "Source",
                     "url": "https://github.com/Ibrahim778/BetterHomebrewBrowser"}
                ],
            }
            (root / "apps" / (APP + ".json")).write_text(json.dumps(official), encoding="utf-8")
            validate_catalog(root)
            official["source_url"] = "https://github.com/stranger/malicious"
            (root / "apps" / (APP + ".json")).write_text(json.dumps(official), encoding="utf-8")
            with self.assertRaises(PilotStop):
                validate_catalog(root)

    def test_no_keys_are_logged_when_upload_fails(self):
        # The exception must deliberately suppress the HTTP error body.
        from scripts.archive_upload_one_pilot import upload_once

        class FakeItem:
            def upload_file(self, *args, **kwargs):
                raise RuntimeError("request headers: SECRET-WOULD-LEAK")

        import sys
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as td:
            with patch.dict(sys.modules, {"internetarchive": SimpleNamespace(get_item=lambda _: FakeItem())}):
                with self.assertRaises(PilotStop) as raised:
                    upload_once(Path(td) / "file.vpk", "ACCESS", "SECRET-WOULD-LEAK")
        self.assertEqual(raised.exception.status, "UPLOAD_RESULT_UNCERTAIN")
        self.assertNotIn("SECRET", raised.exception.message)


if __name__ == "__main__":
    unittest.main()
