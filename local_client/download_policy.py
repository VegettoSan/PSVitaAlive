"""Safe naming and conservative mirror deduplication for the PC catalog client.

Links without reliable matching identifiers are NEVER assumed to be copies
merely because their file sizes are identical. This matters when archiving.
"""
from __future__ import annotations

import re
from pathlib import Path
from urllib.parse import unquote, urlparse


def safe_component(value: str, default: str = "item") -> str:
    """A single portable filesystem component, never a parent directory."""
    text = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", str(value or "")).strip().strip(". ")
    if not text or text in {".", ".."}:
        return default
    # Windows reserves device names even when they have an extension.
    stem = text.split(".", 1)[0].upper()
    if stem in {"CON", "PRN", "AUX", "NUL"} or re.fullmatch(r"COM[1-9]|LPT[1-9]", stem):
        text = "_" + text
    return text[:180] or default


def safe_filename(url: str) -> str:
    name = unquote(urlparse(url).path).rsplit("/", 1)[-1]
    return safe_component(name, "download.bin")


def source_rank(link: dict) -> tuple:
    host = (urlparse(link.get("url", "")).hostname or "").lower()
    return (
        bool(link.get("recommended")),
        host == "archive.org" or host.endswith(".archive.org"),
    )


def file_identity(link: dict) -> tuple:
    """Collapse only links proven to represent the same file.

    An exact URL is identical. A shared hash is strong evidence. A matching
    PlayStation content ID *and* size, region, title and kind is useful for
    game PKG mirrors. Size alone, or an absent content ID, proves nothing.
    """
    url = str(link.get("url", ""))
    size = int(link.get("size") or 0)
    digest = link.get("hash") or link.get("sha256") or link.get("hash2")
    if isinstance(digest, str) and digest.strip():
        return ("hash", digest.strip().lower(), size)
    content_id = link.get("content_id")
    if content_id and size > 0:
        return (
            "content_id", str(link.get("type") or "").lower(),
            str(content_id), str(link.get("region") or ""),
            str(link.get("title_id") or ""), size,
        )
    return ("url", url)


def unique_destination(path: Path, reserved: set[str]) -> Path:
    """Reserve a destination in this batch; never overwrite a different URL."""
    candidate = path
    n = 2
    while str(candidate).casefold() in reserved:
        candidate = path.with_name(f"{path.stem}__{n}{path.suffix}")
        n += 1
    reserved.add(str(candidate).casefold())
    return candidate
