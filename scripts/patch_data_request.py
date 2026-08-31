#!/usr/bin/env python3
"""Add Request Data/Game Files button + Discord webhook modal to FullCatalogScreen."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HPP = ROOT / "Client PSVitaAlive/include/ui/full_catalog_screen.hpp"
CPP = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"


def patch_hpp(t: str) -> str:
    if "itemEligibleForDataRequest" in t:
        return t
    needle = """    void openReportConfirm();
    void closeReportConfirm();

    // Install All (VPK + Game Files + Data Files) wizard + sequential queue"""
    insert = """    void openReportConfirm();
    void closeReportConfirm();

    // Request Data/Game Files (Discord webhook, detail panel)
    bool dataRequestConfirmVisible_ = false;
    std::atomic<bool> dataRequestBusy_{false};
    std::atomic<bool> dataRequestDone_{false};
    std::atomic<bool> dataRequestOk_{false};
    char dataRequestResultMsg_[64] = {};
    SceUID dataRequestThread_ = -1;
    std::string dataReqName_;
    std::string dataReqTitleId_;
    std::string dataReqVersion_;
    std::string dataReqDate_;
    static int dataRequestWorkerEntry(SceSize args, void* argp);
    void openDataRequestConfirm();
    void closeDataRequestConfirm();
    void drawDataRequestConfirmOverlay();
    void trySendDataRequest();
    void pollDataRequestWorker();
    bool itemEligibleForDataRequest(const CatalogItem& item) const;

    // Install All (VPK + Game Files + Data Files) wizard + sequential queue"""
    if needle not in t:
        raise SystemExit("hpp needle not found")
    return t.replace(needle, insert, 1)


def patch_cpp(t: str) -> str:
    if "itemEligibleForDataRequest" in t and "drawDataRequestConfirmOverlay" in t and "pollDataRequestWorker();" in t:
        print("cpp already patched")
        return t

    anchor = """bool itemHasDataOrGameFiles(const CatalogItem& it) {
    return itemHasLinkType(it, "data files") || itemHasLinkType(it, "game files")
        || itemHasLinkType(it, "data file") || itemHasLinkType(it, "game file");
}"""
    helper = anchor + """

/** Categories where Data/Game Files requests make sense (catalog category_id). */
bool categoryWantsDataFiles(const std::string& category) {
    std::string c = lowerAscii(category);
    // ports / games / emulators (+ media). Skip utilities & plugins.
    return c == "ports" || c == "games" || c == "emulators" || c == "media"
        || c.find("port") != std::string::npos
        || c.find("game") != std::string::npos
        || c.find("emulator") != std::string::npos;
}
"""
    if "categoryWantsDataFiles" not in t:
        if anchor not in t:
            raise SystemExit("itemHasDataOrGameFiles anchor missing")
        t = t.replace(anchor, helper, 1)

    close_fn = """void FullCatalogScreen::closeReportConfirm() {
    reportConfirmVisible_ = false;
}
"""
    if "drawDataRequestConfirmOverlay" not in t:
        methods_path = Path(__file__).with_name("patch_data_request_methods.cpp.inc")
        # methods embedded below as METHODS_MARKER
        raise SystemExit("use workflow with full methods")

    return t


def main() -> int:
    print("use apply-data-request workflow")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
