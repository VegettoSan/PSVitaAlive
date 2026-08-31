#!/usr/bin/env python3
"""Filter Discord report logs to the failing file/operation (not unrelated VPK)."""
from __future__ import annotations
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ER = ROOT / "Client PSVitaAlive/source/network/error_reporter.cpp"

NEW_BUILD = r"""// Keep report logs aligned with the failure (e.g. Game Files.zip), not a prior VPK step.
std::string relevantLogWindow(const std::string& text, const std::string& fileName, const std::string& context) {
    if (text.empty()) return text;

    std::string needle = fileName;
    if (needle.empty()) {
        const std::string key = "file=";
        const size_t fp = context.rfind(key);
        if (fp != std::string::npos) {
            needle = context.substr(fp + key.size());
            while (!needle.empty() && (needle.back() == ' ' || needle.back() == '\n' || needle.back() == '\r'))
                needle.pop_back();
            const size_t sp = needle.find('|');
            if (sp != std::string::npos) needle.resize(sp);
            while (!needle.empty() && needle.back() == ' ') needle.pop_back();
        }
    }

    size_t hit = std::string::npos;
    if (!needle.empty())
        hit = text.rfind(needle);
    if (hit == std::string::npos) {
        static const char* kToks[] = {
            "zip_open failed", "ZipExtractor", "zip_fread failed",
            "download exceeded", "HTTP ERROR", "Install All step",
            "installation failed", "download failed", nullptr
        };
        for (int i = 0; kToks[i]; ++i) {
            const size_t p = text.rfind(kToks[i]);
            if (p != std::string::npos && (hit == std::string::npos || p > hit))
                hit = p;
        }
    }
    if (hit == std::string::npos)
        return text;

    static const char* kStarts[] = {
        "LINK INSTALL",
        "[UI] Install All step",
        "[Installer] request job=",
        "[Installer] installing job=",
        "HTTP BEGIN url=",
        "[DownloadManager] attempt",
        "[InstallDispatcher] detect format=",
        "[ZipExtractor]",
        nullptr
    };
    const size_t searchFloor = (hit > 120000) ? (hit - 120000) : 0;
    size_t start = std::string::npos;
    for (int i = 0; kStarts[i]; ++i) {
        size_t p = text.rfind(kStarts[i], hit);
        if (p == std::string::npos || p < searchFloor) continue;
        if (!needle.empty()) {
            const size_t regionEnd = std::min(text.size(), p + 900);
            const size_t fn = text.find(needle, p);
            if (fn == std::string::npos || fn > regionEnd)
                continue;
        }
        if (start == std::string::npos || p > start)
            start = p;
    }
    if (start == std::string::npos) {
        start = (hit > 8000) ? (hit - 8000) : 0;
        const size_t nl = text.find('\n', start);
        if (nl != std::string::npos && nl < hit)
            start = nl + 1;
    } else {
        const size_t nl = text.rfind('\n', start);
        start = (nl == std::string::npos) ? start : (nl + 1);
    }

    std::string window = text.substr(start);
    constexpr size_t kMaxWindow = 10000;
    if (window.size() > kMaxWindow)
        window = std::string("…[truncated]\n") + window.substr(window.size() - kMaxWindow);
    return window;
}

std::string buildLogBlock(const ErrorReportRequest& req) {
    std::string session = readFileTail("ux0:data/psvitaalive/logs/session.log", kMaxLogTailBytes + 8000);
    std::string install = readFileTail("ux0:data/psvitaalive/logs/install.log", 5000);

    session = relevantLogWindow(session, req.fileName, req.context);
    install = relevantLogWindow(install, req.fileName, req.context);

    std::string block;
    if (!session.empty()) {
        block += "=== session.log (relevant) ===\n";
        block += session;
    }
    if (!install.empty()) {
        if (!block.empty()) block += "\n";
        block += "=== install.log (relevant) ===\n";
        block += install;
    }
    if (block.empty())
        block = "(no log files found on device)";
    if (block.size() > kMaxEmbedDesc) {
        block = std::string("…[truncated]\n") + block.substr(block.size() - (kMaxEmbedDesc - 16));
    }
    return block;
}

"""


def main() -> int:
    t = ER.read_text(encoding="utf-8")
    if "relevantLogWindow" in t:
        print("already applied")
        return 0
    m = re.search(
        r"std::string buildLogBlock\(\)\s*\{[\s\S]*?\n\}\n\n(?=const char\* kindTag)",
        t,
    )
    if not m:
        raise SystemExit("buildLogBlock not found")
    t = t[: m.start()] + NEW_BUILD + t[m.end() :]
    if "buildLogBlock(req)" not in t:
        t = t.replace(
            "const std::string logs = buildLogBlock();",
            "const std::string logs = buildLogBlock(req);",
            1,
        )
    ER.write_text(t, encoding="utf-8")
    print("OK: relevant log window for reports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
