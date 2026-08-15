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

    def test_neovitadb_release_metadata_maps_to_version_date(self):
        item = {
            "id": 21,
            "name": "VitaShell",
            "repo": "TheOfficialFloW/VitaShell",
            "titleid": "VITASHELL",
        }
        release = {
            "tag_name": "2.02",
            "published_at": "2024-01-02T03:04:05Z",
        }
        candidate = normalize_neovitadb(item, release=release)
        self.assertEqual(candidate.version, "2.02")
        self.assertEqual(candidate.version_date, "2024-01-02")

    def test_neovitadb_app_metadata_does_not_invent_a_date(self):
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
