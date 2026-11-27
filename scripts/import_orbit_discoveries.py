"""Import reviewed Orbit discoveries into canonical PSVitaAlive sources.

No direct changes to generated catalogs; the canonical validator and backup
workflow are authoritative. Automatic registration is independent of license metadata; preservation uses the existing policy.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import stat
import tempfile
import zipfile
from collections import Counter
from datetime import date, timedelta
from pathlib import Path
from urllib.parse import quote

import requests
from PIL import Image
from scripts.catalog_schema_v2 import to_v1, to_v2
from scripts.vpk_display_name import vpk_display_name
from scripts import orbit_auto_selection as auto
from scripts.archive_backup_batch import Deferred, download_source, official_asset_metadata
from scripts.archive_new_items_batch import automatic_identity, validate_vpk_title
from scripts.sync_release_updates import normalized_repo, release_asset_url_matches

ROOT = Path(__file__).resolve().parents[1]
POLICY = Path("config/orbit_publication_approved.json")
ORBIT = "https://raw.githubusercontent.com/VegettoSan/PsVita-Homebrews-New-Releases/main/data/discovery/candidates.json"
CANONICAL = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/"
MAX_VPK = 64 * 1024 * 1024
MAX_MEDIA = 2 * 1024 * 1024
MAX_UNPACKED = 256 * 1024 * 1024


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_json(path):
    return to_v1(json.loads(path.read_text("utf-8")))


def public_bytes(url, limit, api=False):
    headers = {"User-Agent": "PSVitaAlive-Orbit-Publisher", "Accept-Encoding": "identity"}
    if api:
        require(url.startswith("https://api.github.com/"), "Untrusted API endpoint")
        headers["Accept"] = "application/vnd.github+json"
        if os.environ.get("GH_TOKEN"):
            headers["Authorization"] = "Bearer " + os.environ["GH_TOKEN"]
    else:
        require(url.startswith("https://raw.githubusercontent.com/"), "Untrusted raw endpoint")
    with requests.Session() as session:
        session.trust_env = False
        with session.get(url, headers=headers, stream=True, allow_redirects=False, timeout=(5, 15)) as response:
            require(response.status_code == 200, "Upstream HTTP " + str(response.status_code))
            result = bytearray()
            for chunk in response.iter_content(65536):
                result.extend(chunk)
                require(len(result) <= limit, "Upstream byte limit")
            return bytes(result)


def api_json(repo, endpoint):
    return json.loads(public_bytes("https://api.github.com/repos/" + (repo + "/" + endpoint).rstrip("/"),
                                   2 * 1024 * 1024, api=True))


def reviewed_license(root, app_id, repo, tag):
    """Recheck exact release-specific notice before copying or preserving."""
    policy = json.loads((root / POLICY).read_text("utf-8"))
    matches = [p for p in policy["approved"] if p["id"] == app_id
               and p["project"].casefold() == repo.casefold() and p["tag"] == tag]
    if not matches:
        path = root / auto.RECEIPTS / (app_id + ".json")
        require(path.is_file(), "Release has no automatic license receipt")
        receipt = json.loads(path.read_text("utf-8"))
        require(receipt.get("schema_version") == 1 and receipt.get("app_id") == app_id
                and str(receipt.get("repo", "")).casefold() == repo.casefold()
                and receipt.get("tag") == tag and receipt.get("license") == "MIT",
                "Automatic license receipt does not match release")
        notice = auto.mit_notice(repo, tag, api_json, public_bytes)
        require(notice == receipt.get("license_notice"), "Automatic license notice changed")
        return notice
    require(len(matches) == 1, "Conflicting reviewed redistribution permission")
    spec = matches[0]
    require(spec["license"] == "MIT" and spec["license_path"] == "LICENSE",
            "Only reviewed MIT notices in the initial policy")
    expected = spec["license_text"].strip()
    require("Permission is hereby granted" in expected and "Copyright" in expected,
            "Incomplete license notice")
    raw = public_bytes("https://raw.githubusercontent.com/" + repo + "/" +
                       quote(tag, safe="") + "/LICENSE", 65536).decode("utf-8")
    require(raw.strip() == expected, "License notice changed")
    return raw.strip()


def validate_approval(spec, entry):
    require(re.fullmatch(r"[a-z0-9][a-z0-9-]{0,70}", spec["id"]), "Invalid app ID")
    require(re.fullmatch(r"[A-Z0-9]{9}", spec["title_id"]), "Invalid Title ID")
    require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", spec["project"])
            and not any(p in (".", "..") for p in spec["project"].split("/")), "Invalid project")
    require(spec["status"] in ({"Legacy", "Archive"} if spec.get("automatic") else {"Legacy"}), "Unapproved discovery status")
    require(entry.get("key") == spec["candidate_key"] and
            entry.get("project", "").casefold() == spec["project"].casefold()
            and entry.get("platform") == "github", "Orbit candidate identity changed")


def select_release(spec, release):
    require(isinstance(release, dict) and release.get("draft") is False
            and release.get("prerelease") is False and release.get("tag_name") == spec["tag"],
            "Latest stable release differs from reviewed tag")
    published = date.fromisoformat(str(release.get("published_at", ""))[:10])
    require(published <= date.today(), "Future release date")
    if not spec.get("automatic"):
        require(published < date(2022, 1, 1), "Historical reviewed pilot release expected")
    assets = [a for a in release.get("assets", []) if isinstance(a, dict)
              and str(a.get("name", "")).lower().endswith(".vpk")]
    require(len(assets) == 1 and assets[0].get("state") == "uploaded", "No unique uploaded VPK")
    asset = assets[0]
    url = asset.get("browser_download_url")
    require(release_asset_url_matches(url, spec["project"].lower(), "github", spec["tag"]), "VPK provenance mismatch")
    require(type(asset.get("size")) is int and 0 < asset["size"] <= MAX_VPK, "VPK size outside pilot budget")
    return asset, published.isoformat()


def full_package(path, expected):
    validate_vpk_title(path, expected)
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        names = [i.filename for i in infos]
        require(len(names) <= 10000 and len(names) == len(set(names)), "Duplicate or excessive ZIP entries")
        require(sum(i.file_size for i in infos) <= MAX_UNPACKED, "VPK expands beyond pilot budget")
        for i in infos:
            require(not i.filename.startswith("/") and ".." not in i.filename.split("/")
                    and "\\" not in i.filename and ":" not in i.filename and not i.flag_bits & 1
                    and not stat.S_ISLNK(i.external_attr >> 16), "Unsafe ZIP entry")
        require(z.getinfo("eboot.bin").file_size > 0 and z.testzip() is None, "VPK CRC or executable entry invalid")


def png_bytes(raw):
    if not raw.startswith(b"\x89PNG\r\n\x1a\n") or len(raw) > MAX_MEDIA:
        return False
    try:
        with Image.open(io.BytesIO(raw)) as image:
            require(image.format == "PNG" and not getattr(image, "is_animated", False)
                    and 0 < image.width <= 4096 and 0 < image.height <= 4096
                    and image.width * image.height <= 4*1024*1024, "Invalid PNG dimensions")
            image.verify()
        with Image.open(io.BytesIO(raw)) as image:
            image.load()
        return True
    except (ValueError, OSError, SyntaxError, Image.DecompressionBombError):
        return False


def native_media(path, app_id):
    """First pilot uses real VPK artwork, not guessed external URLs."""
    result, seen = {}, set()
    icon = None
    shots = []
    choices = ("sce_sys/icon0.png", "sce_sys/pic0.png", "sce_sys/livearea/pic0.png",
               "sce_sys/livearea/contents/bg.png", "sce_sys/livearea/contents/bg0.png",
               "sce_sys/livearea/contents/background.png", "sce_sys/bg.png")
    with zipfile.ZipFile(path) as z:
        for source in choices:
            try:
                entry = z.getinfo(source)
            except KeyError:
                continue
            if not 0 < entry.file_size <= MAX_MEDIA:
                continue
            raw = z.read(entry)
            if not png_bytes(raw):
                continue
            digest = hashlib.sha256(raw).hexdigest()
            if digest in seen:
                continue
            seen.add(digest)
            dest = Path("apps/media") / app_id / (digest + ".png")
            result[dest] = raw
            url = CANONICAL + dest.as_posix()
            if source == choices[0]:
                icon = url
            else:
                shots.append(url)
    require(icon is not None, "No valid VPK icon")
    return icon, shots[:5], result


def plan_one(root, spec, entry, new_author=None):
    app_id, tid = spec["id"], spec["title_id"]
    require(re.fullmatch(r"[a-z0-9][a-z0-9-]{0,70}", app_id), "Invalid app ID")
    registry = json.loads((root / "registry/retired_ids.json").read_text("utf-8"))
    require(app_id not in registry["apps"]["ids"] and tid not in
            {x.upper() for x in registry["apps"]["title_ids"]}, "Retired app identity")
    existing = [read_json(p) for p in (root / "apps").glob("*.json")]
    same = [a for a in existing if a.get("id") == app_id]
    if same:
        require(len(same) == 1 and same[0].get("title_id") == tid
                and same[0].get("source_name") == "Orbit"
                and same[0].get("source_id") == spec["candidate_key"], "App ID already registered")
        return None, {}
    validate_approval(spec, entry)
    require(not any(str(a.get("title_id", "")).upper() == tid for a in existing), "Title ID already registered")
    require(not (root / "apps" / (app_id + ".json")).exists(), "Filename already exists")
    require(bool(spec["author_ids"]) and len(set(spec["author_ids"])) == len(spec["author_ids"]), "Invalid reviewed authors")
    for aid in spec["author_ids"]:
        require(re.fullmatch(r"[a-z0-9_-]+", aid) and aid not in registry["authors"]["ids"]
                and ((root / "authors" / (aid + ".json")).is_file() or (new_author and new_author.get("id") == aid)), "Missing or retired reviewed author")
    category = read_json(root / "categories" / (spec["category_id"] + ".json"))
    require(bool(spec["subcategory_ids"]) and set(spec["subcategory_ids"]) <= {s["id"] for s in category["subcategories"]}, "Invalid subcategory")
    repo = spec["project"]
    # Require Orbit's stable numeric repository ID to match current GitHub metadata.
    metadata = api_json(repo, "")
    require("gh:" + str(metadata["id"]) == spec["candidate_key"] and
            metadata["full_name"].casefold() == repo.casefold(), "Upstream repository identity changed")
    asset, published = select_release(spec, api_json(repo, "releases/latest"))
    url = asset["browser_download_url"]
    require(any(l.get("type") == "Download" and l.get("url") == url
                for l in entry.get("app_draft", {}).get("links", []) if isinstance(l, dict)),
            "Orbit and latest release disagree")
    if spec.get("automatic"):
        require(url == spec["expected_url"], "Enrichment and current release differ")
        # Registration links to the original public release; license is not a catalog gate.
    else:
        reviewed_license(root, app_id, repo, spec["tag"])
    size, sha = official_asset_metadata({"source_url": url})
    require(size == asset["size"] and size <= MAX_VPK, "Official release size changed")
    with tempfile.TemporaryDirectory() as temp:
        target = Path(temp) / "app.vpk"
        _, measured = download_source({"source_url": url, "size_bytes": size, "source_sha256": sha},
                                      target, max_file=MAX_VPK)
        if spec.get("automatic"):
            require(measured == spec["expected_sha256"], "VPK changed since enrichment")
        full_package(target, tid)
        native_name, name_field = vpk_display_name(target)
        icon, shots, media = native_media(target, app_id)
    app = {k: spec[k] for k in ("id", "title_id", "name", "description", "long_description",
                                "author_ids", "category_id", "subcategory_ids", "requirements", "status")}
    if spec.get("license"):
        app["license"] = spec["license"]
    if spec.get("automatic"):
        app["status"] = "Legacy" if date.fromisoformat(published) <= date.today() - timedelta(days=365) else "Archive"
    app["name"] = native_name or spec["name"]
    print("Display name:", app_id, repr(app["name"]), "source:", name_field or "reviewed repository fallback", flush=True)
    app.update(version=spec["tag"].lstrip("vV"), version_date=published, size=size,
               icon=icon, screenshots=shots, source_name="Orbit", source_id=spec["candidate_key"],
               source_url="https://github.com/" + repo,
               release_page="https://github.com/" + repo + "/releases/tag/" + quote(spec["tag"], safe=""),
               links=[{"type": "Repository", "name": "GitHub", "url": "https://github.com/" + repo},
                      {"type": "Download", "name": "VPK", "url": url, "recommended": True}])
    # Reuse the same initial preservation eligibility check, not a parallel rule.
    counts = Counter(str(a.get("title_id", "")).upper() for a in existing + [app])
    automatic_identity(app, app_id, counts)
    print("Verified package:", app_id, tid, size, measured, "native images:", len(media), flush=True)
    return app, media


def stage_one(root, spec, entry, author=None):
    app, media = plan_one(root, spec, entry, author)
    if app is None:
        print("Already imported:", spec["id"], flush=True)
        return False
    files = dict(media)
    files[Path("apps") / (app["id"] + ".json")] = (json.dumps(to_v2(app), ensure_ascii=False, indent=2)+"\n").encode()
    if author:
        files[Path("authors") / (author["id"] + ".json")] = (json.dumps(to_v2(author), ensure_ascii=False, indent=2)+"\n").encode()
    if spec.get("automatic"):
        files[auto.RECEIPTS / (app["id"] + ".json")] = (json.dumps(auto.receipt(spec), ensure_ascii=False, indent=2)+"\n").encode()
    # Fail before writing if any target would overwrite an existing file.
    for rel, raw in files.items():
        dest = root / rel
        require(not dest.exists() or (rel in media and dest.read_bytes() == raw), "Canonical path already exists")
    created = []
    try:
        for rel, raw in files.items():
            dest = root / rel
            if not dest.exists():
                dest.parent.mkdir(parents=True, exist_ok=True)
                with dest.open("xb") as out:
                    created.append(dest)
                    out.write(raw)
        # Isolate invalid candidates instead of poisoning the entire batch.
        import subprocess
        check = subprocess.run(["python", "scripts/validate_catalog_ci.py"], cwd=root,
                               env={**os.environ, "VITAHUB_SKIP_REMOTE_CHECKS":"true"},
                               capture_output=True, text=True, timeout=120)
        require(check.returncode == 0, "Canonical validation rejected candidate: " + check.stdout[-3000:])
    except BaseException:
        for dest in reversed(created):
            dest.unlink(missing_ok=True)
        raise
    print("Staged canonical V2 app:", app["id"], flush=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", action="store_true", required=True)
    args = parser.parse_args()
    policy = json.loads((ROOT / POLICY).read_text("utf-8"))
    require(policy["schema_version"] == 1 and len(policy["approved"]) <= 10, "Unsupported approval policy")
    inventory = json.loads(public_bytes(ORBIT, 16*1024*1024))
    for spec in policy["approved"]:
        try:
            stage_one(ROOT, spec, inventory.get(spec["candidate_key"], {}))
        except (ValueError, KeyError, OSError, requests.RequestException, Deferred, zipfile.BadZipFile) as exc:
            print(f"::warning::Reviewed candidate {spec.get('id')} deferred: {type(exc).__name__}: {exc}", flush=True)
    config = policy.get("automatic") or {}
    if not config.get("enabled"):
        return 0
    require(1 <= config["max_new"] <= 6 and config["max_new"] <= config["max_checks"] <= 30, "Unsafe automatic policy")
    state = json.loads(public_bytes(auto.QUEUE,32*1024*1024))
    registry = json.loads((ROOT/"registry/retired_ids.json").read_text("utf-8"))
    diagnostics = {}
    rows = auto.eligible_rows(inventory,state,config,auto.canonical_records(ROOT,"apps"),registry,diagnostics)
    print("Automatic eligibility deferrals:", json.dumps(diagnostics,sort_keys=True), flush=True)
    cursor_path = ROOT/auto.CURSOR
    cursor = json.loads(cursor_path.read_text("utf-8")) if cursor_path.exists() else {"last_key":""}
    last = cursor.get("last_key","")
    rows.sort(key=lambda row: (row[0] <= last,row[0]))
    print("Automatic preflight eligible:",len(rows), "of",len(inventory), flush=True)
    created=0
    checked=0
    for row in rows[:config["max_checks"]]:
        if created >= config["max_new"]:
            break
        key,entry,result=row
        checked+=1
        last=key
        try:
            spec,author=auto.automatic_spec(ROOT,row,config,registry,api_json,public_bytes)
            if stage_one(ROOT,spec,entry,author):
                created+=1
        except (ValueError, KeyError, TypeError, OSError, requests.RequestException, Deferred, zipfile.BadZipFile) as exc:
            print(f"::warning::Automatic candidate {key} deferred: {type(exc).__name__}: {exc}", flush=True)
    if checked:
        cursor_path.parent.mkdir(parents=True,exist_ok=True)
        cursor_path.write_text(json.dumps({"last_key":last},indent=2)+"\n","utf-8")
    print(f"Automatic publication staged: {created}; checked: {checked}; preflight eligible: {len(rows)}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
