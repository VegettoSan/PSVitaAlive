#!/usr/bin/env python3
"""Keep ordinary PSVitaAlive UI statuses provider-neutral across all locale packs.

Provider identity belongs in source attribution and diagnostics, not generic
activity/status/error strings that would need changing for every new host.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1] / "Client PSVitaAlive" / "assets" / "lang"
PROVIDERS = re.compile(r"archive\.org|mediafire|github|vitadb", re.IGNORECASE)
GENERIC_PREFIXES = (
    "HTTP_STAGE_", "ARCHIVE_WARNING_", "ARCHIVE_DOWNLOAD_FRIENDLY_",
    "INSTALL_ERR_", "HINT_SELF_UPDATE", "INFO_SELF_UPDATE_",
    "TOAST_CHECKING_UPDATES", "NET_LINK_", "NET_PAUSED_",
)
NETWORK_KEYS = {
    "NET_LINK_DISCONNECTED", "NET_LINK_CONNECTING",
    "NET_PAUSED_DISCONNECTED", "NET_PAUSED_SOURCE_UNREACHABLE",
    "NET_CONNECTED_SOURCE_UNREACHABLE",
}


def check_text(path: Path) -> list[str]:
    errors: list[str] = []
    found_network_keys: set[str] = set()
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.lstrip().startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key in NETWORK_KEYS:
            if key in found_network_keys or not value.strip():
                errors.append(f"{path.name}:{line_no}: duplicate or empty {key}")
            found_network_keys.add(key)
        if any(key.startswith(prefix) for prefix in GENERIC_PREFIXES):
            if PROVIDERS.search(value):
                errors.append(f"{path.name}:{line_no}: {key} names a provider")
    for missing in sorted(NETWORK_KEYS - found_network_keys):
        errors.append(f"{path.name}: missing localized {missing}")
    return errors


def main() -> None:
    packs = sorted(ROOT.glob("*.lang"))
    assert packs, f"No language packs found under {ROOT}"
    failures = [failure for pack in packs for failure in check_text(pack)]
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"Provider-neutral UI messages verified in {len(packs)} language packs.")


if __name__ == "__main__":
    main()
