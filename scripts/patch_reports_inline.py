#!/usr/bin/env python3
from pathlib import Path

p = Path("Client PSVitaAlive/source/network/error_reporter.cpp")
t = p.read_text(encoding="utf-8")
if "kMaxParts = 5" in t and "truncate(req.context, 1800)" in t:
    print("already applied")
    raise SystemExit(0)

t = t.replace(
    "constexpr size_t kMaxLogTailBytes = 12000;\nconstexpr size_t kMaxEmbedDesc = 3200;",
    "constexpr size_t kMaxLogTailBytes = 28000;\nconstexpr size_t kMaxEmbedDesc = 6000;",
    1,
)
t = t.replace("truncate(req.context, 400)", "truncate(req.context, 1800)", 1)

old_toks = """        static const char* kToks[] = {
            "zip_open failed", "ZipExtractor", "zip_fread failed",
            "download exceeded", "HTTP ERROR", "Install All step",
            "installation failed", "download failed", nullptr
        };"""
new_toks = """        static const char* kToks[] = {
            "zip_open failed", "zip_open_from_source failed", "EOCD pre-check failed",
            "ZipExtractor", "zip_fread failed", "Zlib error",
            "download exceeded", "size limit hit", "HTTP ERROR", "curl error",
            "Install All step", "Install All stopped",
            "installation failed", "download failed", "PromotePkg failed",
            "SSL connect error", "MediaFire", "OpenFailed", nullptr
        };"""
if old_toks not in t:
    raise SystemExit("kToks not found")
t = t.replace(old_toks, new_toks, 1)

marker = "// Discord embed field values max ~1024. Split tail into up to 3 fields."
end = 'appendEmbedField(fields, "Logs (tail)", "_(no session log)_", false);'
i0 = t.find(marker)
i1 = t.find(end)
if i0 < 0 or i1 < 0:
    raise SystemExit("log markers not found")
i1 += len(end)

new_block = (
    "// Discord embed field values max ~1024. Split into up to 5 fields; keep head+tail.\n"
    "        const size_t chunk = 900;\n"
    "        const int kMaxParts = 5;\n"
    "        std::string tail = logs;\n"
    "        if (tail.size() > chunk * static_cast<size_t>(kMaxParts)) {\n"
    "            const size_t keep = chunk * static_cast<size_t>(kMaxParts) - 20;\n"
    "            const size_t headKeep = keep / 3;\n"
    "            const size_t tailKeep = keep - headKeep;\n"
    "            tail = tail.substr(0, headKeep) + \"\\n…[truncated]…\\n\" + tail.substr(tail.size() - tailKeep);\n"
    "        }\n"
    "        if (tail.size() < logs.size()) {\n"
    "            const size_t nl = tail.find('\\n');\n"
    "            if (nl != std::string::npos && nl + 1 < tail.size())\n"
    "                tail.erase(0, nl + 1);\n"
    "        }\n"
    "        int part = 1;\n"
    "        size_t off = 0;\n"
    "        const int totalParts = (int)((tail.size() + chunk - 1) / chunk);\n"
    "        while (off < tail.size() && part <= kMaxParts) {\n"
    "            size_t n = std::min(chunk, tail.size() - off);\n"
    "            if (off + n < tail.size()) {\n"
    "                const size_t cut = tail.rfind('\\n', off + n);\n"
    "                if (cut != std::string::npos && cut > off + chunk / 2)\n"
    "                    n = cut - off + 1;\n"
    "            }\n"
    "            std::string piece = tail.substr(off, n);\n"
    "            off += n;\n"
    "            std::string logVal = \"```\\n\";\n"
    "            logVal += piece;\n"
    "            if (logVal.size() > 1000) logVal.resize(1000);\n"
    "            logVal += \"\\n```\";\n"
    "            char fname[32];\n"
    "            if (totalParts <= 1)\n"
    "                sceClibSnprintf(fname, sizeof(fname), \"Logs\");\n"
    "            else\n"
    "                sceClibSnprintf(fname, sizeof(fname), \"Logs (%d/%d)\", part, totalParts);\n"
    "            appendEmbedField(fields, fname, logVal, false);\n"
    "            ++part;\n"
    "        }\n"
    "        if (tail.empty())\n"
    "            appendEmbedField(fields, \"Logs\", \"_(no session log)_\", false);"
)
t = t[:i0] + new_block + t[i1:]

old_win = (
    "    std::string window = text.substr(start);\n"
    "    constexpr size_t kMaxWindow = 10000;\n"
    "    if (window.size() > kMaxWindow)\n"
    '        window = std::string("…[truncated]\\n") + window.substr(window.size() - kMaxWindow);\n'
    "    return window;"
)
new_win = (
    "    std::string window = text.substr(start);\n"
    "    constexpr size_t kMaxWindow = 14000;\n"
    "    if (window.size() > kMaxWindow) {\n"
    "        const size_t head = 5000;\n"
    "        const size_t tailn = kMaxWindow - head - 20;\n"
    '        window = window.substr(0, head) + "\\n…[truncated]…\\n" + window.substr(window.size() - tailn);\n'
    "    }\n"
    "    return window;"
)
if old_win not in t:
    raise SystemExit("window block not found")
t = t.replace(old_win, new_win, 1)

p.write_text(t, encoding="utf-8")
print("OK patch_reports_inline")
