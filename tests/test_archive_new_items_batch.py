import unittest
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

    def test_changed_version_and_size_block(self):
        s = b.APPROVED["tntv"]
        for k, value in (("version", "1.06"), ("size", 1)):
            app = self.app("tntv")
            app[k] = value
            with self.assertRaises(Deferred):
                b.check_candidate(app, "tntv", s, Counter({s["title_id"]: 1}))

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


if __name__ == "__main__":
    unittest.main()
