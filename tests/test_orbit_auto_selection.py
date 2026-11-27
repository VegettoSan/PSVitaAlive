import copy
import json
import tempfile
import unittest
from pathlib import Path
from scripts import orbit_auto_selection as auto

NOTICE="""MIT License
Copyright (c) 2020 dev
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files, to deal in the Software without restriction.
The above copyright notice and this permission notice shall be included.
THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND."""

class AutomaticTests(unittest.TestCase):
    def setUp(self):
        self.entry={"key":"gh:123","platform":"github","project":"dev/demo","project_type":"homebrew",
                    "author_profile":"https://github.com/dev","app_draft":{"id":"demo","name":"demo","version_date":"2020-01-01","description":"A homebrew demo."}}
        self.result={"state":"inspected","title":{"state":"high","suggestion":"DEMO00001"},
                     "vpk":{"state":"verified","crc_checked":True,"title_id":"DEMO00001","sha256":"a"*64,
                            "bytes_read":100,"artwork":{"icon":{"path":"sce_sys/icon0.png"}}},
                     "category":{"state":"suggested","category_id":"demos","subcategory_ids":["other"]},
                     "author":{"state":"draft"},"requirements":{"state":"no_data","value":"- no data"}}
        self.policy={"excluded_projects":[],"allowed_categories":["demos"],"min_release_age_days":365}
        self.retired={"apps":{"ids":[],"title_ids":[]},"authors":{"ids":[]}}

    def rows(self,entry=None,result=None,existing=(),fingerprint=None):
        entry=entry or self.entry
        state={"schema_version":1,"records":{entry["key"]:{"fingerprint":fingerprint or auto.fingerprint(entry),"result":result or self.result}}}
        return auto.eligible_rows({entry["key"]:entry},state,self.policy,list(existing),self.retired)

    def test_select_without_individual_approval(self):
        self.assertEqual(len(self.rows()),1)

    def test_reject_stale_evidence_title_and_source_collision(self):
        self.assertEqual(self.rows(fingerprint="stale"),[])
        self.assertEqual(self.rows(existing=[{"id":"other","title_id":"DEMO00001"}]),[])
        self.assertEqual(self.rows(existing=[{"id":"other","source_url":"https://github.com/dev/demo"}]),[])

    def test_require_verified_binary_icon_category_and_author(self):
        for field,value in (("title",{"state":"ambiguous"}),("category",{"state":"missing"}),("author",{"state":"review"}),
                            ("vpk",{"state":"verified","title_id":"DEMO00001"})):
            r={**self.result,field:value}
            self.assertEqual(self.rows(result=r),[])

    def test_recent_fork_and_commercial_data_deferred(self):
        for update in ({"lineage_is_fork":True},{"app_draft":{**self.entry["app_draft"],"version_date":"2099-01-01"}},
                       {"app_draft":{**self.entry["app_draft"],"description":"Requires commercial game data"}}):
            self.assertEqual(self.rows(entry={**self.entry,**update}),[])

    def test_license_at_exact_tag(self):
        def api(repo,path):
            self.assertEqual(path,"license?ref=v1")
            return {"license":{"spdx_id":"MIT"},"path":"LICENSE"}
        def raw(url,limit):
            self.assertEqual(url,"https://raw.githubusercontent.com/dev/demo/v1/LICENSE")
            return NOTICE.encode()
        self.assertEqual(auto.mit_notice("dev/demo","v1",api,raw),NOTICE)
        with self.assertRaises(ValueError):
            auto.mit_notice("dev/demo","v1",lambda *a:{"license":{"spdx_id":"NOASSERTION"}},raw)
        with self.assertRaises(ValueError):
            auto.mit_notice("dev/demo","v1",api,lambda *a:(NOTICE+"\nNon-commercial only").encode())

    def test_create_verified_owner_profile_and_reuse(self):
        meta={"fork":False,"owner":{"login":"dev","id":123,"type":"User"}}
        profile={"id":123,"login":"dev","type":"User","html_url":"https://github.com/dev","name":"Dev"}
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            (root/"authors").mkdir()
            reader=lambda *a,**k:json.dumps(profile).encode()
            ids,author=auto.owner_author(root,self.entry,self.result,meta,reader,self.retired,NOTICE)
            self.assertEqual(ids,["dev"])
            self.assertEqual(author["id"],"dev")
            (root/"authors/dev.json").write_text(json.dumps(author))
            self.assertEqual(auto.owner_author(root,self.entry,self.result,meta,reader,self.retired,NOTICE),(["dev"],None))
            with self.assertRaises(ValueError):
                auto.owner_author(root,self.entry,self.result,meta,reader,self.retired,NOTICE.replace("2020 dev","2020 other"))

if __name__=="__main__":
    unittest.main()
