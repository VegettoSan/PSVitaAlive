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

    def test_embeds_use_original_app_icon_for_new_and_updated(self):
        icon = "https://archive.org/download/psvitaalive-wordle-sdl/icon0.png"
        app = {"title_id": "WORDLESDL", "name": "Wordle SDL",
               "version": "1.2", "icon": icon}
        new = dn.app_embed(app)
        updated = dn.app_embed(app, "1.1")
        for embed in (new, updated):
            self.assertEqual(embed["thumbnail"], {"url": icon})
            self.assertEqual(embed["url"], dn.SITE + "WORDLESDL")
        self.assertIn("New Homebrew", new["title"])
        self.assertIn("Homebrew Updated", updated["title"])

    def test_missing_or_invalid_icon_does_not_break_announcement(self):
        app = {"title_id": "TEST00001", "version": "1.0"}
        invalid = (None, "", "not-a-url", "../icon.png",
                   "file:///tmp/icon.png", "javascript:alert(1)",
                   "https://user:pass@example.org/icon.png",
                   "https://example.org/icon.png\\nUnsafe",
                   "https://example.org/" + "a" * 2050,
                   ["https://example.org/icon.png"])
        for icon in invalid:
            app["icon"] = icon
            with self.subTest(icon=repr(icon)[:80]):
                self.assertNotIn("thumbnail", dn.app_embed(app))
                self.assertNotIn("thumbnail", dn.app_embed(app, "0.9"))
        self.assertEqual(
            dn.app_embed({"title_id": "TEST00001", "icon": "http://example.org/a.png"})["thumbnail"],
            {"url": "http://example.org/a.png"},
        )

    def test_icon_changes_alone_do_not_reannounce(self):
        old = {"TEST00001": {"version": "1.0"}}
        apps = [{"title_id": "TEST00001", "version": "1.0",
                 "icon": "https://example.org/new-icon.png"}]
        self.assertEqual(dn.changes(old, dn.snapshot(apps), apps), ([], []))

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
