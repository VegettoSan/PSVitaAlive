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

    def test_recovers_twelve_unannounced_catalog_updates_in_two_batches_once(self):
        """A regenerated catalog must announce missed bot-authored VPK updates."""
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            catalog, state = root / "catalog.json", root / "state.json"
            originals = {
                "FFSQ00007": "1.2.0",
                "BTTD00010": "1.1",
                **{f"APP{i:05d}": "1.0" for i in range(10)},
            }
            dn.save_json(state, {
                "schema": 1, "catalog_initialized": True,
                "apps": {tid: {"version": v} for tid, v in originals.items()},
                "releases": [],
            })
            apps = [
                {"title_id": tid, "name": tid, "version": (
                    "1.3.1" if tid == "FFSQ00007" else
                    "1.2.3" if tid == "BTTD00010" else "1.1"
                )}
                for tid in originals
            ]
            catalog.write_text(json.dumps(apps), encoding="utf-8")
            with patch.object(dn, "webhook_url", return_value="https://example.test/updates"), \
                 patch.object(dn, "send") as send:
                dn.notify_catalog(catalog, state)
                self.assertEqual(send.call_count, 2)
                posts = [call.args[1]["embeds"] for call in send.call_args_list]
                self.assertEqual(list(map(len, posts)), [10, 2])
                self.assertEqual(sum(map(len, posts)), 12)
                self.assertTrue(all("Homebrew Updated" in e["title"]
                                    for group in posts for e in group))
                self.assertEqual(json.loads(state.read_text())["apps"]["FFSQ00007"]["version"], "1.3.1")
                dn.notify_catalog(catalog, state)
                self.assertEqual(send.call_count, 2)

    def test_release_deduplication(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            event, state = base / "event.json", base / "state.json"
            event.write_text(json.dumps({"release": {"tag_name": "v1.2", "html_url": "https://github.com/VegettoSan/PSVitaAlive/releases/tag/v1.2", "assets": [{"name": "PSVitaAlive.vpk", "state": "uploaded"}]}}))
            with patch.object(dn, "webhook_url", return_value="https://example.test/release"), patch.object(dn, "send") as send:
                dn.notify_release(event, state)
                dn.notify_release(event, state)
                self.assertEqual(send.call_count, 1)
                self.assertFalse(json.loads(state.read_text())["catalog_initialized"])

    def test_release_full_markdown_catalog_icon_and_failure_recovery(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            event, state, catalog = root / "event.json", root / "state.json", root / "catalog.json"
            icon = "https://example.org/psvitaalive.png"
            catalog.write_text(json.dumps([{"tid": "PSVAS1178", "ic": icon}]))
            body = ("## Changes\n- **Fix** `APP_VER` @example\n" * 90)
            release = {"tag_name": "02.19", "html_url": "https://github.com/VegettoSan/PSVitaAlive/releases/tag/02.19",
                       "body": body, "assets": [{"name": "PSVitaAlive.vpk", "state": "uploaded"}]}
            event.write_text(json.dumps({"release": release}))
            with patch.object(dn, "webhook_url", return_value="https://example.test/release"), patch.object(dn, "send") as send:
                send.side_effect = RuntimeError("failed")
                with self.assertRaises(RuntimeError):
                    dn.notify_release(event, state, catalog)
                self.assertFalse(state.exists())
                send.side_effect = None
                send.reset_mock()
                dn.notify_release(event, state, catalog)
                embed = send.call_args.args[1]["embeds"][0]
                self.assertEqual(embed["description"], body)
                self.assertEqual(embed["thumbnail"], {"url": icon})
                dn.notify_release(event, state, catalog)
                send.assert_called_once()
            self.assertEqual(dn.state_from_file(state)["releases"], ["02.19"])

    def test_release_long_notes_and_missing_icon(self):
        release = {"tag_name": "02.19", "html_url": "https://github.com/VegettoSan/PSVitaAlive/releases/tag/02.19",
                   "body": "## Changes\n" + "x" * 9000}
        with tempfile.TemporaryDirectory() as folder:
            embeds = dn.release_embeds(release, Path(folder) / "missing.json")
        self.assertEqual("".join(e["description"] for e in embeds), release["body"])
        self.assertTrue(all(len(e["description"]) <= 4096 for e in embeds))
        self.assertEqual(embeds[0]["thumbnail"]["url"], dn.CLIENT_ICON_FALLBACK)

    def test_release_requires_uploaded_vpk_and_skips_unstable(self):
        with tempfile.TemporaryDirectory() as folder:
            event, state = Path(folder) / "event.json", Path(folder) / "state.json"
            release = {"tag_name": "02.19", "html_url": "https://github.com/VegettoSan/PSVitaAlive/releases/tag/02.19"}
            with patch.object(dn, "send") as send:
                for flag in ("draft", "prerelease"):
                    event.write_text(json.dumps({"release": {**release, flag: True}}))
                    dn.notify_release(event, state)
                event.write_text(json.dumps({"release": release}))
                with self.assertRaises(ValueError):
                    dn.notify_release(event, state)
                send.assert_not_called()
                self.assertFalse(state.exists())

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
