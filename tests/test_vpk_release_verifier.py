#!/usr/bin/env python3
"""Offline binary VPK verification tests: no GitHub downloads are performed."""
import io
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from scripts.vpk_release_verifier import (
    MAX_VPK_BYTES, inspect_vpk, parse_sfo_title_id,
    release_asset_url, verify_release_vpk,
)

REPO = "example/vita-app"
TAG = "v1.1"
URL = "https://github.com/example/vita-app/releases/download/v1.1/vita.vpk"
TITLE = "VITA00001"


def sfo(title=TITLE):
    keys = b"TITLE_ID\x00"
    data = title.encode("ascii") + b"\x00"
    table = 20 + 16
    header = struct.pack("<4sIIII", b"\x00PSF", 0x101, table, table + len(keys), 1)
    entry = struct.pack("<HHIII", 0, 0x204, len(data), len(data), 0)
    return header + entry + keys + data


def make_vpk(title=TITLE, *, param=True):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as arc:
        arc.writestr("sce_sys/param.sfo" if param else "README.txt", sfo(title))
        arc.writestr("eboot.bin", b"homebrew-test-payload")
    return output.getvalue()


class FakeResponse(io.BytesIO):
    def __init__(self, content):
        super().__init__(content)
        self.headers = {"Content-Encoding": "identity"}

    def geturl(self):
        return "https://release-assets.githubusercontent.com/123456/test"


class FakeOpener:
    def __init__(self, content):
        self.content = content

    def open(self, request, timeout):
        return FakeResponse(self.content)


class VPKVerifierTests(unittest.TestCase):
    def test_exact_release_url_extension_and_tag(self):
        self.assertTrue(release_asset_url(URL, REPO, TAG))
        self.assertFalse(release_asset_url(URL.replace("v1.1", "v1.0"), REPO, TAG))
        self.assertFalse(release_asset_url(URL.replace(".vpk", ".zip"), REPO, TAG))
        self.assertFalse(release_asset_url(URL.replace("example/vita-app", "other/repo"), REPO, TAG))
        self.assertFalse(release_asset_url("https://evil.test/release.vpk", REPO, TAG))

    def test_binary_sfo_title_id_parser(self):
        self.assertEqual(parse_sfo_title_id(sfo()), TITLE)
        with self.assertRaises(ValueError):
            parse_sfo_title_id(b"not a param.sfo")

    def test_vpk_zip_requires_correct_title_id(self):
        payload = make_vpk()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.vpk"
            path.write_bytes(payload)
            self.assertEqual(inspect_vpk(path, TITLE), TITLE)
            with self.assertRaisesRegex(ValueError, "differs from catalog"):
                inspect_vpk(path, "VITA00002")
            path.write_bytes(make_vpk(param=False))
            with self.assertRaisesRegex(ValueError, "exactly one"):
                inspect_vpk(path, TITLE)
            path.write_bytes(b"<html>not a VPK</html>")
            with self.assertRaises(ValueError):
                inspect_vpk(path, TITLE)

    def test_downloads_stream_and_validates_before_accepting(self):
        payload = make_vpk()
        with patch("scripts.vpk_release_verifier.urllib.request.build_opener",
                   return_value=FakeOpener(payload)):
            ok, message = verify_release_vpk(
                URL, repo=REPO, tag=TAG,
                expected_title_id=TITLE, expected_size=len(payload),
            )
        self.assertTrue(ok, message)
        self.assertIn("sha256=", message)

    def test_rejects_binary_mismatch_and_size_mismatch(self):
        payload = make_vpk("VITA00002")
        with patch("scripts.vpk_release_verifier.urllib.request.build_opener",
                   return_value=FakeOpener(payload)):
            ok, message = verify_release_vpk(
                URL, repo=REPO, tag=TAG,
                expected_title_id=TITLE, expected_size=len(payload),
            )
        self.assertFalse(ok)
        self.assertIn("TITLE_ID", message)

        with patch("scripts.vpk_release_verifier.urllib.request.build_opener",
                   return_value=FakeOpener(payload)):
            ok, message = verify_release_vpk(
                URL, repo=REPO, tag=TAG,
                expected_title_id=TITLE, expected_size=len(payload) + 10,
            )
        self.assertFalse(ok)
        self.assertIn("size mismatch", message)

    def test_download_limit_and_no_untrusted_sources(self):
        ok, _ = verify_release_vpk(
            URL, repo=REPO, tag=TAG, expected_title_id=TITLE,
            expected_size=MAX_VPK_BYTES + 1,
        )
        self.assertFalse(ok)
        ok, _ = verify_release_vpk(
            "https://somehost.test/file.vpk", repo=REPO, tag=TAG,
            expected_title_id=TITLE, expected_size=100,
        )
        self.assertFalse(ok)


if __name__ == "__main__":
    unittest.main()
