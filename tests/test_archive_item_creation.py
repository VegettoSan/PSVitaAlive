import unittest
from unittest.mock import patch
from scripts import archive_item_creation as a
from scripts.archive_backup_batch import Deferred


class CreationTests(unittest.TestCase):
    def test_missing_metadata_only_empty_object(self):
        with patch.object(a, "fetch_item_metadata", return_value={}):
            self.assertIsNone(a.lookup("psvitaalive-vitaforge"))
        with patch.object(a, "fetch_item_metadata", return_value={"metadata": {}}):
            with self.assertRaises(Deferred):
                a.lookup("psvitaalive-vitaforge")
        with patch.object(a, "fetch_item_metadata", side_effect=TimeoutError):
            with self.assertRaises(Deferred):
                a.lookup("psvitaalive-vitaforge")

    def test_archive_hints_ignore_game_data(self):
        app = {"id": "vitaforge", "links": [
            {"type": "Game Files", "url": "https://archive.org/download/other/game.zip"}]}
        self.assertFalse(a.archive_hints(app, {}))
        app["icon"] = "https://archive.org/download/vitaforge/icon.png"
        self.assertEqual(a.archive_hints(app, {}), {"vitaforge"})

    def test_history_prevents_creation(self):
        self.assertEqual(a.archive_hints({"id": "vitaforge"}, {"vitaforge": {"old-item"}}),
                         {"old-item"})

    def test_no_unowned_item(self):
        with self.assertRaises(Deferred):
            a.require_owner({"metadata": {"uploader": "other"}}, "expected")

    def test_absent_remote_no_repeat(self):
        with self.assertRaises(Deferred) as ctx:
            a.verify_remote("psvitaalive-vitaforge", "x.vpk", {}, {"files": []}, None)
        self.assertEqual(ctx.exception.status, "VERIFYING")


    def test_third_party_match_does_not_block_free_identifier(self):
        own = "expected"
        data = {"metadata": {"uploader": "other"}, "files": []}
        a.require_free_destination("psvitaalive-vitaforge", own,
            fetch=lambda i: None if i == "psvitaalive-vitaforge" else data,
            search=lambda: ["unrelated-vitaforge"])

    def test_owned_alternate_identifier_blocks_creation(self):
        data = {"metadata": {"uploader": "expected"}, "files": []}
        with self.assertRaisesRegex(Deferred, "Existing own candidate: old-vitaforge"):
            a.require_free_destination("psvitaalive-vitaforge", "expected",
                fetch=lambda i: None if i == "psvitaalive-vitaforge" else data,
                search=lambda: ["old-vitaforge"])

    def test_exact_third_party_identifier_blocks_creation(self):
        data = {"metadata": {"uploader": "other"}, "files": []}
        with self.assertRaisesRegex(Deferred, "Exact identifier occupied"):
            a.require_free_destination("psvitaalive-vitaforge", "expected",
                fetch=lambda i: data, search=lambda: [])

    def test_unknown_owner_blocks_creation(self):
        with self.assertRaisesRegex(Deferred, "Unknown owner"):
            a.require_free_destination("psvitaalive-vitaforge", "expected",
                fetch=lambda i: None if i == "psvitaalive-vitaforge" else {"metadata": {}, "files": []},
                search=lambda: ["other-vitaforge"])


if __name__ == "__main__":
    unittest.main()
