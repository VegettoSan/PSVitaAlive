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
        data = {"metadata": {"uploader": "other"}, "files": []}
        options = a.identifier_options("vitaforge")
        selected = a.select_destination("vitaforge", "expected",
            fetch=lambda i: data if i == "unrelated-vitaforge" else None,
            search=lambda owner: ["unrelated-vitaforge"])
        self.assertEqual(selected, options[0])

    def test_owned_alternate_identifier_blocks_creation(self):
        data = {"metadata": {"uploader": "expected"}, "files": []}
        with self.assertRaisesRegex(Deferred, "Existing own candidate: old-vitaforge"):
            a.select_destination("vitaforge", "expected",
                fetch=lambda i: data if i == "old-vitaforge" else None,
                search=lambda owner: ["old-vitaforge"])

    def test_three_occupied_options_select_fourth(self):
        data = {"metadata": {"uploader": "other"}, "files": []}
        options = a.identifier_options("vitaforge")
        self.assertEqual(a.select_destination("vitaforge", "expected",
            fetch=lambda i: data if i in options[:3] else None,
            search=lambda owner: []), options[3])

    def test_all_four_occupied_stop(self):
        data = {"metadata": {"uploader": "other"}, "files": []}
        with self.assertRaisesRegex(Deferred, "All four"):
            a.select_destination("vitaforge", "expected",
                fetch=lambda i: data, search=lambda owner: [])

    def test_owned_later_option_blocks_even_if_first_free(self):
        data = {"metadata": {"uploader": "expected"}, "files": []}
        last = a.identifier_options("vitaforge")[-1]
        with self.assertRaisesRegex(Deferred, "Existing own candidate"):
            a.select_destination("vitaforge", "expected",
                fetch=lambda i: data if i == last else None, search=lambda owner: [])

    def test_unknown_owner_blocks_creation(self):
        with self.assertRaisesRegex(Deferred, "Unknown owner"):
            a.select_destination("vitaforge", "expected",
                fetch=lambda i: {"metadata": {}, "files": []} if i == "other-vitaforge" else None,
                search=lambda owner: ["other-vitaforge"])

    def test_metadata_outage_does_not_choose_other_identifier(self):
        with self.assertRaisesRegex(Deferred, "unavailable"):
            a.select_destination("vitaforge", "expected",
                fetch=lambda i: (_ for _ in ()).throw(Deferred("WAITING_ARCHIVE", "unavailable")),
                search=lambda owner: [])


if __name__ == "__main__":
    unittest.main()
