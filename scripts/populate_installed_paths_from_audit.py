#!/usr/bin/env python3
"""Populate *source* apps/*.json Game Files links from audited top-level data roots.

Does not touch Data Files or generated catalog.json.
Only The Drifter is approved for extract_path reassignment to ux0:app/.
Run only with a reviewed audit report. External paths are relative to ux0:data/.
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
 "wonder-boy-the-dragon-s-trap":"wonderboy",
}
# Reviewed multi-root archives: track the main game directory, never claim a
# global-looking shader_cache directory is uniquely owned by this game.
MAIN_ROOT_OVERRIDES={
    "batman-the-dark-knight-rises":"kras",
    "nba-2k16":"nba2k16",
}
# Maintainer confirmed that Title ID-shaped roots for these titles live in
# ux0:data/, not ux0:app/. Other unknown Title ID matches still stop.
APPROVED_DATA_TITLE_ID_ROOTS={
    "call-of-duty-4-modern-warfare-multiplayer",
    "resident-evil-4-gc",
    "crazy-taxi-vita",
    "dead-space-vita",
    "paperboat",
    "spaghetti-kart-vita",
    "ultimate-spiderman-total-mayhem",
}
APP_ROOT_OVERRIDE="the-drifter"
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
        if not row:
            skipped.append((id,"missing audit row"));continue
        if id==APP_ROOT_OVERRIDE:
            if row["status"]!="zip_verified" or row["proposed_paths"]!=[app["title_id"]]:
                raise RuntimeError("The Drifter ZIP no longer matches the application Title ID")
            for link in game_links:
                if link.get("extract_path") not in ("ux0:data/","ux0:app/"):
                    raise RuntimeError("Unexpected The Drifter extract_path")
                if "installed_paths" in link:
                    raise RuntimeError("The Drifter must not set installed_paths for its app path")
                link["extract_path"]="ux0:app/"
            path.write_text(json.dumps(app,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
            touched+=1
            continue
        if id in MAIN_ROOT_OVERRIDES:
            proposed=[MAIN_ROOT_OVERRIDES[id]]
        elif row["status"] in ("zip_verified","from_extract_path"):
            proposed=row["proposed_paths"]
        else:
            proposed=[DOCUMENTED[id]] if id in DOCUMENTED else []
        if len(proposed)!=1 or not VALID.fullmatch(proposed[0]) or proposed[0] in (".",".."):
            skipped.append((id,"unverified/unsafe folder name"));continue
        if proposed[0].casefold()==str(app["title_id"]).casefold() and id not in APPROVED_DATA_TITLE_ID_ROOTS:
            skipped.append((id,"unapproved Title ID-shaped data root"));continue
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
