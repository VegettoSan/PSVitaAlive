import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts import archive_zip_publication as p
from scripts.archive_backup_batch import Deferred

TID = "STFX00001"
REPO = "tchisholm20/starfox-adventures-vita"
TAG = "v0.2.0"
DATE = "2026-11-14T07:13:50Z"
URL = "https://archive.org/download/starfox/new.vpk"


def app():
    return {"id": "star-fox-adventures", "title_id": TID, "name": "Star Fox Adventures",
            "version": "0.1.3", "version_date": "2026-11-05", "status": "Verified",
            "links": [{"type": "Download", "name": "VPK", "url": "https://archive.org/download/starfox/old.vpk", "size": 99},
                      {"type": "Game Files", "name": "Game Files", "url": "https://archive.org/download/starfox/data.zip"},
                      {"type": "Repository", "url": "https://github.com/" + REPO}]}


def asset(n=1):
    return {"repo": REPO, "tag": TAG, "release_id": 12, "asset_id": n, "size": 100,
            "name": f"Vita{n}.zip", "url": f"https://github.com/{REPO}/releases/download/{TAG}/Vita{n}.zip",
            "updated_at": DATE, "digest": None}


def proof(a, status="VPK_FOUND"):
    inner = {"entry": "nested/game.vpk", "title_id": TID, "sha256": "b"*64, "size": 50}
    return {"asset": a, "title_id": TID, "result": {"status": status,
            "matches": [inner] if status == "VPK_FOUND" else [], "outer_sha256": "a"*64}}


class ZipPublicationTests(unittest.TestCase):
    def test_only_vpk_metadata_changes(self):
        old = app()
        new = p.proposed_app(old, REPO, TAG, DATE, 50, URL)
        self.assertEqual(new["version"], "0.2.0")
        self.assertEqual(new["version_date"], "2026-11-14")
        self.assertEqual(new["links"][0]["url"], URL)
        self.assertEqual(new["links"][0]["size"], 50)
        self.assertEqual(new["links"][1:], old["links"][1:])
        self.assertEqual(old["version"], "0.1.3")

    def test_missing_download_and_same_version_guard(self):
        old = app()
        with self.assertRaises(Deferred):
            p.proposed_app(old, REPO, "v0.1.3", DATE, 50, URL)
        old["links"].pop(0)
        new = p.proposed_app(old, REPO, "v0.1.3", DATE, 50, URL)
        self.assertEqual(new["version"], old["version"])
        self.assertEqual(new["links"][:-1], old["links"])
        self.assertEqual(new["links"][-1]["url"], URL)

    def test_all_current_assets_must_be_checked(self):
        one, two = asset(), asset(2)
        cache = {"entries": {p.audit.cache_key(one, app()): proof(one)}}
        release = {"direct": False, "assets": [one, two]}
        with self.assertRaises(Deferred) as err:
            p.choose_verified(release, app(), cache)
        self.assertEqual(err.exception.status, "ZIP_INSPECTION_PENDING")
        cache["entries"][p.audit.cache_key(two, app())] = proof(two, "NO_VPK")
        self.assertEqual(p.choose_verified(release, app(), cache)[1], one)
        cache["entries"][p.audit.cache_key(two, app())] = proof(two)
        with self.assertRaises(Deferred) as err:
            p.choose_verified(release, app(), cache)
        self.assertEqual(err.exception.status, "NO_UNIQUE_VPK")
        with self.assertRaises(Deferred):
            p.choose_verified({"direct": True}, app(), cache)

    def test_owned_upload_checkpoint_precedes_put_and_no_repeat(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            job = {"identifier": "starfox", "app_id": app()["id"], "files": {"new.vpk": {}}, "attempted": []}
            state = {"schema_version": 1, "jobs": {"a"*64: job}}
            def publish(root, paths, msg, validate=False):
                self.assertEqual(json.loads((root / p.STATE).read_text())["jobs"]["a"*64]["attempted"], ["new.vpk"])
            with patch.object(p.initial, "lookup", return_value={"metadata": {"uploader": "owner"}, "files": []}), \
                 patch.object(p.initial, "put_file", side_effect=Deferred("UPLOAD_RESULT_UNCERTAIN", "uncertain")) as put:
                with self.assertRaises(Deferred):
                    p.transfer_owned(root, job, state, root, ("a", "b", "owner"), publish=publish)
                p.transfer_owned(root, job, state, root, ("a", "b", "owner"), publish=publish)
                put.assert_called_once()

    def test_foreign_owner_never_put(self):
        with tempfile.TemporaryDirectory() as tmp:
            job = {"identifier": "foreign", "files": {"new.vpk": {}}, "attempted": []}
            with patch.object(p.initial, "lookup", return_value={"metadata": {"uploader": "other"}, "files": []}), \
                 patch.object(p.initial, "put_file") as put:
                with self.assertRaises(Deferred):
                    p.transfer_owned(Path(tmp), job, {}, Path(tmp), ("a", "b", "owner"))
                put.assert_not_called()

    def test_new_item_reservation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / p.STATE.parent).mkdir()
            job = {"identifier": "psvitaalive-star-fox-adventures", "app_id": app()["id"], "create": True, "status": "CREATING"}
            (root / p.STATE).write_text(json.dumps({"schema_version": 1, "jobs": {"a"*64: job}}))
            self.assertTrue(p.pending_creation(root, app()["id"]))
            self.assertFalse(p.pending_creation(root, "other"))

if __name__ == "__main__":
    unittest.main()
