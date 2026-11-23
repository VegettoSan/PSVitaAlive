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
    def test_backup_starts_from_current_catalog_even_if_orbit_is_stale(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "alpha,psvitaalive-alpha,ok,old.vpk,,\n"
                "bravo,psvitaalive-bravo,ok,old.vpk,,\n", encoding="utf-8")
            # Alpha is already hosted in our IA item: no second upload.
            (root / "apps/alpha.json").write_text(json.dumps(app(
                "alpha", "1.0",
                "https://archive.org/download/psvitaalive-alpha/alpha.vpk")))
            # Bravo already has the new VPK in the catalog, but the monitor
            # snapshot is stale (or missing the release altogether).
            (root / "apps/bravo.json").write_text(json.dumps(app(
                "bravo", "1.1",
                "https://github.com/example/bravo/releases/download/v1.1/bravo.vpk")))
            queue, reviews = candidates(root, {"schema_version": 1, "releases": [
                release("bravo", "1.0", 1000)]})
            self.assertEqual(reviews, [])
            self.assertEqual([(x["app_id"], x["version"], x["kind"]) for x in queue],
                             [("bravo", "1.1", "CATALOG_BACKFILL")])
            self.assertEqual(queue[0]["size_bytes"], 1024)
            self.assertEqual(queue, candidates(root)[0], "Orbit must be optional")

    def test_catalog_archive_status_can_be_preserved_without_version_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "alpha,psvitaalive-alpha,ok,old.vpk,,\n", encoding="utf-8")
            a = app("alpha", "v.0.8",
                    "https://github.com/example/alpha/releases/download/v.0.8/alpha.vpk")
            a["status"] = "Archive"
            (root / "apps/alpha.json").write_text(json.dumps(a))
            queue, reviews = candidates(root)
            self.assertEqual(reviews, [])
            self.assertEqual(queue[0]["version"], "v.0.8")
            self.assertEqual(queue[0]["kind"], "CATALOG_BACKFILL")

    def test_current_vpk_without_size_requires_specific_review(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "alpha,psvitaalive-alpha,ok,old.vpk,,\n", encoding="utf-8")
            a = app("alpha", "1.2",
                    "https://github.com/example/alpha/releases/download/v1.2/alpha.vpk")
            a["size"] = None
            (root / "apps/alpha.json").write_text(json.dumps(a))
            queue, reviews = candidates(root)
            self.assertEqual(queue, [])
            self.assertEqual(reviews[0]["app_id"], "alpha")
            self.assertEqual(reviews[0]["status"], "SOURCE_ADAPTER_PENDING")
            self.assertIn("size", reviews[0]["reason"])

    def test_shared_repository_is_not_rejected_when_catalog_identifies_exact_vpk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n"
                "alpha,psvitaalive-alpha,ok,old.vpk,,\n"
                "bravo,psvitaalive-bravo,ok,old.vpk,,\n", encoding="utf-8")
            a = app("alpha", "1.0",
                    "https://github.com/example/shared/releases/download/v1.0/alpha.vpk")
            b = app("bravo", "1.0",
                    "https://github.com/example/shared/releases/download/v1.0/bravo.vpk")
            a["source_url"] = b["source_url"] = "https://github.com/example/shared"
            a["links"][1]["url"] = b["links"][1]["url"] = "https://github.com/example/shared"
            (root / "apps/alpha.json").write_text(json.dumps(a))
            (root / "apps/bravo.json").write_text(json.dumps(b))
            queue, reviews = candidates(root)
            self.assertEqual(reviews, [])
            self.assertEqual([x["app_id"] for x in queue], ["alpha", "bravo"])

    def test_newer_catalog_versions_are_prioritized_over_smaller_legacy_vpks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n", encoding="utf-8")
            old = app("old", "1.0",
                      "https://github.com/example/old/releases/download/v1.0/old.vpk")
            new = app("new", "2.0",
                      "https://github.com/example/new/releases/download/v2.0/new.vpk")
            old["icon"] = "https://archive.org/download/old-owned/icon.png"
            old["screenshots"] = ["https://archive.org/download/old-owned/shot.png"]
            old["version_date"], old["size"] = "2019-05-01", 1_000
            new["icon"] = "https://archive.org/download/new-owned/icon.png"
            new["screenshots"] = ["https://archive.org/download/new-owned/shot.png"]
            new["version_date"], new["size"] = "2026-11-23", 5_000_000
            (root / "apps/old.json").write_text(json.dumps(old))
            (root / "apps/new.json").write_text(json.dumps(new))
            queue, reviews = candidates(root)
            self.assertEqual(reviews, [])
            self.assertEqual([x["app_id"] for x in queue], ["new", "old"])

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
        self.assertIn('different or unsupported', reason)

    def test_catalog_media_nominates_existing_item_without_historical_upload_log(self):
        """Covers BT3, BT4 and SpongeBob pattern: icon + screenshots own IA item."""
        names = [
            ("dragon-ball-z-bt3", "dragon-ball-z-budokai-tenkaichi-3-ps-vita"),
            ("dragon-ball-z-bt4", "dbz-bt4-ps-vita"),
            ("spongebob-battle-for-bikini-bottom", "battle-for-bikini-bottom-vita"),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "scripts").mkdir()
            (root / "scripts/archive_upload_log.csv").write_text(
                "app_id,identifier,status,files,error,url\n", encoding="utf-8")
            for index, (app_id, ia_item) in enumerate(names):
                a = app(app_id, "1.3",
                        f"https://github.com/example/{app_id}/releases/download/v1.3/app.vpk")
                # Unlike the short generic fixture helper, actual BT3/BT4
                # have distinct Title IDs; model that invariant here.
                a["title_id"] = f"TEST{index:05d}"
                a["icon"] = f"https://archive.org/download/{ia_item}/icon0.png"
                a["screenshots"] = [
                    f"https://archive.org/download/{ia_item}/shot0.jpg",
                    f"https://archive.org/download/{ia_item}/shot1.png",
                ]
                a["links"].append({
                    "type": "Game Files", "name": "Data",
                    "url": "https://archive.org/download/unrelated-data/GameFiles.zip",
                })
                (root / "apps" / f"{app_id}.json").write_text(json.dumps(a))
                self.assertEqual(
                    resolve_item(a, {}),
                    (ia_item, "catalog-media-candidate"),
                )
            queue, reviews = candidates(root)
            self.assertEqual(reviews, [])
            self.assertEqual({entry["app_id"] for entry in queue}, {n for n, _ in names})
            self.assertEqual({entry["archive_identifier"] for entry in queue},
                             {n for _, n in names})
            self.assertTrue(all(x["kind"] == "CATALOG_BACKFILL" for x in queue))

    def test_conflicting_or_missing_catalog_screenshots_never_infer_destination(self):
        a = app("alpha", "1.3",
                "https://github.com/example/alpha/releases/download/v1.3/app.vpk")
        a["icon"] = "https://archive.org/download/my-item/icon0.png"
        a["screenshots"] = [
            "https://archive.org/download/my-item/shot0.png",
            "https://archive.org/download/other-item/shot1.png",
        ]
        self.assertIsNone(resolve_item(a, {})[0])
        a["screenshots"] = []
        self.assertIsNone(resolve_item(a, {})[0])
        a["screenshots"] = ["https://archive.org/download/my-item/GameFiles.zip"]
        self.assertIsNone(resolve_item(a, {})[0])
        a["screenshots"] = ["https://archive.org.evil.test/download/my-item/shot.png"]
        self.assertIsNone(resolve_item(a, {})[0])

    def test_media_hint_never_overrides_historical_upload_log(self):
        a = app("alpha", "1.3",
                "https://github.com/example/alpha/releases/download/v1.3/app.vpk")
        a["icon"] = "https://archive.org/download/a-different-item/icon0.png"
        a["screenshots"] = ["https://archive.org/download/a-different-item/shot0.png"]
        self.assertEqual(
            resolve_item(a, {"alpha": {"psvitaalive-alpha"}}),
            ("psvitaalive-alpha", "historical-upload-log"),
        )

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
