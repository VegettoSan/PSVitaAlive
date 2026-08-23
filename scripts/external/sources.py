#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import urllib.parse
import urllib.request
from dataclasses import dataclass


USER_AGENT = "PSVitaAlive-ExternalCatalog/1.5"

# VitaDB's origin rejects non-browser User-Agents with HTTP 200 + empty [].
# Keep the project UA for GitHub-hosted sources; use a browser UA only for VitaDB.
VITADB_USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/127.0.0.0 Safari/537.36"
)

# Resource roots are source-specific. External catalogs often store a bare
# filename because their own web application knows the directory. VitaHub
# must persist an absolute URL that can be consumed independently.
SOURCE_MEDIA_ROOTS = {
    "vitadbtoo": {
        "base": "https://raw.githubusercontent.com/DrDecki/VitaDBtoo-db/main/",
        "icon": "icons/",
        "screenshot": "screenshots/",
    },
    "vitadb": {
        "base": "https://www.rinnegatamante.eu/vitadb/",
        "icon": "icons/",
        "screenshot": "screenshots/",
    },
    "neovitadb": {
        "base": "https://raw.githubusercontent.com/robin994/NeoVitaDB-Catalog/main/",
        "icon": "icons_vita/",
        # NeoVitaDB's catalog deliberately does not publish VitaDB screenshot
        # files in its repository. Do not invent a screenshot URL for a bare
        # filename; the caller will use the icon fallback instead.
        "screenshot": None,
    },
}


@dataclass
class Candidate:
    source_id: str
    source_item_id: str | None
    title_id: str | None
    name: str
    author_names: list[str]
    repository_url: str | None
    release_page: str | None
    version: str | None
    version_date: str | None
    description: str | None
    long_description: str | None
    requirements: str | None
    changelog: str | None
    icon: str | None
    screenshots: list[str]
    download_url: str | None
    size: int | None
    category_raw: str | None
    platform: str | None
    tags: list[str] | None = None


def fetch_json(url: str):
    request = urllib.request.Request(
        url,
        headers={"User-Agent": USER_AGENT, "Accept": "application/json, text/plain, */*"},
    )
    with urllib.request.urlopen(request, timeout=45) as response:
        return json.load(response)


# Official VitaDB homebrew feed. The documented API is POST with an empty body.
# Prefer HTTPS, fall back to HTTP. Do not treat an empty list as a silent success
# without logging — the upstream may be offline while still returning HTTP 200.
VITADB_ENDPOINTS = [
    "https://www.rinnegatamante.eu/vitadb/list_hbs_json.php",
    "https://rinnegatamante.eu/vitadb/list_hbs_json.php",
    "http://www.rinnegatamante.eu/vitadb/list_hbs_json.php",
    "http://rinnegatamante.eu/vitadb/list_hbs_json.php",
]


def fetch_vitadb(preferred_url: str | None = None):
    """Fetch the official VitaDB list via POST (no parameters).

    Returns a list of raw dict records. Raises RuntimeError only when every
    endpoint fails with a transport/HTTP error. An empty list is a valid
    response and is returned with an explicit log line.
    """
    endpoints = []
    if preferred_url:
        endpoints.append(preferred_url)
    for url in VITADB_ENDPOINTS:
        if url not in endpoints:
            endpoints.append(url)

    last_error = None
    for endpoint in endpoints:
        try:
            print("VitaDB:")
            print(f"  endpoint: {endpoint}")
            print("  method: POST")
            print("  parameters: none")
            request = urllib.request.Request(
                endpoint,
                data=b"",
                method="POST",
                headers={
                    "User-Agent": VITADB_USER_AGENT,
                    "Accept": "application/json, text/plain, */*",
                    "Accept-Language": "en-US,en;q=0.9",
                    "Content-Type": "application/x-www-form-urlencoded",
                    "Content-Length": "0",
                },
            )
            with urllib.request.urlopen(request, timeout=45) as response:
                status = getattr(response, "status", 200)
                raw = response.read()
            print(f"  status: {status}")
            if status < 200 or status >= 300:
                last_error = RuntimeError(f"HTTP {status}")
                print(f"  error: HTTP {status}")
                continue
            data = json.loads(raw.decode("utf-8", errors="replace") or "[]")
            if isinstance(data, list):
                items = [item for item in data if isinstance(item, dict)]
            else:
                items = extract_catalog_items(data, "vitadb")
            print(f"  records: {len(items)}")
            if len(items) == 0:
                print("  warning: empty list from this endpoint; trying next if any")
                last_error = RuntimeError("empty list")
                continue
            return items
        except Exception as exc:
            last_error = exc
            print("VitaDB:")
            print("  status: FAILED")
            print(f"  endpoint: {endpoint}")
            print(f"  error: {type(exc).__name__}: {exc}")
            continue

    # All endpoints failed or returned empty. Prefer an empty list over hard-failing
    # the whole aggregate when upstream is temporarily filtered/offline.
    if last_error is not None and "empty list" in str(last_error):
        print("VitaDB: all endpoints returned empty lists")
        return []
    raise RuntimeError(f"VitaDB feed unavailable: {last_error}")


def _split_author_field(value):
    """Split compound author strings into individual names.

    Separators: &, +, comma, semicolon, newline, and the word "and".
    """
    if isinstance(value, list):
        result = []
        seen = set()
        for item in value:
            for part in _split_author_field(item):
                key = part.casefold()
                if key and key not in seen:
                    seen.add(key)
                    result.append(part)
        return result
    if not isinstance(value, str) or not value.strip():
        return []
    text_value = re.sub(r"\s+", " ", value.replace("\u00a0", " ")).strip()
    text_value = re.sub(r"\s*&\s*", ",", text_value)
    text_value = re.sub(r"\s+\band\b\s+", ",", text_value, flags=re.I)
    text_value = re.sub(r"\s*\+\s*", ",", text_value)
    text_value = re.sub(r"[;\n\r]+", ",", text_value)
    result = []
    seen = set()
    for part in text_value.split(","):
        name = part.strip(" \t,;|")
        if not name:
            continue
        key = name.casefold()
        if key not in seen:
            seen.add(key)
            result.append(name)
    return result


def extract_catalog_items(data, source_id: str) -> list[dict]:
    """Accept list feeds and common API wrapper/object shapes."""
    if isinstance(data, list):
        return [item for item in data if isinstance(item, dict)]
    if isinstance(data, dict):
        for key in ("apps", "homebrew", "homebrews", "items", "results", "data", "entries"):
            value = data.get(key)
            if isinstance(value, list):
                return [item for item in value if isinstance(item, dict)]
        values = list(data.values())
        if values and all(isinstance(item, dict) for item in values):
            if any(item.get("titleid") or item.get("title_id") or item.get("name") for item in values):
                return values
    raise ValueError(f"{source_id}: unsupported JSON feed shape ({type(data).__name__})")


def _as_list(value):
    if isinstance(value, list):
        return [str(item).strip() for item in value if str(item).strip()]
    if isinstance(value, str) and value.strip():
        return [value.strip()]
    return []


def _split_csvish(value):
    """Split legacy catalog fields without corrupting normal URLs."""
    if isinstance(value, list):
        result = []
        for item in value:
            result.extend(_split_csvish(item))
        return result
    if not isinstance(value, str) or not value.strip():
        return []
    return [
        item.strip()
        for item in re.split(r"[;\n,]+", value.replace("\r", "\n"))
        if item.strip()
    ]


def _first_url(value):
    """Return the first usable absolute URL from a scalar/list field."""
    for item in _split_csvish(value):
        if item.startswith(("http://", "https://")):
            return item
    return None


def resolve_media_url(source_id: str, value: str | None, kind: str) -> str | None:
    """Resolve a source-relative media reference to an absolute URL.

    Absolute HTTP(S) URLs are preserved. Bare filenames are expanded only
    when the source's real public resource root is known. This intentionally
    refuses to invent NeoVitaDB screenshot URLs because its catalog repository
    does not publish the old VitaDB screenshot files.
    """
    if not isinstance(value, str) or not value.strip():
        return None
    value = value.strip()
    if value.startswith(("http://", "https://")):
        return value

    config = SOURCE_MEDIA_ROOTS.get(source_id, {})
    base = config.get("base")
    folder = config.get(kind)
    if not base or folder is None:
        return None

    # Accept either "foo.png" or "icons/foo.png" while preventing a
    # duplicated folder such as icons/icons/foo.png.
    clean = value.lstrip("./")
    clean = clean.replace("\\", "/")
    prefix = folder.rstrip("/") + "/"
    if clean.startswith(prefix):
        relative = clean
    else:
        relative = prefix + clean
    quoted = "/".join(urllib.parse.quote(part, safe="@:+,;=-._~") for part in relative.split("/"))
    return urllib.parse.urljoin(base, quoted)


def resolve_media_list(source_id: str, values, kind: str) -> list[str]:
    result = []
    for value in _split_csvish(values):
        url = resolve_media_url(source_id, value, kind)
        if url and url not in result:
            result.append(url)
    return result


def _int_or_none(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None



# Canonical VitaDB / VitaHomebrewDB / NeoVitaDB type → PSVitaAlive slug.
# Numeric values match NeoVitaDB categories.json and the live VitaHomebrewDB feed:
#   1 = Original Game, 2 = Game Port, 4 = Utility, 5 = Emulator.
# Plugins are usually published in a separate list (no type field).
EXTERNAL_TYPE_ALIASES = {
    "1": "game",
    "2": "port",
    "4": "utility",
    "5": "emulator",
    "0": "game",  # legacy / defensive
    "3": "emulator",  # legacy / defensive
    "game": "game",
    "games": "game",
    "original": "game",
    "original game": "game",
    "original_game": "game",
    "ps vita game": "game",
    "port": "port",
    "ports": "port",
    "game port": "port",
    "game_port": "port",
    "utility": "utility",
    "utilities": "utility",
    "tool": "utility",
    "tools": "utility",
    "app": "utility",
    "application": "utility",
    "emulator": "emulator",
    "emulators": "emulator",
    "emu": "emulator",
    "plugin": "plugin",
    "plugins": "plugin",
}


def canonicalize_external_type(value) -> str | None:
    if value is None:
        return None
    key = str(value).strip().lower()
    if not key:
        return None
    return EXTERNAL_TYPE_ALIASES.get(key, key)


def parse_tags(value) -> list[str]:
    """Normalize free-form tags from VitaDB-family feeds into a clean list."""
    if value is None:
        return []
    parts: list[str] = []
    if isinstance(value, list):
        for item in value:
            if item is None:
                continue
            parts.extend(str(item).replace(";", ",").split(","))
    else:
        parts.extend(str(value).replace(";", ",").split(","))
    out: list[str] = []
    seen: set[str] = set()
    for part in parts:
        tag = part.strip().lower()
        if not tag or tag in seen:
            continue
        seen.add(tag)
        out.append(tag)
    return out


def normalize_vitadb(raw: dict) -> Candidate:
    """Normalize the official VitaDB list_hbs_json.php format."""
    category_raw = canonicalize_external_type(raw.get("type") if raw.get("type") is not None else raw.get("category"))
    return Candidate(
        source_id="vitadb",
        source_item_id=str(raw.get("id")) if raw.get("id") is not None else None,
        title_id=raw.get("titleid") or raw.get("title_id"),
        name=str(raw.get("name") or "").strip(),
        author_names=_split_author_field(raw.get("author") or raw.get("authors")),
        repository_url=_first_url(raw.get("source")),
        release_page=_first_url(raw.get("release_page")),
        version=raw.get("version"),
        version_date=raw.get("date") or raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=resolve_media_url("vitadb", raw.get("icon"), "icon"),
        screenshots=resolve_media_list("vitadb", raw.get("screenshots"), "screenshot"),
        download_url=_first_url(raw.get("url") or raw.get("download_url") or raw.get("download")),
        size=_int_or_none(raw.get("size")),
        category_raw=category_raw,
        tags=parse_tags(raw.get("tags")),
        platform="vita",
    )


def normalize_vitadbtoo(raw: dict) -> Candidate:
    return Candidate(
        source_id="vitadbtoo",
        source_item_id=str(raw.get("id")) if raw.get("id") is not None else None,
        title_id=raw.get("titleid") or raw.get("title_id"),
        name=str(raw.get("name") or "").strip(),
        author_names=_split_author_field(raw.get("author") or raw.get("authors")),
        repository_url=_first_url(raw.get("source")),
        release_page=_first_url(raw.get("release_page")),
        version=raw.get("version"),
        version_date=raw.get("date") or raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=resolve_media_url("vitadbtoo", raw.get("icon"), "icon"),
        screenshots=resolve_media_list("vitadbtoo", raw.get("screenshots"), "screenshot"),
        download_url=_first_url(raw.get("url") or raw.get("download_url") or raw.get("download")),
        size=_int_or_none(raw.get("size")),
        category_raw=canonicalize_external_type(raw.get("type") if raw.get("type") is not None else raw.get("category")),
        tags=parse_tags(raw.get("tags")),
        platform="vita",
    )


def normalize_neovitadb(raw: dict) -> Candidate:
    """Normalize NeoVitaDB while resolving only resources it actually publishes."""
    screenshots = resolve_media_list("neovitadb", raw.get("screenshots") or raw.get("screenshot_urls") or [], "screenshot")
    return Candidate(
        source_id="neovitadb",
        source_item_id=str(raw.get("id")) if raw.get("id") is not None else None,
        title_id=raw.get("titleid") or raw.get("title_id"),
        name=str(raw.get("name") or "").strip(),
        author_names=_as_list(raw.get("author")),
        repository_url=_first_url(raw.get("repo") or raw.get("source")),
        release_page=_first_url(raw.get("release_page")),
        version=raw.get("version"),
        version_date=raw.get("date") or raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=resolve_media_url("neovitadb", raw.get("icon") or raw.get("icon_url"), "icon"),
        screenshots=screenshots,
        download_url=_first_url(raw.get("url") or raw.get("download_url")),
        size=_int_or_none(raw.get("size") or raw.get("size_bytes")),
        category_raw=canonicalize_external_type(raw.get("category") if raw.get("category") is not None else raw.get("type")),
        tags=parse_tags(raw.get("tags")),
        platform=raw.get("platform") or "vita",
    )


def normalize_psvitaalive(raw: dict) -> Candidate:
    authors = [str(author_id) for author_id in raw.get("author_ids", [])]
    links = raw.get("links", []) if isinstance(raw.get("links"), list) else []
    downloads = [item for item in links if item.get("type") == "Download"]
    repo = next((item.get("url") for item in links if item.get("type") == "Repository"), None)
    release = next((item.get("url") for item in links if item.get("type") in {"Official Website", "Repository"}), None)
    download = next((item.get("url") for item in downloads if item.get("recommended")), None)
    if not download and downloads:
        download = downloads[0].get("url")
    return Candidate(
        source_id="local",
        source_item_id=raw.get("id"),
        title_id=raw.get("title_id"),
        name=raw.get("name", ""),
        author_names=authors,
        repository_url=repo,
        release_page=release,
        version=raw.get("version"),
        version_date=raw.get("version_date"),
        description=raw.get("description"),
        long_description=raw.get("long_description"),
        requirements=raw.get("requirements"),
        changelog=raw.get("changelog"),
        icon=raw.get("icon"),
        screenshots=list(raw.get("screenshots", [])),
        download_url=download,
        size=_int_or_none(raw.get("size")),
        category_raw=raw.get("category_id"),
        tags=parse_tags(raw.get("tags") or raw.get("subcategory_ids")),
        platform="vita",
    )
