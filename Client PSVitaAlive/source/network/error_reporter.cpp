#include "network/error_reporter.hpp"
#include "network/http_client.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/rtc.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>

namespace psvitaalive {
namespace {

// NOTE: Anyone with the VPK can extract this URL. Rotate the webhook if abused.
constexpr const char* kDiscordWebhookUrl =
    "https://discord.com/api/webhooks/1540832184774959268/"
    "XPinil0HHmwzje7MOMXjXi0iQEHf7lHQtmZZILre3AbXMTxRLnObpYwX5yGhqzrdROWr";

constexpr uint64_t kCooldownMs = 45000ULL;
constexpr size_t kMaxLogTailBytes = 28000;
constexpr size_t kMaxEmbedDesc = 6000;

uint64_t g_lastReportMs = 0;

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 16);
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

std::string readFileTail(const char* path, size_t maxBytes) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return {};
    const SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (size <= 0) {
        sceIoClose(fd);
        return {};
    }
    SceOff start = 0;
    size_t toRead = static_cast<size_t>(size);
    if (toRead > maxBytes) {
        start = static_cast<SceOff>(size - static_cast<SceOff>(maxBytes));
        toRead = maxBytes;
    }
    sceIoLseek(fd, start, SCE_SEEK_SET);
    std::string buf;
    buf.resize(toRead);
    const int n = sceIoRead(fd, &buf[0], toRead);
    sceIoClose(fd);
    if (n <= 0) return {};
    buf.resize(static_cast<size_t>(n));
    if (start > 0) {
        const size_t nl = buf.find('\n');
        if (nl != std::string::npos && nl + 1 < buf.size())
            buf.erase(0, nl + 1);
    }
    return buf;
}

std::string isoTimestampUtc() {
    SceDateTime dt{};
    if (sceRtcGetCurrentClock(&dt, 0) < 0) {
        const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
        char buf[48];
        sceClibSnprintf(buf, sizeof(buf), "session+%llums", (unsigned long long)ms);
        return buf;
    }
    char buf[40];
    sceClibSnprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
                    (int)dt.year, (int)dt.month, (int)dt.day,
                    (int)dt.hour, (int)dt.minute, (int)dt.second);
    return buf;
}

std::string clientVersionString() {
#ifdef PSVITAALIVE_VERSION
    return std::string(PSVITAALIVE_VERSION);
#else
    return "unknown";
#endif
}

// Keep report logs aligned with the failure (e.g. Game Files.zip), not a prior VPK step.
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
            "zip_open failed", "zip_open_from_source failed", "EOCD pre-check failed",
            "ZipExtractor", "zip_fread failed", "Zlib error",
            "download exceeded", "size limit hit", "HTTP ERROR", "curl error",
            "Install All step", "Install All stopped",
            "installation failed", "download failed", "PromotePkg failed",
            "SSL connect error", "MediaFire", "OpenFailed", nullptr
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
    constexpr size_t kMaxWindow = 14000;
    if (window.size() > kMaxWindow) {
        const size_t head = 5000;
        const size_t tailn = kMaxWindow - head - 20;
        window = window.substr(0, head) + "\n…[truncated]…\n" + window.substr(window.size() - tailn);
    }
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

const char* kindTag(ErrorReportKind k) {
    switch (k) {
    case ErrorReportKind::Manual:          return "#manual";
    case ErrorReportKind::InstallFailed:   return "#install_failed";
    case ErrorReportKind::DownloadFailed:  return "#download_failed";
    case ErrorReportKind::Catalog:         return "#catalog";
    case ErrorReportKind::SelfUpdate:      return "#self_update";
    default:                               return "#other";
    }
}

const char* kindLabel(ErrorReportKind k) {
    switch (k) {
    case ErrorReportKind::Manual:          return "Manual report";
    case ErrorReportKind::InstallFailed:   return "Install failed";
    case ErrorReportKind::DownloadFailed:  return "Download failed";
    case ErrorReportKind::Catalog:         return "Catalog";
    case ErrorReportKind::SelfUpdate:      return "Self-update";
    default:                               return "Other";
    }
}

int kindColor(ErrorReportKind k) {
    switch (k) {
    case ErrorReportKind::Manual:          return 0x5865F2;
    case ErrorReportKind::InstallFailed:   return 0xE03232;
    case ErrorReportKind::DownloadFailed:  return 0xE08A10;
    case ErrorReportKind::Catalog:         return 0x3BD960;
    case ErrorReportKind::SelfUpdate:      return 0x9B59B6;
    default:                               return 0x95A5A6;
    }
}

std::string titleIdTag(const std::string& tid) {
    std::string out;
    out.reserve(tid.size() + 4);
    out += "#app_";
    for (unsigned char c : tid) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out.push_back(static_cast<char>(c));
    }
    if (out.size() <= 5) return {};
    return out;
}

std::string truncate(std::string s, size_t max) {
    if (s.size() <= max) return s;
    return s.substr(0, max - 1) + "…";
}

void appendEmbedField(std::string& body, const char* name, const std::string& value, bool inlineField) {
    if (value.empty()) return;
    body += "{\"name\":\"";
    body += jsonEscape(name);
    body += "\",\"value\":\"";
    body += jsonEscape(truncate(value, 900));
    body += "\",\"inline\":";
    body += inlineField ? "true" : "false";
    body += "},";
}

} // namespace

uint64_t errorReportCooldownRemainingMs() {
    if (g_lastReportMs == 0) return 0;
    const uint64_t now = sceKernelGetProcessTimeWide() / 1000ULL;
    if (now < g_lastReportMs) return 0;
    const uint64_t elapsed = now - g_lastReportMs;
    if (elapsed >= kCooldownMs) return 0;
    return kCooldownMs - elapsed;
}

ErrorReportResult sendErrorReport(const std::string& title, const std::string& context) {
    ErrorReportRequest req;
    req.title = title;
    req.context = context;
    std::string low = title;
    for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (low.find("manual") != std::string::npos)
        req.kind = ErrorReportKind::Manual;
    else if (low.find("install") != std::string::npos)
        req.kind = ErrorReportKind::InstallFailed;
    else if (low.find("download") != std::string::npos)
        req.kind = ErrorReportKind::DownloadFailed;
    else if (low.find("catalog") != std::string::npos)
        req.kind = ErrorReportKind::Catalog;
    else if (low.find("self-update") != std::string::npos || low.find("self update") != std::string::npos)
        req.kind = ErrorReportKind::SelfUpdate;
    else
        req.kind = ErrorReportKind::Other;
    return sendErrorReport(req);
}

ErrorReportResult sendErrorReport(const ErrorReportRequest& req) {
    ErrorReportResult out;
    const uint64_t cool = errorReportCooldownRemainingMs();
    if (cool > 0) {
        char m[64];
        sceClibSnprintf(m, sizeof(m), "Wait %llu s", (unsigned long long)((cool + 999) / 1000ULL));
        out.message = m;
        return out;
    }

    const std::string ver = clientVersionString();
    const std::string ts = isoTimestampUtc();
    const std::string logs = buildLogBlock(req);

    std::string safeTitle = req.title.empty() ? kindLabel(req.kind) : req.title;
    if (safeTitle.size() > 200) safeTitle.resize(200);

    // Discord search: plain tokens work; leading "#" often does not (channel syntax).
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
    if (content.size() > 1800) content.resize(1800);

    std::string desc;
    desc.reserve(512);
    desc += "**Type:** ";
    desc += kindLabel(req.kind);
    desc += " `";
    desc += kindTag(req.kind);
    desc += "`\n";
    if (!req.context.empty()) {
        desc += "**Reason:** ";
        desc += truncate(req.context, 1800);
        desc += "\n";
    }
    if (!req.fileName.empty()) {
        desc += "**File:** `";
        desc += truncate(req.fileName, 120);
        desc += "`\n";
    }
    desc += "_Discord search: type the tag **without** # (e.g. `install_failed` or `app_PCSG00000`)._";

    std::string fields;
    fields.reserve(512);
    appendEmbedField(fields, "App", req.app.name.empty() ? "—" : req.app.name, true);
    appendEmbedField(fields, "Title ID", req.app.titleId.empty() ? "—" : req.app.titleId, true);
    if (!req.app.version.empty())
        appendEmbedField(fields, "App version", req.app.version, true);
    appendEmbedField(fields, "Client", std::string("v") + ver, true);
    appendEmbedField(fields, "Store TitleID", "PSVAS1178", true);
    {
        // Discord embed field values max ~1024. Split into up to 5 fields; keep head+tail.
        const size_t chunk = 900;
        const int kMaxParts = 5;
        std::string tail = logs;
        if (tail.size() > chunk * static_cast<size_t>(kMaxParts)) {
            const size_t keep = chunk * static_cast<size_t>(kMaxParts) - 20;
            const size_t headKeep = keep / 3;
            const size_t tailKeep = keep - headKeep;
            tail = tail.substr(0, headKeep) + "\n…[truncated]…\n" + tail.substr(tail.size() - tailKeep);
        }
        if (tail.size() < logs.size()) {
            const size_t nl = tail.find('\n');
            if (nl != std::string::npos && nl + 1 < tail.size())
                tail.erase(0, nl + 1);
        }
        int part = 1;
        size_t off = 0;
        const int totalParts = (int)((tail.size() + chunk - 1) / chunk);
        while (off < tail.size() && part <= kMaxParts) {
            size_t n = std::min(chunk, tail.size() - off);
            if (off + n < tail.size()) {
                const size_t cut = tail.rfind('\n', off + n);
                if (cut != std::string::npos && cut > off + chunk / 2)
                    n = cut - off + 1;
            }
            std::string piece = tail.substr(off, n);
            off += n;
            std::string logVal = "```\n";
            logVal += piece;
            if (logVal.size() > 1000) logVal.resize(1000);
            logVal += "\n```";
            char fname[32];
            if (totalParts <= 1)
                sceClibSnprintf(fname, sizeof(fname), "Logs");
            else
                sceClibSnprintf(fname, sizeof(fname), "Logs (%d/%d)", part, totalParts);
            appendEmbedField(fields, fname, logVal, false);
            ++part;
        }
        if (tail.empty())
            appendEmbedField(fields, "Logs", "_(no session log)_", false);
    }
    if (!fields.empty() && fields.back() == ',') fields.pop_back();

    std::string body;
    body.reserve(desc.size() + fields.size() + content.size() + 400);
    body += "{\"username\":\"PSVitaAlive Reports\",";
    body += "\"content\":\"";
    body += jsonEscape(content);
    body += "\",";
    body += "\"embeds\":[{";
    body += "\"title\":\"";
    body += jsonEscape(safeTitle);
    body += "\",";
    body += "\"description\":\"";
    body += jsonEscape(desc);
    body += "\",";
    body += "\"color\":";
    {
        char cbuf[16];
        sceClibSnprintf(cbuf, sizeof(cbuf), "%d", kindColor(req.kind));
        body += cbuf;
    }
    body += ",";
    body += "\"timestamp\":\"";
    body += jsonEscape(ts);
    body += "\",";
    if (!fields.empty()) {
        body += "\"fields\":[";
        body += fields;
        body += "],";
    }
    body += "\"footer\":{\"text\":\"PSVitaAlive v";
    body += jsonEscape(ver);
    body += " · search #tags to filter\"}";
    body += "}]}";

    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        out.message = "HTTP init failed";
        diagnostics::log(std::string("[ErrorReport] init failed: ") + http.lastError());
        return out;
    }

    diagnostics::log("[ErrorReport] sending webhook title=" + safeTitle +
                     " kind=" + kindTag(req.kind) +
                     " app=" + (req.app.titleId.empty() ? "-" : req.app.titleId) +
                     " ver=" + ver);
    const HttpResult hr = http.postJson(kDiscordWebhookUrl, body);
    const int status = http.lastStatusCode();
    http.shutdown();

    if (hr == HttpResult::Ok || status == 204 || status == 200) {
        g_lastReportMs = sceKernelGetProcessTimeWide() / 1000ULL;
        out.ok = true;
        out.message = "Report sent";
        diagnostics::log("[ErrorReport] sent OK status=" + std::to_string(status));
        return out;
    }

    out.message = http.lastError().empty() ? "Send failed" : http.lastError();
    if (out.message.size() > 40) out.message.resize(40);
    diagnostics::log(std::string("[ErrorReport] failed status=") + std::to_string(status) +
                     " err=" + http.lastError());
    return out;
}

} // namespace psvitaalive
