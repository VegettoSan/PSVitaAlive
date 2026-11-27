import copy
import io
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch
from PIL import Image

from scripts import import_orbit_discoveries as imp
from scripts.catalog_schema_v2 import to_v1, to_v2


def png(color):
    b=io.BytesIO()
    Image.new("RGB",(128,128),color).save(b,"PNG")
    return b.getvalue()


def sfo(tid):
    key=b"TITLE_ID\0\0\0\0"
    value=tid.encode()+b"\0\0\0"
    return struct.pack("<4sIIII",b"\0PSF",0x101,36,36+len(key),1)+struct.pack("<HHIII",0,0x204,10,12,0)+key+value


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.spec={"candidate_key":"gh:123","project":"dev/demo","id":"demo",
                   "title_id":"DEMO00001","tag":"v1.0","status":"Legacy"}
        self.entry={"key":"gh:123","project":"dev/demo","platform":"github"}
        self.asset={"name":"demo.vpk","state":"uploaded","size":123,
                    "browser_download_url":"https://github.com/dev/demo/releases/download/v1.0/demo.vpk"}
        self.release={"draft":False,"prerelease":False,"tag_name":"v1.0",
                      "published_at":"2020-01-01T00:00:00Z","assets":[self.asset]}

    def test_candidate_and_status_policy(self):
        imp.validate_approval(self.spec,self.entry)
        for change in ({"status":"Verified"},{"project":"other/demo"},{"id":"../demo"}):
            with self.assertRaises(ValueError):
                imp.validate_approval({**self.spec,**change},self.entry)

    def test_current_release_requires_exact_unique_official_vpk(self):
        asset,when=imp.select_release(self.spec,self.release)
        self.assertEqual(asset,self.asset)
        self.assertEqual(when,"2020-01-01")
        for change in ({"draft":True},{"prerelease":True},{"tag_name":"v2.0"},
                       {"published_at":"2026-01-01T00:00:00Z"},
                       {"assets":[self.asset,self.asset]},
                       {"assets":[{**self.asset,"browser_download_url":"https://example.org/demo.vpk"}]}):
            with self.assertRaises(ValueError):
                imp.select_release(self.spec,{**self.release,**change})

    def test_verified_vpk_recovers_native_media(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/"demo.vpk"
            with zipfile.ZipFile(path,"w") as z:
                z.writestr("sce_sys/param.sfo",sfo("DEMO00001"))
                z.writestr("eboot.bin",b"synthetic test fixture")
                z.writestr("sce_sys/icon0.png",png("red"))
                z.writestr("sce_sys/pic0.png",png("blue"))
                z.writestr("sce_sys/livearea/contents/bg.png",png("blue"))
            imp.full_package(path,"DEMO00001")
            icon,shots,media=imp.native_media(path,"demo")
            self.assertTrue(icon.startswith(imp.CANONICAL+"apps/media/demo/"))
            self.assertEqual(len(shots),1)
            self.assertEqual(len(media),2)
            with self.assertRaises(Exception):
                imp.full_package(path,"OTHER0001")

    def test_refuse_duplicate_title_and_never_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for d in ("apps","registry"):
                (root/d).mkdir()
            (root/"registry/retired_ids.json").write_text(json.dumps({"apps":{"ids":[],"title_ids":[]},"authors":{"ids":[]}}))
            (root/"apps/other.json").write_text(json.dumps({"id":"other","tid":"DEMO00001"}))
            with self.assertRaisesRegex(ValueError,"Title ID already registered"):
                imp.plan_one(root,self.spec,self.entry)
            (root/"apps/other.json").unlink()
            prior={"id":"demo","title_id":"DEMO00001","source_name":"Orbit","source_id":"gh:123"}
            (root/"apps/demo.json").write_text(json.dumps(to_v2(prior)))
            self.assertEqual(imp.plan_one(root,self.spec,{}),(None,{}))
            self.assertEqual(to_v1(json.loads((root/"apps/demo.json").read_text())),prior)

    def test_license_notice_must_match_reviewed_release(self):
        notice="Copyright Example\nPermission is hereby granted"
        policy={"approved":[{**self.spec,"license":"MIT","license_path":"LICENSE","license_text":notice}]}
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            (root/"config").mkdir()
            (root/imp.POLICY).write_text(json.dumps(policy))
            with patch.object(imp,"public_bytes",return_value=notice.encode()):
                self.assertEqual(imp.reviewed_license(root,"demo","dev/demo","v1.0"),notice)
            with patch.object(imp,"public_bytes",return_value=b"changed"):
                with self.assertRaises(ValueError):
                    imp.reviewed_license(root,"demo","dev/demo","v1.0")
            with self.assertRaises(ValueError):
                imp.reviewed_license(root,"demo","dev/demo","v2.0")


if __name__=="__main__":
    unittest.main()
