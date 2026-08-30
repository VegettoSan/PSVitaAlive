#!/usr/bin/env python3
"""Fix catalog startup hangs: short validator timeouts; prefer cache; don't block forever on PS1."""
from __future__ import annotations
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
CM = ROOT / "Client PSVitaAlive/source/catalog/catalog_manager.cpp"
MAIN = ROOT / "Client PSVitaAlive/source/main.cpp"


def patch_validators() -> None:
    t = HC.read_text(encoding="utf-8")
    if "VALIDATOR_CONNECT_TIMEOUT" in t and "VALIDATOR_TOTAL_TIMEOUT" in t:
        # ensure applied inside function
        pos = t.find("HttpResult HttpClient::fetchRemoteValidators")
        if pos >= 0 and "VALIDATOR_CONNECT_TIMEOUT_SECONDS" in t[pos : pos + 2000]:
            print("http_client: validator timeouts already set")
            return

    if "VALIDATOR_CONNECT_TIMEOUT_SECONDS" not in t:
        marker = "constexpr long LOW_SPEED_TIME_ARCHIVE_SECONDS"
        idx = t.find(marker)
        if idx < 0:
            raise SystemExit("LOW_SPEED_TIME_ARCHIVE not found")
        line_end = t.find("\n", idx)
        insert = """
// Catalog etag checks are HEAD-only — never wait minutes on a stuck GitHub edge.
constexpr long VALIDATOR_CONNECT_TIMEOUT_SECONDS = 12;
constexpr long VALIDATOR_TOTAL_TIMEOUT_SECONDS = 20;
"""
        t = t[: line_end + 1] + insert + t[line_end + 1 :]

    pos = t.find("HttpResult HttpClient::fetchRemoteValidators")
    if pos < 0:
        raise SystemExit("fetchRemoteValidators not found")
    # replace only within a window of the function
    end = pos + 2800
    sub = t[pos:end]
    old_inner = """    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPEED_LIMIT);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);"""
    new_inner = """    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    // Hard ceiling: catalog checks must not hang the splash on PS1/PSP/Vita Games.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, VALIDATOR_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, VALIDATOR_TOTAL_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 0L); // disable low-speed abort for tiny HEAD
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);"""
    if "VALIDATOR_CONNECT_TIMEOUT_SECONDS" in sub and "CURLOPT_TIMEOUT" in sub:
        print("http_client: function already uses validator timeouts")
    elif old_inner not in sub:
        raise SystemExit("inner validator timeouts not found in function")
    else:
        sub = sub.replace(old_inner, new_inner, 1)
        t = t[:pos] + sub + t[end:]

    HC.write_text(t, encoding="utf-8")
    print("http_client: short validator timeouts")


def patch_catalog_manager() -> None:
    t = CM.read_text(encoding="utf-8")
    if "offline/cache fallback" in t:
        print("catalog_manager: already patched")
        return

    old = (
        "if(validatorResult!=HttpResult::Ok&&!storedEtag.empty()){"
        "outItems=std::move(cached);"
        'setStatus(State::Ready,catalog,"Using cached catalog (offline)");'
        'diagnostics::log(std::string("[CatalogManager] offline cache: ")+label(catalog));'
        "http.shutdown();return true;}"
    )
    new = (
        "if(validatorResult!=HttpResult::Ok){"
        "/* prefer local cache after validator timeout/network error — don't block startup */"
        "outItems=std::move(cached);"
        'setStatus(State::Ready,catalog,"Using cached catalog (offline)");'
        'diagnostics::log(std::string("[CatalogManager] offline/cache fallback: ")+label(catalog)'
        '+" validatorErr="+http.lastError());'
        "http.shutdown();return true;}"
    )
    if old not in t:
        t2, n = re.subn(
            r"if\(validatorResult!=HttpResult::Ok&&!storedEtag\.empty\(\)\)\{"
            r"outItems=std::move\(cached\);"
            r"setStatus\(State::Ready,catalog,\"Using cached catalog \(offline\)\"\);"
            r"diagnostics::log\(std::string\(\"\[CatalogManager\] offline cache: \"\)\+label\(catalog\)\);"
            r"http\.shutdown\(\);return true;\}",
            new,
            t,
            count=1,
        )
        if n == 0:
            raise SystemExit("offline cache branch not found")
        t = t2
    else:
        t = t.replace(old, new, 1)

    t = t.replace(
        'setStatus(State::Loading,catalog,"Checking remote catalog...");',
        'setStatus(State::Loading,catalog,"Checking remote catalog (quick)...");',
        1,
    )

    CM.write_text(t, encoding="utf-8")
    print("catalog_manager: cache fallback on any validator failure")


def patch_main_startup() -> None:
    t = MAIN.read_text(encoding="utf-8")
    old = (
        "if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;"
        'screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Checking next catalog cache...");'
        "catalogs.request(next);}"
    )
    new = (
        "if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;"
        'screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,'
        '"Checking catalog (cache ok if offline)...");'
        "catalogs.request(next);}"
    )
    count = t.count(old)
    if count == 0:
        print("main: preload message pattern not found (skip)")
    else:
        t = t.replace(old, new)
        MAIN.write_text(t, encoding="utf-8")
        print(f"main: updated {count} preload messages")


def main() -> int:
    patch_validators()
    patch_catalog_manager()
    patch_main_startup()
    print("OK: catalog startup hang mitigations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
