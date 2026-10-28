#include "network/error_reporter.hpp"
#include "network/feedback_forum.hpp"
#include "network/http_client.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/rtc.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {

// NOTE: Anyone with the VPK can extract this URL. Rotate the webhook if abused.
constexpr const char* kDiscordWebhookUrl =
    "https://discord.com/api/webhooks/1540832184774959268/"
    "XPinil0HHmwzje7MOMXjXi0iQEHf7lHQtmZZILre3AbXMTxRLnObpYwX5yGhqzrdROWr";

// User-written messages go to public Discord forums as individual posts.
// Like the existing diagnostic webhook, these URLs are embedded in the VPK.
constexpr const char* kFeedbackProblemWebhookUrl =
    "https://discord.com/api/webhooks/1565126501815287828/"
    "nJDUU-m6flk-w9eXfcwKyEroOWLP6oRFoPuGgdE4Sx9EWi6yjREXaMt-zV8wfA9b_4FO";
constexpr const char* kFeedbackSuggestionWebhookUrl =
    "https://discord.com/api/webhooks/1565127059745939600/"
    "BnSsDn4kOJPgnEm9eZb8X-VABstgmDAeDl8QMFWvlXUNMcBLRW2zIV3FV50n0kRwPNGg";

constexpr uint64_t kCooldownMs = 45000ULL;
constexpr size_t kSessionTailBytes = 48 * 1024;
constexpr size_t kInstallTailBytes = 24 * 1024;
// Discord API: embed description <= 4096 chars, all embed text <= 6000.
// Keep one single copyable diagnostic block comfortably below both limits.
constexpr size_t kDiscordDescriptionLimit = 4096;
constexpr size_t kReportTextLimit = 3970;
constexpr size_t kMaxExcerptLines = 72;
constexpr size_t kSearchBackLines = 260;
constexpr size_t kSearchForwardLines = 36;

uint64_t g_lastReportMs = 0;

enum class ReportScope {
    Manual,
    Network,
    Zip,
    Install,
    Catalog,
    SelfUpdate,
    Other
};

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

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = asciiLower(c);
    return s;
}

bool containsNoCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    const std::string h = lowerAscii(haystack);
    const std::string n = lowerAscii(needle);
    return h.find(n) != std::string::npos;
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        std::string line = nl == std::string::npos ? text.substr(start) : text.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string fileNameFromRequest(const ErrorReportRequest& req) {
    if (!req.fileName.empty()) return trimCopy(req.fileName);
    const std::string low = lowerAscii(req.context);
    const size_t fp = low.rfind("file=");
    if (fp == std::string::npos) return {};
    std::string name = req.context.substr(fp + 5);
    const size_t pipe = name.find('|');
    if (pipe != std::string::npos) name.resize(pipe);
    const size_t nl = name.find('\n');
    if (nl != std::string::npos) name.resize(nl);
    return trimCopy(name);
}

bool hasAny(const std::string& low, const char* const* tokens) {
    for (int i = 0; tokens[i]; ++i) {
        if (low.find(tokens[i]) != std::string::npos) return true;
    }
    return false;
}

ReportScope classifyScope(const ErrorReportRequest& req) {
    if (req.kind == ErrorReportKind::Manual) return ReportScope::Manual;
    if (req.kind == ErrorReportKind::Catalog) return ReportScope::Catalog;
    if (req.kind == ErrorReportKind::SelfUpdate) return ReportScope::SelfUpdate;

    const std::string file = lowerAscii(fileNameFromRequest(req));
    const std::string low = lowerAscii(req.context + " " + file + " " + req.title);

    static const char* const kNetwork[] = {
        "curl error", "ssl", "tls", "certificate", "http status", "http error",
        "download failed", "network", "timed out", "timeout", "recv error",
        "send error", "resolve host", "mediafire", "content-range", "range mismatch",
        "size mismatch after download", "connection", nullptr
    };
    static const char* const kZip[] = {
        "zip_fread", "zlib", "zip_open", "zip extractor", "zipextractor",
        "eocd", "local header", "compressed data", "crc", "extract failed",
        "unpack failed", nullptr
    };
    static const char* const kInstall[] = {
        "promotepkg", "promoter", "installvpk", "param.sfo", "eboot.bin",
        "fakepackagebuilder", "installation failed", "install failed", nullptr
    };

    // A .zip can fail during download or extraction. Network evidence takes priority.
    if (req.kind == ErrorReportKind::DownloadFailed || hasAny(low, kNetwork)) return ReportScope::Network;
    if (hasAny(low, kZip)) return ReportScope::Zip;
    if (req.kind == ErrorReportKind::InstallFailed || hasAny(low, kInstall)) return ReportScope::Install;
    return ReportScope::Other;
}

bool imageReport(const ErrorReportRequest& req) {
    const std::string low = lowerAscii(fileNameFromRequest(req) + " " + req.context);
    return low.find(".png") != std::string::npos || low.find(".jpg") != std::string::npos ||
           low.find(".jpeg") != std::string::npos || low.find(".webp") != std::string::npos ||
           low.find("image") != std::string::npos || low.find("icon") != std::string::npos ||
           low.find("screenshot") != std::string::npos;
}

bool isNoiseLine(const std::string& line, const ErrorReportRequest& req) {
    const std::string low = lowerAscii(line);
    if (low.empty()) return true;
    if (low.find("[perf] slow frame") != std::string::npos) return true;
    if (low.find("textures=") != std::string::npos && low.find("deferred=") != std::string::npos) return true;
    if (!imageReport(req) && low.find("[imagecache]") != std::string::npos) return true;
    if (!imageReport(req) && low.find("/cache/images/") != std::string::npos) return true;
    // Rendering/navigation chatter is almost never useful for install/download reports.
    if (req.kind != ErrorReportKind::Manual && low.find("[ui] frame") != std::string::npos) return true;
    if (req.kind != ErrorReportKind::Manual && low.find("[ui] error report requested") != std::string::npos) return true;
    return false;
}

bool lineHasStrongError(const std::string& low) {
    static const char* const kStrong[] = {
        "curl error", " failed", "failure", "error=", " error ", "http status",
        "zip_fread", "zlib error", "eocd", "local header", "range mismatch",
        "size mismatch", "promotepkg failed", "sceiowrite failed", "openfailed",
        "ssl", "tls", "certificate", "aborted", "corrupt", "truncated",
        "download exceeded", "size limit hit", nullptr
    };
    return hasAny(low, kStrong);
}

bool lineMatchesScope(const std::string& low, ReportScope scope) {
    switch (scope) {
    case ReportScope::Network: {
        static const char* const k[] = {
            "http ", "[downloadmanager]", "curl", "download", "content-range", "range ",
            "resume", "retry", "archive failover", "effective_url", "ssl", "tls", "certificate",
            "mediafire", "redirect", "remote_total", "remotetotal", "validator", nullptr
        };
        return hasAny(low, k);
    }
    case ReportScope::Zip: {
        static const char* const k[] = {
            "[zipextractor]", "[zipdiag]", "zip_", "zlib", "eocd", "local header",
            "extract", "uncomp=", "comp=", "compression", "archive_size", nullptr
        };
        return hasAny(low, k);
    }
    case ReportScope::Install: {
        static const char* const k[] = {
            "[installer]", "installvpk", "promote", "promoter", "scepromoter",
            "fakepackagebuilder", "param.sfo", "eboot.bin", "package root",
            "[installdispatcher]", "vpk", "pkg", nullptr
        };
        return hasAny(low, k);
    }
    case ReportScope::Catalog: {
        static const char* const k[] = {
            "catalog", "catalog.json", "authors.json", "categories.json", "validator",
            "etag", "http ", "curl", "parse", "json", nullptr
        };
        return hasAny(low, k);
    }
    case ReportScope::SelfUpdate: {
        static const char* const k[] = {
            "self-update", "self update", "update", "release", "github", "http ",
            "curl", "vpk", "version", nullptr
        };
        return hasAny(low, k);
    }
    case ReportScope::Manual:
        return !low.empty();
    case ReportScope::Other:
    default:
        return lineHasStrongError(low) || low.find("[errorreport]") != std::string::npos;
    }
}

bool lineIsStructural(const std::string& low, ReportScope scope) {
    if (scope == ReportScope::Network) {
        // Do not pull Installer/VPK lifecycle lines into a payload download failure.
        return low.find("begin url=") != std::string::npos;
    }
    if (scope == ReportScope::Zip || scope == ReportScope::Install) {
        return low.find("install all step") != std::string::npos ||
               low.find("link install") != std::string::npos ||
               low.find("request job=") != std::string::npos ||
               low.find("installing job=") != std::string::npos ||
               low.find("begin path=") != std::string::npos ||
               low.find("detect format=") != std::string::npos;
    }
    return false;
}

std::vector<std::string> contextFingerprints(const ErrorReportRequest& req) {
    std::vector<std::string> out;
    const std::string low = lowerAscii(req.context);
    static const char* const k[] = {
        "curl error", "zip_fread", "zlib error", "http status", "eocd", "local header",
        "promotepkg", "sceiowrite", "size mismatch", "range mismatch", "mediafire",
        "ssl", "tls", "certificate", "openfailed", "download exceeded", nullptr
    };
    for (int i = 0; k[i]; ++i) {
        if (low.find(k[i]) != std::string::npos) out.emplace_back(k[i]);
    }
    // Preserve the exact curl code when present: "curl error 56", etc.
    const size_t cp = low.find("curl error ");
    if (cp != std::string::npos) {
        size_t end = cp + 11;
        while (end < low.size() && std::isdigit(static_cast<unsigned char>(low[end]))) ++end;
        if (end > cp + 11) out.push_back(low.substr(cp, end - cp));
    }
    return out;
}

bool lineMatchesFingerprint(const std::string& low, const std::vector<std::string>& fingerprints) {
    for (const auto& fp : fingerprints) {
        if (!fp.empty() && low.find(fp) != std::string::npos) return true;
    }
    return false;
}

std::string compactExcerpt(
    const std::string& text,
    const ErrorReportRequest& req,
    ReportScope scope,
    bool strictFileCorrelation,
    const char* sourceName
) {
    if (text.empty()) return {};
    const std::vector<std::string> lines = splitLines(text);
    if (lines.empty()) return {};

    const std::string file = lowerAscii(fileNameFromRequest(req));
    const std::string titleId = lowerAscii(req.app.titleId);
    const std::vector<std::string> fingerprints = contextFingerprints(req);

    bool hasFile = false;
    bool hasFingerprint = false;
    size_t best = std::string::npos;
    int bestScore = -100000;

    for (size_t i = 0; i < lines.size(); ++i) {
        if (isNoiseLine(lines[i], req)) continue;
        const std::string low = lowerAscii(lines[i]);
        const bool fileHit = !file.empty() && low.find(file) != std::string::npos;
        const bool fpHit = lineMatchesFingerprint(low, fingerprints);
        const bool strong = lineHasStrongError(low);
        const bool scopeHit = lineMatchesScope(low, scope);
        const bool titleHit = !titleId.empty() && low.find(titleId) != std::string::npos;
        hasFile = hasFile || fileHit;
        hasFingerprint = hasFingerprint || fpHit;

        int score = 0;
        if (fileHit) score += 120;
        if (fpHit) score += 90;
        if (strong) score += 55;
        if (scopeHit) score += 20;
        if (titleHit && (file.empty() || scope == ReportScope::Zip || scope == ReportScope::Install)) score += 25;
        if (score > bestScore || (score == bestScore && score > 0 && i > best)) {
            bestScore = score;
            best = i;
        }
    }

    // For a report tied to a concrete payload, install.log must prove it is about
    // that payload (filename or the same error fingerprint). This is what prevents
    // a successful VPK promotion from polluting a later Game Files.zip network error.
    if (strictFileCorrelation && !file.empty() && !hasFile && !hasFingerprint)
        return {};

    if (best == std::string::npos || bestScore <= 0) {
        if (scope != ReportScope::Manual) return {};
        best = lines.size() - 1;
    }

    const size_t begin = best > kSearchBackLines ? best - kSearchBackLines : 0;
    const size_t end = std::min(lines.size(), best + kSearchForwardLines + 1);
    std::vector<std::pair<size_t, std::string>> selected;
    selected.reserve(kMaxExcerptLines);

    for (size_t i = begin; i < end; ++i) {
        if (isNoiseLine(lines[i], req)) continue;
        const std::string low = lowerAscii(lines[i]);
        const bool fileHit = !file.empty() && low.find(file) != std::string::npos;
        const bool fpHit = lineMatchesFingerprint(low, fingerprints);
        const bool titleHit = !titleId.empty() && low.find(titleId) != std::string::npos;
        const bool titleRelevant = titleHit &&
            (file.empty() || scope == ReportScope::Zip || scope == ReportScope::Install);
        const bool relevant = fileHit || fpHit || titleRelevant || lineHasStrongError(low) ||
                              lineMatchesScope(low, scope) || lineIsStructural(low, scope);
        if (!relevant && scope != ReportScope::Manual) continue;
        selected.emplace_back(i, lines[i]);
    }

    if (scope == ReportScope::Manual && selected.size() > 36)
        selected.erase(selected.begin(), selected.end() - 36);
    if (selected.size() > kMaxExcerptLines)
        selected.erase(selected.begin(), selected.end() - kMaxExcerptLines);
    if (selected.empty()) return {};

    std::string out;
    out.reserve(3000);
    out += "--- ";
    out += sourceName;
    out += " (correlated) ---\n";
    size_t previous = selected.front().first;
    bool first = true;
    for (const auto& item : selected) {
        if (!first && item.first > previous + 2)
            out += "...\n";
        out += item.second;
        out += "\n";
        previous = item.first;
        first = false;
    }
    return out;
}

std::string buildLogBlock(const ErrorReportRequest& req) {
    diagnostics::flush(); // Snapshot current queued diagnostics before reading report tails.
    const ReportScope scope = classifyScope(req);
    const std::string sessionRaw = readFileTail("ux0:data/psvitaalive/logs/session.log", kSessionTailBytes);
    const std::string installRaw = readFileTail("ux0:data/psvitaalive/logs/install.log", kInstallTailBytes);

    // session.log is the primary source for network/catalog/UI errors. install.log
    // is stricter whenever a concrete file is known, so previous install operations
    // cannot consume the Discord budget of the actual failure.
    std::string session = compactExcerpt(sessionRaw, req, scope, false, "session.log");
    std::string install;
    if (scope == ReportScope::Install || scope == ReportScope::Zip) {
        install = compactExcerpt(installRaw, req, scope, true, "install.log");
    }

    std::string block;
    if (!session.empty()) block += session;
    if (!install.empty()) {
        if (!block.empty()) block += "\n";
        block += install;
    }
    if (block.empty()) {
        block = "(no correlated log lines found; unrelated log tails intentionally omitted)\n";
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
    if (max <= 1) return s.substr(0, max);
    return s.substr(0, max - 1) + "…";
}

std::string middleTrim(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    if (max < 40) return s.substr(0, max);
    const std::string marker = "\n...[report truncated]...\n";
    const size_t usable = max > marker.size() ? max - marker.size() : 0;
    const size_t head = usable / 3;
    const size_t tail = usable - head;
    return s.substr(0, head) + marker + s.substr(s.size() - tail);
}

std::string sanitizeCodeBlock(std::string s) {
    size_t p = 0;
    while ((p = s.find("```", p)) != std::string::npos) {
        s.replace(p, 3, "'''");
        p += 3;
    }
    return s;
}

std::string buildCopyableReportText(
    const ErrorReportRequest& req,
    const std::string& version,
    const std::string& logs
) {
    std::string meta;
    meta.reserve(900);
    meta += "TYPE: ";
    meta += kindLabel(req.kind);
    meta += " ";
    meta += kindTag(req.kind);
    meta += "\n";
    meta += "APP: ";
    meta += req.app.name.empty() ? "-" : req.app.name;
    meta += "\nTITLE_ID: ";
    meta += req.app.titleId.empty() ? "-" : req.app.titleId;
    meta += "\nAPP_VERSION: ";
    meta += req.app.version.empty() ? "-" : req.app.version;
    meta += "\nCLIENT: v";
    meta += version;
    meta += "\nSTORE_TITLE_ID: PSVAS1178\n";
    const std::string file = fileNameFromRequest(req);
    meta += "FILE: ";
    meta += file.empty() ? "-" : file;
    meta += "\nREASON: ";
    meta += req.context.empty() ? "-" : req.context;
    meta += "\n\nRELEVANT LOGS:\n";

    // Metadata/reason always win. Logs consume only the remaining budget.
    if (meta.size() >= kReportTextLimit)
        return sanitizeCodeBlock(truncate(meta, kReportTextLimit));
    const size_t logBudget = kReportTextLimit - meta.size();
    return sanitizeCodeBlock(meta + middleTrim(logs, logBudget));
}

} // namespace

// Manual feedback is deliberately a separate payload from automated diagnostic
// reports: NO buildLogBlock(), log reads, app details or device identifiers.
ErrorReportResult sendManualFeedback(const ManualFeedbackRequest& req) {
    ErrorReportResult out;
    const uint64_t cool=errorReportCooldownRemainingMs();
    if(cool>0) {
        char msg[64];
        sceClibSnprintf(msg,sizeof(msg),"Wait %llu s",
                        (unsigned long long)((cool+999)/1000ULL));
        out.message=msg;
        return out;
    }
    const auto valid=[](const std::string& s,size_t maxChars) {
        size_t chars=0; bool nonblank=false;
        for(unsigned char c:s) {
            if((c&0xC0)!=0x80) ++chars;
            if(c!=' ' && c!='\t' && c!='\r' && c!='\n') nonblank=true;
        }
        return nonblank && chars<=maxChars && s.size()<=maxChars*4;
    };
    if(!valid(req.title,64) || !valid(req.body,700)) {
        out.message="Enter title and description";
        return out;
    }
    const std::string ver=clientVersionString();
    const std::string when=isoTimestampUtc();
    const char* type=req.suggestion?"SUGGESTION":"PROBLEM REPORT";
    const char* tag=req.suggestion?"#suggestion":"#user_report";
    // No logging of users' freeform messages, either.
    std::string payload;
    payload.reserve(req.title.size()+req.body.size()+600);
    payload+="{\"thread_name\":\"";
    payload+=jsonEscape(feedbackForumThreadName(req.title,req.suggestion));
    payload+="\",\"username\":\"PSVitaAlive Feedback\",";
    payload+="\"allowed_mentions\":{\"parse\":[]},";
    payload+="\"content\":\"";
    payload+=jsonEscape(std::string("#feedback ")+tag);
    payload+="\",\"embeds\":[{\"title\":\"";
    payload+=jsonEscape(std::string(req.suggestion?"💡 ":"📝 ")+req.title);
    payload+="\",\"description\":\"";
    payload+=jsonEscape(req.body);
    payload+="\",\"color\":";
    payload+=req.suggestion?"3066993":"3447003";
    payload+=",\"fields\":[{\"name\":\"Type\",\"value\":\"";
    payload+=type;
    payload+="\",\"inline\":true},{\"name\":\"Client version\",\"value\":\"";
    payload+=jsonEscape(std::string("v")+ver);
    payload+="\",\"inline\":true},{\"name\":\"Submitted (UTC)\",\"value\":\"";
    payload+=jsonEscape(when);
    payload+="\",\"inline\":false}],\"footer\":{\"text\":\"PSVitaAlive · user feedback · no logs\"}";
    if(when.size()>10 && when.find('T')!=std::string::npos) {
        payload+=",\"timestamp\":\"";
        payload+=jsonEscape(when);
        payload+="\"";
    }
    payload+="}]}";
    HttpClient http;
    if(http.init()!=HttpResult::Ok) {
        out.message="HTTP init failed";
        return out;
    }
    const char* targetWebhook = req.suggestion ? kFeedbackSuggestionWebhookUrl : kFeedbackProblemWebhookUrl;
    const HttpResult hr=http.postJson(targetWebhook,payload);
    const int code=http.lastStatusCode();
    const std::string httpError=http.lastError();
    http.shutdown();
    if(hr==HttpResult::Ok || code==200 || code==204) {
        g_lastReportMs=sceKernelGetProcessTimeWide()/1000ULL;
        out.ok=true;
        out.message="Feedback sent";
        diagnostics::log("[Feedback] user message sent without logs");
    } else {
        out.message=httpError.empty()?"Send failed":truncate(httpError,40);
        diagnostics::log("[Feedback] send failed status="+std::to_string(code));
    }
    return out;
}

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
    std::string low = lowerAscii(title);
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
    const std::string copyText = buildCopyableReportText(req, ver, logs);

    std::string safeTitle = req.title.empty() ? kindLabel(req.kind) : req.title;
    if (safeTitle.size() > 200) safeTitle.resize(200);

    // Searchable tokens remain outside the embed so Discord search keeps working.
    std::string content;
    {
        const char* kt = kindTag(req.kind);
        content += kt;
        content += " ";
        content += kt[0] == '#' ? (kt + 1) : kt;
    }
    content += " ";
    const std::string appTag = titleIdTag(req.app.titleId);
    if (!appTag.empty()) {
        content += appTag;
        content += " ";
        content += appTag.substr(1);
        content += " ";
    }
    if (!req.app.name.empty())
        content += truncate(req.app.name, 80);
    else if (!req.app.titleId.empty())
        content += req.app.titleId;
    else
        content += "(no app)";
    if (!req.context.empty()) {
        content += "\n";
        content += truncate(req.context, 220);
    }
    if (content.size() > 1800) content.resize(1800);

    // One code block = one Copy action in Discord. No Logs (1/5), Logs (2/5), etc.
    std::string desc = "```text\n" + copyText + "\n```";
    if (desc.size() > kDiscordDescriptionLimit)
        desc = desc.substr(0, kDiscordDescriptionLimit - 4) + "\n```";

    std::string body;
    body.reserve(desc.size() + content.size() + 600);
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
    body += "\"footer\":{\"text\":\"PSVitaAlive v";
    body += jsonEscape(ver);
    body += " · correlated diagnostics · one-block copy\"}";
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
                     " file=" + (fileNameFromRequest(req).empty() ? "-" : fileNameFromRequest(req)) +
                     " scope=" + std::to_string(static_cast<int>(classifyScope(req))) +
                     " payload_chars=" + std::to_string(copyText.size()) +
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
