import unittest

from scripts.external.sources import normalize_vitadb, normalize_vitadbtoo


class ExternalDateTests(unittest.TestCase):
    def test_vitadb_date(self):
        self.assertEqual(normalize_vitadb({"name":"x","version":"1.0","date":"2016-07-30"}).version_date, "2016-07-30")

    def test_vitadbtoo_date(self):
        self.assertEqual(normalize_vitadbtoo({"name":"x","version":"1.0","date":"2016-07-30"}).version_date, "2016-07-30")


if __name__ == '__main__':
    unittest.main()
