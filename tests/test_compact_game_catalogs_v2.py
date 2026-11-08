"""Regression tests for repeated V2 game-catalog publication.

The public games JSONs are already compact. The production generator must
accept V1, V2, and mixed inputs without modifying any game metadata.
"""
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts import compact_game_catalogs_v2 as game_v2
from scripts.catalog_schema_v2 import to_v1, to_v2


class GameCatalogCompactionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.catalogs = game_v2.GAMES
        self.sample = {
            "id": "demo-game",
            "title_ids": {"US": "PCSE00001", "ASIA": "PCSH00002"},
            "name": "Demo & Test",
            "description": "Unchanged details",
            "category_id": "action",
            "subcategory_ids": ["platformer"],
            "version": "1.2",
            "version_date": "2026-11-08",
            "requirements": "vita",
            "size": 1234,
            "cover": "https://example.org/cover.jpg",
            "icon": "https://example.org/icon.png",
            "screenshots": ["https://example.org/shot.jpg"],
            "links": [{
                "type": "PKG", "name": "Install", "region": "US",
                "title_id": "PCSE00001", "url": "https://example.org/a.pkg",
                "size": 1000, "recommended": False,
                "content_id": "UP0000-PCSE00001_00-EXAMPLE00000001",
                "version": "1.2", "required_fw": "3.65",
                "pkg_name": "demo.pkg", "rap": "nonchanging-value",
            }],
        }
        for name in self.catalogs:
            self.write(name, [self.sample])
        self.root_patch = patch.object(game_v2, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def write(self, name, value):
        (self.root / name).write_text(
            json.dumps(value, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    def build(self, dirname):
        out = self.root / dirname
        stats = game_v2.build(out, self.root / f"{dirname}-report.json")
        self.assertTrue(stats["passed"])
        return stats, {n: (out / n).read_bytes() for n in self.catalogs}

    def test_v1_then_v2_twice_is_idempotent(self):
        first, result = self.build("stage1")
        self.assertGreater(first["bytes_saved"], 0)
        for name, raw in result.items():
            self.assertEqual(to_v1(json.loads(raw)), [self.sample])
            self.assertIn("tids", json.loads(raw)[0])
            self.assertNotIn("title_ids", json.loads(raw)[0])
            (self.root / name).write_bytes(raw)
        second, result2 = self.build("stage2")
        self.assertEqual(second["bytes_saved"], 0)
        self.assertEqual(result, result2)
        third, result3 = self.build("stage3")
        self.assertEqual(result, result3)
        self.assertEqual(third["bytes_saved"], 0)

    def test_mixed_v1_and_v2_sources_are_equivalent(self):
        _, result = self.build("mixed-original")
        self.write(self.catalogs[0], [self.sample])
        (self.root / self.catalogs[1]).write_bytes(result[self.catalogs[1]])
        (self.root / self.catalogs[2]).write_bytes(result[self.catalogs[2]])
        _, mixed = self.build("mixed-output")
        self.assertEqual(mixed, result)
        for name in self.catalogs:
            self.assertEqual(to_v1(json.loads(mixed[name])), [self.sample])

    def test_rejects_unknown_keys_in_v1_and_v2(self):
        for compact in (False, True):
            with self.subTest(compact=compact):
                row = dict(self.sample)
                row["unexpected_field"] = "retain safely"
                self.write(self.catalogs[0], [to_v2(row) if compact else row])
                with self.assertRaisesRegex(RuntimeError, "Game V2 audit failed"):
                    game_v2.build(None, None)

    def test_duplicate_ids_are_rejected(self):
        self.write(self.catalogs[0], [self.sample, self.sample])
        with self.assertRaisesRegex(RuntimeError, "Game V2 audit failed"):
            game_v2.build(None, None)

    def test_does_not_modify_input_or_license_sidecar(self):
        sidecar = self.root / "catalog_psvita_games.zrifidx"
        sidecar.write_bytes(b"DO NOT MODIFY LICENSES")
        before = {name: (self.root / name).read_bytes() for name in self.catalogs}
        self.build("safe-output")
        self.assertEqual(
            before, {name: (self.root / name).read_bytes() for name in self.catalogs}
        )
        self.assertEqual(sidecar.read_bytes(), b"DO NOT MODIFY LICENSES")


if __name__ == "__main__":
    unittest.main()
