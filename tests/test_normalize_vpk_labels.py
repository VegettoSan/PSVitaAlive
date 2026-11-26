import json
import tempfile
import unittest
from pathlib import Path
from scripts.normalize_vpk_labels import inspect_app, audit


def download(label, url="https://github.com/o/r/releases/download/v1.0/App.vpk"):
    return {"t": "Download", "n": label, "u": url, "rec": True, "sz": 123}


class VpkLabelTests(unittest.TestCase):
    def test_current_version_app_name_filename_and_aliases(self):
        for label in ["v1.0", "App", "App.vpk", "Download VPK", " vPk "]:
            app = {"id": "app", "n": "App", "v": "1.0", "l": [download(label)]}
            with self.subTest(label=label):
                self.assertEqual(inspect_app(app)[:2], ("NORMALIZE", 0))

    def test_alpha_current_version_label(self):
        app = {"v": "Alpha 0.1.0", "l": [download("Alpha 0.1.0")]}
        self.assertEqual(inspect_app(app)[:2], ("NORMALIZE", 0))

    def test_versioned_filename_label(self):
        app = {"v": "1.05", "l": [download("TNTV v1.05 VPK",
            "https://github.com/o/r/releases/download/v1.05/TNTV_v1.05.vpk")]}
        self.assertEqual(inspect_app(app)[:2], ("NORMALIZE", 0))

    def test_alternative_names_are_preserved(self):
        app = {"n": "App", "l": [download("VPK (unsafe)", "https://example.org/unsafe.vpk")]}
        self.assertEqual(inspect_app(app)[:2], ("REVIEW", None))

    def test_multiple_downloads_do_not_choose_a_variant(self):
        app = {"n": "App", "l": [download("App"), download("Lite", "https://example.org/lite.vpk")]}
        self.assertEqual(inspect_app(app)[:2], ("REVIEW", None))

    def test_existing_vpk_and_alternate_remain(self):
        app = {"l": [download("VPK"), download("Lite", "https://example.org/lite.vpk")]}
        self.assertEqual(inspect_app(app)[:2], ("ALREADY_STANDARD", None))

    def test_multiple_canonical_vpk_buttons_are_reported(self):
        self.assertEqual(inspect_app({"l": [download("VPK"), download("vpk")]})[:2],
                         ("REVIEW", None))

    def test_game_files_and_other_types_are_not_renamed(self):
        game = {"t": "Game Files", "n": "Files", "u": "https://example.org/data.zip"}
        self.assertEqual(inspect_app({"l": [download("Variant A"), game]})[0], "REVIEW")
        self.assertEqual(inspect_app({"l": [dict(game, u="https://example.org/game.vpk")]})[0],
                         "REVIEW")
        self.assertEqual(inspect_app({"n": "App", "l": [download("App"), game]})[:2],
                         ("NORMALIZE", 0))

    def test_release_page_or_zip_not_direct_vpk(self):
        for url in ["https://example.org/release", "https://example.org/app.zip",
                    "https://example.org/a?file=app.vpk"]:
            self.assertEqual(inspect_app({"l": [download("App", url)]})[0], "NO_DIRECT_VPK")

    def test_verbose_schema_and_idempotent_apply(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "apps").mkdir()
            path = root / "apps/app.json"
            raw = {"id": "app", "name": "App", "version": "1.0",
                   "links": [{"type": "Download", "name": "App",
                              "url": "https://example.org/App.vpk", "recommended": True,
                              "size": 123}, {"type": "Game Files", "name": "Data",
                                             "url": "https://example.org/data.zip"}],
                   "icon": "https://example.org/icon.png"}
            path.write_text(json.dumps(raw), "utf-8")
            self.assertEqual(audit(root)["counts"], {"NORMALIZE": 1})
            self.assertEqual(json.loads(path.read_text()), raw)
            audit(root, True)
            raw["links"][0]["name"] = "VPK"
            self.assertEqual(json.loads(path.read_text()), raw)
            self.assertEqual(audit(root, True)["counts"], {"ALREADY_STANDARD": 1})


if __name__ == "__main__":
    unittest.main()
