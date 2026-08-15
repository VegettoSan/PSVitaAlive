import unittest

from scripts.external.sources import normalize_vitadb, normalize_vitadbtoo, normalize_neovitadb


class VersionDateAdapterTests(unittest.TestCase):
    def test_vitadb_uses_date(self):
        item = {"id": 122, "name": "VitaScreenFlasher", "version": "v.1.1", "date": "2016-07-30", "titleid": "VITASCREEN"}
        candidate = normalize_vitadb(item)
        self.assertEqual(candidate.version, "v.1.1")
        self.assertEqual(candidate.version_date, "2016-07-30")

    def test_vitadbtoo_uses_date(self):
        item = {"id": "122", "name": "VitaScreenFlasher", "version": "v.1.1", "date": "2016-07-30", "titleid": "VITASCREEN"}
        candidate = normalize_vitadbtoo(item)
        self.assertEqual(candidate.version, "v.1.1")
        self.assertEqual(candidate.version_date, "2016-07-30")

    def test_neovitadb_does_not_invent_date_from_app_metadata(self):
        item = {"id": 21, "name": "VitaShell", "repo": "TheOfficialFloW/VitaShell", "titleid": "VITASHELL"}
        candidate = normalize_neovitadb(item)
        self.assertIsNone(candidate.version_date)


if __name__ == "__main__":
    unittest.main()
