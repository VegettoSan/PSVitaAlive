#!/usr/bin/env python3
"""Fix Discord reports: fill App/TitleID, longer logs, Discord-searchable tags."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ER = ROOT / "Client PSVitaAlive/source/network/error_reporter.cpp"
FCS = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"
HPP = ROOT / "Client PSVitaAlive/include/ui/full_catalog_screen.hpp"


def patch_er() -> None:
    text = ER.read_text(encoding="utf-8")
    if "Logs (%d/%d)" in text:
        print("error_reporter: already patched")
        return

    if "#include <algorithm>" not in text:
        text = text.replace(
            '#include <string>\n',
            '#include <algorithm>\n#include <string>\n',
            1,
        )

    # Larger log tail from disk
    text = text.replace(
        "constexpr size_t kMaxLogTailBytes = 10000;",
        "constexpr size_t kMaxLogTailBytes = 12000;",
        1,
    )

    # Improve content tags for Discord search (# is unreliable in Discord search)
    old_content = '''    std::string content;
    content += kindTag(req.kind);
    content += " ";
    const std::string appTag = titleIdTag(req.app.titleId);
    if (!appTag.empty()) {
        content += appTag;
        content += " ";
    }
    if (!req.app.name.empty())
        content += truncate(req.app.name, 80);
    else if (!req.app.titleId.empty())
        content += req.app.titleId;
    else
        content += "(no app)";
    if (content.size() > 1800) content.resize(1800);'''

    new_content = '''    // Discord search: plain tokens work; leading "#" often does not (channel syntax).
    // Put both: "install_failed" for search and "#install_failed" for readability.
    std::string content;
    {
        const char* kt = kindTag(req.kind); // e.g. #install_failed
        content += kt;
        content += " ";
        // Plain token without hash for Discord search (search: install_failed)
        if (kt[0] == '#') content += (kt + 1);
        else content += kt;
    }
    content += " ";
    const std::string appTag = titleIdTag(req.app.titleId);
    if (!appTag.empty()) {
        content += appTag;
        content += " ";
        if (appTag.size() > 1 && appTag[0] == '#') content += appTag.substr(1);
        content += " ";
    }
    if (!req.app.name.empty())
        content += truncate(req.app.name, 80);
    else if (!req.app.titleId.empty())
        content += req.app.titleId;
    else
        content += "(no app)";
    if (content.size() > 1800) content.resize(1800);'''

    if old_content not in text:
        raise SystemExit("error_reporter: content block not found")
    text = text.replace(old_content, new_content, 1)

    old_hint = '''    desc += "_Filter in Discord search with the `#` tags above._";'''
    new_hint = '''    desc += "_Discord search: type the tag **without** # (e.g. `install_failed` or `app_PCSG00000`)._";'''
    if old_hint not in text:
        raise SystemExit("error_reporter: filter hint not found")
    text = text.replace(old_hint, new_hint, 1)

    # Multi-field logs (Discord field value limit ~1024)
    old_logs = '''    {
        std::string logVal = "```\\n";
        logVal += logs;
        logVal += "\\n```";
        if (logVal.size() > 1000)
            logVal = "```\\n" + logs.substr(logs.size() > 980 ? logs.size() - 980 : 0) + "\\n```";
        appendEmbedField(fields, "Logs (tail)", logVal, false);
    }'''

    new_logs = '''    {
        // Discord embed field values max ~1024. Split tail into up to 3 fields.
        const size_t chunk = 900;
        std::string tail = logs;
        if (tail.size() > chunk * 3)
            tail = tail.substr(tail.size() - chunk * 3);
        // Prefer starting on a line boundary
        if (tail.size() < logs.size()) {
            const size_t nl = tail.find('\\n');
            if (nl != std::string::npos && nl + 1 < tail.size())
                tail.erase(0, nl + 1);
        }
        int part = 1;
        size_t off = 0;
        const int totalParts = (int)((tail.size() + chunk - 1) / chunk);
        while (off < tail.size() && part <= 3) {
            size_t n = std::min(chunk, tail.size() - off);
            // try not to cut mid-line for middle chunks
            if (off + n < tail.size()) {
                const size_t cut = tail.rfind('\\n', off + n);
                if (cut != std::string::npos && cut > off + chunk / 2)
                    n = cut - off + 1;
            }
            std::string piece = tail.substr(off, n);
            off += n;
            std::string logVal = "```\\n";
            logVal += piece;
            if (logVal.size() > 1000) logVal.resize(1000);
            logVal += "\\n```";
            char fname[32];
            if (totalParts <= 1)
                sceClibSnprintf(fname, sizeof(fname), "Logs (tail)");
            else
                sceClibSnprintf(fname, sizeof(fname), "Logs (%d/%d)", part, totalParts);
            appendEmbedField(fields, fname, logVal, false);
            ++part;
        }
        if (tail.empty())
            appendEmbedField(fields, "Logs (tail)", "_(no session log)_", false);
    }'''

    if old_logs not in text:
        raise SystemExit("error_reporter: logs block not found")
    text = text.replace(old_logs, new_logs, 1)

    # Footer hint
    text = text.replace(
        'body += " · search #tags to filter";}',
        'body += " · search tags without # (install_failed)";}',
        1,
    )

    ER.write_text(text, encoding="utf-8")
    print("error_reporter: patched")


def patch_fcs_hpp() -> None:
    text = HPP.read_text(encoding="utf-8")
    if "reportAppName_" in text:
        print("hpp: already has report app fields")
        return
    old = '''    std::string reportTitle_;
    std::string reportContext_;'''
    new = '''    std::string reportTitle_;
    std::string reportContext_;
    std::string reportAppName_;
    std::string reportAppTitleId_;
    std::string reportAppVersion_;
    std::string reportFileName_;'''
    if old not in text:
        raise SystemExit("hpp: report fields not found")
    HPP.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("hpp: patched")


def patch_fcs() -> None:
    text = FCS.read_text(encoding="utf-8")
    if "reportAppName_" in text and "ErrorReportRequest req" in text:
        print("fcs: already patched")
        return

    # Worker uses full request
    old_worker = '''    const auto res = ::psvitaalive::sendErrorReport(self->reportTitle_, self->reportContext_);'''
    new_worker = '''    ::psvitaalive::ErrorReportRequest req;
    req.title = self->reportTitle_;
    req.context = self->reportContext_;
    req.app.name = self->reportAppName_;
    req.app.titleId = self->reportAppTitleId_;
    req.app.version = self->reportAppVersion_;
    req.fileName = self->reportFileName_;
    {
        std::string low = req.title;
        for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (low.find("manual") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::Manual;
        else if (low.find("install") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::InstallFailed;
        else if (low.find("download") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::DownloadFailed;
        else if (low.find("catalog") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::Catalog;
        else if (low.find("self-update") != std::string::npos || low.find("self update") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::SelfUpdate;
        else
            req.kind = ::psvitaalive::ErrorReportKind::Other;
    }
    const auto res = ::psvitaalive::sendErrorReport(req);'''
    if old_worker not in text:
        raise SystemExit("fcs: worker send not found")
    text = text.replace(old_worker, new_worker, 1)

    old_try = '''void FullCatalogScreen::trySendErrorReport(const std::string& title, const std::string& context) {
    if (reportUiState_ == 1 || reportBusy_.load()) return;
    if (reportThread_ >= 0) return;

    reportTitle_ = title;
    reportContext_ = context;
    reportUiState_ = 1;'''

    new_try = '''void FullCatalogScreen::trySendErrorReport(const std::string& title, const std::string& context) {
    if (reportUiState_ == 1 || reportBusy_.load()) return;
    if (reportThread_ >= 0) return;

    reportTitle_ = title;
    reportContext_ = context;
    reportAppName_.clear();
    reportAppTitleId_.clear();
    reportAppVersion_.clear();
    reportFileName_.clear();
    // Prefer current catalog selection + any install result titleId.
    {
        const int si = selectedIndex();
        if (si >= 0 && si < (int)catalogView().size()) {
            const CatalogItem& it = catalogView()[si];
            reportAppName_ = it.name;
            reportAppTitleId_ = it.titleId;
            reportAppVersion_ = it.version;
        }
        if (reportAppTitleId_.empty() && !installResultTitleId_.empty())
            reportAppTitleId_ = installResultTitleId_;
        if (!installProgressFile_.empty())
            reportFileName_ = installProgressFile_;
    }
    reportUiState_ = 1;'''

    if old_try not in text:
        raise SystemExit("fcs: trySend start not found")
    text = text.replace(old_try, new_try, 1)

    # Sync fallback path also needs full request - replace sendErrorReport(title, context) in trySend
    old_fb = '''        const auto res = ::psvitaalive::sendErrorReport(title, context);
        reportUiState_ = res.ok ? 2 : 3;
        reportUiUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 3500ULL;
        showToast(res.ok ? "Report sent" : (res.message.empty() ? "Report failed" : res.message), 2200);
        return;
    }
    FullCatalogScreen* self = this;'''

    new_fb = '''        ::psvitaalive::ErrorReportRequest req;
        req.title = reportTitle_;
        req.context = reportContext_;
        req.app.name = reportAppName_;
        req.app.titleId = reportAppTitleId_;
        req.app.version = reportAppVersion_;
        req.fileName = reportFileName_;
        {
            std::string low = req.title;
            for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (low.find("manual") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::Manual;
            else if (low.find("install") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::InstallFailed;
            else if (low.find("download") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::DownloadFailed;
            else if (low.find("catalog") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::Catalog;
            else if (low.find("self-update") != std::string::npos || low.find("self update") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::SelfUpdate;
            else
                req.kind = ::psvitaalive::ErrorReportKind::Other;
        }
        const auto res = ::psvitaalive::sendErrorReport(req);
        reportUiState_ = res.ok ? 2 : 3;
        reportUiUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 3500ULL;
        showToast(res.ok ? "Report sent" : (res.message.empty() ? "Report failed" : res.message), 2200);
        return;
    }
    FullCatalogScreen* self = this;'''

    if old_fb not in text:
        raise SystemExit("fcs: sync fallback not found")
    text = text.replace(old_fb, new_fb, 1)

    FCS.write_text(text, encoding="utf-8")
    print("fcs: patched")


def main() -> int:
    patch_er()
    patch_fcs_hpp()
    patch_fcs()
    print("OK: report fixes applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
