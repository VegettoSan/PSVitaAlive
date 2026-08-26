#include "network/news_manager.hpp"
#include "network/http_client.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <cstring>
#include <string>

namespace psvitaalive {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string readWholeFile(const char* path, size_t maxBytes) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return {};
    std::string out;
    char buf[512];
    while (out.size() < maxBytes) {
        const int n = sceIoRead(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    sceIoClose(fd);
    return out;
}

void writeWholeFile(const char* path, const std::string& data) {
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir("ux0:data/psvitaalive/cache", 0777);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    if (!data.empty())
        sceIoWrite(fd, data.data(), data.size());
    sceIoClose(fd);
}

} // namespace

NewsItem NewsManager::parseText(const std::string& text) {
    NewsItem item;
    if (text.size() < 4) return item;

    // Skip UTF-8 BOM if present
    size_t pos0 = 0;
    if (text.size() >= 3 &&
        (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) {
        pos0 = 3;
    }

    bool inBody = false;
    std::string body;
    size_t pos = pos0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = end < text.size() ? end + 1 : end;

        if (!inBody) {
            const std::string t = trim(line);
            if (t == "---") {
                inBody = true;
                continue;
            }
            auto headerVal = [&](const char* key) -> std::string {
                const size_t klen = std::strlen(key);
                if (t.size() < klen) return {};
                // case-insensitive key match
                for (size_t i = 0; i < klen; ++i) {
                    char a = t[i], b = key[i];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                    if (a != b) return {};
                }
                size_t v = klen;
                while (v < t.size() && (t[v] == ' ' || t[v] == '	')) ++v;
                if (v >= t.size() || t[v] != ':') return {};
                ++v;
                return trim(t.substr(v));
            };
            {
                const std::string v = headerVal("id");
                if (!v.empty()) { item.id = v; continue; }
            }
            {
                const std::string v = headerVal("title");
                if (!v.empty()) { item.title = v; continue; }
            }
            {
                const std::string v = headerVal("enabled");
                if (!v.empty()) {
                    const std::string low = trim(v);
                    item.enabled = !(low == "false" || low == "0" || low == "no" || low == "off");
                    continue;
                }
            }
            // Ignore unknown header lines
            continue;
        }
        if (!body.empty()) body.push_back('\n');
        body += line;
    }

    // If no --- separator, treat everything after headers as body was already handled;
    // if still empty body and no separator, use remaining as body from first non-header — already done.
    item.body = body;
    if (item.title.empty()) item.title = "News";
    if (item.id.empty()) {
        // Fallback id from title so at least something is trackable
        item.id = item.title;
        for (char& c : item.id) {
            if (c == ' ') c = '-';
        }
    }
    item.valid = !item.id.empty() && item.enabled;
    return item;
}

NewsItem NewsManager::fetchRemote() {
    NewsItem empty;
    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        diagnostics::log(std::string("[News] http init failed: ") + http.lastError());
        return empty;
    }
    std::string body;
    const HttpResult hr = http.fetchToString(kRemoteUrl, body, 48 * 1024);
    http.shutdown();
    if (hr != HttpResult::Ok || body.empty()) {
        diagnostics::log(std::string("[News] fetch failed: ") +
                         (hr != HttpResult::Ok ? http.lastError() : "empty body"));
        return empty;
    }
    saveCache(body);
    NewsItem item = parseText(body);
    diagnostics::log(std::string("[News] fetched id=") + item.id +
                     " enabled=" + (item.enabled ? "1" : "0") +
                     " title=" + item.title);
    return item;
}

NewsItem NewsManager::loadCached() {
    const std::string raw = readWholeFile(kCachePath, 48 * 1024);
    if (raw.empty()) return {};
    return parseText(raw);
}

std::string NewsManager::loadSeenId() {
    std::string s = readWholeFile(kSeenPath, 256);
    return trim(s);
}

void NewsManager::saveSeenId(const std::string& id) {
    if (id.empty()) return;
    writeWholeFile(kSeenPath, id + "\n");
    diagnostics::log(std::string("[News] marked seen id=") + id);
}

void NewsManager::saveCache(const std::string& rawText) {
    writeWholeFile(kCachePath, rawText);
}

bool NewsManager::shouldAutoShow(const NewsItem& item) {
    if (!item.valid || !item.enabled) {
        diagnostics::log("[News] shouldAutoShow=0 (invalid or disabled)");
        return false;
    }
    const std::string seen = loadSeenId();
    const bool show = seen != item.id;
    diagnostics::log(std::string("[News] shouldAutoShow=") + (show ? "1" : "0") +
                     " id=" + item.id + " seen=" + (seen.empty() ? "(none)" : seen));
    return show;
}

} // namespace psvitaalive
