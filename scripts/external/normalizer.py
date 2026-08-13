#!/usr/bin/env python3
from __future__ import annotations

import re
from urllib.parse import urlparse


def normalize_text(value: str | None) -> str:
    if not isinstance(value, str):
        return ""
    return re.sub(r"\\s+", " ", value.strip()).lower()


def canonical_repo(url: str | None) -> str:
    if not isinstance(url, str) or not url.strip():
        return ""
    value = url.strip().rstrip("/")
    if value.endswith(".git"):
        value = value[:-4]
    parsed = urlparse(value if "://" in value else "https://" + value)
    if parsed.netloc.lower() != "github.com":
        return ""
    parts = [p for p in parsed.path.split("/") if p]
    if len(parts) < 2:
        return ""
    return f"{parts[0].lower()}/{parts[1].lower()}"


def normalize_version(value: str | None) -> tuple:
    raw = normalize_text(value)
    raw = re.sub(r"^(version|ver|v)[:\\s-]*", "", raw)
    match = re.findall(r"\\d+", raw)
    if not match:
        return (0,)
    return tuple(int(item) for item in match)
