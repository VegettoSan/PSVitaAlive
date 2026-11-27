"""Select independently verifiable candidates from Orbit's durable queue.

The local policy authorizes a bounded class, not individual IDs. Never trust
a candidate as a canonical source or silently assign Verified.
"""
from __future__ import annotations

import hashlib
import json
import re
import unicodedata
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import quote

from scripts.catalog_schema_v2 import to_v1

QUEUE = "https://raw.githubusercontent.com/VegettoSan/PsVita-Homebrews-New-Releases/main/data/discovery/enrichment_state.json"
RECEIPTS = Path("data/orbit_imports")
CURSOR = Path("data/orbit_import_cursor.json")


def require(ok, message):
    if not ok:
        raise ValueError(message)


def slug(value):
    raw = unicodedata.normalize("NFKD", str(value or ""))
    return re.sub(r"[^a-z0-9]+", "-", "".join(c for c in raw if not unicodedata.combining(c)).lower()).strip("-")[:70].strip("-")


def fingerprint(entry):
    fields = {k: entry.get(k) for k in ("key", "project", "platform", "author_profile", "project_type", "app_draft")}
    return hashlib.sha256(json.dumps(fields, sort_keys=True, ensure_ascii=False,
                                    separators=(",", ":")).encode()).hexdigest()


def canonical_records(root, kind):
    return [to_v1(json.loads(p.read_text("utf-8"))) for p in (root/kind).glob("*.json")]


def evidence_key(row):
    """Invalidate cooldowns on meaningful discovery or verified package changes."""
    key, entry, result = row
    vpk = result.get("vpk") or {}
    fields = {"fingerprint": fingerprint(entry),
              "package": {k: vpk.get(k) for k in ("tag", "url", "sha256", "title_id", "bytes_read")},
              "category": result.get("category"), "author": result.get("author"),
              "requirements": result.get("requirements")}
    return hashlib.sha256(json.dumps(fields, sort_keys=True, ensure_ascii=False,
                                    separators=(",", ":")).encode()).hexdigest()


def retry_reason(exc):
    """Persist stable reason codes only; do not store raw exceptions or URLs."""
    message = str(exc)
    if "Upstream HTTP 404" in message:
        return "upstream_missing", timedelta(days=7)
    if any(s in message for s in ("Latest stable release differs", "Orbit and latest release disagree", "Enrichment and current release differ", "VPK changed since enrichment", "Official release size changed")):
        return "release_changed", timedelta(hours=12)
    if any(s in message for s in ("No usable description", "Fork or organization", "Owner/profile mismatch", "Author ID/name collision", "Invalid category/subcategory", "Category retired")):
        return "metadata_review", timedelta(days=3)
    return "retry", timedelta(hours=1)


def defer_candidate(feedback, row, exc, now):
    reason, delay = retry_reason(exc)
    feedback[row[0]] = {"evidence": evidence_key(row), "reason": reason,
                        "retry_at": (now + delay).isoformat()}


def is_deferred(feedback, row, now):
    record = feedback.get(row[0], {})
    if not isinstance(record, dict) or record.get("evidence") != evidence_key(row):
        return False
    try:
        return datetime.fromisoformat(record["retry_at"]) > now
    except (ValueError, TypeError, KeyError):
        return False


def prepared_priority(row, state):
    """A producer hint is never authorization; every row passes local eligibility."""
    index = state.get("publication_queue") or {}
    record = (index.get("records") or {}).get(row[0], {}) if isinstance(index, dict) and isinstance(index.get("records"), dict) else {}
    return (isinstance(record, dict) and record.get("state") == "READY_TO_PUBLISH"
            and record.get("fingerprint") == fingerprint(row[1])
            and record.get("sha256") == (row[2].get("vpk") or {}).get("sha256"))


def source_preflight_ready(row, state, now=None):
    """Require v2 producer proof, not merely a historical verified VPK."""
    index = state.get("publication_queue") or {}
    if not isinstance(index, dict):
        return False
    # Backward-compatible rollout: once v2 arrives, old hints no longer qualify.
    if index.get("schema_version") != 2:
        return True
    if not prepared_priority(row, state):
        return False
    result = row[2]
    proof = result.get("publication_preflight") or {}
    asset = proof.get("asset") or {}
    vpk = result.get("vpk") or {}
    try:
        age = (now or datetime.now(timezone.utc)) - datetime.fromisoformat(proof["checked_at"])
    except (TypeError, ValueError, KeyError):
        return False
    return (proof.get("state") == "verified" and timedelta(0) <= age <= timedelta(hours=24)
            and asset.get("state") == "selected"
            and isinstance(asset.get("tag"), str) and bool(asset["tag"])
            and isinstance(asset.get("url"), str) and bool(asset["url"])
            and type(asset.get("asset_id")) is int and asset["asset_id"] > 0
            and asset.get("tag") == vpk.get("tag") and asset.get("url") == vpk.get("url")
            and asset.get("size") == vpk.get("bytes_read")
            and asset.get("asset_id") == vpk.get("asset_id")
            and (not asset.get("digest") or asset["digest"] == "sha256:" + str(vpk.get("sha256", ""))))


def eligible_rows(inventory, state, policy, existing, retired, diagnostics=None):
    require(state.get("schema_version") == 1 and isinstance(state.get("records"), dict)
            and len(state["records"]) <= 5000, "Invalid Orbit checkpoint")
    ids = {a.get("id") for a in existing} | set(retired["apps"]["ids"])
    tids = {str(a.get("title_id", "")).upper() for a in existing} | {x.upper() for x in retired["apps"]["title_ids"]}
    sources = {str(a.get("source_url", "")).rstrip("/").casefold() for a in existing}
    source_ids = {a.get("source_id") for a in existing if a.get("source_name") == "Orbit"}
    rows = []
    def skip(reason):
        if diagnostics is not None:
            diagnostics[reason] = diagnostics.get(reason, 0) + 1
    for key, record in state["records"].items():
        entry = inventory.get(key)
        if not isinstance(entry, dict) or not isinstance(record, dict) or record.get("fingerprint") != fingerprint(entry):
            skip("stale or absent discovery"); continue
        result = record.get("result") or {}
        draft = entry.get("app_draft") or {}
        vpk = result.get("vpk") or {}
        title = result.get("title") or {}
        cat = result.get("category") or {}
        author = result.get("author") or {}
        req = result.get("requirements") or {}
        project = entry.get("project", "")
        app_id = draft.get("id")
        tid = vpk.get("title_id")
        if (not re.fullmatch(r"gh:[0-9]+", key) or entry.get("key") != key
                or entry.get("platform") != "github" or entry.get("project_type") != "homebrew"
                or not isinstance(project, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", project)
                or any(p in (".", "..") for p in project.split("/"))
                or project.casefold() in policy["excluded_projects"] or entry.get("lineage_is_fork") is True):
            skip("source or lineage review"); continue
        if (key in source_ids or ("https://github.com/"+project).casefold() in sources
                or not isinstance(app_id,str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,70}",app_id) or app_id in ids):
            skip("known, retired or invalid app"); continue
        try:
            if date.fromisoformat(draft["version_date"]) > date.today():
                skip("future release date"); continue
        except (ValueError, TypeError, KeyError):
            skip("release date unavailable"); continue
        if (result.get("state") != "inspected" or title.get("state") != "high" or title.get("suggestion") != tid
                or not isinstance(tid,str) or not re.fullmatch(r"[A-Z0-9]{9}",tid) or tid in tids
                or vpk.get("state") != "verified" or vpk.get("crc_checked") is not True
                or not re.fullmatch(r"[a-f0-9]{64}",str(vpk.get("sha256","")))
                or type(vpk.get("bytes_read")) is not int or not 0 < vpk["bytes_read"] <= 64*1024*1024):
            skip("package or identity incomplete"); continue
        desc = draft.get("description")
        if not isinstance(desc, str) or not desc.strip() or len(desc) > 2000:
            skip("description review"); continue
        if not (vpk.get("artwork") or {}).get("icon"):
            skip("native icon not yet verified"); continue
        if cat.get("state") != "suggested" or cat.get("category_id") not in policy["allowed_categories"]:
            skip("category review"); continue
        if author.get("state") not in ("existing","draft") or req.get("state") == "review":
            skip("author or requirements review"); continue
        # Requirements for separately supplied data do not establish that
        # the upstream VPK contains third-party content. Register original
        # homebrew links without rejecting descriptive keywords alone.
        # Defer explicit indications of bundled third-party game content.
        text = " ".join(str(x or "") for x in (
            draft.get("description"), draft.get("long_description"), req.get("value")))
        if re.search(r"\b(?:bundles?|bundled with|includes?|included with|ships? with|contains?)\s+(?:the\s+)?(?:(?:original|commercial|proprietary|copyrighted)\s+)+(?:game(?:\s+(?:data|files|assets))?|roms?|bios)\b", text, re.I):
            skip("bundled third-party content review"); continue
        if not source_preflight_ready((key, entry, result), state):
            skip("awaiting current stable source preflight"); continue
        rows.append((key, entry, result))
    # Reject conflicts across independently discovered projects as well as canonical files.
    from collections import Counter
    tcounts = Counter(row[2]["vpk"]["title_id"] for row in rows)
    icounts = Counter(row[1]["app_draft"]["id"] for row in rows)
    return [row for row in rows if tcounts[row[2]["vpk"]["title_id"]] == 1 and icounts[row[1]["app_draft"]["id"]] == 1]


def mit_notice(repo, tag, api_json, public_bytes):
    """Require the release's own GitHub-recognized MIT license and original notice."""
    meta = api_json(repo, "license?ref="+quote(tag,safe=""))
    require((meta.get("license") or {}).get("spdx_id") == "MIT", "Release is not recognized as MIT")
    path = meta.get("path")
    require(isinstance(path,str) and re.fullmatch(r"(?:LICENSE|LICENCE|COPYING)(?:\.(?:md|txt))?",path,re.I), "Unexpected license path")
    url = "https://raw.githubusercontent.com/"+repo+"/"+quote(tag,safe="")+"/"+path
    notice = public_bytes(url,65536).decode("utf-8").strip()
    flat = " ".join(notice.split()).casefold()
    require("copyright" in flat and "permission is hereby granted, free of charge" in flat
            and "to deal in the software without restriction" in flat
            and "the above copyright notice and this permission notice shall be included" in flat
            and "the software is provided" in flat and "without warranty" in flat,
            "MIT permission notice incomplete")
    require(not re.search(r"\b(?:non-commercial|noncommercial|no redistribution|all rights reserved|additional restrictions)\b",flat),
            "License contains possible extra restrictions")
    return notice


def owner_author(root, entry, result, metadata, public_bytes, retired, notice=None):
    """Match exact public account, or create the same profile as the web generator."""
    owner = metadata.get("owner") or {}
    handle = owner.get("login")
    require(metadata.get("fork") is False and owner.get("type") == "User"
            and isinstance(handle,str) and re.fullmatch(r"[A-Za-z0-9-]{1,39}",handle),
            "Fork or organization requires attribution review")
    url = "https://github.com/"+handle
    require(str(entry.get("author_profile","")).rstrip("/").casefold() == url.casefold(), "Owner/profile mismatch")
    # Public user identity is independently verified, including numeric account ID.
    profile = json.loads(public_bytes("https://api.github.com/users/"+quote(handle,safe=""), 2*1024*1024, api=True))
    require(profile.get("id") == owner.get("id") and profile.get("type") == "User"
            and str(profile.get("login","")).casefold()==handle.casefold()
            and str(profile.get("html_url","")).casefold()==url.casefold(), "Public profile identity changed")
    # Attribution follows the independently verified original repository
    # owner and canonical profiles; it does not depend on license text.
    authors = canonical_records(root,"authors")
    matches = [a for a in authors if any(isinstance(l,dict) and str(l.get("url","")).rstrip("/").casefold()==url.casefold() for l in a.get("links",[]))]
    require(len(matches)<=1, "Multiple author profiles for owner")
    retired_ids = set(retired["authors"]["ids"])
    if matches:
        aid = matches[0]["id"]
        require(aid not in retired_ids, "Retired author")
        return [aid], None
    aid=slug(handle)
    require(aid and aid not in retired_ids and not any(a.get("id")==aid or slug(a.get("name"))==aid for a in authors)
            and not (root/"authors"/(aid+".json")).exists(), "Author ID/name collision")
    avatar = profile.get("avatar_url")
    if not isinstance(avatar,str) or not avatar.startswith("https://avatars.githubusercontent.com/"):
        avatar="https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png"
    return [aid], {"id":aid,"name":str(profile.get("name") or handle).strip()[:120] or handle,
                   "bio":str(profile.get("bio") or "Developer").strip()[:1000] or "Developer",
                   "avatar":avatar,"links":[{"type":"Repository","name":"GitHub","url":url,"recommended":True}]}


def automatic_spec(root, row, policy, retired, api_json, public_bytes):
    key,entry,result=row
    draft=entry["app_draft"]
    vpk=result["vpk"]
    project=entry["project"]
    metadata=api_json(project,"")
    require("gh:"+str(metadata.get("id"))==key and str(metadata.get("full_name","")).casefold()==project.casefold()
            and metadata.get("fork") is False, "Repository identity or lineage changed")
    tag=vpk.get("tag")
    require(isinstance(tag,str) and 0<len(tag)<=100 and not any(c.isspace() for c in tag), "Invalid release tag")
    aids,author=owner_author(root,entry,result,metadata,public_bytes,retired)
    cat=result["category"]
    taxonomy=canonical_records(root,"categories")
    category=next((c for c in taxonomy if c["id"]==cat["category_id"]),None)
    require(category is not None,"Category retired")
    allowed={s["id"] for s in category["subcategories"]}
    subs=cat.get("subcategory_ids") or (["other"] if "other" in allowed else [])
    require(bool(subs) and len(set(subs))==len(subs) and set(subs)<=allowed,"Invalid category/subcategory")
    desc=draft.get("description")
    require(isinstance(desc,str) and bool(desc.strip()) and len(desc)<=2000,"No usable description")
    long_desc=draft.get("long_description") or desc
    require(isinstance(long_desc,str) and len(long_desc)<=10000,"Invalid description")
    req=(result.get("requirements") or {}).get("value") or "- no data"
    require(isinstance(req,str) and len(req)<=12000,"Invalid requirements")
    spec={"automatic":True,"candidate_key":key,"project":project,"id":draft["id"],"title_id":vpk["title_id"],
          "tag":tag,"status":"Legacy" if date.fromisoformat(draft["version_date"]) <= date.today()-timedelta(days=365) else "Archive","author_ids":aids,"category_id":cat["category_id"],"subcategory_ids":subs,
          "name":str(draft.get("name") or project.split("/")[-1])[:128],"description":desc,"long_description":long_desc,
          "requirements":req,
          "expected_sha256":vpk["sha256"],"expected_url":vpk["url"]}
    return spec,author


def receipt(spec):
    return {"schema_version":1,"app_id":spec["id"],"candidate_key":spec["candidate_key"],"repo":spec["project"],
            "tag":spec["tag"],"sha256":spec["expected_sha256"]}
