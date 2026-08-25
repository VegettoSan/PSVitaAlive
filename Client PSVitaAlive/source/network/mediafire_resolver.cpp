#include "network/mediafire_resolver.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

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

int base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string b64Decode(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 3 / 4 + 4);
    int val = 0;
    int valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const int d = base64Value(c);
        if (d < 0) return {};
        val = (val << 6) | d;
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

    for (const char quote : {'"', '\''}) {
        const std::string key = std::string("id=") + quote + "downloadButton" + quote;
        size_t p = 0;
        while ((p = lowerHtml.find(toLowerAscii(key), p)) != std::string::npos) {
            const std::string direct = extractDownloadAttribute(tagContaining(html, p));
            if (!direct.empty()) return direct;
            p += key.size();
        }
    }

    for (const char quote : {'"', '\''}) {
        const std::string key = std::string("aria-label=") + quote + "download file" + quote;
        size_t p = 0;
        while ((p = lowerHtml.find(toLowerAscii(key), p)) != std::string::npos) {
            const std::string direct = extractDownloadAttribute(tagContaining(html, p));
            if (!direct.empty()) return direct;
            p += key.size();
        }
    }

    size_t p = 0;
    while ((p = lowerHtml.find("popsok", p)) != std::string::npos) {
        const std::string direct = extractDownloadAttribute(tagContaining(html, p));
        if (!direct.empty()) return direct;
        p += 6;
    }

    for (const char* prefix : {"https://download", "http://download"}) {
        p = 0;
        while ((p = html.find(prefix, p)) != std::string::npos) {
            const std::string direct = extractUrlAt(html, p);
            if (isDirectMediaFireUrl(direct)) return direct;
            p += std::strlen(prefix);
        }
    }

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


uint64_t unitToBytes(double value, const std::string& unitRaw) {
    std::string u = toLowerAscii(unitRaw);
    while (!u.empty() && (u.back() == 's' || u.back() == ' ')) u.pop_back();
    uint64_t mul = 1;
    if (u == "b" || u == "byte" || u == "bytes") mul = 1;
    else if (u == "k" || u == "kb" || u == "kib") mul = 1024ULL;
    else if (u == "m" || u == "mb" || u == "mib") mul = 1024ULL * 1024ULL;
    else if (u == "g" || u == "gb" || u == "gib") mul = 1024ULL * 1024ULL * 1024ULL;
    else if (u == "t" || u == "tb" || u == "tib") mul = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    else return 0;
    if (value <= 0.0) return 0;
    const double bytes = value * static_cast<double>(mul);
    if (bytes < 1.0) return 0;
    if (bytes > static_cast<double>(~0ULL)) return 0;
    return static_cast<uint64_t>(bytes + 0.5);
}

/** Parse human sizes from MediaFire share HTML (Download (1.47GB), etc.). */
uint64_t findSizeBytesFromHtml(const std::string& html) {
    const std::string lower = toLowerAscii(html);
    uint64_t best = 0;

    auto consider = [&](double val, const std::string& unit) {
        const uint64_t b = unitToBytes(val, unit);
        if (b > best) best = b;
    };

    // Pattern: Download (1.47GB) / download (850.2 MB)
    size_t p = 0;
    while ((p = lower.find("download", p)) != std::string::npos) {
        const size_t open = lower.find('(', p);
        if (open != std::string::npos && open < p + 48) {
            const size_t close = lower.find(')', open);
            if (close != std::string::npos && close > open + 1 && close - open < 32) {
                const std::string inside = lower.substr(open + 1, close - open - 1);
                char unit[16] = {};
                double val = 0.0;
                if (std::sscanf(inside.c_str(), "%lf %15s", &val, unit) >= 1 ||
                    std::sscanf(inside.c_str(), "%lf%15s", &val, unit) >= 1) {
                    if (unit[0]) consider(val, unit);
                }
            }
        }
        p += 8;
    }

    // Generic: 1.47 GB / 850MB near file info
    p = 0;
    while (p < lower.size()) {
        while (p < lower.size() && !std::isdigit(static_cast<unsigned char>(lower[p]))) ++p;
        if (p >= lower.size()) break;
        size_t start = p;
        while (p < lower.size() && (std::isdigit(static_cast<unsigned char>(lower[p])) || lower[p] == '.')) ++p;
        const std::string num = lower.substr(start, p - start);
        while (p < lower.size() && (lower[p] == ' ' || lower[p] == '\t')) ++p;
        size_t u0 = p;
        while (p < lower.size() && std::isalpha(static_cast<unsigned char>(lower[p]))) ++p;
        if (p > u0 && p - u0 <= 4) {
            const std::string unit = lower.substr(u0, p - u0);
            if (unit == "kb" || unit == "mb" || unit == "gb" || unit == "tb" ||
                unit == "kib" || unit == "mib" || unit == "gib" || unit == "tib" ||
                unit == "k" || unit == "m" || unit == "g") {
                char* end = nullptr;
                const double val = std::strtod(num.c_str(), &end);
                if (end != num.c_str() && val > 0.0) consider(val, unit);
            }
        }
    }

    return best;
}

bool isMediaFireUrl(const std::string& url) {
    return toLowerAscii(url).find("mediafire.com") != std::string::npos;
}

bool resolveMediaFireDirectUrl(
    HttpClient& http,
    const std::string& pageUrl,
    std::string& directOut,
    std::string& errorOut,
    uint64_t* sizeBytesOut
) {
    directOut.clear();
    errorOut.clear();
    if (sizeBytesOut) *sizeBytesOut = 0;

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

    const uint64_t htmlSize = findSizeBytesFromHtml(html);
    if (sizeBytesOut) *sizeBytesOut = htmlSize;
    if (htmlSize > 0) {
        char sm[96];
        sceClibSnprintf(sm, sizeof(sm), "[MediaFire] page size hint bytes=%llu",
                        (unsigned long long)htmlSize);
        diagnostics::log(sm);
    } else {
        diagnostics::log("[MediaFire] page size hint unavailable");
    }
    diagnostics::log(std::string("[MediaFire] resolved direct url len=") + std::to_string(directOut.size()));
    return true;
}

} // namespace psvitaalive
