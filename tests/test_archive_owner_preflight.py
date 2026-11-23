"""Offline tests: no real Archive.org requests and no secrets."""
import tempfile
import unittest
import urllib.error
from pathlib import Path

from scripts.archive_owner_preflight import (
    approved_item_ids, evaluate, fetch_item_metadata, STATUS_READY,
    STATUS_MANUAL, STATUS_CONFIG, STATUS_WAITING,
)


class OwnerPreflightTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = Path(self.tmp.name) / "archive_upload_log.csv"
        self.log.write_text(
            "app_id,identifier,status,files,error,url\n"
            "z2,psvitaalive-z2,ok,z2.vpk,,\n"
            "foreign,foreign-item,error,f.vpk,forbidden,\n"
            "bad,danger/other,ok,b.vpk,,\n", encoding="utf-8")
        self.env = {
            "IA_ACCESS_KEY": "test-access",
            "IA_SECRET_KEY": "test-secret",
            "IA_EXPECTED_UPLOADER": "private-owner@example.test",
        }

    def test_only_successful_historical_items_are_known(self):
        self.assertEqual(approved_item_ids(self.log), {"psvitaalive-z2"})

    def test_matching_uploader_is_not_approval_to_upload(self):
        result = evaluate(
            "psvitaalive-z2", self.log, self.env,
            fetch=lambda _: {"metadata": {"uploader": "private-owner@example.test"}})
        self.assertEqual(result["status"], STATUS_READY)
        self.assertFalse(result["upload_permitted"])
        self.assertNotIn("private-owner@example.test", result["message"])
        self.assertNotIn("test-secret", result["message"])

    def test_different_uploader_is_manual_pending(self):
        result = evaluate(
            "psvitaalive-z2", self.log, self.env,
            fetch=lambda _: {"metadata": {"uploader": "other-account@example.test"}})
        self.assertEqual(result["status"], STATUS_MANUAL)
        self.assertFalse(result["upload_permitted"])
        self.assertNotIn("other-account@example.test", result["message"])

    def test_foreign_item_is_rejected_without_network_call(self):
        called = []
        result = evaluate("foreign-item", self.log, self.env,
                          fetch=lambda _: called.append("network"))
        self.assertEqual(result["status"], STATUS_MANUAL)
        self.assertEqual(called, [])

    def test_absent_secrets_stop_and_never_fetch(self):
        called = []
        result = evaluate("psvitaalive-z2", self.log, {}, fetch=lambda _: called.append("network"))
        self.assertEqual(result["status"], STATUS_CONFIG)
        self.assertEqual(called, [])

    def test_service_down_waits_not_manual(self):
        for exc in [
            urllib.error.URLError("offline"),
            urllib.error.HTTPError("https://archive.org/", 503, "unavailable", None, None),
            TimeoutError("timeout"),
        ]:
            def fail(_):
                raise exc
            result = evaluate("psvitaalive-z2", self.log, self.env, fetch=fail)
            self.assertEqual(result["status"], STATUS_WAITING)

    def test_permission_denial_is_manual(self):
        def forbidden(_):
            raise urllib.error.HTTPError("https://archive.org/", 403, "Forbidden", None, None)
        result = evaluate("psvitaalive-z2", self.log, self.env, fetch=forbidden)
        self.assertEqual(result["status"], STATUS_MANUAL)

    def test_invalid_identifier_never_calls_remote(self):
        result = evaluate("../third-party", self.log, self.env,
                          fetch=lambda _: self.fail("No remote requests for invalid id"))
        self.assertEqual(result["status"], STATUS_MANUAL)
        with self.assertRaises(ValueError):
            fetch_item_metadata("../secret")


if __name__ == "__main__":
    unittest.main()
