import csv
import json
import tempfile
import unittest
from pathlib import Path

from scripts.archive_backup_batch_preview import candidates, resolve_item, select_batch


def app(app_id, version, url):
    return {
        'id': app_id, 'title_id': 'TEST' + app_id.upper()[:5].ljust(5, '0'),
        'name': app_id, 'status': 'Legacy', 'version': version,
        'version_date': '2026-10-01', 'size': 1024,
        'source_url': f'https://github.com/example/{app_id}',
        'icon': f'https://archive.org/download/psvitaalive-{app_id}/icon0.png',
        'links': [
            {'type': 'Download', 'name': 'VPK', 'url': url, 'recommended': True},
            {'type': 'Repository', 'name': 'Source', 'url': f'https://github.com/example/{app_id}'},
        ],
    }


def release(app_id, ver, size):
    return {
        'platform': 'github', 'repo': f'example/{app_id}', 'tag': f'v{ver}',
        'prerelease': False, 'published': '2026-11-20T12:00:00Z',
        'assets': [{
            'name': f'{app_id}.vpk', 'size': size,
            'download_url': f'https://github.com/example/{app_id}/releases/download/v{ver}/{app_id}.vpk',
        }],
    }


class PreviewTests(unittest.TestCase):
    def test_batch_includes_new_release_and_same_version_pending(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'apps').mkdir()
            (root / 'scripts').mkdir()
            (root / 'scripts/archive_upload_log.csv').write_text(
                'app_id,identifier,status,files,error,url\n'
                'alpha,psvitaalive-alpha,ok,alpha.vpk,,\n'
                'bravo,psvitaalive-bravo,ok,bravo.vpk,,\n', encoding='utf-8')
            (root / 'apps/alpha.json').write_text(json.dumps(app(
                'alpha', '1.0', 'https://archive.org/download/psvitaalive-alpha/alpha.vpk')))
            (root / 'apps/bravo.json').write_text(json.dumps(app(
                'bravo', '1.1', 'https://github.com/example/bravo/releases/download/v1.1/bravo.vpk')))
            snapshot = {'schema_version': 1, 'releases': [release('alpha', '1.1', 1000000),
                                                           release('bravo', '1.1', 1500000)]}
            queue, reviews = candidates(root, snapshot)
            self.assertEqual(reviews, [])
            self.assertEqual({x['kind'] for x in queue}, {'NEW_RELEASE', 'SAME_VERSION_BACKFILL'})
            self.assertEqual({x['app_id'] for x in queue}, {'alpha', 'bravo'})

    def test_size_budget_does_not_lose_large_pending_app(self):
        sample = [
            {'app_id': 'small', 'size_bytes': 10_000_000, 'status': 'READY'},
            {'app_id': 'huge', 'size_bytes': 300_000_000, 'status': 'READY'},
            {'app_id': 'next', 'size_bytes': 10_000_000, 'status': 'READY'},
        ]
        result = select_batch(sample, max_files=1, max_file_bytes=200_000_000,
                              max_total_bytes=100_000_000)
        self.assertEqual([v['status'] for v in result],
                         ['SELECTED_PREVIEW', 'TOO_LARGE_PENDING', 'DEFERRED'])
        self.assertEqual(sample[0]['status'], 'READY', 'Do not modify caller data')

    def test_unverified_item_or_conflicting_id_requires_review(self):
        a = app('alpha', '1.0', 'https://archive.org/download/one/file.vpk')
        self.assertEqual(resolve_item(a, {'alpha': {'two'}})[0], None)
        a['icon'] = ''
        self.assertEqual(resolve_item(a, {})[1], 'app references (manual confirmation needed)')


if __name__ == '__main__':
    unittest.main()
