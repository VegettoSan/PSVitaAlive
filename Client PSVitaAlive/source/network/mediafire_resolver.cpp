#include "network/mediafire_resolver.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace psvitaalive {
namespace {

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool containsIgnoreCase(const std::string& hay, const char* needle) {
    if (!needle) return false;
    return toLowerAscii(hay).find(toLowerAscii(needle)) != std::string::npos;
}

std::string b64Decode(const std::string& in) {
    static const int8_t kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,-1,-1,62,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::string out;
    out.reserve(in.size() * 3 / 4 + 4);
    int val = 0;
    int valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const int d = kDec[c];
        if (d < 0) return {};
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string htmlEntityDecode(std::string s) {
    const std::pair<const char*, const char*> replacements[] = {
        {"&amp;", "&"},
        {"&quot;", "\""},
        {"&#39;", "'"},
        {"&#x27;", "'"},
        {"&#61;", "="},
        {"&#x3D;", "="},
    };
    for (const auto& r : replacements) {
        size_t p = 0;
        while ((p = s.find(r.first, p)) != std::string::npos) {
            s.replace(p, std::strlen(r.first), r.second);
            p += std::strlen(r.second);
        }
    }
    size_t p = 0;
    while ((p = s.find("\\/", p)) != std::string::npos) {
        s.replace(p, 2, "/");
        ++p;
    }
    return s;
}

bool looksLikeHttpUrl(const std::string& s) {
    return s.size() > 12 &&
        (s.compare(0, 8, "https://") == 0 || s.compare(0, 7, "http://") == 0);
}

bool isDirectMediaFireUrl(const std::string& s) {
    if (!looksLikeHttpUrl(s)) return false;
    const std::string u = toLowerAscii(s);
    return u.find("mediafire.com") != std::string::npos &&
           u.find("/file/") == std::string::npos &&
           u.find("download") != std::string::npos;
}

std::string extractUrlAt(const std::string& html, size_t pos) {
    if (pos == std::string::npos || pos >= html.size()) return {};
    size_t end = pos;
    while (end < html.size()) {
        const char c = html[end];
        if (c == '"' || c == '\'' || c == '<' || c == '>' || c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
        ++end;
    }
    std::string u = htmlEntityDecode(html.substr(pos, end - pos));
    while (!u.empty() && (u.back() == '\\' || u.back() == ')' || u.back() == ',' || u.back() == ';')) u.pop_back();
    return u;
}

bool extractAttribute(const std::string& tag, const char* attribute, std::string& value) {
    value.clear();
    if (!attribute || !*attribute) return false;
    const std::string attr = toLowerAscii(attribute);
    const std::string lowerTag = toLowerAscii(tag);
    size_t p = 0;
    while ((p = lowerTag.find(attr, p)) != std::string::npos) {
        const size_t after = p + attr.size();
        if (p > 0) {
            const char prev = lowerTag[p - 1];
            if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_' || prev == '-') {
                p = after;
                continue;
            }
        }
        size_t eq = after;
        while (eq < lowerTag.size() && std::isspace(static_cast<unsigned char>(lowerTag[eq]))) ++eq;
        if (eq >= lowerTag.size() || lowerTag[eq] != '=') {
            p = after;
            continue;
        }
        ++eq;
        while (eq < lowerTag.size() && std::isspace(static_cast<unsigned char>(lowerTag[eq]))) ++eq;
        if (eq >= lowerTag.size()) return false;
        const char quote = lowerTag[eq];
        if (quote != '\'' && quote != '"') return false;
        const size_t end = lowerTag.find(quote, eq + 1);
        if (end == std::string::npos) return false;
        value = htmlEntityDecode(tag.substr(eq + 1, end - eq - 1));
        return true;
    }
    return false;
}

std::string extractDownloadAttribute(const std::string& tag) {
    static const char* const kAttrs[] = {
        "href", "data-href", "data-url", "data-download-url", "data-download"
    };
    for (const char* attr : kAttrs) {
        std::string value;
        if (extractAttribute(tag, attr, value) && isDirectMediaFireUrl(value)) return value;
    }
    return {};
}

std::string tagContaining(const std::string& html, size_t markerPos) {
    if (markerPos == std::string::npos) return {};
    const size_t from = html.rfind('<', markerPos);
    if (from == std::string::npos) return {};
    const size_t to = html.find('>', markerPos);
    if (to == std::string::npos || to < from) return {};
    return html.substr(from, to - from + 1);
}

std::string findDirectFromHtml(const std::string& html) {
    const std::string lowerHtml = toLowerAscii(html);

    // 1) data-scrambled-url (single- or double-quoted).
    for (const char quote : {'"', '\''}) {
        const std::string key = std::string("data-scrambled-url=") + quote;
        size_t p = 0;
        while ((p = lowerHtml.find(toLowerAscii(key), p)) != std::string::npos) {
            p += key.size();
            const size_t e = html.find(quote, p);
            if (e == std::string::npos || e <= p) break;
            const std::string decoded = htmlEntityDecode(b64Decode(html.substr(p, e - p)));
            if (isDirectMediaFireUrl(decoded)) return decoded;
            p = e + 1;
        }
    }

    // 2) Primary download button. Attribute order may vary.
    for (const char quote : {'"', '\''}) {
        const std::string key = std::string("id=") + quote + "downloadButton" + quote;
        size_t p = 0;
        while ((p = lowerHtml.find(toLowerAscii(key), p)) != std::string::npos) {
            const std::string direct = extractDownloadAttribute(tagContaining(html, p));
            if (!direct.empty()) return direct;
            p += key.size();
        }
    }

    // 3) Accessible current-style download control.
    for (const char quote : {'"', '\''}) {
        const std::string key = std::string("aria-label=") + quote + "download file" + quote;
        size_t p = 0;
        while ((p = lowerHtml.find(toLowerAscii(key), p)) != std::string::npos) {
            const std::string direct = extractDownloadAttribute(tagContaining(html, p));
            if (!direct.empty()) return direct;
            p += key.size();
        }
    }

    // 4) Older MediaFire markup using the popsok class.
    size_t p = 0;
    while ((p = lowerHtml.find("popsok", p)) != std::string::npos) {
        const std::string direct = extractDownloadAttribute(tagContaining(html, p));
        if (!direct.empty()) return direct;
        p += 6;
    }

    // 5) Explicit CDN URL anywhere in the HTML.
    for (const char* prefix : {"https://download", "http://download"}) {
        p = 0;
        while ((p = html.find(prefix, p)) != std::string::npos) {
            const std::string direct = extractUrlAt(html, p);
            if (isDirectMediaFireUrl(direct)) return direct;
            p += std::strlen(prefix);
        }
    }

    // 6) API/JSON-ish fields that expose the normal/direct download link.
    for (const char* key : {"normal_download", "direct_download", "download_link"}) {
        p = 0;
        while ((p = lowerHtml.find(key, p)) != std::string::npos) {
            const size_t colon = html.find(':', p + std::strlen(key));
            if (colon != std::string::npos) {
                const size_t http = html.find("http", colon);
                if (http != std::string::npos && http < colon + 1024) {
                    const std::string direct = extractUrlAt(html, http);
                    if (isDirectMediaFireUrl(direct)) return direct;
                }
            }
            p += std::strlen(key);
        }
    }

    return {};
}

} // namespace

bool isMediaFireUrl(const std::string& url) {
    return toLowerAscii(url).find("mediafire.com") != std::string::npos;
}

bool resolveMediaFireDirectUrl(
    HttpClient& http,
    const std::string& pageUrl,
    std::string& directOut,
    std::string& errorOut
) {
    directOut.clear();
    errorOut.clear();

    if (!isMediaFireUrl(pageUrl)) {
        errorOut = "not a mediafire url";
        return false;
    }
    if (isDirectMediaFireUrl(pageUrl)) {
        directOut = pageUrl;
        return true;
    }
    if (!http.isInitialized()) {
        errorOut = "http not initialized";
        return false;
    }

    std::string html;
    HttpResult hr = HttpResult::NetworkError;
    std::string lastFetchError;
    constexpr int kMaxPageFetchAttempts = 3;

    for (int attempt = 1; attempt <= kMaxPageFetchAttempts; ++attempt) {
        html.clear();
        hr = http.fetchToString(pageUrl, html, 1024 * 1024);
        if (hr == HttpResult::Ok) break;
        lastFetchError = http.lastError();
        char msg[180];
        sceClibSnprintf(msg, sizeof(msg), "[MediaFire] page fetch attempt %d/%d failed: %s",
            attempt, kMaxPageFetchAttempts, lastFetchError.c_str());
        diagnostics::log(msg);
        if (attempt < kMaxPageFetchAttempts) sceKernelDelayThread(250 * 1000);
    }

    if (hr != HttpResult::Ok) {
        errorOut = std::string("mediafire page fetch failed: ") +
            (lastFetchError.empty() ? http.lastError() : lastFetchError);
        diagnostics::log(std::string("[MediaFire] ") + errorOut);
        return false;
    }
    if (html.size() < 64) {
        errorOut = "mediafire page too small";
        return false;
    }
    if (containsIgnoreCase(html, "Invalid or Deleted File") ||
        containsIgnoreCase(html, "File Removed") ||
        containsIgnoreCase(html, "this file is currently unavailable")) {
        errorOut = "mediafire file unavailable or deleted";
        return false;
    }

    directOut = findDirectFromHtml(html);
    if (directOut.empty()) {
        errorOut = "could not find direct link in mediafire HTML";
        diagnostics::log("[MediaFire] parse failed (no direct download URL in page)");
        return false;
    }

    diagnostics::log(std::string("[MediaFire] resolved direct url len=") + std::to_string(directOut.size()));
    return true;
}

} // namespace psvitaalive
