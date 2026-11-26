import tempfile
import unittest
from pathlib import Path
from PIL import Image
from scripts import archive_media_backup as a
from scripts.archive_backup_batch import Deferred


class MediaBackupTests(unittest.TestCase):
    def test_only_media_fields_change_and_order_preserved(self):
        raw = {"id": "test", "ic": "external", "ss": ["old", "external", "new"],
               "v": "1.0", "l": [{"t": "Download", "u": "external"}]}
        out = a.rewrite_media(raw, "external", "archived")
        self.assertEqual(out, {**raw, "ic": "archived", "ss": ["old", "archived", "new"]})
        self.assertEqual(raw["ic"], "external")

    def test_verbose_media_schema(self):
        self.assertEqual(a.rewrite_media({"icon": "a", "screenshots": ["b", "a"]}, "a", "c"),
                         {"icon": "c", "screenshots": ["b", "c"]})

    def test_external_detection_independent_of_vpk(self):
        app = {"id": "test", "icon": "https://raw.githubusercontent.com/o/r/main/icon.png",
               "screenshots": ["https://archive.org/download/item/shot.png"]}
        self.assertEqual(a.external_urls(app), [app["icon"]])
        self.assertEqual(a.resolve_target(app, {"test": {"item"}})[0], "item")

    def test_conflicting_primary_never_uses_logged_item(self):
        app = {"id": "test", "icon": "https://github.com/o/r/icon.png",
               "links": [{"type": "Download", "name": "VPK",
                          "url": "https://archive.org/download/other/test.vpk"}]}
        self.assertIsNone(a.resolve_target(app, {"test": {"item"}})[0])

    def test_corroborated_old_item_with_changed_artwork(self):
        app = {"id": "test", "icon": "https://github.com/o/r/icon.png",
               "screenshots": ["https://archive.org/download/item/shot.png"],
               "links": [{"type": "Download", "name": "VPK",
                          "url": "https://archive.org/download/item/test.vpk"}]}
        self.assertEqual(a.resolve_target(app, {})[0], "item")

    def test_unrelated_item_not_inferred_from_single_reference(self):
        app = {"id": "test", "icon": "https://github.com/o/r/icon.png",
               "links": [{"type": "Download", "name": "VPK",
                          "url": "https://archive.org/download/other/test.vpk"}]}
        self.assertIsNone(a.resolve_target(app, {})[0])

    def test_source_allowlist(self):
        self.assertTrue(a.safe_media_url("https://raw.githubusercontent.com/o/r/main/icon"))
        for url in ["http://github.com/a", "https://github.com.evil.test/a",
                    "https://github.com@127.0.0.1/a", "https://github.com:443/a",
                    "http://169.254.169.254/a"]:
            self.assertFalse(a.safe_media_url(url))

    def test_content_format_not_extension(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "extensionless"
            Image.new("RGB", (8, 8)).save(p, format="PNG")
            info, ext = a.image_info(p)
            self.assertEqual(ext, ".png")
            self.assertEqual(len(info["sha256"]), 64)
            p.write_bytes(b"<html>not artwork</html>")
            with self.assertRaises(Deferred):
                a.image_info(p)

    def test_unknown_mapping_and_game_data_ignored(self):
        app = {"id": "test", "icon": "https://github.com/o/r/icon.png",
               "links": [{"type": "Game Files", "name": "data",
                          "url": "https://archive.org/download/item/game.zip"}]}
        self.assertIsNone(a.resolve_target(app, {})[0])


if __name__ == "__main__":
    unittest.main()
