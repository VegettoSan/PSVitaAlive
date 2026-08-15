import unittest

from scripts.external.merge import merge_group
from scripts.external.sources import Candidate


def candidate(source_id, version, version_date, title_id="SCRENFLSH"):
    return Candidate(
        source_id=source_id,
        source_item_id=source_id,
        title_id=title_id,
        name="VitaScreenFlasher",
        author_names=["NamelessGhoul0"],
        repository_url="https://github.com/NamelessGhoul0/VitaScreenFlasher",
        release_page=None,
        version=version,
        version_date=version_date,
        description="Screen flasher for PSVITA.",
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

    def test_vitadb_and_vitadbtoo_agree_on_historical_date(self):
        local = candidate("local", "v.1.1", "2026-08-15")
        vitadb = candidate("vitadb", "v.1.1", "2016-07-30")
        vitadbtoo = candidate("vitadbtoo", "v.1.1", "2016-07-30")
        merged = merge_group([local, vitadb, vitadbtoo])
        self.assertEqual(merged.version_date, "2016-07-30")

    def test_missing_external_date_does_not_replace_valid_local_date(self):
        local = candidate("local", "v.1.1", "2016-07-30")
        external = candidate("vitadbtoo", "v.1.1", None)
        merged = merge_group([local, external])
        self.assertEqual(merged.version_date, "2016-07-30")

    def test_missing_all_dates_is_rejected(self):
        local = candidate("local", "v.1.1", None)
        external = candidate("vitadbtoo", "v.1.1", None)
        with self.assertRaisesRegex(ValueError, "Missing version_date"):
            merge_group([local, external])


if __name__ == "__main__":
    unittest.main()
