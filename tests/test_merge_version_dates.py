import unittest

from scripts.external.models import Candidate
from scripts.external.merge import merge_group


class MergeVersionDateTests(unittest.TestCase):
    def test_external_date_repairs_same_version_local_date(self):
        local = Candidate(
            source_name="local",
            source_id="vitascreenflasher",
            title_id="VITASCREEN",
            name="VitaScreenFlasher",
            version="v.1.1",
            version_date="2026-08-15",
        )
        external = Candidate(
            source_name="VitaDBtoo",
            source_id="122",
            title_id="VITASCREEN",
            name="VitaScreenFlasher",
            version="v.1.1",
            version_date="2016-07-30",
        )
        merged = merge_group([local, external])
        self.assertEqual(merged.version, "v.1.1")
        self.assertEqual(merged.version_date, "2016-07-30")


if __name__ == "__main__":
    unittest.main()
