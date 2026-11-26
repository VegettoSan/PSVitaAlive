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


    def test_transfer_all_without_public_verification(self):
        files = {"app.vpk": {}, "icon.png": {}, "shot.png": {}}
        job = {"attempted": []}
        calls, snapshots = [], []
        md = {"metadata": {"uploader": "expected"}, "files": []}
        def fetch(identifier):
            return md if calls else None
        def upload(identifier, name, folder, creds, metadata):
            self.assertEqual(snapshots[-1][-1], name)
            calls.append((name, metadata is not None))
        with patch.object(a, "verify_remote", side_effect=AssertionError("public verify during transfer")):
            a.transfer_batch(None, {"name": "Test"}, "item", files, None, job,
                             ("key", "secret", "expected"), fetch=fetch,
                             checkpoint=lambda root, j: snapshots.append(list(j["attempted"])),
                             upload=upload, sleep=lambda n: None)
        self.assertEqual(calls, [("app.vpk", True), ("icon.png", False), ("shot.png", False)])
        self.assertEqual(job["attempted"], list(files))

    def test_resume_old_checkpoint_sends_only_new_file(self):
        files = {"app.vpk": {}, "icon.png": {}, "SOURCES.txt": {}}
        job = {"attempted": ["app.vpk", "icon.png"]}
        md = {"metadata": {"uploader": "expected"}, "files": []}
        calls = []
        a.transfer_batch(None, {}, "item", files, None, job,
                         ("key", "secret", "expected"), fetch=lambda _: md,
                         checkpoint=lambda *_: None,
                         upload=lambda ident, name, *args: calls.append(name),
                         sleep=lambda _: None)
        self.assertEqual(calls, ["SOURCES.txt"])

    def test_uncertain_put_stops_and_retains_intent(self):
        files, job, snapshots = {"a.vpk": {}, "b.png": {}}, {"attempted": []}, []
        def fail(*args):
            raise Deferred("UPLOAD_RESULT_UNCERTAIN", "uncertain")
        with self.assertRaises(Deferred):
            a.transfer_batch(None, {"name": "Test"}, "item", files, None, job,
                             ("key", "secret", "expected"), fetch=lambda _: None,
                             checkpoint=lambda root, j: snapshots.append(list(j["attempted"])),
                             upload=fail)
        self.assertEqual(snapshots, [["a.vpk"]])
        self.assertEqual(job["attempted"], ["a.vpk"])

    def test_wait_for_item_owner_not_file_visibility(self):
        md = {"metadata": {"uploader": "expected"}, "files": []}
        responses = iter([None, {"metadata": {}, "files": []}, md])
        self.assertEqual(a.wait_owned_item("item", "expected", fetch=lambda _: next(responses),
                                           sleep=lambda _: None), md)

    def test_wrong_owner_never_sends_remaining_files(self):
        job = {"attempted": ["a.vpk"]}
        md = {"metadata": {"uploader": "other"}, "files": []}
        with self.assertRaises(Deferred):
            a.transfer_batch(None, {}, "item", {"a.vpk": {}, "b.png": {}}, None, job,
                             ("key", "secret", "expected"), fetch=lambda _: md,
                             checkpoint=lambda *_: self.fail("checkpoint"),
                             upload=lambda *_: self.fail("upload"))

    def test_verify_all_visible_files_even_if_first_missing(self):
        info = {"md5": "hash", "size": 1}
        files = {"a.vpk": info, "b.png": info, "c.png": info}
        md = {"metadata": {"uploader": "expected"}, "files": [
            {"name": "b.png", **info}, {"name": "c.png", **info}]}
        calls = []
        with self.assertRaisesRegex(Deferred, "a.vpk"):
            a.verify_batch("item", files, None, {"attempted": list(files)}, "expected",
                           fetch=lambda _: md, verify=lambda ident, name, *args: calls.append(name))
        self.assertEqual(calls, ["b.png", "c.png"])

    def test_public_delay_does_not_block_other_verifications(self):
        info = {"md5": "hash", "size": 1}
        files = {"a.vpk": info, "b.png": info}
        md = {"metadata": {"uploader": "expected"}, "files": [
            {"name": name, **info} for name in files]}
        calls = []
        def verify(ident, name, *args):
            calls.append(name)
            if name == "a.vpk":
                raise Deferred("WAITING_ARCHIVE", "not public")
        with self.assertRaisesRegex(Deferred, "a.vpk"):
            a.verify_batch("item", files, None, {"attempted": list(files)}, "expected",
                           fetch=lambda _: md, verify=verify)
        self.assertEqual(calls, list(files))

    def test_verification_conflict_is_not_ignored(self):
        info = {"md5": "hash", "size": 1}
        md = {"metadata": {"uploader": "expected"}, "files": [{"name": "a.vpk", **info}]}
        def verify(*args):
            raise Deferred("PENDING_MANUAL", "hash conflict")
        with self.assertRaisesRegex(Deferred, "hash conflict"):
            a.verify_batch("item", {"a.vpk": info}, None, {"attempted": ["a.vpk"]},
                           "expected", fetch=lambda _: md, verify=verify)


if __name__ == "__main__":
    unittest.main()
