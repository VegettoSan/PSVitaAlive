"""Read the display title from a verified Vita package's PARAM.SFO."""
import re
import struct
import unicodedata
import zipfile


def sfo_display_name(raw):
    """Prefer the neutral full TITLE, then English TITLE_01, then STITLE.

    A bad or duplicated text entry is ignored. Never derive identity from a
    display title. Localized-only titles outside English require review/fallback.
    """
    if not isinstance(raw, bytes) or not 20 <= len(raw) <= 65536 or raw[:4] != b"\x00PSF":
        return "", ""
    try:
        _, keys, values, count = struct.unpack_from("<IIII", raw, 4)
        if not 0 < count <= 1024 or not 20 + 16*count <= keys <= values <= len(raw):
            return "", ""
        names = {}
        for i in range(count):
            ko, fmt, size, capacity, offset = struct.unpack_from("<HHIII", raw, 20 + 16*i)
            if not keys <= keys + ko < values:
                return "", ""
            end = raw.find(b"\0", keys + ko, values)
            if end < 0:
                return "", ""
            key = raw[keys + ko:end]
            if key not in (b"TITLE", b"TITLE_01", b"STITLE"):
                continue
            if key in names:
                names[key] = ""
                continue
            names[key] = ""
            if fmt != 0x0204 or not 1 <= size <= min(capacity, 1024) or values+offset+capacity > len(raw):
                continue
            data = raw[values+offset:values+offset+size]
            if not data.endswith(b"\0"):
                continue
            try:
                value = data.rstrip(b"\0").decode("utf-8")
            except UnicodeError:
                continue
            # Preserve accents and scripts, not control/bidi formatting.
            value = " ".join(value.split())
            if not value or len(value) > 128 or any(unicodedata.category(c) in {"Cc", "Cf", "Cs"} for c in value):
                continue
            if value.casefold() in {"unknown", "untitled", "title", "app", "homebrew", "default"}:
                continue
            if re.fullmatch(r"[A-Z0-9]{9}", value) and any(c.isdigit() for c in value):
                continue
            names[key] = value
        for key in (b"TITLE", b"TITLE_01", b"STITLE"):
            if names.get(key):
                return names[key], key.decode("ascii")
    except (struct.error, ValueError, OverflowError):
        pass
    return "", ""


def vpk_display_name(file):
    """Call only after the complete package and internal Title ID are verified."""
    with zipfile.ZipFile(file) as z:
        info = z.getinfo("sce_sys/param.sfo")
        if not 20 <= info.file_size <= 65536:
            return "", ""
        return sfo_display_name(z.read(info))
