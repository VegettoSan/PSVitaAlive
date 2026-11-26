import unittest
import json
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

    def queue_fixture(self, root, pending_count=5):
        (root / "apps").mkdir()
        (root / b.STATE_DIR).mkdir(parents=True)
        ids = [f"pending-{i}" for i in range(pending_count)] + ["new-a", "new-b"]
        counts = Counter()
        for i, app_id in enumerate(ids):
            app = self.app("tntv")
            app.update(id=app_id, title_id=f"APP{i:06d}", name=app_id)
            counts[app["title_id"]] = 1
            (root / "apps" / (app_id + ".json")).write_text(json.dumps(app))
            if app_id.startswith("pending"):
                b.write_job(root, app_id, {"app_id": app_id, "status": "CREATING",
                                           "identifier": "psvitaalive-" + app_id})
        return counts

    def test_pending_and_unfinished_pilots_do_not_block_new_apps(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            counts = self.queue_fixture(root, pending_count=9)
            def run_one(root, app_id, spec, creds, counts):
                if app_id.startswith("pending"):
                    raise Deferred("VERIFYING", "still processing")
                b.write_job(root, app_id, {"app_id": app_id, "status": "CREATING"})
                return "VERIFYING"
            with patch.object(b, "load_policy", return_value={"*": {"enabled": True}}), \
                 patch.object(b, "upload_map", return_value={}), \
                 patch.object(b, "auto_spec", return_value={"size": b.MAX_VPK_BYTES // b.MAX_RESUME}), \
                 patch.object(b, "run_one", side_effect=run_one) as run:
                rows = b.automatic_queue(root, (), counts, execute=True)
            called = [c.args[1] for c in run.call_args_list]
            self.assertEqual(len([x for x in called if x.startswith("pending")]), b.MAX_RESUME)
            self.assertIn("new-a", called)
            self.assertIn("new-b", called)
            self.assertFalse(any(r["status"] == "PHASE_PENDING" for r in rows))

    def test_six_new_items_in_same_run_without_waiting_for_verification(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            counts = self.queue_fixture(root, pending_count=0)
            for i in range(7):
                app_id = f"extra-{i}"
                app = self.app("tntv")
                app.update(id=app_id, title_id=f"EXTRA{i:04d}", name=app_id)
                counts[app["title_id"]] = 1
                (root / "apps" / (app_id + ".json")).write_text(json.dumps(app))
            def run_one(root, app_id, spec, creds, counts):
                b.write_job(root, app_id, {"app_id": app_id, "status": "CREATING"})
                raise Deferred("VERIFYING", "not public yet")
            with patch.object(b, "load_policy", return_value={"*": {"enabled": True}}), \
                 patch.object(b, "upload_map", return_value={}), \
                 patch.object(b, "auto_spec", return_value={"size": 1024}), \
                 patch.object(b, "run_one", side_effect=run_one) as run:
                rows = b.automatic_queue(root, (), counts, execute=True)
            self.assertEqual(run.call_count, 6)
            self.assertEqual(sum(r["status"] == "VERIFYING" for r in rows), 6)
            self.assertEqual(sum(r["status"] == "DEFERRED" for r in rows), 3)

    def test_individual_uncertain_and_unavailable_do_not_stop_next(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            counts = self.queue_fixture(root, pending_count=2)
            def run_one(root, app_id, spec, creds, counts):
                if app_id == "pending-0":
                    raise Deferred("UPLOAD_RESULT_UNCERTAIN", "checkpoint retained")
                if app_id == "pending-1":
                    raise Deferred("WAITING_ARCHIVE", "not visible")
                return "VERIFYING"
            with patch.object(b, "load_policy", return_value={"*": {"enabled": True}}), \
                 patch.object(b, "upload_map", return_value={}), \
                 patch.object(b, "auto_spec", return_value={"size": 1}), \
                 patch.object(b, "run_one", side_effect=run_one) as run:
                b.automatic_queue(root, (), counts, execute=True)
            self.assertEqual(set(c.args[1] for c in run.call_args_list),
                             {"pending-0", "pending-1", "new-a", "new-b"})

    def test_git_failure_still_stops_all_writes(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            counts = self.queue_fixture(root, pending_count=1)
            with patch.object(b, "load_policy", return_value={"*": {"enabled": True}}), \
                 patch.object(b, "upload_map", return_value={}), \
                 patch.object(b, "auto_spec", return_value={"size": 1}), \
                 patch.object(b, "run_one", side_effect=Deferred("CATALOG_PUSH_BLOCKED", "git failed")) as run:
                rows = b.automatic_queue(root, (), counts, execute=True)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(rows[-1]["status"], "CATALOG_PUSH_BLOCKED")

    def test_other_checkpoint_reserves_identifier_even_if_not_public(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            self.queue_fixture(root, pending_count=1)
            with patch.object(b.initial, "load_job", return_value={"identifier": "psvitaalive-vitaforge"}), \
                 patch.object(b, "upload_map", return_value={}):
                self.assertIn("psvitaalive-pending-0", b.reserved_identifiers(root, "new-a"))
                self.assertNotIn("psvitaalive-pending-0", b.reserved_identifiers(root, "pending-0"))

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
