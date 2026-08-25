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

    bool inBody = false;
    std::string body;
    size_t pos = 0;
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
            if (t.size() >= 3 && (t.compare(0, 3, "id:") == 0 || t.compare(0, 3, "ID:") == 0)) {
                item.id = trim(t.substr(3));
                continue;
            }
            if (t.size() >= 6 && (t.compare(0, 6, "title:") == 0 || t.compare(0, 6, "Title:") == 0)) {
                item.title = trim(t.substr(6));
                continue;
            }
            if (t.size() >= 8 && (t.compare(0, 8, "enabled:") == 0 || t.compare(0, 8, "Enabled:") == 0)) {
                const std::string v = trim(t.substr(8));
                item.enabled = !(v == "false" || v == "0" || v == "no" || v == "off");
                continue;
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
    if (!item.valid || !item.enabled) return false;
    const std::string seen = loadSeenId();
    return seen != item.id;
}

} // namespace psvitaalive
