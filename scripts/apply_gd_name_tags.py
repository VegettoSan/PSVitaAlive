#!/usr/bin/env python3
"""Treat Download links named Game/Data Files as those types (tags + filter)."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FCS = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"


def main() -> int:
    text = FCS.read_text(encoding="utf-8")
    if "linkNameSuggestsDataFiles" in text:
        print("full_catalog_screen: already patched")
        return 0

    marker = "bool itemHasLinkType(const CatalogItem& it, const char* needle) {"
    if marker not in text:
        raise SystemExit("itemHasLinkType not found")

    helpers = '''
/** True when link name indicates Data/Game Files (case-insensitive, normalized). */
bool linkNameSuggestsDataFiles(const CatalogLink& l) {
    const std::string n = normalizeLinkType(l.name);
    if (n.empty()) return false;
    if (n == "data" || n == "datafiles") return true;
    if (n.find("data files") != std::string::npos) return true;
    if (n.find("data file") != std::string::npos) return true;
    return false;
}

bool linkNameSuggestsGameFiles(const CatalogLink& l) {
    const std::string n = normalizeLinkType(l.name);
    if (n.empty()) return false;
    if (n == "gamefiles") return true;
    if (n.find("game files") != std::string::npos) return true;
    if (n.find("game file") != std::string::npos) return true;
    return false;
}

bool isDownloadLikeType(const std::string& t) {
    return t == "download" || t == "downloads" || t == "mirror";
}

'''
    text = text.replace(marker, helpers + marker, 1)

    old_iht = '''bool itemHasLinkType(const CatalogItem& it, const char* needle) {
    const std::string want = needle;
    for (const auto& link : it.linkDetails) {
        const std::string t = normalizeLinkType(link.type);
        if (t == want) return true;
        if (want == "data files" && (t == "data file" || t == "datafiles" || t == "data")) return true;
        if (want == "game files" && (t == "game file" || t == "gamefiles")) return true;
        if (want == "mod" && (t == "mods")) return true;
        if (want == "mods" && (t == "mod")) return true;
        if (want == "update" && (t == "updates")) return true;
        if (want == "updates" && (t == "update")) return true;
        if (want == "pkg" && (t == "pkgs")) return true;
        if (want == "download" && (t == "downloads" || t == "mirror")) return true;
        if (want == "downloads" && (t == "download" || t == "mirror")) return true;
    }
    return false;
}'''
    new_iht = '''bool itemHasLinkType(const CatalogItem& it, const char* needle) {
    const std::string want = needle;
    for (const auto& link : it.linkDetails) {
        const std::string t = normalizeLinkType(link.type);
        if (t == want) return true;
        if (want == "data files" || want == "data file") {
            if (t == "data files" || t == "data file" || t == "datafiles" || t == "data") return true;
            // Download-type links named like "Game/Data Files ..." also count.
            if (isDownloadLikeType(t) && linkNameSuggestsDataFiles(link)) return true;
            continue;
        }
        if (want == "game files" || want == "game file") {
            if (t == "game files" || t == "game file" || t == "gamefiles") return true;
            if (isDownloadLikeType(t) && linkNameSuggestsGameFiles(link)) return true;
            continue;
        }
        if (want == "mod" && (t == "mods")) return true;
        if (want == "mods" && (t == "mod")) return true;
        if (want == "update" && (t == "updates")) return true;
        if (want == "updates" && (t == "update")) return true;
        if (want == "pkg" && (t == "pkgs")) return true;
        if (want == "download" && (t == "downloads" || t == "mirror")) return true;
        if (want == "downloads" && (t == "download" || t == "mirror")) return true;
    }
    return false;
}'''
    if old_iht not in text:
        raise SystemExit("itemHasLinkType block not found")
    text = text.replace(old_iht, new_iht, 1)

    old_sz = '''std::string itemExtraDataGameSize(const CatalogItem& it) {
    std::string dataSz;
    for (const auto& link : it.linkDetails) {
        if (link.size.empty()) continue;
        const std::string typ = normalizeLinkType(link.type);
        if (typ == "game files" || typ == "game file" || typ == "gamefiles")
            return link.size;
        if ((typ == "data files" || typ == "data file" || typ == "datafiles" || typ == "data")
            && dataSz.empty())
            dataSz = link.size;
    }
    return dataSz;
}'''
    new_sz = '''std::string itemExtraDataGameSize(const CatalogItem& it) {
    std::string dataSz;
    for (const auto& link : it.linkDetails) {
        if (link.size.empty()) continue;
        const std::string typ = normalizeLinkType(link.type);
        const bool gameByType = (typ == "game files" || typ == "game file" || typ == "gamefiles");
        const bool dataByType = (typ == "data files" || typ == "data file" || typ == "datafiles" || typ == "data");
        const bool gameByName = isDownloadLikeType(typ) && linkNameSuggestsGameFiles(link);
        const bool dataByName = isDownloadLikeType(typ) && linkNameSuggestsDataFiles(link);
        if (gameByType || gameByName)
            return link.size;
        if ((dataByType || dataByName) && dataSz.empty())
            dataSz = link.size;
    }
    return dataSz;
}'''
    if old_sz not in text:
        raise SystemExit("itemExtraDataGameSize not found")
    text = text.replace(old_sz, new_sz, 1)

    old_card = '''            const std::string typ = normalizeLinkType(link.type);
            if (typ == "data files" || typ == "data file" || typ == "datafiles" || typ == "data"
                || typ == "game files" || typ == "game file" || typ == "gamefiles")
                continue;'''
    new_card = '''            const std::string typ = normalizeLinkType(link.type);
            if (typ == "data files" || typ == "data file" || typ == "datafiles" || typ == "data"
                || typ == "game files" || typ == "game file" || typ == "gamefiles")
                continue;
            if (isDownloadLikeType(typ) && (linkNameSuggestsDataFiles(link) || linkNameSuggestsGameFiles(link)))
                continue;'''
    if old_card not in text:
        raise SystemExit("itemCardSizeLabel skip not found")
    text = text.replace(old_card, new_card, 1)

    old_cls = '''LinkSection classifyLinkSection(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (t == "download" || t == "downloads" || t == "mirror") return LinkSection::Downloads;
    if (t == "data files" || t == "data file" || t == "datafiles" || t == "data") return LinkSection::DataFiles;
    if (t == "game files" || t == "game file" || t == "gamefiles") return LinkSection::GameFiles;'''
    new_cls = '''LinkSection classifyLinkSection(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (t == "download" || t == "downloads" || t == "mirror") {
        // Name can reclassify a Download link into Game/Data Files sections.
        if (linkNameSuggestsGameFiles(l)) return LinkSection::GameFiles;
        if (linkNameSuggestsDataFiles(l)) return LinkSection::DataFiles;
        return LinkSection::Downloads;
    }
    if (t == "data files" || t == "data file" || t == "datafiles" || t == "data") return LinkSection::DataFiles;
    if (t == "game files" || t == "game file" || t == "gamefiles") return LinkSection::GameFiles;'''
    if old_cls not in text:
        raise SystemExit("classifyLinkSection not found")
    text = text.replace(old_cls, new_cls, 1)

    old_dl = '''bool isDownloadTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    return t == "download" || t == "downloads";
}

bool isGameFilesTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    return t == "game files" || t == "game file" || t == "gamefiles";
}

bool isDataFilesTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    return t == "data files" || t == "data file" || t == "datafiles" || t == "data";
}'''
    new_dl = '''bool isDownloadTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (!(t == "download" || t == "downloads")) return false;
    // Named Game/Data Files downloads belong to those categories, not VPK downloads.
    if (linkNameSuggestsGameFiles(l) || linkNameSuggestsDataFiles(l)) return false;
    return true;
}

bool isGameFilesTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (t == "game files" || t == "game file" || t == "gamefiles") return true;
    if (isDownloadLikeType(t) && linkNameSuggestsGameFiles(l)) return true;
    return false;
}

bool isDataFilesTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (t == "data files" || t == "data file" || t == "datafiles" || t == "data") return true;
    if (isDownloadLikeType(t) && linkNameSuggestsDataFiles(l)) return true;
    return false;
}'''
    if old_dl not in text:
        raise SystemExit("is*TypeLink block not found")
    text = text.replace(old_dl, new_dl, 1)

    FCS.write_text(text, encoding="utf-8")
    print("OK: name-based Data/Game Files tags + filter")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
