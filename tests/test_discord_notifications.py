import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from scripts import discord_notifications as dn


class DiscordNotificationTests(unittest.TestCase):
    def test_diff_uses_title_id_and_version_only(self):
        old = {"TEST00001": {"version": "1.0"}}
        apps = [{"title_id": "TEST00001", "name": "Renamed", "version": "1.0", "description": "Changed"},
                {"title_id": "TEST00002", "name": "New", "version": "1.0"}]
        added, updates = dn.changes(old, dn.snapshot(apps), apps)
        self.assertEqual([a["title_id"] for a in added], ["TEST00002"])
        self.assertEqual(updates, [])
        apps[0]["version"] = "1.1"
        self.assertEqual(dn.changes(old, dn.snapshot(apps), apps)[1][0][1], "1.0")

    def test_reject_invalid_catalog(self):
        with self.assertRaises(ValueError):
            dn.snapshot([])
        with self.assertRaises(ValueError):
            dn.snapshot([{"title_id": "A"}, {"title_id": "A"}])

    def test_baseline_does_not_post_and_second_run_does_not_post(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            catalog, state = base / "catalog.json", base / "state.json"
            catalog.write_text(json.dumps([{"title_id": "TEST00001", "version": "1.0"}]))
            with patch.object(dn, "send") as send:
                dn.notify_catalog(catalog, state)
                dn.notify_catalog(catalog, state)
                send.assert_not_called()
            self.assertTrue(json.loads(state.read_text())["catalog_initialized"])

    def test_add_update_and_idempotency(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            catalog, state = base / "catalog.json", base / "state.json"
            catalog.write_text(json.dumps([{"title_id": "TEST00001", "version": "1.0"}]))
            dn.notify_catalog(catalog, state)
            catalog.write_text(json.dumps([{"title_id": "TEST00001", "version": "1.1"}, {"title_id": "TEST00002", "version": "2"}]))
            with patch.object(dn, "webhook_url", side_effect=lambda k: "https://example.test/" + k), patch.object(dn, "send") as send:
                dn.notify_catalog(catalog, state)
                self.assertEqual(send.call_count, 2)
                dn.notify_catalog(catalog, state)
                self.assertEqual(send.call_count, 2)

    def test_release_deduplication(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            event, state = base / "event.json", base / "state.json"
            event.write_text(json.dumps({"release": {"tag_name": "v1.2", "html_url": "https://github.com/VegettoSan/PSVitaAlive/releases/tag/v1.2"}}))
            with patch.object(dn, "webhook_url", return_value="https://example.test/release"), patch.object(dn, "send") as send:
                dn.notify_release(event, state)
                dn.notify_release(event, state)
                self.assertEqual(send.call_count, 1)
                self.assertFalse(json.loads(state.read_text())["catalog_initialized"])

    def test_failed_send_does_not_advance_state(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            catalog, state = base / "catalog.json", base / "state.json"
            catalog.write_text(json.dumps([{"title_id": "TEST00001", "version": "1.0"}]))
            dn.notify_catalog(catalog, state)
            catalog.write_text(json.dumps([{"title_id": "TEST00001", "version": "1.2"}]))
            with patch.object(dn, "webhook_url", return_value="https://example.test"), patch.object(dn, "send", side_effect=RuntimeError("failed")):
                with self.assertRaises(RuntimeError):
                    dn.notify_catalog(catalog, state)
            self.assertEqual(json.loads(state.read_text())["apps"]["TEST00001"]["version"], "1.0")


if __name__ == "__main__":
    unittest.main()
