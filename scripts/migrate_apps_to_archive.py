#!/usr/bin/env python3
"""One-shot migration: point homebrew apps at Archive.org mirrors; strip VitaDB/Neo/Too links."""
import csv, json, re, urllib.parse, sys
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[1]
LOG = Path(__file__).resolve().parent / "archive_upload_log.csv"
APPS = ROOT / "apps"

def norm(s):
    return (s or "").lower()

def is_mediafire(url):
    return "mediafire.com" in norm(url)

def is_vitadb_cdn(url):
    u = norm(url)
    if is_mediafire(url):
        return False
    bad = [
        "rinnegatamante.eu/vitadb",
        "get_hb_url.php",
        "raw.githubusercontent.com/drdecki/vitadbtoo",
        "raw.githubusercontent.com/drdecki/vitahomebrewdb",
        "github.com/drdecki/vitadbtoo-db",
        "github.com/drdecki/vitahomebrewdb",
        "raw.githubusercontent.com/robin994/neovitadb",
        "github.com/robin994/neovitadb",
        "raw.githubusercontent.com/robin994/neovitadb-catalog",
    ]
    return any(b in u for b in bad)

def name_banned(name):
    n = norm(name)
    return any(x in n for x in ["vitadb", "neovitadb", "vitahomebrewdb"])

def is_game_files_type(t, name):
    t, n = norm(t), norm(name)
    return "game files" in t or "game data" in t or "game files" in n or "game data" in n

def is_download_type(t):
    return norm(t) in ("download", "downloads", "vpk")

def is_repo_or_web_type(t, name):
    t, n = norm(t), norm(name)
    if t in ("repository", "official website", "website", "source", "homepage"):
        return True
    return any(x in n for x in ["repositorio", "repository", "release page", "website", "homepage"])

def is_mod_type(t):
    return norm(t) in ("mod", "mods", "mod pack", "patch", "patches")

def archive_download(identifier, filename):
    return f"https://archive.org/download/{identifier}/{urllib.parse.quote(filename)}"

def pick_vpk(files):
    vpks = [f for f in files if f.lower().endswith(".vpk")]
    if not vpks:
        return None
    for f in vpks:
        if "stub" not in f.lower():
            return f
    return vpks[0]

def pick_icon(files):
    for cand in ("icon0.png", "icon.png"):
        for f in files:
            if f.lower() == cand:
                return f
    for f in files:
        if "icon" in f.lower() and f.lower().endswith((".png", ".jpg", ".jpeg")):
            return f
    return None

def pick_shots(files):
    shots = []
    for f in files:
        fl = f.lower()
        if (fl.startswith("shot") or fl.startswith("screen")) and fl.endswith((".png", ".jpg", ".jpeg")):
            shots.append(f)
    def key(x):
        m = re.search(r"(\d+)", x)
        return int(m.group(1)) if m else 0
    return sorted(shots, key=key)

def extract_archive_id(url):
    m = re.search(r"archive\.org/(?:download|details)/([^/]+)/", url or "")
    return m.group(1) if m else None

def looks_user_migrated(data, app_id):
    for L in data.get("links") or []:
        if is_game_files_type(L.get("type"), L.get("name")):
            return True
    icon = data.get("icon") or ""
    if "archive.org" in icon and f"psvitaalive-{app_id}" not in icon:
        return True
    return False

def rename_repo(name, url):
    u = norm(url)
    if "github.com" in u and "/releases" in u:
        return "Release Page"
    if "github.com" in u or "gitlab.com" in u:
        return "Repository"
    return "Official Website"

def load_mirrors():
    mirrors = {}
    with LOG.open(encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            app_id = (row.get("app_id") or "").strip()
            status = (row.get("status") or "").strip().lower()
            if app_id and status == "ok":
                files = [x.strip() for x in (row.get("files") or "").split(";") if x.strip()]
                mirrors[app_id] = {
                    "identifier": (row.get("identifier") or "").strip(),
                    "files": files,
                }
    return mirrors

# App id aliases -> mirror id when folder names differ
MIRROR_ALIASES = {
    "vitawolfen-vitawolfe": "vitawolfen",
}

def migrate():
    mirrors = load_mirrors()
    changed = 0
    for path in sorted(APPS.glob("*.json")):
        app_id = path.stem
        data = json.loads(path.read_text(encoding="utf-8"))
        original = json.dumps(data, sort_keys=True)
        mirror = mirrors.get(app_id) or mirrors.get(MIRROR_ALIASES.get(app_id, ""))
        user_mig = looks_user_migrated(data, app_id)

        kept_mf, kept_gf, kept_repo, kept_other = [], [], [], []
        for L in list(data.get("links") or []):
            url = L.get("url") or ""
            name = L.get("name") or ""
            typ = L.get("type") or ""
            if is_mediafire(url):
                NL = dict(L)
                if name_banned(name):
                    NL["name"] = "Game Files - Mediafire" if is_game_files_type(typ, name) else "VPK - Mediafire"
                kept_mf.append(NL)
                continue
            if is_vitadb_cdn(url):
                continue
            if is_download_type(typ) and name_banned(name):
                continue
            if is_game_files_type(typ, name):
                kept_gf.append(dict(L))
                continue
            if is_repo_or_web_type(typ, name):
                NL = dict(L)
                if name_banned(name):
                    NL["name"] = rename_repo(name, url)
                if url.rstrip("/") in ("https://github.com", "http://github.com"):
                    continue
                kept_repo.append(NL)
                continue
            if is_mod_type(typ):
                kept_other.append(dict(L))
                continue
            if is_download_type(typ):
                # Keep Archive.org VPKs; also keep non-CDN hosts (GitHub/GitLab releases)
                # when not named after VitaDB/Neo/Too (those names already filtered above).
                if "archive.org" in norm(url) and url.lower().endswith(".vpk"):
                    kept_other.append(dict(L))
                elif any(h in norm(url) for h in ("github.com/", "gitlab.com/")) and (
                    url.lower().endswith(".vpk") or "/releases/download/" in norm(url)
                ):
                    NL = dict(L)
                    NL["type"] = "Download"
                    if not NL.get("name") or name_banned(NL.get("name")):
                        NL["name"] = "VPK"
                    kept_other.append(NL)
                continue
            if url.rstrip("/") in ("https://github.com", "http://github.com"):
                continue
            kept_other.append(dict(L))

        archive_vpk = None
        if mirror:
            ident = mirror["identifier"]
            files = mirror["files"]
            vpk = pick_vpk(files)
            icon_f = pick_icon(files)
            shots = pick_shots(files)
            if not user_mig:
                if icon_f:
                    data["icon"] = archive_download(ident, icon_f)
                if shots:
                    data["screenshots"] = [archive_download(ident, s) for s in shots]
            else:
                if is_vitadb_cdn(data.get("icon")) and icon_f:
                    data["icon"] = archive_download(ident, icon_f)
                shots_old = data.get("screenshots") or []
                if shots_old and all(is_vitadb_cdn(s) for s in shots_old) and shots:
                    data["screenshots"] = [archive_download(ident, s) for s in shots]
            if vpk:
                archive_vpk = {
                    "type": "Download",
                    "name": "VPK",
                    "url": archive_download(ident, vpk),
                    "recommended": True,
                }

        if is_vitadb_cdn(data.get("icon")):
            aids = []
            for L in kept_gf + kept_mf + kept_other + ([archive_vpk] if archive_vpk else []):
                if L:
                    a = extract_archive_id(L.get("url") or "")
                    if a:
                        aids.append(a)
            pref = next((a for a in aids if not a.startswith("psvitaalive-")), aids[0] if aids else None)
            if pref:
                data["icon"] = f"https://archive.org/download/{pref}/icon0.png"

        final = []
        if archive_vpk:
            final.append(archive_vpk)
        else:
            for L in kept_other:
                if is_download_type(L.get("type")) and (L.get("url") or "").lower().endswith(".vpk") and "archive.org" in norm(L.get("url")):
                    NL = dict(L)
                    NL["type"] = "Download"
                    NL["name"] = "VPK"
                    NL["recommended"] = True
                    final.append(NL)
                    break
        for L in kept_mf:
            NL = dict(L)
            if archive_vpk:
                NL["recommended"] = False
            final.append(NL)
        final.extend(kept_gf)
        for L in kept_other:
            if is_download_type(L.get("type")) and (L.get("url") or "").lower().endswith(".vpk"):
                continue
            final.append(L)
        final.extend(kept_repo)

        seen = set()
        uniq = []
        for L in final:
            k = (L.get("url") or "").strip().lower()
            if k and k in seen:
                continue
            if k:
                seen.add(k)
            uniq.append(L)
        final = uniq

        recs = [i for i, L in enumerate(final) if L.get("recommended") is True]
        if len(recs) > 1:
            keep = recs[0]
            for i in recs:
                L = final[i]
                if norm(L.get("name")) == "vpk" and "archive.org" in norm(L.get("url")):
                    keep = i
                    break
            for i in recs:
                if i != keep:
                    final[i]["recommended"] = False
        elif not recs:
            for L in final:
                if is_download_type(L.get("type")) and norm(L.get("name")) == "vpk":
                    L["recommended"] = True
                    break

        data["links"] = final
        new = json.dumps(data, sort_keys=True)
        if new != original:
            path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            changed += 1
    print(f"changed={changed}")
    return changed

if __name__ == "__main__":
    sys.exit(0 if migrate() >= 0 else 1)
