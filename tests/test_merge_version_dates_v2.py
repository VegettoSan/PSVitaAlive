import unittest
from scripts.external.models import Candidate
from scripts.external.merge import merge_group

class MergeVersionDateTests(unittest.TestCase):
    def test_external_date_repairs_same_version_local_date(self):
        local = Candidate(source_name='local', source_id='local', title_id='T', name='x', version='1.1', version_date='2026-08-15')
        external = Candidate(source_name='VitaDBtoo', source_id='122', title_id='T', name='x', version='1.1', version_date='2016-07-30')
        self.assertEqual(merge_group([local, external]).version_date, '2016-07-30')

if __name__ == '__main__': unittest.main()
