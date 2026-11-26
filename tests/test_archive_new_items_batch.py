import unittest
import struct
import tempfile
import zipfile
from pathlib import Path
from unittest.mock import patch
from collections import Counter
from scripts import archive_new_items_batch as b
from scripts.archive_backup_batch import Deferred


class NewItemsBatchTests(unittest.TestCase):
    def app(self, app_id):
        s = b.APPROVED[app_id]
        return {"id": app_id, "title_id": s["title_id"], "name": s["name"],
                "version": s["version"], "status": "Verified", "size": s["size"],
                "links": [{"type": "Download", "name": "VPK", "url": b.source_url(s)},
                          {"type": "Repository", "name": "Source", "url": "https://github.com/" + s["repo"]}]}

    def test_approved_exact_sources(self):
        for app_id, spec in b.APPROVED.items():
            self.assertEqual(b.check_candidate(self.app(app_id), app_id, spec,
                Counter({spec["title_id"]: 1}))["size"], spec["size"])

    def test_changed_version_blocks_but_estimate_does_not(self):
        s = b.APPROVED["tntv"]
        app = self.app("tntv")
        app["version"] = "1.06"
        with self.assertRaises(Deferred):
            b.check_candidate(app, "tntv", s, Counter({s["title_id"]: 1}))
        for estimate in (1, s["size"] + 1048576, None):
            app = self.app("tntv")
            app["size"] = estimate
            b.check_candidate(app, "tntv", s, Counter({s["title_id"]: 1}))

    def test_release_artwork_same_repo_only(self):
        s = b.APPROVED["supertuxkart-vita"]
        self.assertTrue(b.source_artwork("https://github.com/edgarlugo/stk-code/releases/download/vita-1.0/screenshot-1.png", s))
        self.assertFalse(b.source_artwork("https://github.com/other/stk-code/releases/download/vita-1.0/screenshot-1.png", s))
        self.assertFalse(b.source_artwork("https://github.com/edgarlugo/stk-code/releases/download/vita-1.0/game.zip", s))

    def test_duplicate_title_id_blocks(self):
        s = b.APPROVED["tntv"]
        with self.assertRaises(Deferred):
            b.check_candidate(self.app("tntv"), "tntv", s, Counter({s["title_id"]: 2}))

    def test_state_paths_are_isolated(self):
        self.assertNotEqual(b.job_path("tntv"), b.job_path("vitatwitch"))
        with self.assertRaises(Deferred):
            b.job_path("vitaforge")

    def test_artwork_requires_same_repository(self):
        s = b.APPROVED["tntv"]
        self.assertTrue(b.source_artwork("https://raw.githubusercontent.com/T64NCollection/TNTV/v1.05/sce_sys/icon0.png", s))
        self.assertFalse(b.source_artwork("https://raw.githubusercontent.com/other/TNTV/main/icon.png", s))
        self.assertFalse(b.source_artwork("https://github.com/T64NCollection/TNTV/blob/main/icon.png", s))

    def test_metadata_keeps_correct_creator_and_source(self):
        for app_id, spec in b.APPROVED.items():
            md = b.metadata(self.app(app_id), spec)
            self.assertEqual(md["creator"], spec["creator"])
            self.assertEqual(md["source"], "https://github.com/" + spec["repo"])


    def test_automatic_identity_does_not_use_approximate_size(self):
        app = self.app("tntv")
        app["size"] = 1
        spec = b.automatic_identity(app, "tntv", Counter({"NTET00001": 1}))
        self.assertEqual(spec["url"], b.source_url(b.APPROVED["tntv"]))
        self.assertNotIn("size", spec)

    def test_auto_gate_never_creates_before_all_pilots(self):
        with tempfile.TemporaryDirectory() as td:
            with patch.object(b, "run_one") as run:
                rows = b.automatic_queue(Path(td), (), Counter(), execute=True)
                self.assertEqual(rows[0]["status"], "PHASE_PENDING")
                run.assert_not_called()

    def test_auto_checkpoint_uses_pinned_sha_not_new_api_call(self):
        app = self.app("tntv")
        count = Counter({"NTET00001": 1})
        spec = dict(b.automatic_identity(app, "tntv", count),
                    size=123, sha256="a"*64, creator="Author")
        with patch.object(b, "official_asset_metadata") as api:
            self.assertEqual(b.auto_spec(Path("."), app, "tntv", count, {"source_spec": spec}), spec)
            api.assert_not_called()
        app["version"] = "2.0"
        with self.assertRaises(Deferred):
            b.auto_spec(Path("."), app, "tntv", count, {"source_spec": spec})

    def test_auto_artwork_blob_and_host_restrictions(self):
        app = self.app("tntv")
        spec = b.automatic_identity(app, "tntv", Counter({"NTET00001": 1}))
        self.assertEqual(b.artwork_download_url("https://github.com/T64NCollection/TNTV/blob/main/icon.png?raw=true", spec),
                         "https://raw.githubusercontent.com/T64NCollection/TNTV/main/icon.png")
        self.assertIsNone(b.artwork_download_url("https://github.com/other/repo/blob/main/icon.png?raw=true", spec))
        self.assertIsNone(b.artwork_download_url("https://127.0.0.1/icon.png", spec))

    def test_title_id_checks_real_bounded_sfo(self):
        value = b"NTET00001\x00"
        keys = b"TITLE_ID\x00"
        payload = struct.pack("<4sIIII", b"\x00PSF", 0x101, 36, 36+len(keys), 1)
        payload += struct.pack("<HHIII", 0, 0x0204, len(value), len(value), 0) + keys + value
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "test.vpk"
            with zipfile.ZipFile(path, "w") as z:
                z.writestr("sce_sys/param.sfo", payload)
                z.writestr("eboot.bin", b"binary")
            b.validate_vpk_title(path, "NTET00001")
            with self.assertRaises(Deferred):
                b.validate_vpk_title(path, "OTHER0001")

    def test_dynamic_checkpoint_paths_safe(self):
        self.assertEqual(b.job_path("new-app").name, "new-app.json")
        for app_id in ("../escape", "a/b", "vitaforge"):
            with self.assertRaises(Deferred):
                b.job_path(app_id)


if __name__ == "__main__":
    unittest.main()
