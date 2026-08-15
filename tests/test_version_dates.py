import unittest

from scripts.external.sources import (
    normalize_neovitadb,
    normalize_vitadb,
    normalize_vitadbtoo,
)


class VersionDateAdapterTests(unittest.TestCase):
    def test_vitadb_real_vitascreenflasher_record_uses_date(self):
        # VitaDB list_hbs_json.php record shape: date is the release/update
        # date paired with the published version.
        item = {
            "id": 122,
            "name": "VitaScreenFlasher",
            "version": "v.1.1",
            "date": "2016-07-30",
            "titleid": "SCRENFLSH",
        }
        candidate = normalize_vitadb(item)
        self.assertEqual(candidate.title_id, "SCRENFLSH")
        self.assertEqual(candidate.version, "v.1.1")
        self.assertEqual(candidate.version_date, "2016-07-30")

    def test_vitadbtoo_real_vitascreenflasher_record_uses_date(self):
        # VitaDBtoo apps.json uses the same historical field name: date.
        item = {
            "id": 122,
            "name": "VitaScreenFlasher",
            "version": "v.1.1",
            "date": "2016-07-30",
            "titleid": "SCRENFLSH",
        }
        candidate = normalize_vitadbtoo(item)
        self.assertEqual(candidate.title_id, "SCRENFLSH")
        self.assertEqual(candidate.version, "v.1.1")
        self.assertEqual(candidate.version_date, "2016-07-30")

    def test_neovitadb_generated_catalog_record_uses_date(self):
        # NeoVitaDB's published static feed contains the fields produced by
        # tools/build_catalog.py. That builder derives `date` from the GitHub
        # release published_at (falling back to created_at only if needed).
        item = {
            "id": 21,
            "name": "VitaShell",
            "repo": "TheOfficialFloW/VitaShell",
            "titleid": "VITASHELL",
            "version": "2.02",
            "date": "2024-01-02",
        }
        candidate = normalize_neovitadb(item)
        self.assertEqual(candidate.version, "2.02")
        self.assertEqual(candidate.version_date, "2024-01-02")

    def test_neovitadb_raw_app_metadata_without_date_does_not_invent_one(self):
        # apps/vita/*.json is input metadata, not the generated release feed.
        # It has no version/date pair, so the adapter must not invent a date.
        item = {
            "id": 21,
            "name": "VitaShell",
            "repo": "TheOfficialFloW/VitaShell",
            "titleid": "VITASHELL",
        }
        candidate = normalize_neovitadb(item)
        self.assertIsNone(candidate.version_date)


if __name__ == "__main__":
    unittest.main()
