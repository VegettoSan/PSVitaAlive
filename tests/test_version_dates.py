import unittest

from scripts.external.sources import (
    normalize_neovitadb,
    normalize_vitadb,
    normalize_vitadbtoo,
)


class VersionDateAdapterTests(unittest.TestCase):
    def test_vitadb_uses_date(self):
        item = {
            "id": 122,
            "name": "VitaScreenFlasher",
            "version": "v.1.1",
            "date": "2016-07-30",
            "titleid": "VITASCREEN",
        }
        candidate = normalize_vitadb(item)
        self.assertEqual(candidate.version, "v.1.1")
        self.assertEqual(candidate.version_date, "2016-07-30")

    def test_vitadbtoo_uses_date(self):
        item = {
            "id": "122",
            "name": "VitaScreenFlasher",
            "version": "v.1.1",
            "date": "2016-07-30",
            "titleid": "VITASCREEN",
        }
        candidate = normalize_vitadbtoo(item)
        self.assertEqual(candidate.version, "v.1.1")
        self.assertEqual(candidate.version_date, "2016-07-30")

    def test_neovitadb_dist_catalog_uses_published_release_date(self):
        # NeoVitaDB's generated dist/vita.json is built from GitHub Releases.
        # Its `date` is the release publication date; VitaHub should consume
        # that normalized field instead of querying every repository itself.
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

    def test_neovitadb_app_metadata_without_date_does_not_invent_one(self):
        # Raw apps/vita/*.json does not contain a version date. The importer
        # must not substitute today's date when that field is absent.
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
