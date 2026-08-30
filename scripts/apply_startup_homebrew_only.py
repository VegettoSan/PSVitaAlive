#!/usr/bin/env python3
"""Startup: Homebrew only; use local catalog cache immediately; hard timeout on catalog GET."""
from __future__ import annotations
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CM = ROOT / "Client PSVitaAlive/source/catalog/catalog_manager.cpp"
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
MAIN = ROOT / "Client PSVitaAlive/source/main.cpp"


def patch_load_catalog() -> None:
    t = CM.read_text(encoding="utf-8")
    if "cache first — no network on critical path" in t:
        print("catalog_manager: cache-first already")
        return

    old = (
        "if(haveCache){std::vector<ui::CatalogItem>cached;if(CatalogParser::parseFile(path,cached)&&!cached.empty()){"
        "std::string storedText,storedEtag,storedModified;parseValidators(readTextFile(meta,storedText)?storedText:std::string(),storedEtag,storedModified);"
        "std::string remoteEtag,remoteModified;setStatus(State::Loading,catalog,\"Checking remote catalog (quick)...\");"
        "const HttpResult validatorResult=http.fetchRemoteValidators(url,remoteEtag,remoteModified);"
        "if(validatorResult==HttpResult::Ok&&validatorsMatch(storedEtag,storedModified,remoteEtag,remoteModified)){"
        "outItems=std::move(cached);setStatus(State::Ready,catalog,\"Using cached catalog\");"
        "diagnostics::log(std::string(\"[CatalogManager] cache valid: \")+label(catalog));http.shutdown();return true;}"
        "if(validatorResult!=HttpResult::Ok){/* prefer local cache after validator timeout/network error — don't block startup */"
        "outItems=std::move(cached);setStatus(State::Ready,catalog,\"Using cached catalog (offline)\");"
        "diagnostics::log(std::string(\"[CatalogManager] offline/cache fallback: \")+label(catalog)"
        "+\" validatorErr=\"+http.lastError());http.shutdown();return true;}}}"
    )
    new = (
        "if(haveCache){std::vector<ui::CatalogItem>cached;"
        "if(CatalogParser::parseFile(path,cached)&&!cached.empty()){"
        "/* cache first — no network on critical path (uninstall keeps ux0:data/psvitaalive) */"
        "outItems=std::move(cached);"
        "setStatus(State::Ready,catalog,\"Using cached catalog\");"
        "diagnostics::log(std::string(\"[CatalogManager] cache first (skip remote check): \")+label(catalog));"
        "http.shutdown();return true;}"
        "diagnostics::log(std::string(\"[CatalogManager] cache present but parse failed: \")+label(catalog));"
        "}"
    )
    if old not in t:
        m = re.search(
            r"if\(haveCache\)\{.*?\}\s*setStatus\(State::Loading,catalog,\"Downloading catalog",
            t,
            flags=re.S,
        )
        if not m:
            raise SystemExit("haveCache block not found")
        t = t[: m.start()] + new + "setStatus(State::Loading,catalog,\"Downloading catalog" + t[m.end() :]
        print("catalog_manager: cache-first via regex")
    else:
        t = t.replace(old, new, 1)
        print("catalog_manager: cache-first exact")

    CM.write_text(t, encoding="utf-8")


def patch_github_download_timeout() -> None:
    t = HC.read_text(encoding="utf-8")
    if "GITHUB_TOTAL_TIMEOUT_SECONDS" in t and "CURLOPT_TIMEOUT, GITHUB_TOTAL" in t:
        print("http_client: github timeout already")
        return
    if "GITHUB_TOTAL_TIMEOUT_SECONDS" not in t:
        anchor = "constexpr long VALIDATOR_TOTAL_TIMEOUT_SECONDS = 20;"
        if anchor not in t:
            raise SystemExit("validator total const missing")
        t = t.replace(
            anchor,
            anchor + "\nconstexpr long GITHUB_TOTAL_TIMEOUT_SECONDS = 60; // catalogs/JSON must not hang splash\n",
            1,
        )
    pos = t.find("const bool isArchive")
    if pos < 0:
        raise SystemExit("isArchive not found")
    frag = "curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);"
    idx = t.find(frag, pos)
    if idx < 0:
        raise SystemExit("connect timeout in download loop not found")
    if "GITHUB_TOTAL_TIMEOUT_SECONDS" in t[idx : idx + 200]:
        print("http_client: timeout already near connect")
    else:
        line_end = t.find("\n", idx)
        add = """
        if (isGithub) {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, GITHUB_TOTAL_TIMEOUT_SECONDS);
        }
"""
        t = t[: line_end + 1] + add + t[line_end + 1 :]
    HC.write_text(t, encoding="utf-8")
    print("http_client: GitHub total timeout 60s")


def patch_main_homebrew_only() -> None:
    t = MAIN.read_text(encoding="utf-8")
    if "startup: Homebrew only" in t:
        print("main: already homebrew-only startup")
        return

    m = re.search(
        r"if\(startupCatalogs\)\{\s*"
        r"if\(readyCatalog==psvitaalive::ui::CatalogType::Homebrew\)\{[\s\S]*?"
        r"homebrewReady=true;\s*\}[\s\S]*?"
        r"\+\+preloadIndex;[\s\S]*?"
        r"all catalogs ready; waiting for image warmup choice\"\);\}"
        r"\s*\}",
        t,
    )
    if not m:
        raise SystemExit("startup Ready chain not found")

    new = """if(startupCatalogs){
                // startup: Homebrew only — other tabs load on demand (avoids PS1/PSP splash hangs)
                if(readyCatalog==psvitaalive::ui::CatalogType::Homebrew){
                    screen.setCatalogItems(ready);
                    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
                    homebrewReady=true;
                }
                startupCatalogs=false;
                startupImageChoicePending=true;
                startupImagesJobs.clear();
                startupImageSeen.clear();
                // Only Homebrew icons for optional warmup list (other catalogs not preloaded)
                if(homebrewReady){
                    collectCatalogImages(startupImagesJobs,startupImageSeen,images,
                        startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew],false);
                }
                screen.setCatalogLoading(false,"",0,(uint64_t)startupImagesJobs.size(),"Catalog ready");
                psvitaalive::diagnostics::log("[Startup] Homebrew ready; secondary catalogs load on tab switch");
            }"""
    t = t[: m.start()] + new + t[m.end() :]

    mf = re.search(
        r"else if\(startupCatalogs&&cs\.state==psvitaalive::CatalogManager::State::Failed\)\{[\s\S]*?"
        r"all catalogs processed; waiting for image warmup choice\"\);",
        t,
    )
    if mf:
        fail_new = """else if(startupCatalogs&&cs.state==psvitaalive::CatalogManager::State::Failed){
            psvitaalive::diagnostics::log(std::string("[Startup] catalog failed: ")+cs.label+" error="+cs.error);
            startupCatalogs=false;
            startupImageChoicePending=true;
            screen.setCatalogLoading(false,"",0,0,"Catalog unavailable — check network");
            psvitaalive::diagnostics::log("[Startup] Homebrew failed; aborting splash preload");"""
        t = t[: mf.start()] + fail_new + t[mf.end() :]
        print("main: simplified Failed startup branch")
    else:
        print("main: Failed branch pattern skip")

    MAIN.write_text(t, encoding="utf-8")
    print("main: Homebrew-only startup")


def main() -> int:
    patch_load_catalog()
    patch_github_download_timeout()
    patch_main_homebrew_only()
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
