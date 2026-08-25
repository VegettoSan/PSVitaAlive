#include "network/error_reporter.hpp"
#include "network/http_client.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/rtc.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace psvitaalive {
namespace {

// NOTE: Anyone with the VPK can extract this URL. Rotate the webhook if abused.
constexpr const char* kDiscordWebhookUrl =
    "https://discord.com/api/webhooks/1540832184774959268/"
    "XPinil0HHmwzje7MOMXjXi0iQEHf7lHQtmZZILre3AbXMTxRLnObpYwX5yGhqzrdROWr";

constexpr uint64_t kCooldownMs = 45000ULL;
constexpr size_t kMaxLogTailBytes = 12000;
constexpr size_t kMaxEmbedDesc = 3500;

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
    // Align to next newline if we skipped the start of a line
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

std::string buildLogBlock() {
    std::string session = readFileTail("ux0:data/psvitaalive/logs/session.log", kMaxLogTailBytes);
    std::string install = readFileTail("ux0:data/psvitaalive/logs/install.log", 4000);
    std::string block;
    if (!session.empty()) {
        block += "=== session.log (tail) ===\n";
        block += session;
    }
    if (!install.empty()) {
        if (!block.empty()) block += "\n";
        block += "=== install.log (tail) ===\n";
        block += install;
    }
    if (block.empty())
        block = "(no log files found on device)";
    if (block.size() > kMaxEmbedDesc) {
        block = std::string("…[truncated]\n") + block.substr(block.size() - (kMaxEmbedDesc - 16));
    }
    return block;
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
    const std::string logs = buildLogBlock();

    std::string safeTitle = title.empty() ? "User report" : title;
    if (safeTitle.size() > 200) safeTitle.resize(200);

    std::string desc;
    desc.reserve(logs.size() + 256);
    desc += "**Version:** `";
    desc += ver;
    desc += "`\n**TitleID:** `PSVAS1178`\n";
    if (!context.empty()) {
        desc += "**Context:** ";
        desc += context.size() > 300 ? context.substr(0, 300) + "…" : context;
        desc += "\n";
    }
    desc += "```\n";
    desc += logs;
    desc += "\n```";
    if (desc.size() > 3900) desc.resize(3900);

    // Minimal Discord webhook JSON (one embed)
    std::string body;
    body.reserve(desc.size() + 512);
    body += "{\"username\":\"PSVitaAlive Reports\",";
    body += "\"embeds\":[{";
    body += "\"title\":\"";
    body += jsonEscape(safeTitle);
    body += "\",";
    body += "\"description\":\"";
    body += jsonEscape(desc);
    body += "\",";
    body += "\"color\":15158332,";
    body += "\"timestamp\":\"";
    body += jsonEscape(ts);
    body += "\",";
    body += "\"footer\":{\"text\":\"PSVitaAlive v";
    body += jsonEscape(ver);
    body += "\"}";
    body += "}]}";

    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        out.message = "HTTP init failed";
        diagnostics::log(std::string("[ErrorReport] init failed: ") + http.lastError());
        return out;
    }

    diagnostics::log("[ErrorReport] sending webhook title=" + safeTitle + " ver=" + ver);
    const HttpResult hr = http.postJson(kDiscordWebhookUrl, body);
    const int status = http.lastStatusCode();
    http.shutdown();

    // Discord returns 204 No Content or 200 on success
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
