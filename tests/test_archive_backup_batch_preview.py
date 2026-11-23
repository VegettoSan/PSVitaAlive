import csv
import json
import tempfile
import unittest
from pathlib import Path

from scripts.archive_backup_batch_preview import (candidates, item_owned_by_account, resolve_item, select_batch)


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

    def test_foreign_media_never_becomes_upload_destination(self):
        a = app('alpha', '1.0', 'https://github.com/example/alpha/releases/download/v1.0/alpha.vpk')
        a['icon'] = 'https://archive.org/download/another-person/icon0.png'
        a['screenshots'] = ['https://archive.org/download/third-party/screens.png']
        a['links'].append({
            'type': 'Game Files', 'name': 'Game Files',
            'url': 'https://archive.org/download/gamefiles-by-other-user/data.zip',
        })
        self.assertEqual(resolve_item(a, {'alpha': {'psvitaalive-alpha'}}),
                         ('psvitaalive-alpha', 'historical-upload-log'))
        # Without successful historical upload, foreign media must not
        # authorize writing to *any* item.
        item, reason = resolve_item(a, {})
        self.assertIsNone(item)
        self.assertIn('manual backup', reason)

    def test_foreign_primary_vpk_or_ambiguous_log_needs_manual_review(self):
        a = app('alpha', '1.0', 'https://archive.org/download/other-person/file.vpk')
        self.assertIsNone(resolve_item(a, {'alpha': {'psvitaalive-alpha'}})[0])
        self.assertIsNone(resolve_item(a, {'alpha': {'one', 'two'}})[0])

    def test_uploader_must_match_private_configured_account(self):
        record = {'metadata': {'uploader': 'my-owner@example.test'}}
        self.assertEqual(item_owned_by_account(record, 'my-owner@example.test'),
                         (True, 'uploader verified'))
        self.assertEqual(item_owned_by_account(record, 'MY-OWNER@example.test')[0], True)
        self.assertEqual(item_owned_by_account(record, 'another@example.test'),
                         (False, 'Archive.org item managed by a different account'))
        self.assertEqual(item_owned_by_account(record, '')[0], False)
        self.assertEqual(item_owned_by_account({'metadata': {}}, 'my-owner@example.test')[0], False)
        self.assertEqual(item_owned_by_account({}, 'my-owner@example.test')[0], False)

    def test_no_own_upload_log_never_queued_for_automatic_upload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'apps').mkdir()
            (root / 'scripts').mkdir()
            item = app('alpha', '1.0', 'https://github.com/example/alpha/releases/download/v1.0/alpha.vpk')
            item['icon'] = 'https://archive.org/download/other-person/icon.png'
            (root / 'apps/alpha.json').write_text(json.dumps(item), encoding='utf-8')
            queue, reviews = candidates(root, {'schema_version': 1, 'releases': [release('alpha', '1.1', 1000000)]})
            self.assertEqual(queue, [])
            self.assertTrue(any(entry.get('status') == 'PENDING_MANUAL' and entry.get('app_id') == 'alpha'
                                for entry in reviews))



if __name__ == '__main__':
    unittest.main()
