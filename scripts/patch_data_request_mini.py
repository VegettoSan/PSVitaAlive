#!/usr/bin/env python3
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
HPP = ROOT / 'Client PSVitaAlive/include/ui/full_catalog_screen.hpp'
CPP = ROOT / 'Client PSVitaAlive/source/ui/full_catalog_screen.cpp'
HELPER = (ROOT / 'scripts/data_request_helper.inc').read_text()
METHODS = (ROOT / 'scripts/data_request_methods.inc').read_text()

def main():
    hpp = HPP.read_text()
    if 'itemEligibleForDataRequest' not in hpp:
        needle = '''    void openReportConfirm();
    void closeReportConfirm();

    // Install All (VPK + Game Files + Data Files) wizard + sequential queue'''
        insert = '''    void openReportConfirm();
    void closeReportConfirm();

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

    // Install All (VPK + Game Files + Data Files) wizard + sequential queue'''
        if needle not in hpp: raise SystemExit('hpp')
        hpp = hpp.replace(needle, insert, 1)
        HPP.write_text(hpp)
    t = CPP.read_text()
    if 'pollDataRequestWorker();' in t and 'Request data' in t:
        print('already'); return
    if 'categoryWantsDataFiles' not in t:
        a = '''bool itemHasDataOrGameFiles(const CatalogItem& it) {
    return itemHasLinkType(it, "data files") || itemHasLinkType(it, "game files")
        || itemHasLinkType(it, "data file") || itemHasLinkType(it, "game file");
}'''
        t = t.replace(a, a + '\n\n' + HELPER + '\n', 1)
    if 'drawDataRequestConfirmOverlay' not in t:
        c = '''void FullCatalogScreen::closeReportConfirm() {
    reportConfirmVisible_ = false;
}
'''
        t = t.replace(c, c + '\n' + METHODS + '\n', 1)
    old_btn = '''        vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");
    }
    drawDetailContent(it, x, y, w, h);'''
    new_btn = '''        vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");
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
    drawDetailContent(it, x, y, w, h);'''
    t = t.replace(old_btn, new_btn, 1)
    t = t.replace('if(reportConfirmVisible_)drawReportConfirmOverlay();', 'if(reportConfirmVisible_)drawReportConfirmOverlay();if(dataRequestConfirmVisible_)drawDataRequestConfirmOverlay();')
    rm = 'if(reportConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeReportConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeReportConfirm();trySendErrorReport("Manual report from UI","User confirmed report from footer");return;}return;}'
    dm = 'if(dataRequestConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeDataRequestConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeDataRequestConfirm();trySendDataRequest();return;}return;}' + rm
    t = t.replace(rm, dm, 1)
    osq = 'if(pressed&SCE_CTRL_SQUARE){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast("Filters cleared",1200);}return;}'
    nsq = 'if(pressed&SCE_CTRL_SQUARE){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast("Filters cleared",1200);return;}if(state_.mode==UiMode::SPLIT_DETAIL&&state_.activePanel==UiPanel::Detail&&!state_.linkNavigation){const int si=selectedIndex();if(si>=0&&itemEligibleForDataRequest(catalogView()[si])){openDataRequestConfirm();return;}}return;}'
    t = t.replace(osq, nsq, 1)
    if 'pollDataRequestWorker();' not in t:
        t = t.replace('bool FullCatalogScreen::updateAndDraw(){\n    if(!ready_)return false;\n    pollReportWorker();\n', 'bool FullCatalogScreen::updateAndDraw(){\n    if(!ready_)return false;\n    pollReportWorker();\n    pollDataRequestWorker();\n', 1)
    if 'http_client.hpp' not in t:
        t = t.replace('#include "network/error_reporter.hpp"', '#include "network/error_reporter.hpp"\n#include "network/http_client.hpp"', 1)
    CPP.write_text(t)
    print('OK mini patch')

if __name__ == '__main__':
    main()
