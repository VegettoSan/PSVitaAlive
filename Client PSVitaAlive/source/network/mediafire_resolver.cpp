#include "network/mediafire_resolver.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>

#include <cctype>
#include <cstring>
#include <algorithm>
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
    return toLowerAscii(hay).find(toLowerAscii(needle)) != std::string::npos;
}

// Minimal base64 decode (RFC 4648). Returns empty on failure.
std::string b64Decode(const std::string& in) {
    static const int8_t kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::string out;
    out.reserve(in.size() * 3 / 4 + 4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const int d = kDec[c];
        if (d < 0) return std::string();
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

bool looksLikeHttpUrl(const std::string& s) {
    return s.size() > 12 &&
           (s.compare(0, 8, "https://") == 0 || s.compare(0, 7, "http://") == 0);
}

// Extract first URL starting at pos that looks like https://download... until quote/space
std::string extractUrlAt(const std::string& html, size_t pos) {
    if (pos == std::string::npos || pos >= html.size()) return {};
    size_t end = pos;
    while (end < html.size()) {
        const char c = html[end];
        if (c == '"' || c == '\'' || c == '<' || c == '>' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
            break;
        ++end;
    }
    std::string u = html.substr(pos, end - pos);
    // Strip trailing punctuation
    while (!u.empty() && (u.back() == '\\' || u.back() == ')' || u.back() == ','))
        u.pop_back();
    return u;
}

std::string findDirectFromHtml(const std::string& html) {
    // 1) Modern: data-scrambled-url="BASE64"
    {
        const char* key = "data-scrambled-url=\"";
        size_t p = html.find(key);
        if (p != std::string::npos) {
            p += std::strlen(key);
            const size_t e = html.find('"', p);
            if (e != std::string::npos && e > p) {
                const std::string b64 = html.substr(p, e - p);
                const std::string decoded = b64Decode(b64);
                if (looksLikeHttpUrl(decoded) && containsIgnoreCase(decoded, "mediafire")) {
                    return decoded;
                }
                if (looksLikeHttpUrl(decoded) && containsIgnoreCase(decoded, "download")) {
                    return decoded;
                }
            }
        }
    }

    // 2) href near downloadButton
    {
        size_t btn = html.find("id=\"downloadButton\"");
        if (btn == std::string::npos) btn = html.find("id='downloadButton'");
        if (btn != std::string::npos) {
            // search href in a window around the button
            const size_t from = btn > 200 ? btn - 200 : 0;
            const size_t to = std::min(html.size(), btn + 400);
            const std::string win = html.substr(from, to - from);
            size_t h = win.find("href=\"");
            if (h == std::string::npos) h = win.find("href='");
            if (h != std::string::npos) {
                const char q = win[h + 5];
                h += 6;
                const size_t he = win.find(q, h);
                if (he != std::string::npos) {
                    std::string u = win.substr(h, he - h);
                    if (looksLikeHttpUrl(u) && containsIgnoreCase(u, "download"))
                        return u;
                }
            }
        }
    }

    // 3) Any https://download*.mediafire.com link
    {
        size_t p = 0;
        while ((p = html.find("https://download", p)) != std::string::npos) {
            std::string u = extractUrlAt(html, p);
            if (containsIgnoreCase(u, "mediafire.com")) return u;
            p += 16;
        }
        p = 0;
        while ((p = html.find("http://download", p)) != std::string::npos) {
            std::string u = extractUrlAt(html, p);
            if (containsIgnoreCase(u, "mediafire.com")) return u;
            p += 15;
        }
    }

    // 4) class="input popsok" style buttons (older pages)
    {
        size_t p = html.find("popsok");
        if (p != std::string::npos) {
            const size_t from = p > 300 ? p - 300 : 0;
            const std::string win = html.substr(from, 600);
            size_t h = win.find("https://download");
            if (h != std::string::npos) {
                std::string u = extractUrlAt(win, h);
                if (containsIgnoreCase(u, "mediafire")) return u;
            }
        }
    }

    return {};
}

} // namespace

bool isMediaFireUrl(const std::string& url) {
    const std::string u = toLowerAscii(url);
    return u.find("mediafire.com") != std::string::npos;
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
    // Already a CDN direct link — use as-is
    const std::string low = toLowerAscii(pageUrl);
    if (low.find("download") != std::string::npos &&
        (low.find("download.") != std::string::npos || low.find("//download") != std::string::npos) &&
        low.find("/file/") == std::string::npos) {
        // Heuristic: downloadXXXX.mediafire.com/... is usually direct
        if (low.find("mediafire.com/file/") == std::string::npos &&
            low.find("www.mediafire.com") == std::string::npos) {
            directOut = pageUrl;
            return true;
        }
    }

    if (!http.isInitialized()) {
        errorOut = "http not initialized";
        return false;
    }

    std::string html;
    const HttpResult hr = http.fetchToString(pageUrl, html, 768 * 1024);
    if (hr != HttpResult::Ok) {
        errorOut = std::string("mediafire page fetch failed: ") + http.lastError();
        diagnostics::log(std::string("[MediaFire] ") + errorOut);
        return false;
    }
    if (html.size() < 64) {
        errorOut = "mediafire page too small";
        return false;
    }

    // Password / error pages
    if (containsIgnoreCase(html, "Invalid or Deleted File") ||
        containsIgnoreCase(html, "File Removed") ||
        containsIgnoreCase(html, "this file is currently unavailable")) {
        errorOut = "mediafire file unavailable or deleted";
        return false;
    }

    std::string direct = findDirectFromHtml(html);
    if (direct.empty()) {
        errorOut = "could not find direct link in mediafire HTML";
        diagnostics::log("[MediaFire] parse failed (no download URL in page)");
        return false;
    }

    directOut = direct;
    diagnostics::log(std::string("[MediaFire] resolved direct url len=") + std::to_string(direct.size()));
    return true;
}

} // namespace psvitaalive
