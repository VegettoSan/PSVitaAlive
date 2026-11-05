#!/usr/bin/env python3
"""Validate Game Files installed_paths in canonical apps/*.json.

For current PSVitaAlive Game Files, values are relative to ux0:data/,
not to links[].extract_path. App folder ZIPs do not use installed_paths.
No network access, deletion, or generated catalog changes.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Relative components only, without traversal, mounts or control characters.
def valid_path(value):
    if not isinstance(value, str) or not value or value.startswith("/"):
        return False
    parts = value.split("/")
    return all(part not in ("", ".", "..") and ":" not in part and "\\\\" not in part and all(ord(ch) >= 32 for ch in part) for part in parts)

def main():
    errors=[]
    external_apps=app_apps=ext_links=app_links=0
    shared={}
    drifter=False
    for file in sorted((ROOT/"apps").glob("*.json")):
        app=json.loads(file.read_text(encoding="utf-8"))
        game=[l for l in app.get("links",[]) if l.get("type")=="Game Files"]
        if not game:continue
        types=set()
        for i,link in enumerate(game):
            dest=str(link.get("extract_path",""))
            paths=link.get("installed_paths",None)
            location=f"{file.name} links[{i}]"
            if dest.startswith("ux0:data/"):
                ext_links+=1;types.add("data")
                if not isinstance(paths,list) or not paths or not all(valid_path(x) for x in paths):
                    errors.append(f"{location}: invalid/missing installed_paths in data")
                    continue
                if len(paths)!=len(set(paths)):errors.append(f"{location}: duplicate installed paths")
                # The catalog records the complete data root. If a ZIP extracts
                # deeper, that extract destination must still lie inside its root.
                deeper=dest[len("ux0:data/"):].strip("/")
                if deeper and not any(deeper==x or deeper.startswith(x+"/") for x in paths):
                    errors.append(f"{location}: extraction destination outside declared data root")
                for x in paths:shared.setdefault(x,[]).append(app["id"])
            elif dest in ("ux0:app/",f'ux0:app/{app["title_id"]}/'):
                app_links+=1;types.add("app")
                if paths is not None:errors.append(f"{location}: app Game Files must omit installed_paths")
            else:errors.append(f"{location}: unknown Game Files destination {dest!r}")
        if types=={"data"}:external_apps+=1
        elif types=={"app"}:app_apps+=1
        else:errors.append(f"{file.name}: mixes Game Files data and app destinations")
        if app["id"]=="the-drifter":
            drifter=len(game)==1 and game[0].get("extract_path")=="ux0:app/" and "installed_paths" not in game[0]
    if not drifter:errors.append("The Drifter must extract ZIP title-id root under ux0:app/")
    print(f"Game Files: data apps={external_apps}, app apps={app_apps}, data links={ext_links}, app links={app_links}")
    print("Shared roots:",{k:sorted(set(v)) for k,v in shared.items() if len(set(v))>1})
    if external_apps!=150 or app_apps!=8:
        errors.append("expected 150 data applications and 8 app applications from current audit")
    if errors:
        for message in errors:print("ERROR:",message,file=sys.stderr)
        return 1
    print("PASS: all current Game Files destinations and installed_paths are consistent")
    return 0

if __name__=="__main__":sys.exit(main())
