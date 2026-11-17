#!/usr/bin/env python3
"""Offline regressions for the contributed PSVitaAlive PC client."""
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication

from scripts.catalog_schema_v2 import to_v1, to_v2
from local_client.download_policy import file_identity, safe_component, safe_filename
from local_client import local_psvitaalive_client as client


class FakeResponse:
    def __init__(self, payload=None, status_code=200):
        self.payload = payload
        self.status_code = status_code
        self.headers = {"Content-Length": "10"}

    def raise_for_status(self):
        return None

    def json(self):
        return self.payload

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False


class FakeSession:
    def __init__(self, payloads):
        self.payloads = payloads

    def get(self, url, **kwargs):
        if url in self.payloads:
            return FakeResponse(self.payloads[url])
        return FakeResponse(status_code=206)


def homebrew():
    return {
        "id": "test-homebrew", "title_id": "TEST00001",
        "name": "Test Homebrew", "description": "Portable app",
        "long_description": "Details", "author_ids": ["dev"],
        "category_id": "ports", "subcategory_ids": ["racing"],
        "version": "1.2", "version_date": "2026-11-16",
        "requirements": "", "size": 123, "status": "Verified",
        "icon": "https://example.org/icon.png",
        "screenshots": ["https://example.org/shot.jpg"],
        "links": [
            {"type": "Download", "name": "VPK", "url": "https://one.test/file.vpk",
             "size": 123, "recommended": True}
        ],
    }


def game():
    return {
        "id": "sample-game", "name": "Example Game",
        "title_ids": {"US": "PCSE00001", "EU": "PCSB00001"},
        "category_id": "games", "subcategory_ids": ["action"],
        "version": "1.0",
        "links": [{"type": "Download", "name": "US PKG",
                   "url": "https://one.test/file.pkg", "size": 1000,
                   "region": "US", "content_id": "UP0001-TEST00001_00-EXAMPLE123456789"}],
    }


class CodecTests(unittest.TestCase):
    def test_v1_v2_roundtrips_homebrew_and_games(self):
        for value in (homebrew(), game(),
                      [{"id": "dev", "name": "Developer", "avatar": "", "links": []}],
                      [{"id": "ports", "name": "Game Ports", "subcategories": [
                          {"id": "racing", "name": "Racing"}]}]):
            self.assertEqual(to_v1(to_v2(value)), value)

    def test_loader_decodes_all_six_catalogs(self):
        files = {
            "catalog.json": [homebrew()],
            "catalog_psvita_games.json": [game()],
            "catalog_psp_games.json": [game()],
            "catalog_ps1_games.json": [game()],
            "authors.json": [{"id": "dev", "name": "Developer", "avatar": "", "links": []}],
            "categories.json": [{"id": "ports", "name": "Game Ports",
                                 "subcategories": [{"id": "racing", "name": "Racing"}]}],
        }
        compact_payloads = {f"{client.RAW_BASE}/{name}": to_v2(value)
                            for name, value in files.items()}
        session = FakeSession(compact_payloads)
        self.assertEqual(client._get_json(session, "catalog.json"), files["catalog.json"])
        received = []
        with patch.object(client, "make_session", return_value=session):
            loader = client.CatalogLoader()
            loader.loaded.connect(received.append)
            loader.run()
        self.assertEqual(len(received), 1)
        entries = received[0]["entries"]
        self.assertEqual(len(entries), 4)
        self.assertEqual(entries[0].name, "Test Homebrew")
        self.assertEqual(entries[0].author, "Developer")
        self.assertEqual(entries[0].category, "Game Ports")
        self.assertEqual(entries[0].subcategories, "Racing")
        self.assertEqual(entries[0].selected_links()[0]["name"], "VPK")
        self.assertEqual(entries[1].name, "Example Game")
        self.assertEqual(entries[1].selected_links()[0]["region"], "US")

    def test_old_v1_still_supported(self):
        data = [homebrew()]
        session = FakeSession({f"{client.RAW_BASE}/catalog.json": data})
        self.assertEqual(client._get_json(session, "catalog.json"), data)


class DownloadPolicyTests(unittest.TestCase):
    def test_unknown_identity_never_collapses_same_size(self):
        links = [
            {"type": "Download", "url": "https://a.test/alpha.vpk", "size": 100},
            {"type": "Download", "url": "https://b.test/beta.vpk", "size": 100},
        ]
        self.assertNotEqual(file_identity(links[0]), file_identity(links[1]))
        entry = client.CatalogEntry(dict(homebrew(), links=links), "Homebrew")
        self.assertEqual(len(entry.selected_links(True)), 2)

    def test_equal_hash_prefers_recommended_source(self):
        links = [
            {"type": "Download", "url": "https://a.test/alpha.vpk",
             "size": 100, "hash": "abcd"},
            {"type": "Download", "url": "https://b.test/beta.vpk",
             "size": 100, "hash": "abcd", "recommended": True},
        ]
        entry = client.CatalogEntry(dict(homebrew(), links=links), "Homebrew")
        self.assertEqual(entry.selected_links(True), [links[1]])
        self.assertEqual(len(entry.selected_links(False)), 2)

    def test_content_id_mirrors_and_distinct_regions(self):
        one = {"type": "Download", "content_id": "cid", "size": 100,
               "region": "US", "url": "https://a.test/game.pkg"}
        copy = dict(one, url="https://b.test/game.pkg")
        europe = dict(one, region="EU", url="https://c.test/game.pkg")
        self.assertEqual(file_identity(one), file_identity(copy))
        self.assertNotEqual(file_identity(one), file_identity(europe))

    def test_names_cannot_escape_download_directory(self):
        for value in ("..", ".", "../escape", "CON.txt", ""):
            part = safe_component(value)
            self.assertNotIn("/", part)
            self.assertNotIn("\\", part)
            self.assertNotIn(part, {".", "..", ""})
        self.assertEqual(safe_filename("https://example.org/a/file.vpk"), "file.vpk")
        self.assertEqual(safe_filename("https://example.org/.."), "download.bin")

    def test_same_basename_different_urls_do_not_overwrite(self):
        if QApplication.instance() is None:
            self.app = QApplication([])
        with patch.object(client.MainWindow, "_load_catalogs"):
            window = client.MainWindow()
        try:
            first = dict(homebrew(), links=[
                {"type": "Download", "url": "https://a.test/file.vpk", "size": 100},
                {"type": "Download", "url": "https://b.test/file.vpk", "size": 100},
            ])
            other = dict(homebrew(), id="other-homebrew", links=[
                {"type": "Download", "url": "https://a.test/file.vpk", "size": 100},
            ])
            tasks = window._build_tasks(
                [client.CatalogEntry(first, "Homebrew"),
                 client.CatalogEntry(other, "Homebrew")], True)
            self.assertEqual(len(tasks), 3)
            self.assertEqual(len({t["dest"].casefold() for t in tasks}), 3)
        finally:
            window.close()

    def test_partial_206_rejected(self):
        worker = client.DownloadWorker([])
        with tempfile.TemporaryDirectory() as directory:
            status, reason = worker._fetch(FakeSession({}), "https://a.test/file",
                                           Path(directory) / "file.part", 0,
                                           "file", False)
            self.assertEqual(status, client.FAILED)
            self.assertIn("206", reason)


if __name__ == "__main__":
    unittest.main()
