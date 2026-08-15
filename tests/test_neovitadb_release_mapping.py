import unittest

from scripts.external.sources import normalize_neovitadb


class NeoVitaDBReleaseMappingTests(unittest.TestCase):
    def test_release_metadata_maps_to_candidate(self):
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


if __name__ == "__main__":
    unittest.main()
