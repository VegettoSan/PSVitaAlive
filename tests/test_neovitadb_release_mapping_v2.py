import unittest

from scripts.external.sources import normalize_neovitadb

class NeoVitaDBMappingTests(unittest.TestCase):
    def test_current_adapter_accepts_optional_release_metadata(self):
        item = {'id': 21, 'name': 'VitaShell', 'repo': 'TheOfficialFloW/VitaShell', 'titleid': 'VITASHELL'}
        candidate = normalize_neovitadb(item)
        self.assertIsNone(candidate.version_date)

if __name__ == '__main__': unittest.main()
