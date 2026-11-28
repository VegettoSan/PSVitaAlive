import io
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from scripts import audit_zip_vpks as a

TID = "STFX00001"


def vpk(tid=TID):
    keys, value = b"TITLE_ID\x00", tid.encode() + b"\x00"
    sfo = struct.pack("<4sIIII", b"\x00PSF", 0x101, 36, 36 + len(keys), 1)
    sfo += struct.pack("<HHIII", 0, 0x0204, len(value), len(value), 0) + keys + value
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        z.writestr("sce_sys/param.sfo", sfo)
        z.writestr("eboot.bin", b"binary")
    return out.getvalue()


def app():
    return {"id": "star-fox-adventures", "title_id": TID, "name": "Star Fox Adventures",
            "status": "Verified", "version": "0.1.3", "version_date": "2026-11-05",
            "links": [{"type": "Repository", "url": "https://github.com/tchisholm20/starfox-adventures-vita"},
                      {"type": "Download", "name": "VPK", "url": "https://archive.org/download/starfox/old.vpk"}]}


def release_asset():
    return {"repo": "tchisholm20/starfox-adventures-vita", "tag": "v0.2.0", "release_id": 445102222,
            "asset_id": 746659833, "updated_at": "2026-11-14T07:13:49Z", "name": "StarFox-Vita.zip",
            "url": "https://github.com/tchisholm20/starfox-adventures-vita/releases/download/v0.2.0/StarFox-Vita.zip",
            "size": 1024, "digest": "sha256:" + "a"*64}


def snapshot():
    return {"schema_version": 1, "releases": [{"platform": "github", "repo": release_asset()["repo"],
            "tag": "v0.2.0", "release_id": "445102222", "published": "2026-11-14T07:13:50Z",
            "prerelease": False, "assets": [{"name": "StarFox-Vita.zip", "download_url": release_asset()["url"]}]}]}


class ZipAuditTests(unittest.TestCase):
    def inspect(self, files):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "release.zip"
            with zipfile.ZipFile(p, "w") as z:
                for name, data in files:
                    z.writestr(name, data)
            return a.inspect_zip(p, TID)

    def test_nested_vpk_is_identified_and_hashed(self):
        result = self.inspect([("README.txt", b"instructions"), ("Vita/build/game.vpk", vpk())])
        self.assertEqual(result["status"], "VPK_FOUND")
        self.assertEqual(result["matches"][0]["entry"], "Vita/build/game.vpk")
        self.assertEqual(result["matches"][0]["title_id"], TID)
        self.assertEqual(len(result["matches"][0]["sha256"]), 64)

    def test_negative_and_ambiguous_results(self):
        self.assertEqual(self.inspect([("readme.txt", b"not vpk")])["status"], "NO_VPK")
        self.assertEqual(self.inspect([("fake.vpk", b"invalid")])["status"], "NO_VALID_VPK")
        self.assertEqual(self.inspect([("wrong.vpk", vpk("OTHER0001"))])["status"], "NO_VALID_VPK")
        self.assertEqual(self.inspect([("a.vpk", vpk()), ("b.vpk", vpk())])["status"], "AMBIGUOUS_VPK")
        self.assertEqual(self.inspect([("../escape.vpk", vpk())])["status"], "PACKAGE_REVIEW")
        self.assertEqual(self.inspect([("A.vpk", vpk()), ("a.vpk", vpk())])["status"], "PACKAGE_REVIEW")

    def test_cache_identity_reopens_same_tag_replacements(self):
        asset = release_asset()
        key = a.cache_key(asset, app())
        for field, value in [("asset_id", 42), ("release_id", 42), ("tag", "v0.3.0"),
                             ("size", 1025), ("digest", "sha256:" + "b"*64),
                             ("updated_at", "2026-11-15T07:13:49Z")]:
            self.assertNotEqual(a.cache_key(dict(asset, **{field: value}), app()), key)
        self.assertNotEqual(a.cache_key(asset, dict(app(), title_id="OTHER0001")), key)

    def test_direct_vpk_precedes_zip(self):
        info = {"id": 445102222, "tag_name": "v0.2.0", "draft": False, "prerelease": False,
                "assets": [{"name": "game.vpk", "state": "uploaded"}]}
        get = lambda *args, **kw: SimpleNamespace(status_code=200, json=lambda: info)
        self.assertTrue(a.release_assets(release_asset()["repo"], "v0.2.0", "445102222", get=get)["direct"])

    def test_scan_caches_negatives_and_retries_transient_failures(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            (root / "apps/star-fox-adventures.json").write_text(json.dumps(app()))
            asset = release_asset()
            metadata = lambda *args: {"direct": False, "prerelease": False, "assets": [dict(asset)]}
            cache = a.load_cache(root)
            with patch.object(a, "inspect_asset", return_value={"status": "SOURCE_RETRY", "matches": []}) as fetch:
                report = a.scan(root, snapshot(), cache, metadata=metadata)
                self.assertEqual(report["downloads"], 1)
                self.assertFalse(cache["entries"])
                fetch.assert_called_once()
            with patch.object(a, "inspect_asset", return_value={"status": "NO_VPK", "matches": []}) as fetch:
                self.assertEqual(a.scan(root, snapshot(), cache, metadata=metadata)["downloads"], 1)
                self.assertEqual(a.scan(root, snapshot(), cache, metadata=metadata)["downloads"], 0)
                asset["updated_at"] = "2026-11-15T07:13:49Z"
                self.assertEqual(a.scan(root, snapshot(), cache, metadata=metadata)["downloads"], 1)
                self.assertEqual(fetch.call_count, 2)
            self.assertEqual(len(cache["entries"]), 2)
            self.assertEqual(json.loads((root / "apps/star-fox-adventures.json").read_text()), app())

    def test_pinned_missing_download_targets_and_shared_repo(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "apps").mkdir()
            x = app()
            x["links"] = [x["links"][0], {"type": "Official Website",
                       "url": "https://github.com/tchisholm20/starfox-adventures-vita/releases/tag/v0.1.3"}]
            (root / "apps/star-fox-adventures.json").write_text(json.dumps(x))
            targets = a.collect_targets(root, {"schema_version": 1, "releases": []})
            self.assertEqual(targets[0][2], "v0.1.3")
            (root / "apps/other.json").write_text(json.dumps(dict(x, id="other", title_id="OTHER0001")))
            self.assertFalse(a.collect_targets(root, {"schema_version": 1, "releases": []}))

    def test_rounds_prioritize_updates_and_do_not_starve_other_apps(self):
        star = app()
        old = dict(app(), id="old-app", title_id="OTHER0001", links=[])
        another = dict(app(), id="third-app", title_id="OTHER0002", links=[])
        base_asset = release_asset()
        def assets(prefix, count):
            return [dict(base_asset, asset_id=i + 1, name=f"{prefix}-{i}.zip", size=100)
                    for i in range(count)]
        mapping = {"old": assets("linux", 4), "star": assets("StarFox-Vita", 2),
                   "third": assets("other", 2), "old2": assets("other", 2)}
        targets = [(old, "a/b", "old", None, False),
                   (old, "a/b", "old2", None, False),
                   (another, "a/c", "third", None, False),
                   (star, "a/d", "v0.2.0", None, False)]
        # The fake metadata supplies distinct URLs/asset IDs by release.
        def metadata(repo, tag, rid):
            key = "star" if tag == "v0.2.0" else tag
            return {"direct": False, "prerelease": False,
                    "assets": [dict(x, tag=tag) for x in mapping[key]]}
        order = []
        def inspect(asset, application, **kw):
            order.append(application["id"])
            return {"status": "NO_VPK", "matches": []}
        with tempfile.TemporaryDirectory() as tmp, patch.object(a, "collect_targets", return_value=targets), \
                patch.object(a, "MAX_DOWNLOADS", 6):
            cache = a.load_cache(Path(tmp))
            report = a.scan(Path(tmp), {}, cache, metadata=metadata, inspector=inspect)
            self.assertEqual(order[:3], ["star-fox-adventures", "old-app", "third-app"])
            self.assertEqual(len(set(order[:3])), 3)
            self.assertEqual(report["downloads"], 6)
            self.assertEqual(report["rounds"], 2)
            self.assertEqual(report["deferred_reasons"]["DOWNLOAD_COUNT"], 4)
            self.assertEqual(len(cache["entries"]), 6)
            again = a.scan(Path(tmp), {}, cache, metadata=metadata, inspector=inspect)
            self.assertEqual(again["downloads"], 4)
            self.assertEqual(again["cache_hits"], 6)

    def test_byte_budget_skips_large_candidate_and_continues(self):
        star = app()
        small = dict(app(), id="small", title_id="OTHER0001", links=[])
        targets = [(star, "a/b", "v0.2.0", None, False),
                   (small, "a/c", "v1", None, False)]
        large = dict(release_asset(), name="Vita-large.zip", size=101)
        tiny = dict(release_asset(), asset_id=2, name="Vita-small.zip", size=40)
        def metadata(repo, tag, rid):
            return {"direct": False, "prerelease": False,
                    "assets": [large if repo == "a/b" else tiny]}
        with tempfile.TemporaryDirectory() as tmp, patch.object(a, "collect_targets", return_value=targets), \
                patch.object(a, "MAX_BATCH", 100), \
                patch.object(a, "inspect_asset", return_value={"status": "NO_VPK", "matches": []}) as inspect:
            report = a.scan(Path(tmp), {}, a.load_cache(Path(tmp)), metadata=metadata)
            self.assertEqual(report["downloads"], 1)
            self.assertEqual(report["download_budget_bytes"], 40)
            self.assertEqual(report["deferred_reasons"], {"BYTE_BUDGET": 1})
            self.assertEqual(inspect.call_args.args[0], tiny)

    def test_integrity_mismatch_not_cached(self):
        asset = release_asset()
        with patch.object(a, "inspect_zip") as inspect:
            def download(url, path, maximum, **kwargs):
                path.write_bytes(b"x")
                return asset["size"], "m", "b"*64
            result = a.inspect_asset(asset, app(), deadline=999999999, download=download)
            self.assertEqual(result["status"], "SOURCE_INTEGRITY_RETRY")
            self.assertNotIn(result["status"], a.TERMINAL)
            inspect.assert_not_called()


if __name__ == "__main__":
    unittest.main()
