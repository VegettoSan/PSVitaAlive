#!/usr/bin/env python3
"""Add Request Data/Game Files button + Discord webhook modal to FullCatalogScreen."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HPP = ROOT / "Client PSVitaAlive/include/ui/full_catalog_screen.hpp"
CPP = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"

HPP_NEEDLE = """    void openReportConfirm();
    void closeReportConfirm();

    // Install All (VPK + Game Files + Data Files) wizard + sequential queue"""

HPP_INSERT = """    void openReportConfirm();
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

HELPER = r'''
/** Categories where Data/Game Files requests make sense (catalog category_id). */
bool categoryWantsDataFiles(const std::string& category) {
    std::string c = lowerAscii(category);
    // ports / games / emulators (+ media). Skip utilities & plugins.
    return c == "ports" || c == "games" || c == "emulators" || c == "media"
        || c.find("port") != std::string::npos
        || c.find("game") != std::string::npos
        || c.find("emulator") != std::string::npos;
}
'''

METHODS = r'''
bool FullCatalogScreen::itemEligibleForDataRequest(const CatalogItem& item) const {
    if (itemHasDataOrGameFiles(item)) return false;
    if (state_.catalog == CatalogType::VitaGames || state_.catalog == CatalogType::PspGames
        || state_.catalog == CatalogType::Ps1Games) return true;
    return categoryWantsDataFiles(item.category);
}

void FullCatalogScreen::openDataRequestConfirm() {
    if (dataRequestBusy_.load() || reportConfirmVisible_ || newsVisible_) return;
    if (installAllPhase_ != InstallAllPhase::Hidden) return;
    const int i = selectedIndex();
    if (i < 0) return;
    const CatalogItem& it = catalogView()[i];
    if (!itemEligibleForDataRequest(it)) return;
    dataReqName_ = it.name;
    dataReqTitleId_ = it.titleId;
    dataReqVersion_ = it.version;
    dataReqDate_ = it.versionDate;
    dataRequestConfirmVisible_ = true;
}

void FullCatalogScreen::closeDataRequestConfirm() {
    dataRequestConfirmVisible_ = false;
}

void FullCatalogScreen::drawDataRequestConfirmOverlay() {
    if (!dataRequestConfirmVisible_ || !font_) return;
    const unsigned ACC = ACCENT;
    const int w = 560, h = 280;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 160));
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, w, 3, ACC);
    vita2d_draw_rectangle(x, y + 3, 3, h - 6, ACC);
    vita2d_draw_rectangle(x + w - 3, y + 3, 3, h - 6, BORDER);
    vita2d_draw_rectangle(x, y + h - 3, w, 3, BORDER);

    vita2d_pgf_draw_text(font_, x + 24, y + 36, ACC, 0.62f, "REQUEST DATA / GAME FILES");
    vita2d_pgf_draw_text(font_, x + 24, y + 72, WHITE, 0.60f, "This app has no Data/Game Files links.");
    vita2d_pgf_draw_text(font_, x + 24, y + 100, TEXT, 0.54f, "Send a request so we can look for them.");
    vita2d_pgf_draw_text(font_, x + 24, y + 122, TEXT, 0.54f, "It may take several days — we will add them");
    vita2d_pgf_draw_text(font_, x + 24, y + 144, TEXT, 0.54f, "when available. Thank you for your patience.");

    const int by = y + h - 56, bh = 40, bw = 180, gap = 24;
    const int bxCancel = x + (w - (bw * 2 + gap)) / 2;
    const int bxSend = bxCancel + bw + gap;

    vita2d_draw_rectangle(bxCancel, by, bw, bh, SURFACE2);
    vita2d_draw_rectangle(bxCancel, by, bw, 1, BORDER);
    vita2d_draw_rectangle(bxCancel, by + bh - 1, bw, 1, BORDER);
    {
        const char* lab = "O  Cancel";
        const float sc = 0.62f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, bxCancel + (bw - tw) / 2, by + 27, WHITE, sc, lab);
    }
    vita2d_draw_rectangle(bxSend, by, bw, bh, ACC);
    {
        const char* lab = "X  Send";
        const float sc = 0.62f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, bxSend + (bw - tw) / 2, by + 27, BG, sc, lab);
    }
}

namespace {
// Same Discord webhook as error reports (rotate if abused).
constexpr const char* kDataRequestWebhookUrl =
    "https://discord.com/api/webhooks/1540832184774959268/"
    "XPinil0HHmwzje7MOMXjXi0iQEHf7lHQtmZZILre3AbXMTxRLnObpYwX5yGhqzrdROWr";

std::string dataReqJsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                sceClibSnprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}
} // namespace

int FullCatalogScreen::dataRequestWorkerEntry(SceSize args, void* argp) {
    (void)args;
    FullCatalogScreen* self = *reinterpret_cast<FullCatalogScreen**>(argp);
    if (!self) return 0;

    auto esc = dataReqJsonEscape;
    std::string body = "{";
    body += "\"embeds\":[{";
    body += "\"title\":\"Data / Game Files request\",";
    body += "\"color\":3447003,";
    body += "\"fields\":[";
    body += "{\"name\":\"App\",\"value\":\"" + esc(self->dataReqName_.empty() ? "(unknown)" : self->dataReqName_) + "\",\"inline\":true},";
    body += "{\"name\":\"Title ID\",\"value\":\"" + esc(self->dataReqTitleId_.empty() ? "—" : self->dataReqTitleId_) + "\",\"inline\":true},";
    body += "{\"name\":\"Version\",\"value\":\"" + esc(self->dataReqVersion_.empty() ? "—" : self->dataReqVersion_) + "\",\"inline\":true},";
    body += "{\"name\":\"Date\",\"value\":\"" + esc(self->dataReqDate_.empty() ? "—" : self->dataReqDate_) + "\",\"inline\":true},";
    body += "{\"name\":\"Message\",\"value\":\"User requested Data/Game Files for this app (may take days).\",\"inline\":false}";
    body += "]";
    body += "}]";
    body += "}";

    HttpClient http;
    bool ok = false;
    if (http.init() == HttpResult::Ok) {
        const HttpResult hr = http.postJson(kDataRequestWebhookUrl, body);
        ok = (hr == HttpResult::Ok);
        if (!ok) {
            diagnostics::log(std::string("[DataRequest] webhook failed: ") + http.lastError());
        } else {
            diagnostics::log("[DataRequest] sent name=" + self->dataReqName_ + " tid=" + self->dataReqTitleId_);
        }
        http.shutdown();
    } else {
        diagnostics::log("[DataRequest] HTTP init failed");
    }

    sceClibSnprintf(self->dataRequestResultMsg_, sizeof(self->dataRequestResultMsg_),
                    ok ? "Request sent" : "Request failed");
    self->dataRequestOk_.store(ok);
    self->dataRequestDone_.store(true);
    self->dataRequestBusy_.store(false);
    return 0;
}

void FullCatalogScreen::trySendDataRequest() {
    if (dataRequestBusy_.load()) {
        showToast("Request already in progress", 1500);
        return;
    }
    dataRequestBusy_.store(true);
    dataRequestDone_.store(false);
    dataRequestOk_.store(false);
    dataRequestResultMsg_[0] = '\0';

    FullCatalogScreen* self = this;
    dataRequestThread_ = sceKernelCreateThread(
        "PSVitaAliveDataReq", &FullCatalogScreen::dataRequestWorkerEntry,
        0x10000100, 32 * 1024, 0, 0, nullptr);
    if (dataRequestThread_ < 0) {
        dataRequestBusy_.store(false);
        showToast("Could not start request", 1800);
        return;
    }
    sceKernelStartThread(dataRequestThread_, sizeof(self), &self);
    showToast("Sending request…", 1200);
}

void FullCatalogScreen::pollDataRequestWorker() {
    if (!dataRequestDone_.load()) return;
    dataRequestDone_.store(false);
    if (dataRequestThread_ >= 0) {
        sceKernelWaitThreadEnd(dataRequestThread_, nullptr, nullptr);
        sceKernelDeleteThread(dataRequestThread_);
        dataRequestThread_ = -1;
    }
    showToast(dataRequestResultMsg_[0] ? dataRequestResultMsg_ : (dataRequestOk_.load() ? "Request sent" : "Request failed"),
              2200);
}
'''


def patch_hpp(t: str) -> str:
    if "itemEligibleForDataRequest" in t:
        print("hpp already patched")
        return t
    if HPP_NEEDLE not in t:
        raise SystemExit("hpp needle not found")
    return t.replace(HPP_NEEDLE, HPP_INSERT, 1)


def patch_cpp(t: str) -> str:
    if "itemEligibleForDataRequest" in t and "drawDataRequestConfirmOverlay" in t and "pollDataRequestWorker();" in t:
        print("cpp already patched")
        return t

    # include http_client if missing
    if '#include "network/http_client.hpp"' not in t:
        t = t.replace(
            '#include "network/error_reporter.hpp"',
            '#include "network/error_reporter.hpp"\n#include "network/http_client.hpp"',
            1,
        )

    # helper after itemHasDataOrGameFiles
    anchor = """bool itemHasDataOrGameFiles(const CatalogItem& it) {
    return itemHasLinkType(it, "data files") || itemHasLinkType(it, "game files")
        || itemHasLinkType(it, "data file") || itemHasLinkType(it, "game file");
}"""
    if "categoryWantsDataFiles" not in t:
        if anchor not in t:
            raise SystemExit("itemHasDataOrGameFiles anchor missing")
        t = t.replace(anchor, anchor + "\n" + HELPER, 1)

    # methods after closeReportConfirm
    close_fn = """void FullCatalogScreen::closeReportConfirm() {
    reportConfirmVisible_ = false;
}
"""
    if "drawDataRequestConfirmOverlay" not in t:
        if close_fn not in t:
            raise SystemExit("closeReportConfirm not found")
        t = t.replace(close_fn, close_fn + "\n" + METHODS + "\n", 1)

    # button under Select links
    old_btn = """        vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");
    }
    drawDetailContent(it, x, y, w, h);"""
    new_btn = """        vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");
        if (itemEligibleForDataRequest(it)) {
            const int rbx = bx, rby = by + bh + 6, rbw = bw, rbh = 26;
            vita2d_draw_rectangle(rbx, rby, rbw, rbh, SURFACE2);
            vita2d_draw_rectangle(rbx, rby, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx, rby, 1, rbh, ACCENT);
            vita2d_draw_rectangle(rbx, rby + rbh - 1, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx + rbw - 1, rby, 1, rbh, ACCENT);
            vita2d_pgf_draw_text(font_, rbx + 6, rby + 18, ACCENT, 0.48f, "□ Request data");
        }
    }
    drawDetailContent(it, x, y, w, h);"""
    if "□ Request data" not in t:
        if old_btn not in t:
            raise SystemExit("Select links button anchor missing")
        t = t.replace(old_btn, new_btn, 1)

    # draw overlay next to report overlay (both occurrences)
    old_draw = "if(reportConfirmVisible_)drawReportConfirmOverlay();"
    new_draw = "if(reportConfirmVisible_)drawReportConfirmOverlay();if(dataRequestConfirmVisible_)drawDataRequestConfirmOverlay();"
    if "if(dataRequestConfirmVisible_)drawDataRequestConfirmOverlay()" not in t:
        if t.count(old_draw) < 1:
            raise SystemExit("drawReportConfirmOverlay call missing")
        t = t.replace(old_draw, new_draw)

    # input: data request modal before report modal
    old_in = 'if(reportConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeReportConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeReportConfirm();trySendErrorReport("Manual report from UI","User confirmed report from footer");return;}return;}'
    new_in = 'if(dataRequestConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeDataRequestConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeDataRequestConfirm();trySendDataRequest();return;}return;}' + old_in
    if 'if(dataRequestConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeDataRequestConfirm()' not in t:
        if old_in not in t:
            raise SystemExit("reportConfirm input block missing")
        t = t.replace(old_in, new_in, 1)

    # Square: open data request when eligible in detail
    old_sq = 'if(pressed&SCE_CTRL_SQUARE){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast("Filters cleared",1200);}return;}'
    new_sq = 'if(pressed&SCE_CTRL_SQUARE){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast("Filters cleared",1200);return;}if(state_.mode==UiMode::SPLIT_DETAIL&&state_.activePanel==UiPanel::Detail&&!state_.linkNavigation){const int si=selectedIndex();if(si>=0&&itemEligibleForDataRequest(catalogView()[si])){openDataRequestConfirm();return;}}return;}'
    if old_sq in t:
        t = t.replace(old_sq, new_sq, 1)

    # poll in updateAndDraw
    old_upd = """bool FullCatalogScreen::updateAndDraw(){\n    if(!ready_)return false;\n    pollReportWorker();\n"""
    new_upd = """bool FullCatalogScreen::updateAndDraw(){\n    if(!ready_)return false;\n    pollReportWorker();\n    pollDataRequestWorker();\n"""
    if "pollDataRequestWorker();" not in t:
        if old_upd not in t:
            raise SystemExit("updateAndDraw anchor missing")
        t = t.replace(old_upd, new_upd, 1)

    return t


def main() -> int:
    hpp = patch_hpp(HPP.read_text())
    HPP.write_text(hpp)
    print("hpp patched")
    cpp = patch_cpp(CPP.read_text())
    CPP.write_text(cpp)
    print("OK full data-request patch applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
