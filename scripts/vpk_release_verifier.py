#!/usr/bin/env python3
"""Download and verify one official GitHub release VPK before automatic catalog edits.

Do not execute an installer or extract any game files. Read only the bounded
sce_sys/param.sfo entry and compare TITLE_ID with the canonical app's Title ID.
"""
from __future__ import annotations

import hashlib
import struct
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path
from urllib.parse import unquote, urlparse

MAX_VPK_BYTES = 4 * 1024**3  # Larger releases require manual review.
MAX_DOWNLOAD_SECONDS = 180  # Avoid exhausting GitHub Actions for one VPK.
MAX_SFO_BYTES = 128 * 1024
CHUNK_BYTES = 512 * 1024


def _allowed_github_host(host: str | None) -> bool:
    value = str(host or "").lower()
    return value == "github.com" or value.endswith(".githubusercontent.com")


class _SafeRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        parsed = urlparse(newurl)
        if parsed.scheme != "https" or not _allowed_github_host(parsed.hostname):
            raise ValueError("VPK download redirects outside GitHub hosts")
        return super().redirect_request(request, fp, code, msg, headers, newurl)


def release_asset_url(url: object, repo: str, tag: str) -> bool:
    if not isinstance(url, str):
        return False
    parsed = urlparse(url.strip())
    if parsed.scheme != "https" or parsed.hostname != "github.com":
        return False
    path = [unquote(part) for part in parsed.path.split("/") if part]
    parts = repo.split("/")
    return (
        len(parts) == 2 and len(path) == 6
        and path[0].lower() == parts[0].lower()
        and path[1].lower() == parts[1].lower()
        and path[2:4] == ["releases", "download"]
        and path[4] == tag
        and path[5].lower().endswith(".vpk")
        and not parsed.username and not parsed.password and not parsed.port
    )


def parse_sfo_title_id(payload: bytes) -> str:
    """Parse the binary PSF header/entry offsets (not a plain string search)."""
    if len(payload) < 20:
        raise ValueError("missing param.sfo header")
    magic, _version, keys, values, count = struct.unpack_from("<4sIIII", payload)
    if magic != b"\x00PSF" or count > 1024 or keys >= len(payload) or values >= len(payload):
        raise ValueError("invalid param.sfo structure")

    for index in range(count):
        entry = 20 + index * 16
        if entry + 16 > len(payload):
            raise ValueError("truncated param.sfo entry")
        key_offset, _fmt, value_size, padded_size, data_offset = struct.unpack_from("<HHIII", payload, entry)
        key_start = keys + key_offset
        value_start = values + data_offset
        if key_start >= len(payload) or value_size > padded_size or value_start + value_size > len(payload):
            raise ValueError("param.sfo entry exceeds file")
        key_end = payload.find(b"\x00", key_start, min(len(payload), key_start + 128))
        if key_end < 0:
            raise ValueError("invalid param.sfo key")
        if payload[key_start:key_end] == b"TITLE_ID":
            title = payload[value_start:value_start + value_size].split(b"\x00", 1)[0]
            try:
                decoded = title.decode("ascii").strip().upper()
            except UnicodeDecodeError as exc:
                raise ValueError("invalid TITLE_ID encoding") from exc
            if not decoded or len(decoded) > 16:
                raise ValueError("invalid TITLE_ID content")
            return decoded

    raise ValueError("param.sfo has no TITLE_ID")


def inspect_vpk(path: Path, expected_title_id: str) -> str:
    try:
        with zipfile.ZipFile(path) as bundle:
            candidates = [
                item for item in bundle.infolist()
                if item.filename.lower() == "sce_sys/param.sfo"
            ]
            if len(candidates) != 1:
                raise ValueError("VPK must contain exactly one sce_sys/param.sfo")
            entry = candidates[0]
            if entry.file_size > MAX_SFO_BYTES or entry.file_size <= 0:
                raise ValueError("param.sfo is missing or excessively large")
            with bundle.open(entry) as stream:
                sfo = stream.read(MAX_SFO_BYTES + 1)
            if len(sfo) != entry.file_size:
                raise ValueError("truncated param.sfo")
            actual = parse_sfo_title_id(sfo)
    except (zipfile.BadZipFile, OSError, RuntimeError, EOFError) as exc:
        raise ValueError(f"invalid or unreadable VPK archive: {exc}") from exc

    if actual != str(expected_title_id).strip().upper():
        raise ValueError(f"VPK TITLE_ID {actual!r} differs from catalog {expected_title_id!r}")
    return actual


def verify_release_vpk(url: str, *, repo: str, tag: str,
                       expected_title_id: str, expected_size: int) -> tuple[bool, str]:
    """Stream-download to a disposable temp file; never keep the VPK in git."""
    if not release_asset_url(url, repo, tag):
        return False, "VPK URL does not match the exact GitHub release tag"
    if not isinstance(expected_size, int) or isinstance(expected_size, bool):
        return False, "unknown VPK size"
    if expected_size <= 0 or expected_size > MAX_VPK_BYTES:
        return False, "VPK size outside safe automatic download limits"

    opener = urllib.request.build_opener(_SafeRedirects)
    request = urllib.request.Request(
        url, headers={"User-Agent": "PSVitaAlive-VPK-Validation/1.0", "Accept-Encoding": "identity"}
    )
    started = time.monotonic()
    digest = hashlib.sha256()
    count = 0

    try:
        with tempfile.TemporaryDirectory(prefix="psva-vpk-check-") as folder:
            filename = Path(folder) / "candidate.vpk"
            with opener.open(request, timeout=45) as response, filename.open("wb") as file:
                final_url = urlparse(response.geturl())
                if final_url.scheme != "https" or not _allowed_github_host(final_url.hostname):
                    raise ValueError("asset was served by an unexpected host")
                if response.headers.get("Content-Encoding", "identity").lower() != "identity":
                    raise ValueError("compressed HTTP transfer is not supported")
                while True:
                    if time.monotonic() - started > MAX_DOWNLOAD_SECONDS:
                        raise TimeoutError("VPK download exceeded time limit")
                    chunk = response.read(CHUNK_BYTES)
                    if not chunk:
                        break
                    count += len(chunk)
                    if count > MAX_VPK_BYTES or count > expected_size:
                        raise ValueError("VPK exceeds declared asset size")
                    file.write(chunk)
                    digest.update(chunk)
            if count != expected_size:
                raise ValueError(f"VPK size mismatch: downloaded {count}, expected {expected_size}")
            inspect_vpk(filename, expected_title_id)
    except (OSError, ValueError, TimeoutError, zipfile.BadZipFile) as exc:
        return False, str(exc)

    return True, f"VPK downloaded and TITLE_ID verified ({count} bytes, sha256={digest.hexdigest()})"
