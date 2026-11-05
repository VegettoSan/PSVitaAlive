#!/usr/bin/env python3
"""Populate *source* apps/*.json Game Files links from audited top-level data roots.

Does not touch Data Files, extract_path, or generated catalog.json.
Run only with a reviewed audit report. All paths are relative to ux0:data/.
"""
import json
import re
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
AUDIT=ROOT/"docs/audits/game_files_zip_roots_20261105.json"
# Fallbacks are individually documented upstream but their ZIP index was unavailable.
DOCUMENTED={
 "batman-arkham-origins":"batmanarkhamorigins",
 "castle-of-illusion":"castleofillusion",
 "conker-s-bad-fur-day":"conker",
 "ff3-vita":"ff3",
 "ffd-vita":"ffd",
 "lighthouse-vita":"lighthouse",
 "paper-mario-vita":"papership",
 "real-racing-3":"rr3",
 "tomb-raider-1-2-classic-collection":"tombraider",
}
# Shared, unexpected independent roots: do NOT silently claim one folder covers the ZIP.
SHARED_ROOTS={"batman-the-dark-knight-rises","nba-2k16"}
# Filename matching app title ID suggests assets may actually be intended for ux0:app/.
# Their current metadata says ux0:data/: stop instead of silently changing extract_path.
TITLE_ID_REVIEW={"call-of-duty-4-modern-warfare-multiplayer",
                 "resident-evil-4-gc","the-drifter"}
VALID=re.compile(r"^[\w .()\-]+$", re.UNICODE)
def main():
    raw=json.loads(AUDIT.read_text(encoding="utf-8"))
    rows={x["id"]:x for x in raw["results"]}
    touched=0;links_done=0;skipped=[]
    for path in sorted((ROOT/"apps").glob("*.json")):
        app=json.loads(path.read_text(encoding="utf-8"))
        game_links=[link for link in app.get("links",[]) if link.get("type")=="Game Files"]
        if not game_links:continue
        if all(str(link.get("extract_path","")).startswith("ux0:app/") for link in game_links):
            continue
        id=app["id"]
        row=rows.get(id)
        if id in SHARED_ROOTS or id in TITLE_ID_REVIEW:
            skipped.append((id,"manual review: shared root or title-id folder"));continue
        if not row:
            skipped.append((id,"missing audit row"));continue
        if row["status"] in ("zip_verified","from_extract_path"):
            proposed=row["proposed_paths"]
        else:
            proposed=[DOCUMENTED[id]] if id in DOCUMENTED else []
        if len(proposed)!=1 or not VALID.fullmatch(proposed[0]) or proposed[0] in (".",".."):
            skipped.append((id,"unverified/unsafe folder name"));continue
        if proposed[0].casefold()==str(app["title_id"]).casefold():
            skipped.append((id,"root equals Title ID: check ux0:app before editing"));continue
        root=proposed[0]
        changed=False
        for link in game_links:
            base=str(link.get("extract_path","")).replace("\\","/")
            if not base.startswith("ux0:data/"):
                skipped.append((id,"non-data extract_path:"+base));continue
            # IMPORTANT: these paths are relative to ux0:data/, NOT link.extract_path.
            if "installed_paths" not in link:
                link["installed_paths"]=[root]
                links_done+=1
                changed=True
            elif link["installed_paths"]!=[root]:
                skipped.append((id,"already has conflicting installed_paths"))
        if changed:
            path.write_text(json.dumps(app,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
            touched+=1
    print(f"UPDATED apps={touched} Game Files links={links_done}")
    print(f"SKIPPED {len(skipped)} apps:")
    for x in skipped:print(" -",*x)
if __name__=="__main__":main()
