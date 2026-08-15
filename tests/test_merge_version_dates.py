import unittest

from scripts.external.merge import merge_group
from scripts.external.sources import Candidate


def candidate(source_id, version, version_date):
    return Candidate(
        source_id=source_id,
        source_item_id=source_id,
        title_id="VITASCREEN",
        name="VitaScreenFlasher",
        author_names=["SMOKE"],
        repository_url=None,
        release_page=None,
        version=version,
        version_date=version_date,
        description="",
        long_description="",
        requirements="",
        changelog="",
        icon=None,
        screenshots=[],
        download_url=None,
        size=None,
        category_raw="utility",
        platform="vita",
    )


class MergeVersionDateTests(unittest.TestCase):
    def test_external_date_repairs_same_version_local_date(self):
        local = candidate("local", "v.1.1", "2026-08-15")
        external = candidate("vitadbtoo", "v.1.1", "2016-07-30")
        merged = merge_group([local, external])
        self.assertEqual(merged.version, "v.1.1")
        self.assertEqual(merged.version_date, "2016-07-30")


if __name__ == "__main__":
    unittest.main()
