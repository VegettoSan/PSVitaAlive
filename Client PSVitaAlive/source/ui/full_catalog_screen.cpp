#include "ui/full_catalog_screen.hpp"
#include "ui/news_markdown.hpp"
#include "installer/app_settings.hpp"
#include "installer/plugin_detector.hpp"
#include "update/update_checker.hpp"

#ifndef PSVITAALIVE_VERSION
#define PSVITAALIVE_VERSION "01.00"
#endif
#include "diagnostic_logger.hpp"
#include "network/error_reporter.hpp"
#include "network/http_client.hpp"
#include "network/news_manager.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/devctl.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>
namespace psvitaalive::ui { namespace {


std::string currentTimeLabel() {
    SceDateTime dt{};
    if (sceRtcGetCurrentClockLocalTime(&dt) < 0) return "--:--";
    char buf[16];
    sceClibSnprintf(buf, sizeof(buf), "%02d:%02d", (int)dt.hour, (int)dt.minute);
    return buf;
}

std::string formatBytesShort(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%.2fG", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ULL * 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%.1fM", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%lluK", (unsigned long long)(bytes / 1024ULL));
    else
        sceClibSnprintf(buf, sizeof(buf), "%lluB", (unsigned long long)bytes);
    return buf;
}

struct Ux0SpaceInfo {
    bool ok = false;
    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
};

Ux0SpaceInfo queryUx0Space() {
    static Ux0SpaceInfo cached{};
    static uint64_t lastMs = 0;
    const uint64_t nowMs = sceKernelGetProcessTimeWide() / 1000ULL;
    if (lastMs != 0 && nowMs >= lastMs && (nowMs - lastMs) < 3000ULL) return cached;

    struct {
        uint64_t max_size;
        uint64_t free_size;
        uint32_t cluster_size;
        void* unk;
    } info{};
    const int ret = sceIoDevctl("ux0:", 0x3001, nullptr, 0, &info, sizeof(info));
    lastMs = nowMs;
    cached = {};
    if (ret < 0) return cached;
    cached.ok = true;
    cached.freeBytes = info.free_size;
    cached.totalBytes = info.max_size > 0 ? info.max_size : info.free_size;
    return cached;
}

std::string ux0FreeSpaceLabel() {
    const Ux0SpaceInfo s = queryUx0Space();
    if (!s.ok) return "ux0 --";
    char buf[64];
    sceClibSnprintf(buf, sizeof(buf), "%s/%s",
                    formatBytesShort(s.freeBytes).c_str(),
                    formatBytesShort(s.totalBytes).c_str());
    return buf;
}


/** Best human-readable size for a catalog card (item.size or first download link size). */
std::string itemDisplaySize(const CatalogItem& it) {
    if (!it.size.empty()) return it.size;
    for (const auto& link : it.linkDetails) {
        if (link.recommended && !link.size.empty()) return link.size;
    }
    for (const auto& link : it.linkDetails) {
        if (!link.size.empty()) return link.size;
    }
    return std::string();
}

/** Normalize link.type for comparisons (lowercase, collapse spaces). */
std::string normalizeLinkType(std::string t) {
    for (char& c : t) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '_' || c == '-') c = ' ';
    }
    std::string out;
    out.reserve(t.size());
    bool space = false;
    for (char c : t) {
        if (c == ' ') {
            if (!space && !out.empty()) {
                out.push_back(' ');
                space = true;
            }
        } else {
            out.push_back(c);
            space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}


/** True when link name indicates Data/Game Files (case-insensitive, normalized). */
bool linkNameSuggestsDataFiles(const CatalogLink& l) {
    const std::string n = normalizeLinkType(l.name);
    if (n.empty()) return false;
    if (n == "data" || n == "datafiles") return true;
    if (n.find("data files") != std::string::npos) return true;
    if (n.find("data file") != std::string::npos) return true;
    return false;
}

bool linkNameSuggestsGameFiles(const CatalogLink& l) {
    const std::string n = normalizeLinkType(l.name);
    if (n.empty()) return false;
    if (n == "gamefiles") return true;
    if (n.find("game files") != std::string::npos) return true;
    if (n.find("game file") != std::string::npos) return true;
    return false;
}

bool isDownloadLikeType(const std::string& t) {
    return t == "download" || t == "downloads" || t == "mirror";
}

bool itemHasLinkType(const CatalogItem& it, const char* needle) {
    const std::string want = needle;
    for (const auto& link : it.linkDetails) {
        const std::string t = normalizeLinkType(link.type);
        if (t == want) return true;
        if (want == "data files" || want == "data file") {
            if (t == "data files" || t == "data file" || t == "datafiles" || t == "data") return true;
            // Download-type links named like "Game/Data Files ..." also count.
            if (isDownloadLikeType(t) && linkNameSuggestsDataFiles(link)) return true;
            continue;
        }
        if (want == "game files" || want == "game file") {
            if (t == "game files" || t == "game file" || t == "gamefiles") return true;
            if (isDownloadLikeType(t) && linkNameSuggestsGameFiles(link)) return true;
            continue;
        }
        if (want == "mod" && (t == "mods")) return true;
        if (want == "mods" && (t == "mod")) return true;
        if (want == "update" && (t == "updates")) return true;
        if (want == "updates" && (t == "update")) return true;
        if (want == "pkg" && (t == "pkgs")) return true;
        if (want == "download" && (t == "downloads" || t == "mirror")) return true;
        if (want == "downloads" && (t == "download" || t == "mirror")) return true;
    }
    return false;
}

/** Size from first Data Files / Game Files link (prefer Game Files). */
std::string itemExtraDataGameSize(const CatalogItem& it) {
    std::string dataSz;
    for (const auto& link : it.linkDetails) {
        if (link.size.empty()) continue;
        const std::string typ = normalizeLinkType(link.type);
        const bool gameByType = (typ == "game files" || typ == "game file" || typ == "gamefiles");
        const bool dataByType = (typ == "data files" || typ == "data file" || typ == "datafiles" || typ == "data");
        const bool gameByName = isDownloadLikeType(typ) && linkNameSuggestsGameFiles(link);
        const bool dataByName = isDownloadLikeType(typ) && linkNameSuggestsDataFiles(link);
        if (gameByType || gameByName)
            return link.size;
        if ((dataByType || dataByName) && dataSz.empty())
            dataSz = link.size;
    }
    return dataSz;
}

/**
 * Card size label: app size, or "16 MB + 1.5 GB" when a Data/Game Files
 * payload size is also known (and different from the main size).
 */
std::string itemCardSizeLabel(const CatalogItem& it) {
    std::string base;
    if (!it.size.empty()) {
        base = it.size;
    } else {
        for (const auto& link : it.linkDetails) {
            if (link.size.empty()) continue;
            const std::string typ = normalizeLinkType(link.type);
            if (typ == "data files" || typ == "data file" || typ == "datafiles" || typ == "data"
                || typ == "game files" || typ == "game file" || typ == "gamefiles")
                continue;
            if (isDownloadLikeType(typ) && (linkNameSuggestsDataFiles(link) || linkNameSuggestsGameFiles(link)))
                continue;
            if (link.recommended) {
                base = link.size;
                break;
            }
            if (base.empty()) base = link.size;
        }
    }
    const std::string extra = itemExtraDataGameSize(it);
    if (base.empty()) return extra.empty() ? itemDisplaySize(it) : extra;
    if (extra.empty() || extra == base) return base;
    return base + " + " + extra;
}

// LiveArea brand palette (mutable - ColorTheme swaps accent/surfaces at runtime)
unsigned BG=RGBA8(0x0A,0x0A,0x0A,255);
unsigned SURFACE=RGBA8(0x1A,0x1A,0x1A,255);
unsigned SURFACE2=RGBA8(0x12,0x12,0x14,255);
unsigned PANEL=RGBA8(0x0E,0x0E,0x10,255);
unsigned BORDER=RGBA8(0x2A,0x2A,0x2E,255);
unsigned TEXT=RGBA8(0xAA,0xAA,0xAA,255);
unsigned DIM=RGBA8(0x66,0x66,0x6A,255);
unsigned ACCENT=RGBA8(0x3B,0xFF,0x00,255);       // #3BFF00 default
unsigned ACCENT_DIM=RGBA8(0x3B,0xFF,0x00,90);
unsigned ACCENT_SOFT=RGBA8(0x3B,0xFF,0x00,40);
unsigned WHITE=RGBA8(0xF0,0xF0,0xF0,255);
unsigned SILVER=RGBA8(0xC8,0xC8,0xCC,255);

/** Rebuild RGBA with a new alpha; keeps RGB from c (vita2d RGBA8 = A<<24 | B<<16 | G<<8 | R). */
unsigned withAlpha(unsigned c, unsigned a) {
    return (c & 0x00FFFFFFu) | ((a & 0xFFu) << 24);
}

void applyColorTheme(::psvitaalive::ColorTheme t) {
    BG = RGBA8(0x0A,0x0A,0x0A,255);
    SURFACE = RGBA8(0x1A,0x1A,0x1A,255);
    SURFACE2 = RGBA8(0x12,0x12,0x14,255);
    PANEL = RGBA8(0x0E,0x0E,0x10,255);
    BORDER = RGBA8(0x2A,0x2A,0x2E,255);
    TEXT = RGBA8(0xAA,0xAA,0xAA,255);
    DIM = RGBA8(0x66,0x66,0x6A,255);
    WHITE = RGBA8(0xF0,0xF0,0xF0,255);
    SILVER = RGBA8(0xC8,0xC8,0xCC,255);

    unsigned ar=0x3B, ag=0xFF, ab=0x00;
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan:
            ar=0x00; ag=0xE5; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Rose:
            ar=0xFF; ag=0x5C; ab=0xA8; break;
        case ::psvitaalive::ColorTheme::Amber:
            ar=0xFF; ag=0xB0; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Violet:
            ar=0xB2; ag=0x4D; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Mono:
            ar=0xC8; ag=0xC8; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Oled:
            ar=0x5C; ag=0xFF; ab=0x9A;
            BG = RGBA8(0x00,0x00,0x00,255);
            SURFACE = RGBA8(0x0C,0x0C,0x0C,255);
            SURFACE2 = RGBA8(0x08,0x08,0x08,255);
            PANEL = RGBA8(0x05,0x05,0x05,255);
            break;
        case ::psvitaalive::ColorTheme::NeonLime:
        default:
            ar=0x3B; ag=0xFF; ab=0x00; break;
    }
    ACCENT = RGBA8(ar, ag, ab, 255);
    ACCENT_DIM = RGBA8(ar, ag, ab, 90);
    ACCENT_SOFT = RGBA8(ar, ag, ab, 40);
}

/** Thin neon frame used across cards, panels, and modals (uses current ACCENT). */
void drawNeonFrame(int x, int y, int w, int h, unsigned alphaOuter = 70, unsigned alphaInner = 180) {
    const unsigned outer = withAlpha(ACCENT, alphaOuter);
    const unsigned inner = withAlpha(ACCENT, alphaInner);
    vita2d_draw_rectangle(x - 1, y - 1, w + 2, 1, outer);
    vita2d_draw_rectangle(x - 1, y + h, w + 2, 1, outer);
    vita2d_draw_rectangle(x - 1, y - 1, 1, h + 2, outer);
    vita2d_draw_rectangle(x + w, y - 1, 1, h + 2, outer);
    vita2d_draw_rectangle(x, y, w, 1, inner);
    vita2d_draw_rectangle(x, y + h - 1, w, 1, inner);
    vita2d_draw_rectangle(x, y, 1, h, inner);
    vita2d_draw_rectangle(x + w - 1, y, 1, h, inner);
}


constexpr int FULL_CARD_H=136,SPLIT_CARD_H=118,DETAIL_HEADER_H=108,LINE_H=24,DETAIL_SECTION_H=30,DETAIL_META_H=28,DETAIL_SECTION_GAP=16,TRANSITION_MS=340,LINK_ROW_H=46,LINK_GAP=6,SCREENSHOT_ROW_H=250;
constexpr size_t MAX_APP_TEXTURES=18,MAX_SCREENSHOT_TEXTURES=6;
constexpr int CATALOG_SWITCH_COOLDOWN_FRAMES=50; // ~0.83s at 60fps
constexpr uint64_t CATALOG_SWITCH_MIN_MS=900; // hard debounce against L/R spam
constexpr size_t MAX_DEFERRED_FREES_PER_FRAME=8;constexpr uint64_t DIRECTION_REPEAT_DELAY_US=320000,DIRECTION_REPEAT_INTERVAL_US=420000;
const char* extOf(const std::string&p){const size_t d=p.find_last_of('.');return d==std::string::npos?"":p.c_str()+d;}std::string formatBytes(uint64_t b){char o[64];double v=(double)b;if(b>=1024ULL*1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f GB",v/(1024.0*1024.0*1024.0));else if(b>=1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f MB",v/(1024.0*1024.0));else if(b>=1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f KB",v/1024.0);else sceClibSnprintf(o,sizeof(o),"%llu B",(unsigned long long)b);return o;}std::string lowerAscii(std::string s){for(char&c:s)c=(char)std::tolower((unsigned char)c);return s;}std::string ellipsize(const std::string&s,size_t n){if(s.size()<=n)return s;if(n<=3)return s.substr(0,n);return s.substr(0,n-3)+"...";}

/** Soft horizontal marquee for long titles when focused (list card + detail header). */
void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,
                     const std::string& text, bool animate) {
    if (!font || text.empty() || maxW <= 8) return;
    const int tw = vita2d_pgf_text_width(font, scale, text.c_str());
    // IMPORTANT: do not disable global clipping on the static path — parent panels
    // (catalog grid / detail body) rely on their scissor staying active.
    if (tw <= maxW) {
        vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());
        return;
    }
    if (!animate) {
        // Static: pixel-accurate ellipsis so long names never spill past maxW.
        std::string s = text;
        const char* dots = "...";
        const int dotsW = vita2d_pgf_text_width(font, scale, dots);
        while (s.size() > 1 && vita2d_pgf_text_width(font, scale, s.c_str()) + dotsW > maxW)
            s.pop_back();
        while (!s.empty() && (unsigned char)s.back() < 0x80 && (s.back() == ' ' || s.back() == '.'))
            s.pop_back();
        s += dots;
        vita2d_pgf_draw_text(font, x, y, color, scale, s.c_str());
        return;
    }
    // Animated marquee needs a tight scissor for the scrolling glyphs.
    // Callers that own a wider panel clip must re-assert it after this returns.
    const int gap = 48;
    const int cycle = tw + gap;
    const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
    // Pause ~1.2s at the start, then scroll ~35 px/s, loop seamlessly.
    const uint64_t period = static_cast<uint64_t>(cycle) * 28ULL + 1200ULL;
    const uint64_t t = ms % period;
    int offset = 0;
    if (t > 1200ULL) offset = static_cast<int>((t - 1200ULL) / 28ULL);
    if (offset > cycle) offset = cycle;
    const int clipTop = y - 20;
    const int clipBottom = y + 6;
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, clipTop, x + maxW, clipBottom);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    // Do not leave scissor disabled: rest of the card (badges, chips) would spill
    // outside the panel while scrolling. Expand to full screen; caller re-asserts
    // the panel clip right after drawCatalogCard returns.
    vita2d_set_clip_rectangle(0, 0, SCREEN_W, SCREEN_H);
}

/** Word-wrap text to max pixel width (vita2d_pgf). Long tokens are hard-split. */
std::vector<std::string> wrapTextToWidth(vita2d_pgf* font, float scale, const std::string& text, int maxW) {
    std::vector<std::string> out;
    if (!font || maxW < 8) {
        out.push_back(text);
        return out;
    }
    if (text.empty()) {
        out.push_back("");
        return out;
    }
    auto widthOf = [&](const std::string& s) -> int {
        return vita2d_pgf_text_width(font, scale, s.c_str());
    };
    // Split into words keeping spaces attached to following word for simple rebuild
    size_t i = 0;
    std::string line;
    while (i < text.size()) {
        // Skip leading spaces on a new line only when line empty? keep single spaces between words
        size_t start = i;
        if (text[i] == ' ' || text[i] == '\t') {
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
            if (line.empty()) continue; // trim leading
            // treat as space before next word
            start = i;
        }
        while (i < text.size() && text[i] != ' ' && text[i] != '\t') ++i;
        std::string word = text.substr(start, i - start);
        if (word.empty()) continue;

        std::string candidate = line.empty() ? word : (line + " " + word);
        if (widthOf(candidate) <= maxW) {
            line = std::move(candidate);
            continue;
        }
        // word alone may exceed maxW
        if (line.empty()) {
            // hard-split word
            std::string chunk;
            for (char c : word) {
                std::string tryChunk = chunk + c;
                if (!chunk.empty() && widthOf(tryChunk) > maxW) {
                    out.push_back(chunk);
                    chunk = std::string(1, c);
                } else {
                    chunk = std::move(tryChunk);
                }
            }
            if (!chunk.empty()) line = chunk;
        } else {
            out.push_back(line);
            // place word on new line (hard-split if needed)
            if (widthOf(word) <= maxW) {
                line = word;
            } else {
                std::string chunk;
                for (char c : word) {
                    std::string tryChunk = chunk + c;
                    if (!chunk.empty() && widthOf(tryChunk) > maxW) {
                        out.push_back(chunk);
                        chunk = std::string(1, c);
                    } else {
                        chunk = std::move(tryChunk);
                    }
                }
                line = chunk;
            }
        }
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}
bool actionableLink(const CatalogLink&l){
    // Types shown as install/download buttons in detail view.
    std::string t=normalizeLinkType(l.type);
    if(t=="download"||t=="downloads"||t=="mirror")return true;
    if(t=="data files"||t=="data file"||t=="datafiles"||t=="data")return true;
    if(t=="game files"||t=="game file"||t=="gamefiles")return true;
    if(t=="mod"||t=="mods")return true;
    if(t=="dlc")return true;
    if(t=="update"||t=="updates")return true;
    if(t=="pkg"||t=="pkgs")return true;
    // Fallback: actionable by file extension in URL.
    std::string u=lowerAscii(l.url);
    return u.find(".vpk")!=std::string::npos||u.find(".pkg")!=std::string::npos||u.find(".zip")!=std::string::npos||u.find(".pbp")!=std::string::npos||u.find(".iso")!=std::string::npos||u.find(".cso")!=std::string::npos;
}
bool isDownloadLink(const CatalogLink&l){
    // Which links appear in the detail download button list.
    std::string t=normalizeLinkType(l.type);
    if(t=="download"||t=="downloads"||t=="mirror")return true;
    if(t=="data files"||t=="data file"||t=="datafiles"||t=="data")return true;
    if(t=="game files"||t=="game file"||t=="gamefiles")return true;
    if(t=="mod"||t=="mods")return true;
    if(t=="dlc")return true;
    if(t=="update"||t=="updates")return true;
    if(t=="pkg"||t=="pkgs")return true;
    return false;
}

/** Section order for detail link list (△ navigation follows this order). */
enum class LinkSection : int {
    Downloads = 0,
    DataFiles,
    GameFiles,
    Mods,
    Dlc,
    Updates,
    Pkg,
    Other,
    Count
};

LinkSection classifyLinkSection(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (t == "download" || t == "downloads" || t == "mirror") {
        // Name can reclassify a Download link into Game/Data Files sections.
        if (linkNameSuggestsGameFiles(l)) return LinkSection::GameFiles;
        if (linkNameSuggestsDataFiles(l)) return LinkSection::DataFiles;
        return LinkSection::Downloads;
    }
    if (t == "data files" || t == "data file" || t == "datafiles" || t == "data") return LinkSection::DataFiles;
    if (t == "game files" || t == "game file" || t == "gamefiles") return LinkSection::GameFiles;
    if (t == "mod" || t == "mods") return LinkSection::Mods;
    if (t == "dlc") return LinkSection::Dlc;
    if (t == "update" || t == "updates") return LinkSection::Updates;
    if (t == "pkg" || t == "pkgs") return LinkSection::Pkg;
    return LinkSection::Other;
}

const char* linkSectionTitle(LinkSection s) {
    switch (s) {
        case LinkSection::Downloads: return "DOWNLOADS";
        case LinkSection::DataFiles: return "DATA FILES";
        case LinkSection::GameFiles: return "GAME FILES";
        case LinkSection::Mods: return "MODS";
        case LinkSection::Dlc: return "DLC";
        case LinkSection::Updates: return "UPDATES";
        case LinkSection::Pkg: return "PKG";
        default: return "OTHER";
    }
}

const char* linkSectionMetaLabel(LinkSection s) {
    switch (s) {
        case LinkSection::Downloads: return "Download";
        case LinkSection::DataFiles: return "Data Files";
        case LinkSection::GameFiles: return "Game Files";
        case LinkSection::Mods: return "Mod";
        case LinkSection::Dlc: return "DLC";
        case LinkSection::Updates: return "Update";
        case LinkSection::Pkg: return "PKG";
        default: return "Download";
    }
}

/** Downloadable links grouped by section (stable focus order for △). */
std::vector<int> downloadLinkIndices(const CatalogItem& it) {
    std::vector<int> buckets[static_cast<int>(LinkSection::Count)];
    for (size_t i = 0; i < it.linkDetails.size(); ++i) {
        if (!isDownloadLink(it.linkDetails[i])) continue;
        const int b = static_cast<int>(classifyLinkSection(it.linkDetails[i]));
        buckets[b].push_back(static_cast<int>(i));
    }
    std::vector<int> out;
    for (int s = 0; s < static_cast<int>(LinkSection::Count); ++s) {
        for (int idx : buckets[s]) out.push_back(idx);
    }
    return out;
}

/** One row in the visual list: section header or a focusable link. */
struct LinkLayoutRow {
    bool isSection = false;
    LinkSection section = LinkSection::Other;
    int focusIndex = -1;   // 0..n-1 for links; -1 for headers
    int detailIndex = -1;  // index into CatalogItem::linkDetails
    int y = 0;
    int h = 0;
};

static const int LINK_SECTION_H = 26;

std::vector<LinkLayoutRow> buildLinkLayout(const CatalogItem& it) {
    std::vector<int> buckets[static_cast<int>(LinkSection::Count)];
    for (size_t i = 0; i < it.linkDetails.size(); ++i) {
        if (!isDownloadLink(it.linkDetails[i])) continue;
        buckets[static_cast<int>(classifyLinkSection(it.linkDetails[i]))].push_back(static_cast<int>(i));
    }
    std::vector<LinkLayoutRow> rows;
    int focus = 0;
    int y = 10;
    for (int s = 0; s < static_cast<int>(LinkSection::Count); ++s) {
        if (buckets[s].empty()) continue;
        LinkLayoutRow header;
        header.isSection = true;
        header.section = static_cast<LinkSection>(s);
        header.y = y;
        header.h = LINK_SECTION_H;
        rows.push_back(header);
        y += LINK_SECTION_H + 4;
        for (int di : buckets[s]) {
            LinkLayoutRow row;
            row.isSection = false;
            row.section = static_cast<LinkSection>(s);
            row.focusIndex = focus++;
            row.detailIndex = di;
            row.y = y;
            row.h = LINK_ROW_H;
            rows.push_back(row);
            y += LINK_ROW_H + LINK_GAP;
        }
    }
    return rows;
}

int linkLayoutTotalHeight(const std::vector<LinkLayoutRow>& rows) {
    if (rows.empty()) return 0;
    const LinkLayoutRow& last = rows.back();
    return last.y + last.h + 8;
}

int yOfLinkFocus(const std::vector<LinkLayoutRow>& rows, int focusIndex) {
    for (const auto& row : rows) {
        if (!row.isSection && row.focusIndex == focusIndex) return row.y;
    }
    return 10;
}


bool itemHasDataOrGameFiles(const CatalogItem& it) {
    return itemHasLinkType(it, "data files") || itemHasLinkType(it, "game files")
        || itemHasLinkType(it, "data file") || itemHasLinkType(it, "game file");
}

/** Categories where Data/Game Files requests make sense (catalog category_id). */
bool categoryWantsDataFiles(const std::string& category) {
    std::string c = lowerAscii(category);
    // ports / games / emulators (+ media). Skip utilities & plugins.
    return c == "ports" || c == "games" || c == "emulators" || c == "media"
        || c.find("port") != std::string::npos
        || c.find("game") != std::string::npos
        || c.find("emulator") != std::string::npos;
}


bool isDownloadTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (!(t == "download" || t == "downloads")) return false;
    // Named Game/Data Files downloads belong to those categories, not VPK downloads.
    if (linkNameSuggestsGameFiles(l) || linkNameSuggestsDataFiles(l)) return false;
    return true;
}

bool isGameFilesTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (t == "game files" || t == "game file" || t == "gamefiles") return true;
    if (isDownloadLikeType(t) && linkNameSuggestsGameFiles(l)) return true;
    return false;
}

bool isDataFilesTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    if (t == "data files" || t == "data file" || t == "datafiles" || t == "data") return true;
    if (isDownloadLikeType(t) && linkNameSuggestsDataFiles(l)) return true;
    return false;
}

/** Extra height for Install All button block at top of links. */
static const int INSTALL_ALL_BLOCK_H = 68;

uint64_t parseUiSizeBytes(const std::string& raw) {
    if (raw.empty()) return 0;
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == ' ' || c == '\t') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        s.push_back(static_cast<char>(c));
    }
    if (s.empty()) return 0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return 0;
    std::string u = end ? std::string(end) : std::string();
    uint64_t mul = 1;
    if (u.empty() || u == "b" || u == "byte" || u == "bytes") mul = 1;
    else if (u == "k" || u == "kb" || u == "kib") mul = 1024ULL;
    else if (u == "m" || u == "mb" || u == "mib") mul = 1024ULL * 1024ULL;
    else if (u == "g" || u == "gb" || u == "gib") mul = 1024ULL * 1024ULL * 1024ULL;
    else return 0;
    if (v <= 0.0) return 0;
    return static_cast<uint64_t>(v * static_cast<double>(mul) + 0.5);
}

std::string formatLinkSizeLabel(const CatalogLink&l,const CatalogItem&it){if(!l.size.empty())return l.size;if(!it.size.empty())return it.size;return {};}
}
bool isInsufficientSpaceError(const std::string& msg) {
    // Match InstallController pre-flight and any similar free-space failures.
    std::string m = msg;
    for (char& c : m) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return m.find("not enough free space") != std::string::npos
        || m.find("insufficient space") != std::string::npos
        || m.find("no space left") != std::string::npos
        || m.find("disk full") != std::string::npos;
}

/** Errors the user can fix without a Discord report (space, offline, incomplete DL). */
bool isNonReportableInstallError(const std::string& msg) {
    if (isInsufficientSpaceError(msg)) return true;
    std::string m = msg;
    for (char& c : m) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return m.find("no internet connection") != std::string::npos
        || m.find("download incomplete") != std::string::npos
        || m.find("connection lost before finish") != std::string::npos
        || m.find("network error") != std::string::npos
        || m.find("could not resolve host") != std::string::npos
        || m.find("connection timed out") != std::string::npos
        || m.find("failed to connect") != std::string::npos;
}

std::string formatEta(uint64_t seconds){if(seconds==0)return "--";uint64_t h=seconds/3600,m=(seconds%3600)/60,sec=seconds%60;char o[64];if(h)sceClibSnprintf(o,sizeof(o),"%llu:%02llu:%02llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)sec);else sceClibSnprintf(o,sizeof(o),"%02llu:%02llu",(unsigned long long)m,(unsigned long long)sec);return o;}
FullCatalogScreen::FullCatalogScreen()=default;FullCatalogScreen::~FullCatalogScreen(){shutdown();}



void FullCatalogScreen::closeNewsModal(bool markSeen) {
    if (markSeen && !newsId_.empty()) {
        ::psvitaalive::NewsManager::saveSeenId(newsId_);
    }
    newsVisible_ = false;
    newsMarkSeenOnClose_ = false;
}

void FullCatalogScreen::runNewsCheck(bool forceShow) {
    if (newsVisible_) return;
    if (!forceShow && newsCheckedOnce_) return;

    ::psvitaalive::NewsItem item = ::psvitaalive::NewsManager::fetchRemote();
    if (!item.valid) {
        item = ::psvitaalive::NewsManager::loadCached();
    }
    if (!item.valid || !item.enabled) {
        if (forceShow) {
            newsCheckedOnce_ = true;
            showToast("No news available", 1600);
            diagnostics::log("[UI] news check: no valid item (force)");
            return;
        }
        // Soft retry: network may not be ready right after catalog load
        ++newsFetchAttempts_;
        diagnostics::log(std::string("[UI] news check: no valid item yet attempts=") +
                         std::to_string(newsFetchAttempts_));
        if (newsFetchAttempts_ >= 5) {
            newsCheckedOnce_ = true;
            diagnostics::log("[UI] news check: giving up auto-show for this session");
        }
        return;
    }

    newsCheckedOnce_ = true;
    newsId_ = item.id;
    newsTitle_ = item.title.empty() ? "News" : item.title;
    newsBody_ = item.body;
    newsLines_.clear();
    {
        // Markdown-lite: headings, lists, HR, **bold**, *italic*, `code`.
        const int maxTextW = 600;
        size_t pos = 0;
        const std::string& b = newsBody_;
        while (pos <= b.size()) {
            size_t endLine = b.find('\n', pos);
            std::string raw;
            if (endLine == std::string::npos) {
                raw = b.substr(pos);
                pos = b.size() + 1;
            } else {
                raw = b.substr(pos, endLine - pos);
                pos = endLine + 1;
            }

            news_md::ParsedLine pl = news_md::classifyRawLine(raw);

            NewsDrawLine base;
            base.color = 0;
            if (pl.kind == news_md::Kind::Blank) {
                base.isBlank = true;
                base.heightPx = 12;
                base.text.clear();
                newsLines_.push_back(base);
                if (endLine == std::string::npos) break;
                continue;
            }
            if (pl.kind == news_md::Kind::Hr) {
                base.isHr = true;
                base.heightPx = 16;
                base.text.clear();
                newsLines_.push_back(base);
                if (endLine == std::string::npos) break;
                continue;
            }

            float scale = 0.70f;
            int height = 28;
            int indent = 0;
            bool emphasize = false;
            if (pl.kind == news_md::Kind::H1) {
                scale = 1.04f; height = 38; emphasize = true;
            } else if (pl.kind == news_md::Kind::H2) {
                scale = 0.90f; height = 34; emphasize = true;
            } else if (pl.kind == news_md::Kind::H3) {
                scale = 0.80f; height = 30; emphasize = true;
            } else if (pl.kind == news_md::Kind::List) {
                scale = 0.70f; height = 28; indent = 12; emphasize = true;
            }

            std::string content = pl.text;
            if (pl.kind == news_md::Kind::List)
                content = std::string("• ") + content;

            if (font_) {
                const std::string measure = news_md::plainForWidth(content);
                auto wrapped = wrapTextToWidth(font_, scale, measure, maxTextW - indent);
                if (wrapped.size() <= 1) {
                    NewsDrawLine dl = base;
                    dl.text = content;
                    dl.scale = scale;
                    dl.heightPx = height;
                    dl.indentPx = indent;
                    dl.emphasize = emphasize;
                    newsLines_.push_back(std::move(dl));
                } else {
                    for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                        NewsDrawLine dl = base;
                        dl.text = wrapped[wi];
                        dl.scale = scale;
                        dl.heightPx = (wi == 0 ? height : 22);
                        dl.indentPx = indent + (wi > 0 && pl.kind == news_md::Kind::List ? 14 : 0);
                        dl.emphasize = emphasize;
                        newsLines_.push_back(std::move(dl));
                    }
                }
            } else {
                NewsDrawLine dl = base;
                dl.text = content;
                dl.scale = scale;
                dl.heightPx = height;
                dl.indentPx = indent;
                dl.emphasize = emphasize;
                newsLines_.push_back(std::move(dl));
            }
            if (endLine == std::string::npos) break;
        }
        if (newsLines_.empty()) {
            NewsDrawLine dl;
            dl.isBlank = true;
            dl.heightPx = 12;
            newsLines_.push_back(dl);
        }
    }
    newsScrollLine_ = 0;
    visualNewsScroll_ = 0.f;

    const bool autoShow = ::psvitaalive::NewsManager::shouldAutoShow(item);
    if (forceShow || autoShow) {
        newsVisible_ = true;
        newsMarkSeenOnClose_ = true;
        diagnostics::log(std::string("[UI] news modal show id=") + newsId_ +
                         (forceShow ? " (manual)" : " (auto)"));
    } else {
        diagnostics::log(std::string("[UI] news not shown (already seen) id=") + newsId_);
    }
}

void FullCatalogScreen::drawNewsChip() {
    // Left of Report chip (which is left of ux0 panel)
    const int panelW = 220;
    const int panelX = SCREEN_W - panelW - 6;
    const int chipW = 100;
    const int chipH = FOOTER_H - 8;
    const int reportW = 100;
    const int chipX = panelX - reportW - 8 - chipW - 8;
    const int chipY = SCREEN_H - FOOTER_H + 4;
    if (!font_) return;
    // Match Install All CTA: SURFACE2 fill + soft green border pulse
    const float pulse = 0.40f + 0.60f * focusPulse();
    const unsigned borderA = (unsigned)(120.f + 135.f * pulse);
    const unsigned borderCol = withAlpha(ACCENT, borderA > 255 ? 255 : borderA);
    const unsigned fill = SURFACE2;
    const int bwPulse = 2 + (int)(1.5f * pulse);
    vita2d_draw_rectangle(chipX, chipY, chipW, chipH, borderCol);
    vita2d_draw_rectangle(chipX + bwPulse, chipY + bwPulse,
                          chipW - bwPulse * 2, chipH - bwPulse * 2, fill);
    const char* lab = "News";
    const float scale = 0.70f;
    const int tw = vita2d_pgf_text_width(font_, scale, lab);
    const int th = 20;
    vita2d_pgf_draw_text(font_, chipX + (chipW - tw) / 2, chipY + (chipH + th) / 2 - 2, ACCENT, scale, lab);
}

void FullCatalogScreen::drawNewsOverlay() {
    if (!newsVisible_ || !font_) return;
    const unsigned BLACK = RGBA8(0, 0, 0, 255);
    const unsigned CODE_COL = RGBA8(0x7E, 0xC8, 0xFF, 255);
    const int w = 700, h = 420;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 140));
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, w, 3, ACCENT);
    vita2d_draw_rectangle(x, y + 3, 3, h - 6, ACCENT);
    vita2d_draw_rectangle(x + w - 3, y + 3, 3, h - 6, BORDER);
    vita2d_draw_rectangle(x, y + h - 3, w, 3, BORDER);

    vita2d_pgf_draw_text(font_, x + 24, y + 36, ACCENT, 0.82f, "NEWS");
    vita2d_pgf_draw_text(font_, x + 24, y + 68, WHITE, 1.00f,
                         ellipsize(newsTitle_, 52).c_str());

    const int textTop = y + 96;
    const int textBottom = y + h - 60;
    const int lineH = 26;
    const int maxVisible = std::max(1, (textBottom - textTop) / lineH);
    int total = (int)newsLines_.size();
    const int maxScroll = std::max(0, total - maxVisible);
    if (newsScrollLine_ < 0) newsScrollLine_ = 0;
    if (newsScrollLine_ > maxScroll) newsScrollLine_ = maxScroll;
    if (visualNewsScroll_ < 0.f) visualNewsScroll_ = 0.f;
    if (visualNewsScroll_ > (float)maxScroll) visualNewsScroll_ = (float)maxScroll;

    const float vs = visualNewsScroll_;
    const int start = (int)std::floor(vs);
    const float frac = vs - (float)start;

    auto heightAt = [&](int idx) -> int {
        if (idx < 0 || idx >= total) return lineH;
        int hp = newsLines_[(size_t)idx].heightPx;
        return hp > 0 ? hp : lineH;
    };

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 20, textTop, x + w - 22, textBottom);
    int cursorY = textTop - (int)(frac * (float)heightAt(start));
    if (start > 0) cursorY -= heightAt(start - 1);
    for (int i = std::max(0, start - 1); i < total; ++i) {
        const NewsDrawLine& nl = newsLines_[(size_t)i];
        const int hp = heightAt(i);
        if (cursorY > textBottom + hp) break;
        if (cursorY + hp >= textTop - hp) {
            if (nl.isHr) {
                const int hy = cursorY + hp / 2;
                vita2d_draw_rectangle(x + 28, hy, w - 56, 2, BORDER);
            } else if (!nl.isBlank && !nl.text.empty()) {
                float scale = nl.scale > 0.01f ? nl.scale : 0.55f;
                unsigned baseCol = TEXT;
                unsigned boldCol = WHITE;
                if (nl.emphasize) baseCol = WHITE;
                if (scale >= 0.90f) baseCol = ACCENT;
                else if (scale >= 0.78f) baseCol = WHITE;
                const int tx = x + 28 + nl.indentPx;
                const int baseY = cursorY + std::min(hp - 4, (int)(scale * 18.f) + 4);
                news_md::drawInlineMarkdown(font_, tx, baseY, scale, baseCol, boldCol, CODE_COL, nl.text);
            }
        }
        cursorY += hp;
        if (i >= start + maxVisible + 3) break;
    }
    vita2d_disable_clipping();

    if (total > maxVisible) {
        const int trackX = x + w - 16;
        const int trackY = textTop;
        const int trackH = textBottom - textTop;
        vita2d_draw_rectangle(trackX, trackY, 4, trackH, BORDER);
        const float thumbH = std::max(22.f, (float)trackH * ((float)maxVisible / (float)total));
        const float thumbT = (maxScroll > 0) ? (vs / (float)maxScroll) : 0.f;
        const int thumbY = trackY + (int)(thumbT * ((float)trackH - thumbH));
        vita2d_draw_rectangle(trackX, thumbY, 4, (int)thumbH, ACCENT);
        char scr[32];
        sceClibSnprintf(scr, sizeof(scr), "%d/%d", std::min(total, start + 1), total);
        vita2d_pgf_draw_text(font_, x + w - 96, y + 36, DIM, 0.58f, scr);
    }

    const int by = y + h - 48, bw = 220, bh = 36;
    vita2d_draw_rectangle(x + (w - bw) / 2, by, bw, bh, ACCENT);
    {
        const char* clab = "O  Close";
        const float sc = 0.68f;
        const int tw = vita2d_pgf_text_width(font_, sc, clab);
        vita2d_pgf_draw_text(font_, x + (w - bw) / 2 + (bw - tw) / 2, by + 25, BLACK, sc, clab);
    }
    vita2d_pgf_draw_text(font_, x + 24, y + h - 14, DIM, 0.72f, "D-Pad: scroll   Circle: close");
}

void FullCatalogScreen::drawReportChip() {
    const int panelW = 220;
    const int panelX = SCREEN_W - panelW - 6;
    const int chipW = 100;
    const int chipH = FOOTER_H - 8;
    const int chipX = panelX - chipW - 8;
    const int chipY = SCREEN_H - FOOTER_H + 4;
    if (!font_) return;

    const unsigned BLACK = RGBA8(0, 0, 0, 255);
    const unsigned GREEN = RGBA8(0x3B, 0xD9, 0x60, 255);
    const unsigned RED = RGBA8(0xE0, 0x32, 0x32, 255);

    // Expire transient Sent/Fail chip state
    if (reportUiState_ >= 2 && reportUiUntilMs_ > 0) {
        const uint64_t now = sceKernelGetProcessTimeWide() / 1000ULL;
        if (now >= reportUiUntilMs_) {
            reportUiState_ = 0;
            reportUiMsg_[0] = 0;
        }
    }

    unsigned fill = RED;
    unsigned textCol = WHITE;
    const char* lab = "Report";
    if (reportUiState_ == 1) {
        fill = SURFACE;
        textCol = WHITE;
        lab = ""; // progress bar instead
    } else if (reportUiState_ == 2) {
        fill = GREEN;
        textCol = BLACK;
        lab = "Sent";
    } else if (reportUiState_ == 3) {
        fill = RED;
        textCol = WHITE;
        lab = "Fail";
    }

    vita2d_draw_rectangle(chipX, chipY, chipW, chipH, fill);

    if (reportUiState_ == 1) {
        // Indeterminate bar (same idea as image loading placeholders)
        const int barX = chipX + 10;
        const int barW = chipW - 16;
        const int barH = 8;
        const int barY = chipY + (chipH - barH) / 2;
        vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
        const float t = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1000) / 1000.f;
        const float phase = t < 0.5f ? (t * 2.f) : (2.f - t * 2.f);
        const int fillW = (int)(barW * (0.25f + 0.55f * phase));
        if (fillW > 0) vita2d_draw_rectangle(barX, barY, fillW, barH, RED);
    } else {
        const float scale = 0.70f;
        const int tw = vita2d_pgf_text_width(font_, scale, lab);
        const int th = 20;
        vita2d_pgf_draw_text(font_, chipX + (chipW - tw) / 2, chipY + (chipH + th) / 2 - 2, textCol, scale, lab);
    }
}


void FullCatalogScreen::openReportConfirm() {
    if (reportUiState_ == 1 || reportBusy_.load()) return;
    if (newsVisible_) return;
    reportConfirmVisible_ = true;
}

void FullCatalogScreen::closeReportConfirm() {
    reportConfirmVisible_ = false;
}


bool FullCatalogScreen::itemEligibleForDataRequest(const CatalogItem& item) const {
    // Data/Game Files requests only make sense for homebrew ports/games/emulators.
    if (state_.catalog != CatalogType::Homebrew) return false;
    if (itemHasDataOrGameFiles(item)) return false;
    return categoryWantsDataFiles(item.category);
}

void FullCatalogScreen::openDataRequestConfirm() {
    if (dataRequestBusy_.load() || reportConfirmVisible_ || newsVisible_) return;
    if (installAllPhase_ != InstallAllPhase::Hidden) return;
    const int i = selectedIndex();
    if (i < 0) return;
    const CatalogItem& it = catalogView()[i];
    if (!itemEligibleForDataRequest(it)) return;
    dataReqName_ = it.name;
    dataReqTitleId_ = it.titleId;
    dataReqVersion_ = it.version;
    dataReqDate_ = it.versionDate;
    dataRequestConfirmVisible_ = true;
}

void FullCatalogScreen::closeDataRequestConfirm() {
    dataRequestConfirmVisible_ = false;
}

void FullCatalogScreen::drawDataRequestConfirmOverlay() {
    if (!dataRequestConfirmVisible_ || !font_) return;
    const unsigned ACC = ACCENT;
    const int w = 560, h = 280;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 160));
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, w, 3, ACC);
    vita2d_draw_rectangle(x, y + 3, 3, h - 6, ACC);
    vita2d_draw_rectangle(x + w - 3, y + 3, 3, h - 6, BORDER);
    vita2d_draw_rectangle(x, y + h - 3, w, 3, BORDER);

    vita2d_pgf_draw_text(font_, x + 24, y + 40, ACC, 0.86f, "REQUEST DATA / GAME FILES");
    vita2d_pgf_draw_text(font_, x + 24, y + 78, WHITE, 0.78f, "This app has no Data/Game Files links.");
    vita2d_pgf_draw_text(font_, x + 24, y + 110, TEXT, 0.72f, "Send a request so we can look for them.");
    vita2d_pgf_draw_text(font_, x + 24, y + 136, TEXT, 0.72f, "It may take several days — we will add them");
    vita2d_pgf_draw_text(font_, x + 24, y + 162, TEXT, 0.72f, "when available. Thank you for your patience.");

    const int by = y + h - 56, bh = 40, bw = 180, gap = 24;
    const int bxCancel = x + (w - (bw * 2 + gap)) / 2;
    const int bxSend = bxCancel + bw + gap;

    vita2d_draw_rectangle(bxCancel, by, bw, bh, SURFACE2);
    vita2d_draw_rectangle(bxCancel, by, bw, 1, BORDER);
    vita2d_draw_rectangle(bxCancel, by + bh - 1, bw, 1, BORDER);
    {
        const char* lab = "O  Cancel";
        const float sc = 0.62f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, bxCancel + (bw - tw) / 2, by + 27, WHITE, sc, lab);
    }
    vita2d_draw_rectangle(bxSend, by, bw, bh, ACC);
    {
        const char* lab = "X  Send";
        const float sc = 0.62f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, bxSend + (bw - tw) / 2, by + 27, BG, sc, lab);
    }
}

namespace {
// Same Discord webhook as error reports (rotate if abused).
constexpr const char* kDataRequestWebhookUrl =
    "https://discord.com/api/webhooks/1544111419895840772/"
    "hoqpAh5rNz_lt6-T7UroKCwPBRTRZ1RtXlGlsruyO3T--7yIk3jgx_ml0y2OuC9Bgqef";

std::string dataReqJsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
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
} // namespace

int FullCatalogScreen::dataRequestWorkerEntry(SceSize args, void* argp) {
    (void)args;
    FullCatalogScreen* self = *reinterpret_cast<FullCatalogScreen**>(argp);
    if (!self) return 0;

    auto esc = dataReqJsonEscape;
    std::string body = "{";
    body += "\"embeds\":[{";
    body += "\"title\":\"Data / Game Files request\",";
    body += "\"color\":3447003,";
    body += "\"fields\":[";
    body += "{\"name\":\"App\",\"value\":\"" + esc(self->dataReqName_.empty() ? "(unknown)" : self->dataReqName_) + "\",\"inline\":true},";
    body += "{\"name\":\"Title ID\",\"value\":\"" + esc(self->dataReqTitleId_.empty() ? "—" : self->dataReqTitleId_) + "\",\"inline\":true},";
    body += "{\"name\":\"Version\",\"value\":\"" + esc(self->dataReqVersion_.empty() ? "—" : self->dataReqVersion_) + "\",\"inline\":true},";
    body += "{\"name\":\"Date\",\"value\":\"" + esc(self->dataReqDate_.empty() ? "—" : self->dataReqDate_) + "\",\"inline\":true},";
    body += "{\"name\":\"Message\",\"value\":\"User requested Data/Game Files for this app (may take days).\",\"inline\":false}";
    body += "]";
    body += "}]";
    body += "}";

    HttpClient http;
    bool ok = false;
    if (http.init() == HttpResult::Ok) {
        const HttpResult hr = http.postJson(kDataRequestWebhookUrl, body);
        ok = (hr == HttpResult::Ok);
        if (!ok) {
            diagnostics::log(std::string("[DataRequest] webhook failed: ") + http.lastError());
        } else {
            diagnostics::log("[DataRequest] sent name=" + self->dataReqName_ + " tid=" + self->dataReqTitleId_);
        }
        http.shutdown();
    } else {
        diagnostics::log("[DataRequest] HTTP init failed");
    }

    sceClibSnprintf(self->dataRequestResultMsg_, sizeof(self->dataRequestResultMsg_),
                    ok ? "Request sent" : "Request failed");
    self->dataRequestOk_.store(ok);
    self->dataRequestDone_.store(true);
    self->dataRequestBusy_.store(false);
    return 0;
}

void FullCatalogScreen::trySendDataRequest() {
    if (dataRequestBusy_.load()) {
        showToast("Request already in progress", 1500);
        return;
    }
    dataRequestBusy_.store(true);
    dataRequestDone_.store(false);
    dataRequestOk_.store(false);
    dataRequestResultMsg_[0] = '\0';

    FullCatalogScreen* self = this;
    dataRequestThread_ = sceKernelCreateThread(
        "PSVitaAliveDataReq", &FullCatalogScreen::dataRequestWorkerEntry,
        0x10000100, 32 * 1024, 0, 0, nullptr);
    if (dataRequestThread_ < 0) {
        dataRequestBusy_.store(false);
        showToast("Could not start request", 1800);
        return;
    }
    sceKernelStartThread(dataRequestThread_, sizeof(self), &self);
    showToast("Sending request…", 1200);
}

void FullCatalogScreen::pollDataRequestWorker() {
    if (!dataRequestDone_.load()) return;
    dataRequestDone_.store(false);
    if (dataRequestThread_ >= 0) {
        sceKernelWaitThreadEnd(dataRequestThread_, nullptr, nullptr);
        sceKernelDeleteThread(dataRequestThread_);
        dataRequestThread_ = -1;
    }
    showToast(dataRequestResultMsg_[0] ? dataRequestResultMsg_ : (dataRequestOk_.load() ? "Request sent" : "Request failed"),
              2200);
}


void FullCatalogScreen::drawReportConfirmOverlay() {
    if (!reportConfirmVisible_ || !font_) return;
    const unsigned BLACK = RGBA8(0, 0, 0, 255);
    const unsigned RED = RGBA8(0xE0, 0x32, 0x32, 255);
    const int w = 560, h = 260;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 160));
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, w, 3, RED);
    vita2d_draw_rectangle(x, y + 3, 3, h - 6, RED);
    vita2d_draw_rectangle(x + w - 3, y + 3, 3, h - 6, BORDER);
    vita2d_draw_rectangle(x, y + h - 3, w, 3, BORDER);

    vita2d_pgf_draw_text(font_, x + 24, y + 40, RED, 0.86f, "REPORT AN ISSUE");
    vita2d_pgf_draw_text(font_, x + 24, y + 78, WHITE, 0.80f, "Did something go wrong?");
    vita2d_pgf_draw_text(font_, x + 24, y + 112, TEXT, 0.72f, "Send a report with the recent logs so we can");
    vita2d_pgf_draw_text(font_, x + 24, y + 138, TEXT, 0.72f, "review it and fix the problem as soon as possible.");

    const int by = y + h - 56, bh = 40, bw = 180, gap = 24;
    const int bxCancel = x + (w - (bw * 2 + gap)) / 2;
    const int bxReport = bxCancel + bw + gap;

    // Cancel (outline)
    vita2d_draw_rectangle(bxCancel, by, bw, bh, SURFACE2);
    vita2d_draw_rectangle(bxCancel, by, bw, 1, BORDER);
    vita2d_draw_rectangle(bxCancel, by + bh - 1, bw, 1, BORDER);
    {
        const char* lab = "O  Cancel";
        const float sc = 0.62f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, bxCancel + (bw - tw) / 2, by + 27, WHITE, sc, lab);
    }
    // Report (red CTA)
    vita2d_draw_rectangle(bxReport, by, bw, bh, RED);
    {
        const char* lab = "X  Report";
        const float sc = 0.62f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, bxReport + (bw - tw) / 2, by + 27, WHITE, sc, lab);
    }
}

int FullCatalogScreen::reportWorkerEntry(SceSize args, void* argp) {
    (void)args;
    FullCatalogScreen* self = *reinterpret_cast<FullCatalogScreen**>(argp);
    if (!self) return 0;
    ::psvitaalive::ErrorReportRequest req;
    req.title = self->reportTitle_;
    req.context = self->reportContext_;
    req.app.name = self->reportAppName_;
    req.app.titleId = self->reportAppTitleId_;
    req.app.version = self->reportAppVersion_;
    req.fileName = self->reportFileName_;
    {
        std::string low = req.title;
        for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (low.find("manual") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::Manual;
        else if (low.find("install") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::InstallFailed;
        else if (low.find("download") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::DownloadFailed;
        else if (low.find("catalog") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::Catalog;
        else if (low.find("self-update") != std::string::npos || low.find("self update") != std::string::npos)
            req.kind = ::psvitaalive::ErrorReportKind::SelfUpdate;
        else
            req.kind = ::psvitaalive::ErrorReportKind::Other;
    }
    const auto res = ::psvitaalive::sendErrorReport(req);
    self->reportOk_.store(res.ok);
    sceClibSnprintf(self->reportResultMsg_, sizeof(self->reportResultMsg_), "%s",
                    res.message.empty() ? (res.ok ? "Report sent" : "Report failed") : res.message.c_str());
    self->reportDone_.store(true);
    self->reportBusy_.store(false);
    return 0;
}

void FullCatalogScreen::pollReportWorker() {
    if (!reportDone_.load()) return;
    reportDone_.store(false);
    if (reportThread_ >= 0) {
        sceKernelWaitThreadEnd(reportThread_, nullptr, nullptr);
        sceKernelDeleteThread(reportThread_);
        reportThread_ = -1;
    }
    const bool ok = reportOk_.load();
    reportUiState_ = ok ? 2 : 3;
    sceClibSnprintf(reportUiMsg_, sizeof(reportUiMsg_), "%s", reportResultMsg_);
    reportUiUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 3500ULL;
    showToast(ok ? (reportResultMsg_[0] ? reportResultMsg_ : "Report sent")
                 : (reportResultMsg_[0] ? reportResultMsg_ : "Report failed"),
              2200);
    diagnostics::log(std::string("[UI] error report finished ok=") + (ok ? "1" : "0") +
                     " msg=" + reportResultMsg_);
}

void FullCatalogScreen::trySendErrorReport(const std::string& title, const std::string& context) {
    if (reportUiState_ == 1 || reportBusy_.load()) return;
    if (reportThread_ >= 0) return;

    reportTitle_ = title;
    reportContext_ = context;
    reportAppName_.clear();
    reportAppTitleId_.clear();
    reportAppVersion_.clear();
    reportFileName_.clear();
    // Prefer current catalog selection + any install result titleId.
    {
        const int si = selectedIndex();
        if (si >= 0 && si < (int)catalogView().size()) {
            const CatalogItem& it = catalogView()[si];
            reportAppName_ = it.name;
            reportAppTitleId_ = it.titleId;
            reportAppVersion_ = it.version;
        }
        if (reportAppTitleId_.empty() && !installResultTitleId_.empty())
            reportAppTitleId_ = installResultTitleId_;
        if (!installProgressFile_.empty())
            reportFileName_ = installProgressFile_;
    }
    reportUiState_ = 1;
    reportUiUntilMs_ = 0;
    reportDone_.store(false);
    reportOk_.store(false);
    reportResultMsg_[0] = 0;
    sceClibSnprintf(reportUiMsg_, sizeof(reportUiMsg_), "Sending...");
    diagnostics::log("[UI] error report requested: " + title);

    reportBusy_.store(true);
    reportThread_ = sceKernelCreateThread("PSVA_Report", reportWorkerEntry, 0x10000100, 0x10000, 0, 0, nullptr);
    if (reportThread_ < 0) {
        reportBusy_.store(false);
        reportUiState_ = 0;
        // Fallback: synchronous send
        ::psvitaalive::ErrorReportRequest req;
        req.title = reportTitle_;
        req.context = reportContext_;
        req.app.name = reportAppName_;
        req.app.titleId = reportAppTitleId_;
        req.app.version = reportAppVersion_;
        req.fileName = reportFileName_;
        {
            std::string low = req.title;
            for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (low.find("manual") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::Manual;
            else if (low.find("install") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::InstallFailed;
            else if (low.find("download") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::DownloadFailed;
            else if (low.find("catalog") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::Catalog;
            else if (low.find("self-update") != std::string::npos || low.find("self update") != std::string::npos)
                req.kind = ::psvitaalive::ErrorReportKind::SelfUpdate;
            else
                req.kind = ::psvitaalive::ErrorReportKind::Other;
        }
        const auto res = ::psvitaalive::sendErrorReport(req);
        reportUiState_ = res.ok ? 2 : 3;
        reportUiUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 3500ULL;
        showToast(res.ok ? "Report sent" : (res.message.empty() ? "Report failed" : res.message), 2200);
        return;
    }
    FullCatalogScreen* self = this;
    if (sceKernelStartThread(reportThread_, sizeof(self), &self) < 0) {
        sceKernelDeleteThread(reportThread_);
        reportThread_ = -1;
        reportBusy_.store(false);
        reportUiState_ = 0;
        showToast("Report failed to start", 1800);
    }
}


void FullCatalogScreen::setInstallCallbacks(InstallRequestFn r,InstallStatusFn s){installRequest_=std::move(r);installStatusText_=std::move(s);}void FullCatalogScreen::setInstallCancelCallback(InstallCancelFn c){installCancel_=std::move(c);}void FullCatalogScreen::setInstallAcknowledgeCallback(InstallAcknowledgeFn c){installAcknowledge_=std::move(c);}void FullCatalogScreen::setCatalogChangeCallback(CatalogChangeFn c){catalogChange_=std::move(c);}void FullCatalogScreen::setSearchCallback(SearchRequestFn c){searchRequest_=std::move(c);}void FullCatalogScreen::setLinkActionCallback(LinkActionFn c){linkAction_=std::move(c);}void FullCatalogScreen::setImageCache(ImageCache*c){imageCache_=c;}
void FullCatalogScreen::setCatalogItems(std::vector<CatalogItem>items){
    // Textures for the previous tab may still be draining; only schedule free if needed.
    releaseTextures();
    installStatusCache_.clear();
    allItems_=std::move(items);
    sortItemsByDate(allItems_);
    items_.clear();
    applySearch(searchQuery_);
    visualCatalogScroll_=0.f;
    visualDetailScroll_=0.f;
    visualFocusIndex_=0.f;
    detailCrossfade_=1.f;
    detailCrossfadeFrom_=-1;
    contentFade_=0.4f;
    catalogLoading_=false;
    catalogError_.clear();
    // Give the catalog/UI time to settle before any ux0:app probes begin.
    installStatusWarmupUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 1500ULL;
    // Instant memory-cache hits clear loading quickly; keep debounce so spam L/R cannot thrash.
    if(catalogSwitchCooldownFrames_<CATALOG_SWITCH_COOLDOWN_FRAMES/2)
        catalogSwitchCooldownFrames_=CATALOG_SWITCH_COOLDOWN_FRAMES/2;
    lastCatalogSwitchMs_=sceKernelGetProcessTimeWide()/1000ULL;
}void FullCatalogScreen::setActiveCatalog(CatalogType c){
    // Textures usually already released in changeCatalog; release again only if any remain.
    if(!textures_.empty() || !deferredFreeTextures_.empty()){
        releaseTextures();
    }
    state_.catalog=c;
    installStatusWarmupUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 1500ULL;
    searchQuery_.clear();
    dataFilesFilter_ = false;
    items_.clear(); // browse via allItems_ (catalogView) — no duplicate
    state_.focusIndex=0;
    state_.catalogScrollRow=0;
    state_.detailScroll=0;
    visualCatalogScroll_=0.f;
    visualDetailScroll_=0.f;
    contentFade_=0.45f;
    detailScrollBeforeLinkMode_=0;
    state_.linkFocus=-1;
    state_.linkNavigation=false;
}void FullCatalogScreen::setCatalogLoading(bool l,const std::string&lab,uint64_t cur,uint64_t tot,const std::string&msg){
    if (l) {
        catalogLoading_ = true;
        catalogSplashAlpha_ = 1.f;
        catalogError_.clear();
    } else {
        catalogLoading_ = false;
        // Keep splash visible until fade-out completes in updateAnimations / drawLoadingOverlay
        if (catalogSplashAlpha_ < 0.05f) catalogSplashAlpha_ = 0.f;
    }
    catalogLoadingLabel_=lab;
    catalogLoadingCurrent_=cur;
    catalogLoadingTotal_=tot;
    catalogLoadingMessage_=msg;
}void FullCatalogScreen::setCatalogError(const std::string&e){catalogLoading_=false;catalogError_=e;}void FullCatalogScreen::setInstallProgress(
    bool active,
    uint64_t current,
    uint64_t total,
    uint64_t bytesPerSecond,
    const std::string& stage,
    const std::string& fileName,
    const std::string& message,
    int outcome,
    bool liveAreaOk,
    const std::string& installPath,
    const std::string& titleId,
    uint64_t resultAutoCloseRemainingMs)
{
    installProgressActive_ = active;
    installProgressCurrent_ = current;
    installProgressTotal_ = total;
    installProgressSpeed_ = bytesPerSecond;

    installProgressStage_ = stage;
    installProgressFile_ = fileName;
    installProgressMessage_ = message;
    installResultAutoCloseMs_ = resultAutoCloseRemainingMs;

    installOutcome_ = outcome;
    installLiveAreaOk_ = liveAreaOk;
    installResultPath_ = installPath;
    installResultTitleId_ = titleId;
    // Refresh local install badges after a finished install attempt
    if (!titleId.empty() && (outcome == 1 || outcome == 2)) {
        invalidateInstallStatus(titleId);
    }

    // Install All queue: advance on Completed, abort tracking on fail/cancel
    installAllTryAdvanceFromProgress(outcome);
}
bool FullCatalogScreen::init(){
    vita2d_init();
    vita2d_set_clear_color(BG);
    font_=vita2d_load_default_pgf();
    if(!font_)return false;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    // Optional full-screen catalog splash (bundled in VPK as ui/catalog_loading.png)
    catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.png");
    if (!catalogLoadingTex_) {
        catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.PNG");
    }
    if (catalogLoadingTex_) {
        diagnostics::log("[UI] catalog_loading.png loaded");
    } else {
        diagnostics::log("[UI] catalog_loading.png not found (fallback overlay)");
    }
    headerLogoTex_ = vita2d_load_PNG_file("app0:ui/PSVitaAlive_Store_logo_text.png");
    if (!headerLogoTex_) {
        headerLogoTex_ = vita2d_load_PNG_file("app0:ui/logo.png");
    }
    if (headerLogoTex_) {
        diagnostics::log("[UI] header logo loaded");
    } else {
        diagnostics::log("[UI] header logo not found (text fallback)");
    }
    state_=UiState{};
    ready_=true;
    diagnostics::log("[UI] initialized");
    return true;
}void FullCatalogScreen::scheduleTextureFree(vita2d_texture* texture){
    if(!texture)return;
    deferredFreeTextures_.push_back(texture);
}
void FullCatalogScreen::flushDeferredTextureFrees(){
    if(deferredFreeTextures_.empty())return;
    // Previous frame has been presented; safe to return memory to vita2d.
    // Cap per frame so spam L/R cannot free dozens of textures in one shot (Vita3K crash).
    vita2d_wait_rendering_done();
    size_t n=0;
    while(!deferredFreeTextures_.empty() && n<MAX_DEFERRED_FREES_PER_FRAME){
        vita2d_texture* t=deferredFreeTextures_.front();
        deferredFreeTextures_.erase(deferredFreeTextures_.begin());
        if(t)vita2d_free_texture(t);
        ++n;
    }
}
void FullCatalogScreen::releaseTextures(){
    if(textures_.empty()){
        textureOrder_.clear();
    } else {
        for(auto& e:textures_){
            if(e.second){
                scheduleTextureFree(e.second);
                e.second=nullptr;
            }
        }
        textures_.clear();
        textureOrder_.clear();
    }
    // Catalog switches free many textures at once — drain immediately after GPU
    // wait so the next catalog does not pile deferred frees / UAF under load.
    if(!deferredFreeTextures_.empty()){
        vita2d_wait_rendering_done();
        while(!deferredFreeTextures_.empty()){
            vita2d_texture* t=deferredFreeTextures_.front();
            deferredFreeTextures_.erase(deferredFreeTextures_.begin());
            if(t)vita2d_free_texture(t);
        }
    }
}
void FullCatalogScreen::releaseScreenshotTextures(){
    bool any=false;
    for(const auto& kv:textures_){
        if(kv.first.find("/shot_")!=std::string::npos){any=true;break;}
    }
    if(!any)return;
    for(auto i=textures_.begin();i!=textures_.end();){
        if(i->first.find("/shot_")!=std::string::npos){
            scheduleTextureFree(i->second);
            i=textures_.erase(i);
        }else ++i;
    }
    textureOrder_.erase(std::remove_if(textureOrder_.begin(),textureOrder_.end(),[](const std::string& p){
        return p.find("/shot_")!=std::string::npos;
    }),textureOrder_.end());
}
void FullCatalogScreen::releaseTexturesNotIn(const std::unordered_set<std::string>& keep){
    bool any=false;
    for(const auto& kv:textures_){
        if(keep.find(kv.first)==keep.end()){any=true;break;}
    }
    if(!any)return;
    size_t freed=0;
    for(auto i=textures_.begin();i!=textures_.end();){
        if(keep.find(i->first)==keep.end()){
            scheduleTextureFree(i->second);
            i=textures_.erase(i);
            ++freed;
        }else ++i;
    }
    textureOrder_.erase(std::remove_if(textureOrder_.begin(),textureOrder_.end(),[&](const std::string& p){
        return keep.find(p)==keep.end();
    }),textureOrder_.end());
    if(freed>0){
        char m[96];
        sceClibSnprintf(m,sizeof(m),"[UI] deferred-free %u off-screen textures (kept %u)",
            (unsigned)freed,(unsigned)textures_.size());
        diagnostics::log(m);
    }
}
void FullCatalogScreen::touchTexture(const std::string& p){
    auto i=std::find(textureOrder_.begin(),textureOrder_.end(),p);
    if(i!=textureOrder_.end())textureOrder_.erase(i);
    textureOrder_.push_back(p);
}
void FullCatalogScreen::evictTextureIfNeeded(const std::string& ns){
    const size_t lim=(ns=="shot")?MAX_SCREENSHOT_TEXTURES:MAX_APP_TEXTURES;
    const char* marker=(ns=="shot")?"/shot_":"/app_";
    size_t c=0;
    for(const auto& p:textureOrder_)if(p.find(marker)!=std::string::npos)++c;
    while(c>=lim){
        bool removed=false;
        for(auto i=textureOrder_.begin();i!=textureOrder_.end();++i){
            if(i->find(marker)==std::string::npos)continue;
            auto t=textures_.find(*i);
            if(t!=textures_.end()){
                scheduleTextureFree(t->second);
                textures_.erase(t);
            }
            textureOrder_.erase(i);
            --c;
            removed=true;
            break;
        }
        if(!removed)break;
    }
}
void FullCatalogScreen::shutdown(){
    releaseTextures();
    flushDeferredTextureFrees();
    if (catalogLoadingTex_) {
        vita2d_free_texture(catalogLoadingTex_);
        catalogLoadingTex_ = nullptr;
    }
    if (headerLogoTex_) {
        vita2d_free_texture(headerLogoTex_);
        headerLogoTex_ = nullptr;
    }
    if(font_){vita2d_free_pgf(font_);font_=nullptr;}
    if(ready_){vita2d_fini();ready_=false;}
    diagnostics::log("[UI] shutdown");
}
int FullCatalogScreen::totalRows()const{return catalogView().empty()?0:(int(catalogView().size())+2)/3;}int FullCatalogScreen::visibleRowsFull()const{return 3;}int FullCatalogScreen::visibleRowsSplit()const{return std::max(1,(SCREEN_H-HEADER_H-TABS_H-FOOTER_H-GRID_PAD*2)/(SPLIT_CARD_H+CARD_GAP));}int FullCatalogScreen::selectedIndex()const{return catalogView().empty()?-1:std::max(0,std::min(state_.focusIndex,(int)catalogView().size()-1));}void FullCatalogScreen::clampCatalogFocus(){if(catalogView().empty())state_.focusIndex=0;else state_.focusIndex=std::max(0,std::min(state_.focusIndex,(int)catalogView().size()-1));}void FullCatalogScreen::clampCatalogScroll(){if(catalogView().empty()){state_.catalogScrollRow=0;return;}int v=state_.mode==UiMode::FULL_CATALOG?visibleRowsFull():visibleRowsSplit();if(state_.mode==UiMode::FULL_CATALOG){int r=state_.focusIndex/3;if(r<state_.catalogScrollRow)state_.catalogScrollRow=r;if(r>=state_.catalogScrollRow+v)state_.catalogScrollRow=r-v+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,std::max(0,totalRows()-v)));}else{int m=std::max(0,(int)catalogView().size()-v);if(state_.focusIndex<state_.catalogScrollRow)state_.catalogScrollRow=state_.focusIndex;if(state_.focusIndex>=state_.catalogScrollRow+v)state_.catalogScrollRow=state_.focusIndex-v+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,m));}}
void FullCatalogScreen::sortItemsByDate(std::vector<CatalogItem>&v)const{std::stable_sort(v.begin(),v.end(),[](const CatalogItem&a,const CatalogItem&b){if(a.versionDate!=b.versionDate)return a.versionDate>b.versionDate;return lowerAscii(a.name)<lowerAscii(b.name);});}bool FullCatalogScreen::matchesSearch(const CatalogItem&i,const std::string&q)const{if(q.empty())return true;std::string x=lowerAscii(q),h=lowerAscii(i.name+"\n"+i.titleId+"\n"+i.author+"\n"+i.description+"\n"+i.longDescription+"\n"+i.category+"\n"+i.subcategory);return h.find(x)!=std::string::npos;}void FullCatalogScreen::rebuildFilteredItems() {
    items_.clear();
    if (searchQuery_.empty() && !dataFilesFilter_) {
        // catalogView() uses allItems_ — no second full copy in RAM.
        return;
    }
    items_.reserve(allItems_.size() > 256 ? 256 : allItems_.size());
    for (const auto& i : allItems_) {
        if (dataFilesFilter_ && !itemHasDataOrGameFiles(i)) continue;
        if (!matchesSearch(i, searchQuery_)) continue;
        items_.push_back(i);
    }
}

void FullCatalogScreen::setDataFilesFilter(bool enabled) {
    if (dataFilesFilter_ == enabled) return;
    dataFilesFilter_ = enabled;
    rebuildFilteredItems();
    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
    detailScrollBeforeLinkMode_ = 0;
    state_.linkFocus = -1;
    state_.linkNavigation = false;
    visualCatalogScroll_ = 0.f;
    char m[160];
    sceClibSnprintf(m, sizeof(m), "[UI] data-files filter=%d results=%u",
                    dataFilesFilter_ ? 1 : 0, (unsigned)catalogView().size());
    diagnostics::log(m);
    showToast(dataFilesFilter_ ? "Filter: Game/Data Files only" : "Filter cleared", 1400);
}

void FullCatalogScreen::applySearch(const std::string& q) {
    searchQuery_ = q;
    rebuildFilteredItems();
    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
    detailScrollBeforeLinkMode_ = 0;
    state_.linkFocus = -1;
    state_.linkNavigation = false;
    char m[256];
    sceClibSnprintf(m, sizeof(m), "[UI] search query='%s' dataFilter=%d results=%u",
                    q.c_str(), dataFilesFilter_ ? 1 : 0, (unsigned)catalogView().size());
    diagnostics::log(m);
}
int FullCatalogScreen::detailContentHeight(const CatalogItem& i, int w) const {
    const int cw = std::max(1, w - 36);
    const int mc = std::max(16, cw / 7);
    auto bodyLines = [&](const std::string& text) -> int {
        if (text.empty()) return 0;
        std::vector<std::string> lines;
        wrapText(text, mc, lines);
        return (int)lines.size();
    };
    auto reqLines = [&](const std::string& text) -> int {
        if (text.empty()) return 0;
        std::string normalized;
        normalized.reserve(text.size() + 8);
        for (size_t p = 0; p < text.size(); ++p) {
            if (text[p] == '-' && (p == 0 || text[p - 1] == ' ') && p + 1 < text.size() && text[p + 1] == ' ') {
                if (!normalized.empty() && normalized.back() != '\n') normalized.push_back('\n');
            }
            normalized.push_back(text[p]);
        }
        std::vector<std::string> lines;
        wrapText(normalized, mc, lines);
        return (int)lines.size();
    };

    int h = linkLayoutTotalHeight(buildLinkLayout(i));
    if (itemHasDataOrGameFiles(i)) h += LINK_SECTION_H + 4 + INSTALL_ALL_BLOCK_H + 8;

    auto addSection = [&](int body) {
        if (body <= 0) return;
        h += DETAIL_SECTION_H + body * LINE_H + DETAIL_SECTION_GAP;
    };
    addSection(bodyLines(i.description));
    addSection(bodyLines(i.longDescription));

    const int sc = std::min(5, (int)i.screenshots.size());
    if (sc > 0) h += DETAIL_SECTION_H + sc * SCREENSHOT_ROW_H + DETAIL_SECTION_GAP;

    addSection(reqLines(i.requirements));

    int meta = 0;
    if (!i.titleId.empty()) ++meta;
    if (!i.version.empty()) ++meta;
    if (!i.titleId.empty()) ++meta; // install row
    if (!i.versionDate.empty()) ++meta;
    if (!i.category.empty()) ++meta;
    if (!i.subcategory.empty()) ++meta;
    if (!i.size.empty()) ++meta;
    if (!i.status.empty()) ++meta;
    if (meta > 0) h += DETAIL_SECTION_H + 8 + meta * DETAIL_META_H + 12 + DETAIL_SECTION_GAP;

    addSection(bodyLines(i.changelog));
    return h + 24;
}

int FullCatalogScreen::detailLinkScrollLimit(const CatalogItem&i,int w,int h)const{
    (void)w;
    const auto rows = buildLinkLayout(i);
    if (rows.empty()) return 0;
    int lh = linkLayoutTotalHeight(rows);
    if (itemHasDataOrGameFiles(i)) lh += LINK_SECTION_H + 4 + INSTALL_ALL_BLOCK_H + 8;
    const int v = std::max(1, h - DETAIL_HEADER_H - 18);
    return std::max(0, lh - v);
}
void FullCatalogScreen::clampDetailScroll(){int i=selectedIndex();if(i<0){state_.detailScroll=0;return;}int vh=std::max(1,SCREEN_H-HEADER_H-TABS_H-FOOTER_H-DETAIL_HEADER_H-18),total=detailContentHeight(catalogView()[i],SCREEN_W/2),mx=std::max(0,total-vh);state_.detailScroll=std::max(0,std::min(state_.detailScroll,mx));if(catalogView()[i].linkDetails.empty()){state_.linkFocus=-1;state_.linkNavigation=false;}else if(state_.linkFocus>=(int)catalogView()[i].linkDetails.size())state_.linkFocus=(int)catalogView()[i].linkDetails.size()-1;if(state_.linkNavigation){int lim=detailLinkScrollLimit(catalogView()[i],SCREEN_W/2,SCREEN_H-HEADER_H-TABS_H-FOOTER_H);state_.detailScroll=std::max(0,std::min(state_.detailScroll,lim));}}
void FullCatalogScreen::moveCatalogFocus(int d){if(catalogView().empty())return;if(state_.mode!=UiMode::FULL_CATALOG)releaseScreenshotTextures();if(state_.mode==UiMode::FULL_CATALOG){if(d<0&&state_.focusIndex>=3)state_.focusIndex-=3;if(d>0&&state_.focusIndex+3<(int)catalogView().size())state_.focusIndex+=3;}else{if(d<0&&state_.focusIndex>0)--state_.focusIndex;if(d>0&&state_.focusIndex+1<(int)catalogView().size())++state_.focusIndex;}clampCatalogFocus();clampCatalogScroll();state_.detailScroll=0;detailScrollBeforeLinkMode_=0;state_.linkFocus=-1;state_.linkNavigation=false;}void FullCatalogScreen::moveDetailScroll(int d){state_.detailScroll+=d<0?-72:72;clampDetailScroll();}void FullCatalogScreen::enterLinkNavigation(){
    int i=selectedIndex();
    if(i<0)return;
    const auto idxs=downloadLinkIndices(catalogView()[i]);
    const bool hasAll=itemHasDataOrGameFiles(catalogView()[i]);
    if(idxs.empty()&&!hasAll)return;
    detailScrollBeforeLinkMode_=state_.detailScroll;
    state_.linkNavigation=true;
    state_.linkFocus=0;
    state_.detailScroll=0;
    clampDetailScroll();
    diagnostics::log("[UI] link navigation enabled (downloads only)");
}void FullCatalogScreen::exitLinkNavigation(){if(!state_.linkNavigation)return;state_.linkNavigation=false;state_.linkFocus=-1;state_.detailScroll=detailScrollBeforeLinkMode_;detailScrollBeforeLinkMode_=0;clampDetailScroll();diagnostics::log("[UI] link navigation disabled; detail scroll restored");}void FullCatalogScreen::moveLinkFocus(int dx, int dy) {
    (void)dx;
    int i = selectedIndex();
    if (i < 0) return;
    const auto idxs = downloadLinkIndices(catalogView()[i]);
    const bool hasAll = itemHasDataOrGameFiles(catalogView()[i]);
    const int off = hasAll ? 1 : 0;
    const int c = (int)idxs.size() + off;
    if (c <= 0) return;
    if (state_.linkFocus < 0) state_.linkFocus = 0;
    else state_.linkFocus = std::max(0, std::min(c - 1, state_.linkFocus + dy));
    state_.linkNavigation = true;
    const auto rows = buildLinkLayout(catalogView()[i]);
    int top = 0;
    if (hasAll && state_.linkFocus == 0) {
        top = 0;
    } else {
        const int linkFocus = state_.linkFocus - off;
        top = yOfLinkFocus(rows, linkFocus) + (hasAll ? (LINK_SECTION_H + 4 + INSTALL_ALL_BLOCK_H + 8) : 0);
    }
    const int vis = SCREEN_H - HEADER_H - TABS_H - FOOTER_H - DETAIL_HEADER_H - 18;
    const int lim = detailLinkScrollLimit(catalogView()[i], SCREEN_W / 2, SCREEN_H - HEADER_H - TABS_H - FOOTER_H);
    if (top < state_.detailScroll) state_.detailScroll = top;
    if (top + LINK_ROW_H > state_.detailScroll + vis) state_.detailScroll = top + LINK_ROW_H - vis;
    state_.detailScroll = std::max(0, std::min(state_.detailScroll, lim));
}void FullCatalogScreen::activateFocusedLink(){
    int i=selectedIndex();
    if(i<0||state_.linkFocus<0)return;
    const CatalogItem& item = catalogView()[i];
    const bool hasAll = itemHasDataOrGameFiles(item);
    if(hasAll && state_.linkFocus==0){
        openInstallAllWizard();
        return;
    }
    const auto idxs=downloadLinkIndices(item);
    const int off = hasAll ? 1 : 0;
    const int li = state_.linkFocus - off;
    if(li<0||li>=(int)idxs.size())return;
    const CatalogLink&l=item.linkDetails[idxs[li]];
    if(!linkAction_||!actionableLink(l)){diagnostics::log(std::string("[UI] non-download link selected: ")+l.url);return;}
    if(linkAction_(item,l))exitLinkNavigation();
}void FullCatalogScreen::changeCatalog(int d){
    if(catalogLoading_||installProgressActive_||isTransitioning())return;
    if(catalogSwitchCooldownFrames_>0)return;

    // Drain any pending texture frees before switching (do not soft-skip the input).
    if(!deferredFreeTextures_.empty()){
        vita2d_wait_rendering_done();
        while(!deferredFreeTextures_.empty()){
            vita2d_texture* t=deferredFreeTextures_.front();
            deferredFreeTextures_.erase(deferredFreeTextures_.begin());
            if(t)vita2d_free_texture(t);
        }
    }

    const uint64_t nowMs=sceKernelGetProcessTimeWide()/1000ULL;
    if(lastCatalogSwitchMs_!=0 && nowMs>=lastCatalogSwitchMs_ &&
       (nowMs-lastCatalogSwitchMs_)<CATALOG_SWITCH_MIN_MS){
        return;
    }

    int v=(int)state_.catalog+d,c=(int)CatalogType::Count;
    if(v<0)v=c-1;if(v>=c)v=0;
    CatalogType n=(CatalogType)v;
    if(n==state_.catalog)return;

    if(state_.mode!=UiMode::FULL_CATALOG){
        exitLinkNavigation();
        state_.mode=UiMode::FULL_CATALOG;
        state_.activePanel=UiPanel::Catalog;
        state_.detailScroll=0;
        releaseScreenshotTextures();
    }

    // Lock BEFORE loader callback — blocks install probes and image binds.
    catalogLoading_=true;
    catalogLoadingLabel_=catalogName(n);
    catalogLoadingCurrent_=0;
    catalogLoadingTotal_=0;
    catalogLoadingMessage_="Checking catalog cache...";
    catalogError_.clear();
    showToast(std::string("Loading ") + catalogName(n) + "...", 1200);
    state_.catalog=n;
    items_.clear();
    // Drop previous catalog payload early to free RAM before the next load.
    {
        std::vector<CatalogItem> empty;
        allItems_.swap(empty);
    }
    state_.focusIndex=0;
    state_.catalogScrollRow=0;
    releaseTextures();
    installStatusCache_.clear();
    catalogSwitchCooldownFrames_=CATALOG_SWITCH_COOLDOWN_FRAMES;
    lastCatalogSwitchMs_=nowMs;
    installStatusWarmupUntilMs_=nowMs+1500ULL;

    if(catalogChange_){
        if(!catalogChange_(n)){
            catalogLoading_=false;
            catalogError_="Could not start catalog load";
            diagnostics::log("[UI] catalogChange callback returned false");
        }
        return;
    }
    setActiveCatalog(n);
    catalogLoading_=false;
}
bool FullCatalogScreen::isTransitioning()const{return state_.mode==UiMode::OPENING_DETAIL||state_.mode==UiMode::CLOSING_DETAIL;}void FullCatalogScreen::startOpeningDetail(){if(state_.mode!=UiMode::FULL_CATALOG||catalogLoading_||installProgressActive_||selectedIndex()<0)return;state_.detailScroll=0;detailScrollBeforeLinkMode_=0;state_.linkFocus=-1;state_.linkNavigation=false;state_.transitionStart=sceKernelGetProcessTimeWide();state_.mode=UiMode::OPENING_DETAIL;diagnostics::log("[UI] opening detail");}void FullCatalogScreen::startClosingDetail(){
    if(state_.mode!=UiMode::SPLIT_DETAIL)return;
    exitLinkNavigation();
    state_.transitionStart=sceKernelGetProcessTimeWide();
    state_.mode=UiMode::CLOSING_DETAIL;
    diagnostics::log("[UI] closing detail");
}float FullCatalogScreen::transitionProgress()const{
    if(!isTransitioning())return 1.0f;
    uint64_t e=sceKernelGetProcessTimeWide()-state_.transitionStart;
    float t=std::max(0.0f,std::min(1.0f,(float)e/(float)(TRANSITION_MS*1000)));
    return easeInOut(t);
}void FullCatalogScreen::updateTransition(){
    if(!isTransitioning()||transitionProgress()<1.0f)return;
    const bool closing=(state_.mode==UiMode::CLOSING_DETAIL);
    const bool opening = (state_.mode == UiMode::OPENING_DETAIL);
    state_.mode = opening ? UiMode::SPLIT_DETAIL : UiMode::FULL_CATALOG;
    // After opening detail, put focus on DETAIL so the user can browse metadata first.
    state_.activePanel = opening ? UiPanel::Detail : UiPanel::Catalog;
    clampCatalogScroll();
    if(closing){
        releaseScreenshotTextures();
        catalogSwitchCooldownFrames_=2;
        diagnostics::log("[UI] detail closed — screenshot textures scheduled for free");
    }
}
void FullCatalogScreen::handleTouch() {
    if (isTransitioning()) return;

    SceTouchData td{};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) <= 0) return;

    // Vita front touch is typically 1920x1088 logical units.
    auto mapX = [](int tx) { return tx * SCREEN_W / 1920; };
    auto mapY = [](int ty) { return ty * SCREEN_H / 1088; };
    auto hit = [](int x, int y, int rx, int ry, int rw, int rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    };

    // --- News modal: drag to scroll + Close tap ---
    if (newsVisible_) {
        const int ow = 700, oh = 420, ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
        const int textTop = oy + 88;
        const int textBottom = oy + oh - 56;
        const int lineH = 22;
        const int maxVisible = std::max(1, (textBottom - textTop) / lineH);
        const int total = (int)newsLines_.size();
        const int maxScroll = std::max(0, total - maxVisible);

        if (td.reportNum > 0) {
            const int x = mapX(td.report[0].x);
            const int yy = mapY(td.report[0].y);
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = x;
                touchStartY_ = yy;
                touchLastY_ = yy;
                touchMoved_ = false;
                touchAccumY_ = 0.f;
            } else {
                const int dy = yy - touchLastY_;
                touchLastY_ = yy;
                if (std::abs(x - touchStartX_) > 14 || std::abs(yy - touchStartY_) > 14)
                    touchMoved_ = true;
                // Finger up → content up (increase scroll). Soft sensitivity.
                touchAccumY_ += static_cast<float>(-dy);
                const float step = 18.f;
                while (touchAccumY_ >= step) {
                    if (newsScrollLine_ < maxScroll) ++newsScrollLine_;
                    touchAccumY_ -= step;
                }
                while (touchAccumY_ <= -step) {
                    if (newsScrollLine_ > 0) --newsScrollLine_;
                    touchAccumY_ += step;
                }
                if (newsScrollLine_ < 0) newsScrollLine_ = 0;
                if (newsScrollLine_ > maxScroll) newsScrollLine_ = maxScroll;
            }
        } else if (touchDown_) {
            const int x = touchStartX_, y = touchStartY_;
            const bool wasMoved = touchMoved_;
            touchDown_ = false;
            touchAccumY_ = 0.f;
            if (!wasMoved) {
                const int by = oy + oh - 48, bw = 220, bh = 36;
                const int bx = ox + (ow - bw) / 2;
                if (hit(x, y, bx - 12, by - 12, bw + 24, bh + 24))
                    closeNewsModal(newsMarkSeenOnClose_);
            }
        }
        return;
    }


    // --- Install All wizard overlay ---
    if (installAllPhase_ != InstallAllPhase::Hidden && installAllPhase_ != InstallAllPhase::Running) {
        if (td.reportNum > 0) {
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = mapX(td.report[0].x);
                touchStartY_ = mapY(td.report[0].y);
                touchMoved_ = false;
            }
        } else if (touchDown_) {
            const int x = touchStartX_, y = touchStartY_;
            touchDown_ = false;
            if (touchMoved_) return;
            const int ow = 620, oh = 360;
            const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
            if (installAllPhase_ == InstallAllPhase::Confirm) {
                const int bw = 200, bh = 40;
                const int by = oy + oh - 56;
                const int bxOk = ox + 28;
                const int bxCancel = ox + ow - 28 - bw;
                if (hit(x, y, bxOk - 12, by - 12, bw + 24, bh + 24)) { installAllFocus_ = 0; installAllAdvancePick(); return; }
                if (hit(x, y, bxCancel - 12, by - 12, bw + 24, bh + 24)) { closeInstallAllWizard(true); return; }
                return;
            }
            if (installAllItemIndex_ >= 0 && installAllItemIndex_ < (int)catalogView().size()) {
                const int listTop = oy + 86;
                const int rowH = LINK_ROW_H + 6;
                const int maxVis = 5;
                int start = 0;
                if (installAllFocus_ >= maxVis) start = installAllFocus_ - maxVis + 1;
                for (int n = 0; n < maxVis; ++n) {
                    const int idx = start + n;
                    if (idx >= (int)installAllOptions_.size()) break;
                    const int ry = listTop + n * (rowH + 6);
                    if (hit(x, y, ox + 20, ry, ow - 40, rowH)) {
                        installAllFocus_ = idx;
                        const int di = installAllOptions_[idx];
                        if (installAllPhase_ == InstallAllPhase::PickDownload) installAllChosenDownload_ = di;
                        else if (installAllPhase_ == InstallAllPhase::PickGameFiles) installAllChosenGameFiles_ = di;
                        else if (installAllPhase_ == InstallAllPhase::PickDataFiles) installAllChosenDataFiles_ = di;
                        installAllAdvancePick();
                        return;
                    }
                }
            }
        }
        return;
    }

    // --- Install overlay: only the explicit button is tappable ---
    // (Tapping the whole card used to cancel mid-download → "Download cancelled".)
    if (installProgressActive_) {
        if (td.reportNum > 0) {
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = mapX(td.report[0].x);
                touchStartY_ = mapY(td.report[0].y);
                touchMoved_ = false;
            }
        } else if (touchDown_) {
            const int x = touchStartX_, y = touchStartY_;
            touchDown_ = false;
            if (touchMoved_) return;
            const int ow = 640, oh = 380, ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
            const int by = oy + 300, bh = 44;
            if (installOutcome_ == 3) {
                // Cancelled: only Close (centered), no Report
                const int bwClose = 280;
                const int bxClose = ox + (ow - bwClose) / 2;
                if (hit(x, y, bxClose, by, bwClose, bh)) {
                    if (installAcknowledge_) installAcknowledge_();
                    reportUiState_ = 0;
                }
                return;
            }
            if (installOutcome_ == 2) {
                const bool spaceErr = isNonReportableInstallError(installProgressMessage_);
                if (spaceErr) {
                    const int bwClose = 280;
                    const int bxClose = ox + (ow - bwClose) / 2;
                    if (hit(x, y, bxClose, by, bwClose, bh)) {
                        if (installAcknowledge_) installAcknowledge_();
                        reportUiState_ = 0;
                    }
                    return;
                }
                const int bwReport = 200, bwClose = 200;
                const int bxReport = ox + 28, bxClose = ox + ow - 28 - bwClose;
                if (hit(x, y, bxReport, by, bwReport, bh)) {
                    trySendErrorReport(
                        "Installation failed",
                        installProgressMessage_ + " | file=" + installProgressFile_);
                    return;
                }
                if (hit(x, y, bxClose, by, bwClose, bh)) {
                    if (installAcknowledge_) installAcknowledge_();
                    reportUiState_ = 0;
                    return;
                }
                return;
            }
            const int bw = 280;
            if (!hit(x, y, ox + 28, by, bw, bh)) return;
            if (installOutcome_ == 1) {
                if (installAcknowledge_) installAcknowledge_();
                if (installAllFinishedToast_) {
                    installAllFinishedToast_ = false;
                    showToast("All installed — ready to use", 2800);
                }
            } else if (installCancel_) {
                installCancel_();
            }
        }
        return;
    }

    // --- Settings: must run before generic drag/scroll handling ---
    if (state_.mode == UiMode::SETTINGS) {
        const int margin = 20;
        const int contentTop = HEADER_H + 56;
        const int colW = SCREEN_W - margin * 2;
        const int listX = margin;
        const int listW = (colW * 58) / 100;
        struct Meta { bool sectionStart; const char* section; };
        Meta meta[6] = {
            {true, "INSTALL"}, {false, ""}, {true, "INTERFACE"}, {false, ""}, {true, "CATALOG"}, {true, "UPDATES"}
        };
        int rowY[6];
        int y = contentTop - static_cast<int>(settingsScrollY_);
        for (int i = 0; i < 6; ++i) {
            if (meta[i].sectionStart && meta[i].section[0]) y += 22;
            rowY[i] = y;
            y += 52 + 8;
        }
        const int rowH = 52;
        const int measured = 6 * (52 + 8) + 4 * 22;
        const int listViewH = SCREEN_H - contentTop - FOOTER_H - 8;
        const float maxScroll = static_cast<float>(std::max(0, measured - listViewH));

        if (td.reportNum > 0) {
            const int x = mapX(td.report[0].x);
            const int yy = mapY(td.report[0].y);
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = x;
                touchStartY_ = yy;
                touchLastY_ = yy;
                touchMoved_ = false;
                touchAccumY_ = 0.f;
            } else {
                const int dy = yy - touchLastY_;
                touchLastY_ = yy;
                if (std::abs(x - touchStartX_) > 20 || std::abs(yy - touchStartY_) > 20)
                    touchMoved_ = true;
                // Vertical drag scrolls the settings list (finger up → content up)
                if (touchMoved_) {
                    settingsScrollY_ -= static_cast<float>(dy);
                    if (settingsScrollY_ < 0.f) settingsScrollY_ = 0.f;
                    if (settingsScrollY_ > maxScroll) settingsScrollY_ = maxScroll;
                }
            }
        } else if (touchDown_) {
            const int x = touchStartX_, yy = touchStartY_;
            touchDown_ = false;
            // Allow slight finger jitter — still treat as tap if not a long drag
            for (int i = 0; i < 6; ++i) {
                if (hit(x, yy, listX, rowY[i], listW, rowH)) {
                    if (settingsFocus_ == i) cycleSettingsOption(i, +1);
                    else settingsFocus_ = i;
                    return;
                }
            }
            if (yy < HEADER_H + 48) {
                closeSettings(true);
                return;
            }
        }
        return;
    }

    if (catalogLoading_) return;

    constexpr int kDragSlop = 20;       // px before a gesture counts as drag
    constexpr float kScrollPx = 48.f;   // pixels of drag per one catalog/detail step

    if (td.reportNum > 0) {
        const int x = mapX(td.report[0].x);
        const int y = mapY(td.report[0].y);
        if (!touchDown_) {
            touchDown_ = true;
            touchStartX_ = x;
            touchStartY_ = y;
            touchLastY_ = y;
            touchMoved_ = false;
            touchAccumY_ = 0.f;
            touchDownMs_ = sceKernelGetProcessTimeWide() / 1000ULL;
        } else {
            const int dy = y - touchLastY_;
            touchLastY_ = y;
            if (std::abs(x - touchStartX_) > kDragSlop || std::abs(y - touchStartY_) > kDragSlop)
                touchMoved_ = true;

            if (!touchMoved_) return;

            touchAccumY_ += static_cast<float>(dy);
            while (touchAccumY_ <= -kScrollPx) {
                touchAccumY_ += kScrollPx;
                // Scroll direction: finger up → content moves up → next items
                if (state_.mode == UiMode::FULL_CATALOG) {
                    moveCatalogFocus(1);
                } else if (state_.mode == UiMode::SPLIT_DETAIL) {
                    if (state_.activePanel == UiPanel::Detail || touchStartX_ >= SCREEN_W / 2) {
                        state_.activePanel = UiPanel::Detail;
                        moveDetailScroll(1);
                    } else {
                        moveCatalogFocus(1);
                    }
                }
            }
            while (touchAccumY_ >= kScrollPx) {
                touchAccumY_ -= kScrollPx;
                if (state_.mode == UiMode::FULL_CATALOG) {
                    moveCatalogFocus(-1);
                } else if (state_.mode == UiMode::SPLIT_DETAIL) {
                    if (state_.activePanel == UiPanel::Detail || touchStartX_ >= SCREEN_W / 2) {
                        state_.activePanel = UiPanel::Detail;
                        moveDetailScroll(-1);
                    } else {
                        moveCatalogFocus(-1);
                    }
                }
            }
        }
        return;
    }

    if (!touchDown_) return;

    // Finger lifted
    const int x = touchStartX_;
    const int y = touchStartY_;
    const bool wasDrag = touchMoved_;
    touchDown_ = false;
    touchAccumY_ = 0.f;
    if (wasDrag) return;

    // Data/Game Files request confirmation modal touch
    if (dataRequestConfirmVisible_) {
        const int mw = 560, mh = 280;
        const int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;
        const int by = my + mh - 56, bh = 40, bw = 180, gap = 24;
        const int bxCancel = mx + (mw - (bw * 2 + gap)) / 2;
        const int bxSend = bxCancel + bw + gap;
        if (hit(x, y, bxCancel - 12, by - 12, bw + 24, bh + 24)) {
            closeDataRequestConfirm();
            return;
        }
        if (hit(x, y, bxSend - 12, by - 12, bw + 24, bh + 24)) {
            closeDataRequestConfirm();
            trySendDataRequest();
            return;
        }
        return; // consume other touches while modal is open
    }

    // Report confirmation modal touch
    if (reportConfirmVisible_) {
        const int mw = 560, mh = 260;
        const int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;
        const int by = my + mh - 56, bh = 40, bw = 180, gap = 24;
        const int bxCancel = mx + (mw - (bw * 2 + gap)) / 2;
        const int bxReport = bxCancel + bw + gap;
        if (hit(x, y, bxCancel - 12, by - 12, bw + 24, bh + 24)) {
            closeReportConfirm();
            return;
        }
        if (hit(x, y, bxReport - 12, by - 12, bw + 24, bh + 24)) {
            closeReportConfirm();
            trySendErrorReport("Manual report from UI", "User confirmed report from footer");
            return;
        }
        return; // consume other touches while modal is open
    }

    // Footer News + Report chips (left of ux0 space panel)
    {
        const int panelW = 220;
        const int panelX = SCREEN_W - panelW - 6;
        const int reportW = 108;
        const int newsW = 100;
        const int chipH = FOOTER_H - 6;
        const int reportX = panelX - reportW - 8;
        const int newsX = reportX - newsW - 8;
        const int chipY = SCREEN_H - FOOTER_H + 3;
        if (hit(x, y, newsX, chipY, newsW, chipH)) {
            runNewsCheck(true);
            return;
        }
        if (hit(x, y, reportX, chipY, reportW, chipH)) {
            openReportConfirm();
            return;
        }
    }

// --- Header search bar + G/D Files filter (Homebrew only) ---
    if (y < HEADER_H) {
        const int barY = 10, barH = 32;
        const int gdW = 118;
        const int clockReserve = 92;
        // Approximate logo width used in drawHeader (searchLeft often ~200)
        const int barX = 200;
        const bool showGd = (state_.catalog == CatalogType::Homebrew);
        const int barW = std::max(120, SCREEN_W - barX - clockReserve - (showGd ? (gdW + 10) : 0));
        const int gdX = barX + barW + 6;
        if (showGd && hit(x, y, gdX, barY, gdW, barH)) {
            setDataFilesFilter(!dataFilesFilter_);
            return;
        }
        if (hit(x, y, barX, barY, barW, barH)) {
            // Right edge of bar clears text filter when active
            if (!searchQuery_.empty() && x > barX + barW - 56) {
                applySearch("");
                return;
            }
            if (searchRequest_) applySearch(searchRequest_(searchQuery_));
            return;
        }
        return;
    }

    // --- Tabs (L/R equivalent) ---
    if (y >= HEADER_H && y < HEADER_H + TABS_H) {
        const float tw = static_cast<float>(SCREEN_W) / static_cast<float>(CatalogType::Count);
        const int tab = std::min((int)CatalogType::Count - 1, std::max(0, (int)(x / tw)));
        const int delta = tab - (int)state_.catalog;
        if (delta != 0) changeCatalog(delta);
        return;
    }

    const int panelTop = HEADER_H + TABS_H;
    const int panelBottom = SCREEN_H - FOOTER_H;
    if (y < panelTop || y >= panelBottom) {
        // Header/footer chrome only — do not open search from footer taps.
        return;
    }

    // --- Full catalog grid ---
    if (state_.mode == UiMode::FULL_CATALOG) {
        const int cw = (SCREEN_W - GRID_PAD * 2 - CARD_GAP * 2) / 3;
        const float rowH = static_cast<float>(FULL_CARD_H + CARD_GAP);
        const int localX = x - GRID_PAD;
        const int localY = y - (panelTop + GRID_PAD);
        if (localX < 0 || localY < 0) return;
        const int col = localX / (cw + CARD_GAP);
        const int row = static_cast<int>(localY / rowH + visualCatalogScroll_);
        if (col < 0 || col > 2) return;
        const int idx = row * 3 + col;
        if (idx < 0 || idx >= (int)catalogView().size()) return;
        if (idx == state_.focusIndex) startOpeningDetail();
        else {
            state_.focusIndex = idx;
            clampCatalogFocus();
            clampCatalogScroll();
        }
        return;
    }

    if (state_.mode != UiMode::SPLIT_DETAIL) return;

    const int mid = SCREEN_W / 2;

    // --- Left list ---
    if (x < mid) {
        state_.activePanel = UiPanel::Catalog;
        const float rowH = static_cast<float>(SPLIT_CARD_H + CARD_GAP);
        const int localY = y - (panelTop + GRID_PAD);
        if (localY >= 0) {
            const int idx = static_cast<int>(localY / rowH + visualCatalogScroll_);
            if (idx >= 0 && idx < (int)catalogView().size()) {
                state_.focusIndex = idx;
                state_.detailScroll = 0;
                visualDetailScroll_ = 0.f;
                clampCatalogFocus();
                clampCatalogScroll();
            }
        }
        // Tap near left edge bottom could close — not needed
        return;
    }

    // --- Right detail panel ---
    state_.activePanel = UiPanel::Detail;
    const int dx = mid, dy = panelTop;
    const int dw = SCREEN_W - mid, dh = panelBottom - panelTop;

    // Close detail: tap top-left of detail header strip or outside-ish
    // Links + Request data buttons (same coords as drawDetailPanel)
    if (!catalogView().empty()) {
        const int i = selectedIndex();
        if (i >= 0) {
            const CatalogItem& tapItem = catalogView()[i];
            const int bx = dx + dw - 156, by = dy + 12, bw = 142, bh = 32;
            if (!tapItem.linkDetails.empty() && hit(x, y, bx, by, bw, bh)) {
                if (state_.linkNavigation) exitLinkNavigation();
                else enterLinkNavigation();
                return;
            }
            if (itemEligibleForDataRequest(tapItem)) {
                const int rby = !tapItem.linkDetails.empty() ? (by + bh + 6) : by;
                if (hit(x, y, bx, rby, bw, 32)) {
                    openDataRequestConfirm();
                    return;
                }
            }
        }
    }

    // Tap in header left → close detail (○)
    if (y < dy + DETAIL_HEADER_H && x < dx + 90) {
        startClosingDetail();
        return;
    }

    // Install All + download link rows
    // If not in link mode: enter mode and focus the tapped row (do not install yet).
    // If already in link mode: activate the tapped control.
    {
        const int i = selectedIndex();
        if (i >= 0) {
            const CatalogItem& it = catalogView()[i];
            const bool hasAll = itemHasDataOrGameFiles(it);
            const int yOff = hasAll ? (LINK_SECTION_H + 4 + INSTALL_ALL_BLOCK_H + 8) : 0;
            const int listTop = dy + DETAIL_HEADER_H - (int)visualDetailScroll_;
            const int allBtnY = listTop + (hasAll ? (LINK_SECTION_H + 4) : 0);
            if (hasAll && hit(x, y, dx + 18, allBtnY, dw - 36, INSTALL_ALL_BLOCK_H)) {
                if (!state_.linkNavigation) {
                    state_.linkNavigation = true;
                    state_.linkFocus = 0;
                    diagnostics::log("[UI] link navigation enabled (touch Install All)");
                    return;
                }
                openInstallAllWizard();
                return;
            }
            const auto rows = buildLinkLayout(it);
            const int focusOff = hasAll ? 1 : 0;
            for (const auto& row : rows) {
                if (row.isSection) continue;
                const int ry = listTop + yOff + row.y;
                if (hit(x, y, dx + 18, ry, dw - 36, LINK_ROW_H)) {
                    const int focus = row.focusIndex + focusOff;
                    if (!state_.linkNavigation) {
                        state_.linkNavigation = true;
                        state_.linkFocus = focus;
                        diagnostics::log("[UI] link navigation enabled (touch link)");
                        return;
                    }
                    state_.linkFocus = focus;
                    activateFocusedLink();
                    return;
                }
            }
        }
    }
}



void FullCatalogScreen::setAppSettings(const ::psvitaalive::AppSettingsData& settings) {
    settingsEdit_ = settings;
    applyColorTheme(settingsEdit_.colorTheme);
}

void FullCatalogScreen::setPluginStatus(const ::psvitaalive::PluginStatus& plugins) {
    pluginsStatus_ = plugins;
}

void FullCatalogScreen::setSettingsSaveCallback(SettingsSaveFn callback) {
    settingsSave_ = std::move(callback);
}

void FullCatalogScreen::openSettings() {
    if (installProgressActive_ || catalogLoading_ || selfUpdateBusy_.load()) {
        showToast("LOCKED: finish download/install first. PS & power menu disabled.", 2800);
        return;
    }
    if (state_.mode == UiMode::SETTINGS) return;
    settingsReturnMode_ = (state_.mode == UiMode::SPLIT_DETAIL) ? UiMode::SPLIT_DETAIL : UiMode::FULL_CATALOG;
    settingsFocus_ = 0;
    settingsEnter_ = 0.f;
    settingsFocusY_ = 0.f;
    settingsScrollY_ = 0.f;
    state_.mode = UiMode::SETTINGS;
    diagnostics::log("[UI] settings opened");
}

void FullCatalogScreen::closeSettings(bool save) {
    if (state_.mode != UiMode::SETTINGS) return;
    if (save) {
        if (settingsSave_) settingsSave_(settingsEdit_);
        showToast("Settings saved", 1600);
        diagnostics::log("[UI] settings saved");
    }
    state_.mode = settingsReturnMode_;
}


void FullCatalogScreen::pollSelfUpdateProgress() {
    if (!selfUpdateBusy_.load() && !selfUpdateDone_.load()) return;

    if (selfUpdateBusy_.load()) {
        const uint64_t cur = selfUpdateCur_.load();
        const uint64_t tot = selfUpdateTot_.load();
        setInstallProgress(
            true,
            cur,
            tot,
            0,
            "Self-update",
            "PSVitaAlive.vpk",
            selfUpdateMsg_[0] ? selfUpdateMsg_ : "Updating...",
            0,
            false
        );
        return;
    }

    if (selfUpdateDone_.load()) {
        const bool ok = selfUpdateOk_.load();
        setInstallProgress(
            true,
            1,
            1,
            0,
            "Self-update",
            "PSVitaAlive.vpk",
            selfUpdateMsg_[0] ? selfUpdateMsg_ : (ok ? "Update installed — press START to exit, then reopen" : "Update failed"),
            ok ? 1 : 2,
            false
        );
        selfUpdateDone_.store(false);
        if (selfUpdateThread_ >= 0) {
            sceKernelWaitThreadEnd(selfUpdateThread_, nullptr, nullptr);
            sceKernelDeleteThread(selfUpdateThread_);
            selfUpdateThread_ = -1;
        }
    }
}

int FullCatalogScreen::selfUpdateWorkerEntry(SceSize args, void* argp) {
    (void)args;
    FullCatalogScreen* self = *reinterpret_cast<FullCatalogScreen**>(argp);
    if (!self) return 0;

    auto onProg = [self](const ::psvitaalive::UpdateChecker::ApplyProgress& p) {
        self->selfUpdateCur_.store(p.current);
        self->selfUpdateTot_.store(p.total);
        sceClibSnprintf(self->selfUpdateMsg_, sizeof(self->selfUpdateMsg_), "%s", p.message.c_str());
    };

    const bool ok = ::psvitaalive::UpdateChecker::applyUpdate(self->selfUpdateInfo_, onProg, nullptr);
    self->selfUpdateOk_.store(ok);
    if (ok) {
        sceClibSnprintf(self->selfUpdateMsg_, sizeof(self->selfUpdateMsg_),
                        "Update installed — press START to exit, then reopen");
    } else if (self->selfUpdateMsg_[0] == 0) {
        sceClibSnprintf(self->selfUpdateMsg_, sizeof(self->selfUpdateMsg_), "Update failed");
    }
    self->selfUpdateBusy_.store(false);
    self->selfUpdateDone_.store(true);
    return 0;
}

void FullCatalogScreen::triggerSelfUpdateAction() {
    if (selfUpdateBusy_.load() || installProgressActive_) {
        showToast("LOCKED: install still running. Screen on — do not power off.", 2800);
        return;
    }

    if (selfUpdateChecked_ &&
        selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable &&
        !selfUpdateInfo_.downloadUrl.empty()) {
        // Start VitaDB-style in-place install on a worker thread.
        selfUpdateBusy_.store(true);
        selfUpdateDone_.store(false);
        selfUpdateOk_.store(false);
        selfUpdateCur_.store(0);
        selfUpdateTot_.store(selfUpdateInfo_.assetSize);
        sceClibSnprintf(selfUpdateMsg_, sizeof(selfUpdateMsg_), "Starting update...");
        closeSettings(true);
        setInstallProgress(true, 0, selfUpdateInfo_.assetSize, 0, "Self-update", "PSVitaAlive.vpk",
                           "Starting update...", 0, false);

        FullCatalogScreen* self = this;
        selfUpdateThread_ = sceKernelCreateThread(
            "PSVitaAliveSelfUpdate",
            &FullCatalogScreen::selfUpdateWorkerEntry,
            0x10000100,
            64 * 1024,
            0,
            0,
            nullptr
        );
        if (selfUpdateThread_ < 0) {
            selfUpdateBusy_.store(false);
            setInstallProgress(true, 0, 0, 0, "Self-update", "PSVitaAlive.vpk",
                               "Could not start update thread", 2, false);
            showToast("Update thread failed", 2000);
            return;
        }
        const int st = sceKernelStartThread(selfUpdateThread_, sizeof(self), &self);
        if (st < 0) {
            sceKernelDeleteThread(selfUpdateThread_);
            selfUpdateThread_ = -1;
            selfUpdateBusy_.store(false);
            setInstallProgress(true, 0, 0, 0, "Self-update", "PSVitaAlive.vpk",
                               "Could not start update thread", 2, false);
            return;
        }
        diagnostics::log("[UI] self-update apply started");
        return;
    }

    showToast("Checking GitHub for updates...", 1200);
    selfUpdateInfo_ = ::psvitaalive::UpdateChecker::checkLatest(PSVITAALIVE_VERSION);
    selfUpdateChecked_ = true;
    if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable) {
        showToast(std::string("Update ") + selfUpdateInfo_.remoteVersion + " available — press X to install", 2800);
    } else if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate) {
        showToast(std::string("Already up to date (v") + selfUpdateInfo_.localVersion + ")", 2200);
    } else {
        showToast(selfUpdateInfo_.error.empty() ? "Update check failed" : selfUpdateInfo_.error, 2600);
    }
}

void FullCatalogScreen::cycleSettingsOption(int row, int delta) {
    if (delta == 0) return;
    if (row == 0) {
        int v = static_cast<int>(settingsEdit_.installMethod);
        v = (v + delta) % 3;
        if (v < 0) v += 3;
        settingsEdit_.installMethod = static_cast<::psvitaalive::InstallMethod>(v);
    } else if (row == 1) {
        settingsEdit_.pspTarget = (settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline)
            ? ::psvitaalive::PspTarget::LiveArea : ::psvitaalive::PspTarget::Adrenaline;
    } else if (row == 2) {
        const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
        int v = static_cast<int>(settingsEdit_.colorTheme);
        v = (v + delta) % n;
        if (v < 0) v += n;
        settingsEdit_.colorTheme = static_cast<::psvitaalive::ColorTheme>(v);
        applyColorTheme(settingsEdit_.colorTheme); // live preview
    } else if (row == 3) {
        settingsEdit_.warnMissingPlugins = !settingsEdit_.warnMissingPlugins;
    } else if (row == 4) {
        settingsEdit_.promptImageWarmup = !settingsEdit_.promptImageWarmup;
    } else if (row == 5) {
        triggerSelfUpdateAction();
    }
}

void FullCatalogScreen::handleSettingsInput(uint32_t pressed, uint32_t nav) {
    constexpr int kRows = 6;
    if (nav & SCE_CTRL_UP) {
        settingsFocus_ = (settingsFocus_ + kRows - 1) % kRows;
    }
    if (nav & SCE_CTRL_DOWN) {
        settingsFocus_ = (settingsFocus_ + 1) % kRows;
    }
    if ((nav & SCE_CTRL_LEFT) || (pressed & SCE_CTRL_SQUARE)) {
        cycleSettingsOption(settingsFocus_, -1);
    }
    if ((nav & SCE_CTRL_RIGHT) || (pressed & SCE_CTRL_CROSS)) {
        cycleSettingsOption(settingsFocus_, +1);
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        closeSettings(true);
        return;
    }
    if (pressed & SCE_CTRL_SELECT) {
        closeSettings(true);
        return;
    }
}

void FullCatalogScreen::drawSettings() {
    const float enter = settingsEnter_;
    const int slide = static_cast<int>((1.f - enter) * 28.f);
    const unsigned dimA = static_cast<unsigned>(enter * 255.f);

    vita2d_start_drawing();
    vita2d_set_clear_color(BG);vita2d_clear_screen();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, BG);
    drawHeader(SCREEN_W);

    vita2d_draw_rectangle(0, HEADER_H + slide, SCREEN_W, 44, SURFACE2);
    vita2d_draw_rectangle(0, HEADER_H + slide, SCREEN_W, 3, ACCENT);
    vita2d_pgf_draw_text(font_, 20, HEADER_H + 30 + slide, ACCENT, 1.00f, "Settings");
    vita2d_pgf_draw_text(font_, SCREEN_W - 300, HEADER_H + 28 + slide, DIM, 0.56f, "O / SELECT: Save & back");

    const int margin = 20;
    const int contentTop = HEADER_H + 56 + slide;
    const int contentH = SCREEN_H - contentTop - FOOTER_H - 6;
    const int listClipBottom = contentTop + contentH - 8;

    auto methodLabel = [&]() -> std::string {
        if (settingsEdit_.installMethod == ::psvitaalive::InstallMethod::Auto) return "Auto";
        if (settingsEdit_.installMethod == ::psvitaalive::InstallMethod::Direct) return "Direct";
        return "BGDL";
    };
    auto pspLabel = [&]() -> std::string {
        return settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline ? "Adrenaline" : "LiveArea";
    };
    auto updateLabel = [&]() -> std::string {
        if (selfUpdateBusy_.load()) return "Working...";
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable) {
            return std::string("Install ") + selfUpdateInfo_.remoteVersion;
        }
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate) {
            return "Up to date";
        }
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::Failed) {
            return "Check failed";
        }
        return std::string("v") + PSVITAALIVE_VERSION;
    };

    struct Opt {
        const char* section;
        const char* label;
        std::string value;
        const char* hint;
        bool sectionStart;
    };
        auto themeLabel = [&]() -> std::string {
        switch (settingsEdit_.colorTheme) {
            case ::psvitaalive::ColorTheme::Cyan: return "Cyan";
            case ::psvitaalive::ColorTheme::Rose: return "Rose";
            case ::psvitaalive::ColorTheme::Amber: return "Amber";
            case ::psvitaalive::ColorTheme::Violet: return "Violet";
            case ::psvitaalive::ColorTheme::Mono: return "Mono";
            case ::psvitaalive::ColorTheme::Oled: return "OLED";
            case ::psvitaalive::ColorTheme::NeonLime:
            default: return "Neon Lime";
        }
    };
    Opt opts[6] = {
        {"INSTALL", "Install method", methodLabel(), "Auto: BGDL for PKG when available", true},
        {"", "PSP / PS1 target", pspLabel(), "ISO/CSO/PBP under ux0:pspemu", false},
        {"INTERFACE", "Color theme", themeLabel(), "Accent color - changes live", true},
        {"", "Warn missing plugins", settingsEdit_.warnMissingPlugins ? "Yes" : "No", "Startup toast if NoNpDrm is missing", false},
        {"CATALOG", "Prompt image download", settingsEdit_.promptImageWarmup ? "Yes" : "No", "If you choose No once, it will not ask again", true},
        {"UPDATES", "App updates", updateLabel(), "GitHub Releases - X to check / install", true},
    };

    // List (left) + contextual help panel (right)
    const int colW = SCREEN_W - margin * 2;
    const int listX = margin;
    const int listW = (colW * 58) / 100;
    const int sideX = listX + listW + 12;
    const int sideW = colW - listW - 12;
    const int rowH = 52;
    const int sectionH = 22;
    const int rowGap = 8;

    int measured = 0;
    for (int i = 0; i < 6; ++i) {
        if (opts[i].sectionStart && opts[i].section[0]) measured += sectionH;
        measured += rowH + rowGap;
    }
    const int listViewH = listClipBottom - contentTop;
    const float maxScroll = static_cast<float>(std::max(0, measured - listViewH));
    if (settingsScrollY_ < 0.f) settingsScrollY_ = 0.f;
    if (settingsScrollY_ > maxScroll) settingsScrollY_ = maxScroll;

    {
        int fy = 0;
        for (int i = 0; i <= settingsFocus_ && i < 6; ++i) {
            if (opts[i].sectionStart && opts[i].section[0]) fy += sectionH;
            if (i < settingsFocus_) fy += rowH + rowGap;
        }
        const float rowTop = static_cast<float>(fy);
        const float rowBot = rowTop + static_cast<float>(rowH);
        if (rowTop < settingsScrollY_) settingsScrollY_ = rowTop;
        if (rowBot > settingsScrollY_ + static_cast<float>(listViewH))
            settingsScrollY_ = rowBot - static_cast<float>(listViewH);
        if (settingsScrollY_ < 0.f) settingsScrollY_ = 0.f;
        if (settingsScrollY_ > maxScroll) settingsScrollY_ = maxScroll;
    }

    int rowY[5] = {};
    int y = contentTop - static_cast<int>(settingsScrollY_);
    for (int i = 0; i < 6; ++i) {
        if (opts[i].sectionStart && opts[i].section[0]) {
            if (y + 16 >= contentTop && y + 4 <= listClipBottom)
                vita2d_pgf_draw_text(font_, listX + 6, y + 16, DIM, 0.56f, opts[i].section);
            y += sectionH;
        }
        rowY[i] = y;
        const bool focus = (settingsFocus_ == i);
        if (y + rowH >= contentTop && y <= listClipBottom) {
            vita2d_draw_rectangle(listX, y, listW, rowH, focus ? SURFACE : SURFACE2);
            if (focus) {
                vita2d_draw_rectangle(listX, y, 4, rowH, ACCENT);
                vita2d_draw_rectangle(listX, y, listW, 2, ACCENT);
                vita2d_draw_rectangle(listX, y + rowH - 2, listW, 2, ACCENT);
            } else {
                vita2d_draw_rectangle(listX, y + rowH - 1, listW, 1, BORDER);
            }
            vita2d_pgf_draw_text(font_, listX + 14, y + 22, focus ? WHITE : TEXT, 0.76f, opts[i].label);
            const int chipW = 110;
            const int chipX = listX + listW - chipW - 10;
            const int chipY = y + 12;
            vita2d_draw_rectangle(chipX, chipY, chipW, 24, focus ? ACCENT : SURFACE);
            vita2d_pgf_draw_text(font_, chipX + 8, chipY + 17, focus ? BG : ACCENT, 0.54f, opts[i].value.c_str());
            vita2d_pgf_draw_text(font_, listX + 14, y + 42, DIM, 0.48f, opts[i].hint);
            if (focus) vita2d_pgf_draw_text(font_, chipX - 32, chipY + 17, ACCENT, 0.52f, "<>");
        }
        y += rowH + rowGap;
    }

    {
        const int panelH = listClipBottom - contentTop;
        vita2d_draw_rectangle(sideX, contentTop, sideW, panelH, SURFACE2);
        vita2d_draw_rectangle(sideX, contentTop, 3, panelH, ACCENT);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 22, ACCENT, 0.62f, "INFO");

        const char* title = opts[settingsFocus_].label;
        const char* body1 = "";
        const char* body2 = "";
        const char* body3 = "";
        switch (settingsFocus_) {
        case 0:
            body1 = "How packages are installed.";
            body2 = "Auto uses BGDL for PKG when";
            body3 = "Shell supports it, else Direct.";
            break;
        case 1:
            body1 = "Where PSP/PS1 content goes.";
            body2 = "Adrenaline: ISO/CSO under";
            body3 = "ux0:pspemu. LiveArea needs plugins.";
            break;
        case 2:
            body1 = "UI accent palette. Neon is the";
            body2 = "brand default; Cyan / Rose are";
            body3 = "popular. Report stays red.";
            break;
        case 3:
            body1 = "Show a toast at startup when";
            body2 = "NoNpDrm / NoPspEmuDrm are";
            body3 = "missing from taiHEN config.";
            break;
        case 4:
            body1 = "Ask once whether to download";
            body2 = "all catalog images at startup.";
            body3 = "Off = load images on demand.";
            break;
        case 5:
            body1 = "Checks GitHub Releases for a";
            body2 = "newer PSVitaAlive.vpk and can";
            body3 = "install it in-place (VitaDB style).";
            break;
        default: break;
        }
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 48, WHITE, 0.64f, title);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 74, TEXT, 0.52f, body1);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 94, TEXT, 0.52f, body2);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 114, TEXT, 0.52f, body3);

        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 150, DIM, 0.52f, "SYSTEM");
        char plug[96];
        sceClibSnprintf(plug, sizeof(plug), "NoNpDrm: %s", pluginsStatus_.nonpdrm ? "OK" : "missing");
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 172, TEXT, 0.52f, plug);
        sceClibSnprintf(plug, sizeof(plug), "NoPspEmuDrm: %s", pluginsStatus_.nopspemudrmKern ? "OK" : "missing");
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 192, TEXT, 0.52f, plug);
        if (!pluginsStatus_.configPathUsed.empty()) {
            vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 214, DIM, 0.46f,
                                 ellipsize(pluginsStatus_.configPathUsed, 28).c_str());
        }
        if (settingsFocus_ == 5) {
            char ver[64];
            sceClibSnprintf(ver, sizeof(ver), "Local: v%s", PSVITAALIVE_VERSION);
            vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 240, ACCENT, 0.52f, ver);
            if (selfUpdateChecked_) {
                if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable)
                    sceClibSnprintf(ver, sizeof(ver), "Remote: v%s", selfUpdateInfo_.remoteVersion.c_str());
                else if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate)
                    sceClibSnprintf(ver, sizeof(ver), "Remote: up to date");
                else
                    sceClibSnprintf(ver, sizeof(ver), "Remote: check failed");
                vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 260, TEXT, 0.50f, ver);
            }
        }
    }

    if (maxScroll > 1.f) {
        const float ratio = settingsScrollY_ / maxScroll;
        const int trackH = listViewH - 8;
        const int thumbH = std::max(24, trackH / 4);
        const int thumbY = contentTop + 4 + static_cast<int>(ratio * (trackH - thumbH));
        vita2d_draw_rectangle(listX + listW + 2, contentTop + 4, 3, trackH, BORDER);
        vita2d_draw_rectangle(listX + listW + 2, thumbY, 3, thumbH, ACCENT);
    }

    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT, 0.56f, "D-Pad: move   X / <>: change   O: save & back");
    drawToast();
    vita2d_end_drawing();
    vita2d_swap_buffers();

    // Expose row geometry for touch via static (same frame layout)
    // Stored for handleTouch — simple approach: recompute same math there.
    (void)rowY;
}

void FullCatalogScreen::handleInput(){if(isTransitioning())return;SceCtrlData p{};sceCtrlPeekBufferPositive(0,&p,1);static uint32_t prev=0;static uint64_t repeatAt=0;uint32_t mask=SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT,pressed=p.buttons&~prev,direct=pressed&mask;uint64_t now=sceKernelGetProcessTimeWide(),repeat=0;if((p.buttons&mask)==0)repeatAt=0;else if(direct)repeatAt=now+DIRECTION_REPEAT_DELAY_US;else if(repeatAt&&now>=repeatAt){repeat=p.buttons&mask;repeatAt=now+DIRECTION_REPEAT_INTERVAL_US;}prev=p.buttons;uint32_t nav=direct|repeat;if(state_.mode==UiMode::SETTINGS){handleSettingsInput(pressed,nav);return;}
if(pressed&SCE_CTRL_SELECT){openSettings();return;}
if(pressed&SCE_CTRL_START){
        if(installProgressActive_ && installOutcome_==0){
            showToast("LOCKED: cannot exit yet. Wait until download/install finishes.", 2800);
            return;
        }
        // After a successful self-update the running binary is stale — force exit.
        if(installProgressActive_ && installOutcome_==1 &&
           installProgressStage_.find("Self-update")!=std::string::npos){
            sceKernelExitProcess(0);
            return;
        }
        state_.requestExit=true;
        return;
    }if((pressed&SCE_CTRL_LTRIGGER)||(pressed&SCE_CTRL_RTRIGGER)){
        const bool canSwitch=!catalogLoading_&&!installProgressActive_&&!isTransitioning()
            &&catalogSwitchCooldownFrames_<=0&&deferredFreeTextures_.empty();
        if(canSwitch){
            if(pressed&SCE_CTRL_LTRIGGER)changeCatalog(-1);else changeCatalog(1);
        }else if(installProgressActive_){
            showToast("LOCKED: cannot change catalog during download/install.", 2600);
        }else if(catalogLoading_){
            showToast("Cambiando catalogo...", 1000);
        }else if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty()){
            showToast("Please wait...", 900);
        }
        return;
    }if(installAllPhase_!=InstallAllPhase::Hidden&&installAllPhase_!=InstallAllPhase::Running){
        if(pressed&SCE_CTRL_CIRCLE){closeInstallAllWizard(true);return;}
        if(installAllPhase_==InstallAllPhase::Confirm){
            if(nav&SCE_CTRL_LEFT||nav&SCE_CTRL_RIGHT||nav&SCE_CTRL_UP||nav&SCE_CTRL_DOWN){
                installAllFocus_ = 1 - installAllFocus_;
                return;
            }
            if(pressed&SCE_CTRL_CROSS){
                if(installAllFocus_==1){closeInstallAllWizard(true);return;}
                installAllAdvancePick();
                return;
            }
            return;
        }
        // Pick lists
        if(nav&SCE_CTRL_UP){if(installAllFocus_>0)--installAllFocus_;return;}
        if(nav&SCE_CTRL_DOWN){if(installAllFocus_+1<(int)installAllOptions_.size())++installAllFocus_;return;}
        if(pressed&SCE_CTRL_CROSS){
            if(installAllFocus_>=0&&installAllFocus_<(int)installAllOptions_.size()){
                const int di = installAllOptions_[installAllFocus_];
                if(installAllPhase_==InstallAllPhase::PickDownload) installAllChosenDownload_ = di;
                else if(installAllPhase_==InstallAllPhase::PickGameFiles) installAllChosenGameFiles_ = di;
                else if(installAllPhase_==InstallAllPhase::PickDataFiles) installAllChosenDataFiles_ = di;
                installAllAdvancePick();
            }
            return;
        }
        return;
    }if(dataRequestConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeDataRequestConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeDataRequestConfirm();trySendDataRequest();return;}return;}if(reportConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeReportConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeReportConfirm();trySendErrorReport("Manual report from UI","User confirmed report from footer");return;}return;}if(newsVisible_){if(pressed&SCE_CTRL_CIRCLE){closeNewsModal(newsMarkSeenOnClose_);return;}if(pressed&SCE_CTRL_UP||(nav&SCE_CTRL_UP)){if(newsScrollLine_>0)--newsScrollLine_;return;}if(pressed&SCE_CTRL_DOWN||(nav&SCE_CTRL_DOWN)){const int mv=std::max(1,(420-56-88)/22);const int ms=std::max(0,(int)newsLines_.size()-mv);if(newsScrollLine_<ms)++newsScrollLine_;return;}return;}if(installProgressActive_&&(pressed&SCE_CTRL_SQUARE)&&(installOutcome_==2)&&!isNonReportableInstallError(installProgressMessage_)){trySendErrorReport("Installation failed",installProgressMessage_+" | file="+installProgressFile_);return;}if(installProgressActive_&&(pressed&SCE_CTRL_CIRCLE)){if(installOutcome_==1||installOutcome_==2||installOutcome_==3){if(installAcknowledge_)installAcknowledge_();if(installOutcome_==1&&installAllFinishedToast_){installAllFinishedToast_=false;showToast("All installed — ready to use",2800);}reportUiState_=0;}else if(installCancel_)installCancel_();return;}if(installProgressActive_){if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE|SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT)){if(installOutcome_==0)showToast("LOCKED: only CIRCLE (cancel) works until finished.",2400);}return;}if(catalogLoading_)return;if(pressed&SCE_CTRL_SQUARE){if(state_.mode==UiMode::FULL_CATALOG){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast("Filters cleared",1200);}return;}if(state_.mode==UiMode::SPLIT_DETAIL&&state_.activePanel==UiPanel::Detail&&!state_.linkNavigation){const int si=selectedIndex();if(si>=0&&itemEligibleForDataRequest(catalogView()[si])){openDataRequestConfirm();return;}}return;}if(state_.mode==UiMode::FULL_CATALOG){if(pressed&SCE_CTRL_TRIANGLE){if(searchRequest_)applySearch(searchRequest_(searchQuery_));return;}if(nav&SCE_CTRL_LEFT&&state_.focusIndex%3>0)--state_.focusIndex;if(nav&SCE_CTRL_RIGHT&&state_.focusIndex%3<2&&state_.focusIndex+1<(int)catalogView().size())++state_.focusIndex;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);clampCatalogScroll();if(pressed&SCE_CTRL_CROSS)startOpeningDetail();return;}if(state_.mode!=UiMode::SPLIT_DETAIL)return;if(pressed&SCE_CTRL_CIRCLE){startClosingDetail();return;}if(state_.activePanel==UiPanel::Catalog){if(pressed&SCE_CTRL_RIGHT)state_.activePanel=UiPanel::Detail;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);return;}if(nav&SCE_CTRL_LEFT)state_.activePanel=UiPanel::Catalog;if(pressed&SCE_CTRL_TRIANGLE){if(state_.linkNavigation)exitLinkNavigation();else enterLinkNavigation();return;}if(state_.linkNavigation){if(nav&SCE_CTRL_UP)moveLinkFocus(0,-1);if(nav&SCE_CTRL_DOWN)moveLinkFocus(0,1);if(pressed&SCE_CTRL_CROSS)activateFocusedLink();return;}if(nav&SCE_CTRL_UP)moveDetailScroll(-1);if(nav&SCE_CTRL_DOWN)moveDetailScroll(1);}
unsigned FullCatalogScreen::colorForStatus(const std::string&s)const{if(s=="Verified")return ACCENT;if(s=="Legacy")return TEXT;if(s=="Archive")return DIM;return TEXT;}void FullCatalogScreen::drawHeader(int w){
    // Near-black bar + dual neon edge (LiveArea brand)
    vita2d_draw_rectangle(0, 0, w, HEADER_H, SURFACE2);
    vita2d_draw_rectangle(0, 0, w, 2, ACCENT);
    vita2d_draw_rectangle(0, HEADER_H - 1, w, 1, ACCENT_SOFT);
    // Brand: logo image (preferred) or compact text fallback
    int searchLeft = 200;
    if (headerLogoTex_) {
        const float lw = (float)vita2d_texture_get_width(headerLogoTex_);
        const float lh = (float)vita2d_texture_get_height(headerLogoTex_);
        const float maxH = (float)(HEADER_H - 10);
        const float maxW = 190.f;
        float sc = maxH / (lh > 1.f ? lh : 1.f);
        if (lw * sc > maxW) sc = maxW / (lw > 1.f ? lw : 1.f);
        const float dw = lw * sc;
        const float dh = lh * sc;
        const float dx = 10.f;
        const float dy = ((float)HEADER_H - dh) * 0.5f;
        vita2d_draw_texture_scale(headerLogoTex_, dx, dy, sc, sc);
        searchLeft = (int)(dx + dw + 12.f);
        if (searchLeft < 160) searchLeft = 160;
    } else {
        vita2d_pgf_draw_text(font_, 14, 30, ACCENT, 0.98f, "PSVitaAlive");
        searchLeft = 200;
    }
    // Search field + optional G/D Files filter chip (Homebrew only) + clock
    const int barY = 10, barH = 32;
    const int gdW = 118;
    const int clockReserve = 92;
    const int barX = searchLeft;
    const bool showGd = (state_.catalog == CatalogType::Homebrew);
    const int barW = std::max(120, w - barX - clockReserve - (showGd ? (gdW + 10) : 0));
    const int gdX = barX + barW + 6;
    vita2d_draw_rectangle(barX, barY, barW, barH, SURFACE);
    vita2d_draw_rectangle(barX - 1, barY - 1, barW + 2, 1, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX - 1, barY + barH, barW + 2, 1, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX - 1, barY - 1, 1, barH + 2, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX + barW, barY - 1, 1, barH + 2, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX, barY, barW, 1, withAlpha(ACCENT, 140));
    vita2d_draw_rectangle(barX, barY + barH - 1, barW, 1, withAlpha(ACCENT, 140));
    vita2d_draw_rectangle(barX, barY, 1, barH, withAlpha(ACCENT, 140));
    vita2d_draw_rectangle(barX + barW - 1, barY, 1, barH, withAlpha(ACCENT, 140));
    if (searchQuery_.empty()) {
        vita2d_pgf_draw_text(font_, barX + 12, barY + 22, DIM, 0.66f, "Search...  (△)");
    } else {
        vita2d_pgf_draw_text(font_, barX + 12, barY + 22, ACCENT, 0.64f, "FILTER");
        vita2d_pgf_draw_text(font_, barX + 78, barY + 22, WHITE, 0.66f, ellipsize(searchQuery_, 20).c_str());
        vita2d_pgf_draw_text(font_, barX + barW - 52, barY + 21, DIM, 0.52f, "□ clear");
    }
    // G/D Files filter — only on Homebrew; same folder-chip style as card tags
    if (state_.catalog == CatalogType::Homebrew) {
        const unsigned folderBg = dataFilesFilter_ ? RGBA8(0x5A, 0x42, 0x12, 255) : RGBA8(0x3A, 0x2C, 0x10, 255);
        const unsigned folderEdge = RGBA8(0xE8, 0xB4, 0x3A, 255);
        const unsigned folderText = RGBA8(0xFF, 0xD2, 0x6A, 255);
        vita2d_draw_rectangle(gdX, barY, gdW, barH, folderBg);
        vita2d_draw_rectangle(gdX, barY, gdW, 3, folderEdge); // top tab
        vita2d_draw_rectangle(gdX, barY, 3, barH, folderEdge);
        vita2d_draw_rectangle(gdX + gdW - 1, barY, 1, barH, folderEdge);
        vita2d_draw_rectangle(gdX, barY + barH - 1, gdW, 1, folderEdge);
        if (dataFilesFilter_) {
            // Active: brighter top edge
            vita2d_draw_rectangle(gdX, barY, gdW, 3, RGBA8(0xFF, 0xD2, 0x6A, 255));
        }
        const char* lab = "G/D Files";
        const float sc = 0.70f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, gdX + (gdW - tw) / 2, barY + 22, folderText, sc, lab);
    }
    {
        const std::string clock = currentTimeLabel();
        const int clockX = w - 16 - (int)clock.size() * 11;
        vita2d_pgf_draw_text(font_, clockX, 32, SILVER, 0.78f, clock.c_str());
    }
}

void FullCatalogScreen::drawTabs(int w){
    vita2d_draw_rectangle(0, HEADER_H, w, TABS_H, SURFACE2);
    vita2d_draw_rectangle(0, HEADER_H, w, 1, BORDER);
    float tw = (float)w / (int)CatalogType::Count;
    // Sliding neon underline under active tab
    vita2d_draw_rectangle((int)tabIndicatorX_ + 8, HEADER_H + TABS_H - 3, (int)tw - 16, 3, ACCENT);
    vita2d_draw_rectangle((int)tabIndicatorX_ + 8, HEADER_H + TABS_H - 5, (int)tw - 16, 2, ACCENT_SOFT);
    for (int i = 0; i < (int)CatalogType::Count; ++i) {
        int x = (int)(i * tw);
        bool a = (int)state_.catalog == i;
        if (a) {
            vita2d_draw_rectangle(x + 4, HEADER_H + 4, (int)tw - 8, TABS_H - 8, SURFACE);
        }
        vita2d_pgf_draw_text(font_, x + 12, HEADER_H + 24, a ? ACCENT : TEXT, a ? 0.86f : 0.76f,
                             catalogName((CatalogType)i));
    }
}
void FullCatalogScreen::prepareImageTexture(const std::string&url,const std::string&ns){
    static std::set<std::string> failedTextureLoads;
    if(!imageCache_||url.empty())return;
    std::string path=imageCache_->request(url,ns);
    if(imageCache_->isFailed(path)||!imageCache_->isReady(path))return;
    if(failedTextureLoads.find(path)!=failedTextureLoads.end())return;
    auto it=textures_.find(path);
    if(it!=textures_.end()){touchTexture(path);return;}
    evictTextureIfNeeded(ns);
    // Avoid decoding a file the worker may still be writing.
    SceIoStat stCheck={};
    if(sceIoGetstat(path.c_str(),&stCheck)<0||stCheck.st_size<=0)return;
    vita2d_texture*t=nullptr;
    const char*e=extOf(path);
    if(std::strcmp(e,".jpg")==0||std::strcmp(e,".jpeg")==0)t=vita2d_load_JPEG_file(path.c_str());
    else t=vita2d_load_PNG_file(path.c_str());
    if(!t){failedTextureLoads.insert(path);SceIoStat st={};long long sz=-1;if(sceIoGetstat(path.c_str(),&st)>=0)sz=(long long)st.st_size;char m[700];sceClibSnprintf(m,sizeof(m),"[UI] texture load failed ns=%s path=%s size=%lld",ns.c_str(),path.c_str(),sz);diagnostics::log(m);return;}
    textures_[path]=t;textureOrder_.push_back(path);
}
void FullCatalogScreen::prepareVisibleTextures(){
    if(!imageCache_||catalogLoading_||installProgressActive_)return;
    // Do not thrash GPU while frees are still draining or right after a catalog switch.
    if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty())return;

    std::unordered_set<std::string> keep;
    auto pathOnly=[&](const std::string& url, const char* ns)->std::string{
        if(url.empty())return {};
        return imageCache_->pathFor(url, ns);
    };
    auto markKeep=[&](const std::string& url, const char* ns){
        const std::string path=pathOnly(url, ns);
        if(!path.empty())keep.insert(path);
    };

    // At most one new GPU texture decode per frame (avoids hitch + free/load storms).
    constexpr int kLoadsPerFrame = 1;

    if(state_.mode==UiMode::FULL_CATALOG){
        const int first=std::max(0, state_.catalogScrollRow*3);
        const int last=std::min((int)catalogView().size(), first+9);
        for(int i=first;i<last;++i){
            const CatalogItem& it=catalogView()[i];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
        }
        // Drop GPU textures that scrolled away, then drop their download jobs too.
        releaseTexturesNotIn(keep);
        imageCache_->cancelQueuedExcept(keep);

        int loads=0;
        for(int i=first;i<last&&loads<kLoadsPerFrame;++i){
            const CatalogItem& it=catalogView()[i];
            const std::string& url=!it.icon.empty()?it.icon:it.cover;
            if(url.empty())continue;
            // Only enqueue download / decode for the current viewport.
            const size_t before=textures_.size();
            prepareImageTexture(url, "app");
            if(textures_.size()>before)++loads;
        }
        return;
    }

    if(state_.mode==UiMode::OPENING_DETAIL||state_.mode==UiMode::SPLIT_DETAIL||state_.mode==UiMode::CLOSING_DETAIL){
        const int first=std::max(0, state_.catalogScrollRow);
        const int last=std::min((int)catalogView().size(), state_.catalogScrollRow+visibleRowsSplit());
        for(int i=first;i<last;++i){
            const CatalogItem& it=catalogView()[i];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
        }
        const int sel=selectedIndex();
        if(sel>=0){
            const CatalogItem& it=catalogView()[sel];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
            const int panelX=SCREEN_W/2, panelY=HEADER_H+TABS_H;
            const int panelW=SCREEN_W-panelX, panelH=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;
            const int top=panelY+DETAIL_HEADER_H+10, bottom=panelY+panelH-10;
            const int scroll=std::max(0, (int)visualDetailScroll_);
            int mc=std::max(18, (panelW-36)/7);
            std::vector<std::string> pre;
            auto add=[&](const std::string& v){
                if(v.empty())return;
                pre.push_back("");
                std::vector<std::string> q; wrapText(v, mc, q);
                pre.insert(pre.end(), q.begin(), q.end());
                pre.push_back("");
            };
            add(it.description);
            add(it.longDescription);
            const int links=it.linkDetails.empty()?0:10+(int)it.linkDetails.size()*(LINK_ROW_H+LINK_GAP);
            const int shotTop=top+links+(int)pre.size()*LINE_H-scroll;
            const int shotH=SCREENSHOT_ROW_H-12;
            const int sc=std::min((int)it.screenshots.size(), 8);
            for(int k=0;k<sc;++k){
                const int sy=shotTop+k*SCREENSHOT_ROW_H;
                if(sy+shotH>top&&sy<bottom)markKeep(it.screenshots[k], "shot");
            }
        }
        releaseTexturesNotIn(keep);
        imageCache_->cancelQueuedExcept(keep);

        int loads=0;
        auto prepareOne=[&](const std::string& url, const char* ns){
            if(loads>=kLoadsPerFrame||url.empty())return;
            const size_t before=textures_.size();
            prepareImageTexture(url, ns);
            if(textures_.size()>before)++loads;
        };
        for(int i=first;i<last;++i){
            const CatalogItem& it=catalogView()[i];
            prepareOne(!it.icon.empty()?it.icon:it.cover, "app");
        }
        if(sel>=0){
            const CatalogItem& it=catalogView()[sel];
            const int panelX=SCREEN_W/2, panelY=HEADER_H+TABS_H;
            const int panelW=SCREEN_W-panelX, panelH=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;
            const int top=panelY+DETAIL_HEADER_H+10, bottom=panelY+panelH-10;
            const int scroll=std::max(0, (int)visualDetailScroll_);
            int mc=std::max(18, (panelW-36)/7);
            std::vector<std::string> pre;
            auto add=[&](const std::string& v){
                if(v.empty())return;
                pre.push_back("");
                std::vector<std::string> q; wrapText(v, mc, q);
                pre.insert(pre.end(), q.begin(), q.end());
                pre.push_back("");
            };
            add(it.description);
            add(it.longDescription);
            const int links=it.linkDetails.empty()?0:10+(int)it.linkDetails.size()*(LINK_ROW_H+LINK_GAP);
            const int shotTop=top+links+(int)pre.size()*LINE_H-scroll;
            const int shotH=SCREENSHOT_ROW_H-12;
            const int sc=std::min((int)it.screenshots.size(), 8);
            for(int k=0;k<sc;++k){
                const int sy=shotTop+k*SCREENSHOT_ROW_H;
                if(sy+shotH>top&&sy<bottom)prepareOne(it.screenshots[k], "shot");
            }
        }
        return;
    }

    releaseScreenshotTextures();
}

void FullCatalogScreen::drawImageLoadingPlaceholder(const std::string& url, const std::string& ns, int x, int y, int w, int h) {
    vita2d_draw_rectangle(x, y, w, h, SURFACE2);
    if (w < 8 || h < 6 || !imageCache_ || url.empty()) return;

    const std::string path = imageCache_->pathFor(url, ns);
    const auto prog = imageCache_->progress();
    float pct = 0.f;
    bool determinate = false;
    if (prog.active && !prog.localPath.empty() && prog.localPath == path && prog.total > 0) {
        pct = std::min(1.f, static_cast<float>(prog.downloaded) / static_cast<float>(prog.total));
        determinate = true;
    } else if (imageCache_->isPending(path)) {
        pct = focusPulse();
    } else {
        return; // not loading — nothing to show
    }

    const int pad = 3;
    const int barH = std::max(3, std::min(6, h / 8));
    const int barX = x + pad;
    const int barY = y + h - pad - barH;
    const int barW = std::max(1, w - pad * 2);
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    if (determinate) {
        vita2d_draw_rectangle(barX, barY, std::max(1, (int)(barW * pct)), barH, ACCENT);
    } else {
        const int slideW = std::max(8, barW / 3);
        const int slide = (int)((barW - slideW) * pct);
        vita2d_draw_rectangle(barX + slide, barY, slideW, barH, ACCENT);
    }
}

void FullCatalogScreen::drawImage(const std::string& url, const std::string& ns, int x, int y, int w, int h) {
    if (!imageCache_ || url.empty()) {
        vita2d_draw_rectangle(x, y, w, h, SURFACE2);
        return;
    }
    if (ns == "shot") {
        const int clipTop = HEADER_H + TABS_H + DETAIL_HEADER_H;
        const int clipBottom = SCREEN_H - FOOTER_H;
        if (y + h <= clipTop || y >= clipBottom) return;
    }

    // Resolve path without enqueueing. Downloads are only started from prepareVisibleTextures.
    const std::string path = imageCache_->pathFor(url, ns);
    if (path.empty()) {
        vita2d_draw_rectangle(x, y, w, h, SURFACE2);
        return;
    }

    if (imageCache_->isFailed(path)) {
        vita2d_draw_rectangle(x, y, w, h, SURFACE2);
        return;
    }

    if (!imageCache_->isReady(path)) {
        // Soft nudge: if file already on disk, request() will mark ready; else may queue once.
        // prepareVisibleTextures is the primary enqueue path for visible cells only.
        drawImageLoadingPlaceholder(url, ns, x, y, w, h);
        return;
    }

    auto it = textures_.find(path);
    if (it == textures_.end() || !it->second) {
        drawImageLoadingPlaceholder(url, ns, x, y, w, h);
        return;
    }
    vita2d_draw_rectangle(x, y, w, h, SURFACE2);
    touchTexture(path);
    vita2d_texture* t = it->second;
    float tw = (float)vita2d_texture_get_width(t), th = (float)vita2d_texture_get_height(t);
    if (tw <= 0 || th <= 0) return;
    float sc = std::min((float)w / tw, (float)h / th), dw = tw * sc, dh = th * sc;
    vita2d_draw_texture_scale(t, x + (w - dw) / 2.0f, y + (h - dh) / 2.0f, sc, sc);
}

float FullCatalogScreen::focusPulse() const {
    // 0..1 soft pulse for focused UI chrome (~1.2s period)
    const double t = static_cast<double>(sceKernelGetProcessTimeWide()) / 1000000.0;
    return 0.55f + 0.45f * static_cast<float>(std::sin(t * 5.2));
}


float FullCatalogScreen::easeInOut(float t) const {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    // Smoothstep-ish cubic
    return t * t * (3.f - 2.f * t);
}

void FullCatalogScreen::showToast(const std::string& message, uint64_t durationMs) {
    toastMessage_ = message;
    toastShownMs_ = sceKernelGetProcessTimeWide() / 1000ULL;
    toastExpiresMs_ = toastShownMs_ + durationMs;
}

void FullCatalogScreen::updateAnimations() {
    // Catalog list scroll (row units)
    const float targetCat = static_cast<float>(state_.catalogScrollRow);
    visualCatalogScroll_ += (targetCat - visualCatalogScroll_) * 0.18f;
    if (std::fabs(targetCat - visualCatalogScroll_) < 0.008f)
        visualCatalogScroll_ = targetCat;

    // Detail body scroll (pixel-ish line units) — same smoothing as catalog
    const float targetDet = static_cast<float>(state_.detailScroll);
    visualDetailScroll_ += (targetDet - visualDetailScroll_) * 0.18f;
    if (std::fabs(targetDet - visualDetailScroll_) < 0.02f)
        visualDetailScroll_ = targetDet;

    // News modal scroll (line units)
    if (newsVisible_) {
        const float targetNews = static_cast<float>(newsScrollLine_);
        visualNewsScroll_ += (targetNews - visualNewsScroll_) * 0.22f;
        if (std::fabs(targetNews - visualNewsScroll_) < 0.02f)
            visualNewsScroll_ = targetNews;
    } else {
        visualNewsScroll_ = static_cast<float>(newsScrollLine_);
    }

    // Keep visual focus in sync with logical focus (no laggy card handoff).
    visualFocusIndex_ = static_cast<float>(state_.focusIndex);

    // When selection jumps to another app in detail mode, fade content briefly
    if (state_.mode == UiMode::SPLIT_DETAIL || state_.mode == UiMode::OPENING_DETAIL) {
        if (detailCrossfadeFrom_ != state_.focusIndex) {
            if (detailCrossfadeFrom_ >= 0)
                detailCrossfade_ = 0.25f;
            detailCrossfadeFrom_ = state_.focusIndex;
        }
        detailCrossfade_ += (1.f - detailCrossfade_) * 0.14f;
        if (detailCrossfade_ > 0.995f) detailCrossfade_ = 1.f;
    } else {
        detailCrossfade_ = 1.f;
        detailCrossfadeFrom_ = state_.focusIndex;
    }

    // Tab underline slides toward active catalog
    const float tabW = static_cast<float>(SCREEN_W) / static_cast<float>(CatalogType::Count);
    const float targetTabX = tabW * static_cast<float>(static_cast<int>(state_.catalog));
    if (tabIndicatorReady_ < 0.5f) {
        tabIndicatorX_ = targetTabX;
        tabIndicatorReady_ = 1.f;
    } else {
        tabIndicatorX_ += (targetTabX - tabIndicatorX_) * 0.18f;
    }

    // Soft fade-in after catalog data arrives
    if (catalogLoading_) {
        contentFade_ = 0.35f;
    } else {
        contentFade_ += (1.f - contentFade_) * 0.12f;
        if (contentFade_ > 0.995f) contentFade_ = 1.f;
    }


    // Catalog splash fade-out when loading finished
    if (catalogLoading_) {
        if (catalogSplashAlpha_ < 1.f) {
            catalogSplashAlpha_ += (1.f - catalogSplashAlpha_) * 0.25f;
            if (catalogSplashAlpha_ > 0.995f) catalogSplashAlpha_ = 1.f;
        }
    } else if (catalogSplashAlpha_ > 0.f) {
        catalogSplashAlpha_ *= 0.88f;
        catalogSplashAlpha_ -= 0.02f;
        if (catalogSplashAlpha_ < 0.02f) catalogSplashAlpha_ = 0.f;
    }

    // Settings open fade/slide
    if (state_.mode == UiMode::SETTINGS) {
        settingsEnter_ += (1.f - settingsEnter_) * 0.18f;
        if (settingsEnter_ > 0.995f) settingsEnter_ = 1.f;
        // target focus Y roughly matches row layout (updated in draw too)
        const float targetY = static_cast<float>(settingsFocus_);
        settingsFocusY_ += (targetY - settingsFocusY_) * 0.25f;
    } else {
        settingsEnter_ = 0.f;
    }
}

void FullCatalogScreen::drawToast() const {
    if (toastMessage_.empty() || toastExpiresMs_ == 0) return;
    const uint64_t now = sceKernelGetProcessTimeWide() / 1000ULL;
    if (now >= toastExpiresMs_) return;
    const uint64_t life = toastExpiresMs_ - toastShownMs_;
    const uint64_t age = now - toastShownMs_;
    float a = 1.f;
    if (age < 120) a = static_cast<float>(age) / 120.f;
    else if (toastExpiresMs_ - now < 200)
        a = static_cast<float>(toastExpiresMs_ - now) / 200.f;
    const unsigned alpha = static_cast<unsigned>(std::max(0.f, std::min(1.f, a)) * 230.f);
    const int tw = std::min(520, 40 + static_cast<int>(toastMessage_.size()) * 8);
    const int th = 46;
    const int x = (SCREEN_W - tw) / 2;
    const int y = SCREEN_H - FOOTER_H - th - 16;
    vita2d_draw_rectangle(x, y, tw, th, RGBA8(0x18, 0x18, 0x18, alpha));
    vita2d_draw_rectangle(x, y, tw, 2, withAlpha(ACCENT, alpha));
    vita2d_draw_rectangle(x, y + th - 1, tw, 1, withAlpha(ACCENT, static_cast<unsigned>(alpha * 0.5f)));
    if (font_)
        vita2d_pgf_draw_text(font_, x + 16, y + 30, RGBA8(255, 255, 255, alpha), 0.76f, toastMessage_.c_str());
}

void FullCatalogScreen::drawScrollFades(int x, int y, int width, int height) const {
    // Soft top/bottom vignette so scrolling content does not hard-clip against the panel edge.
    constexpr int steps = 14;
    for (int i = 0; i < steps; ++i) {
        const unsigned a = static_cast<unsigned>((steps - i) * (steps - i) * 180 / (steps * steps));
        const unsigned col = RGBA8(0, 0, 0, a);
        vita2d_draw_rectangle(x, y + i, width, 1, col);
        vita2d_draw_rectangle(x, y + height - 1 - i, width, 1, col);
    }
}

void FullCatalogScreen::drawActivePanelFrame(int x, int y, int width, int height, const char* label) const {
    const float pulse = focusPulse();
    const unsigned glow = withAlpha(ACCENT, static_cast<unsigned>(40 + pulse * 70));
    const unsigned solid = ACCENT;
    // Outer soft glow
    vita2d_draw_rectangle(x - 2, y - 2, width + 4, 2, glow);
    vita2d_draw_rectangle(x - 2, y + height, width + 4, 2, glow);
    vita2d_draw_rectangle(x - 2, y, 2, height, glow);
    vita2d_draw_rectangle(x + width, y, 2, height, glow);
    // Solid frame
    vita2d_draw_rectangle(x, y, width, 3, solid);
    vita2d_draw_rectangle(x, y + height - 3, width, 3, solid);
    vita2d_draw_rectangle(x, y, 3, height, solid);
    vita2d_draw_rectangle(x + width - 3, y, 3, height, solid);
    // Corner ticks
    const int tick = 14;
    vita2d_draw_rectangle(x, y, tick, 3, WHITE);
    vita2d_draw_rectangle(x, y, 3, tick, WHITE);
    vita2d_draw_rectangle(x + width - tick, y, tick, 3, WHITE);
    vita2d_draw_rectangle(x + width - 3, y, 3, tick, WHITE);
    vita2d_draw_rectangle(x, y + height - 3, tick, 3, WHITE);
    vita2d_draw_rectangle(x, y + height - tick, 3, tick, WHITE);
    vita2d_draw_rectangle(x + width - tick, y + height - 3, tick, 3, WHITE);
    vita2d_draw_rectangle(x + width - 3, y + height - tick, 3, tick, WHITE);
    // Label chip
    if (label && font_) {
        const int lw = 78, lh = 22;
        const int lx = x + 10, ly = y + 8;
        vita2d_draw_rectangle(lx, ly, lw, lh, solid);
        vita2d_draw_rectangle(lx, ly, lw, 1, WHITE);
        vita2d_pgf_draw_text(font_, lx + 10, ly + 16, BG, 0.58f, label);
    }
}




namespace {
bool readSfoStringKey(const std::string& path, const char* keyName, std::string& out) {
    out.clear();
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceIoStat st{};
    if (sceIoGetstat(path.c_str(), &st) < 0 || st.st_size < 0x14 || st.st_size > 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }
    std::string data(static_cast<size_t>(st.st_size), '\0');
    size_t done = 0;
    while (done < data.size()) {
        const int r = sceIoRead(fd, &data[done], data.size() - done);
        if (r <= 0) { sceIoClose(fd); return false; }
        done += static_cast<size_t>(r);
    }
    sceIoClose(fd);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
    if (std::memcmp(p, "\0PSF", 4) != 0) return false;
    auto u16 = [](const unsigned char* q) -> uint16_t {
        return static_cast<uint16_t>(q[0]) | static_cast<uint16_t>(q[1] << 8);
    };
    auto u32 = [](const unsigned char* q) -> uint32_t {
        return static_cast<uint32_t>(q[0]) | (static_cast<uint32_t>(q[1]) << 8) |
               (static_cast<uint32_t>(q[2]) << 16) | (static_cast<uint32_t>(q[3]) << 24);
    };
    const uint32_t keyTable = u32(p + 8);
    const uint32_t dataTable = u32(p + 12);
    const uint32_t count = u32(p + 16);
    if (keyTable >= data.size() || dataTable >= data.size()) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t entry = 0x14 + static_cast<size_t>(i) * 16;
        if (entry + 16 > data.size()) break;
        const uint16_t keyOff = u16(p + entry);
        const uint32_t dataLen = u32(p + entry + 4);
        const uint32_t dataOff = u32(p + entry + 12);
        const size_t keyPos = static_cast<size_t>(keyTable) + keyOff;
        const size_t valPos = static_cast<size_t>(dataTable) + dataOff;
        if (keyPos >= data.size() || valPos >= data.size()) continue;
        const char* key = reinterpret_cast<const char*>(p + keyPos);
        if (std::strcmp(key, keyName) != 0) continue;
        const size_t n = std::min(static_cast<size_t>(dataLen), data.size() - valPos);
        out.assign(reinterpret_cast<const char*>(p + valPos), n);
        while (!out.empty() && (out.back() == '\0' || out.back() == ' ')) out.pop_back();
        return !out.empty();
    }
    return false;
}

void versionParts(const std::string& s, int* parts, int maxParts) {
    for (int i = 0; i < maxParts; ++i) parts[i] = 0;
    int idx = 0;
    int cur = -1;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            if (cur < 0) cur = 0;
            cur = cur * 10 + (ch - '0');
        } else if (cur >= 0) {
            if (idx < maxParts) parts[idx++] = cur;
            cur = -1;
        }
    }
    if (cur >= 0 && idx < maxParts) parts[idx] = cur;
}

int compareVersionStrings(const std::string& installed, const std::string& catalog) {
    int a[4], b[4];
    versionParts(installed, a, 4);
    versionParts(catalog, b, 4);
    for (int i = 0; i < 4; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}
struct InstallProbeEntry {
    LocalInstallInfo info{};
    uint64_t checkedMs = 0;
    bool valid = false;
};

std::unordered_map<std::string, InstallProbeEntry>& installProbeCache() {
    static std::unordered_map<std::string, InstallProbeEntry> cache;
    return cache;
}

bool consumeInstallProbeBudget(uint64_t nowMs) {
    static uint64_t windowMs = 0;
    static unsigned probes = 0;
    constexpr uint64_t kWindowMs = 250;
    constexpr unsigned kMaxProbesPerWindow = 2;
    if (windowMs == 0 || nowMs < windowMs || (nowMs - windowMs) >= kWindowMs) {
        windowMs = nowMs;
        probes = 0;
    }
    if (probes >= kMaxProbesPerWindow) return false;
    ++probes;
    return true;
}

std::string installProbeKey(const std::string&titleId, const std::string&version) {
    return titleId + "\n" + version;
}

} // namespace (install-status helpers)

LocalInstallInfo FullCatalogScreen::queryLocalInstall(const CatalogItem& item) {
    LocalInstallInfo unknown;
    unknown.state = LocalInstallState::Unknown;
    if (item.titleId.empty()) return unknown;

    std::string tid = item.titleId;
    for (char& c : tid) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }

    const std::string key = installProbeKey(tid, item.version);
    const uint64_t nowMs = sceKernelGetProcessTimeWide() / 1000ULL;
    // Never probe the Vita filesystem while catalogs are loading or immediately after.
    if (catalogLoading_ || nowMs < installStatusWarmupUntilMs_) {
        return unknown;
    }
    auto& cache = installProbeCache();
    auto cached = cache.find(key);

    constexpr uint64_t kPositiveTtlMs = 8000;
    constexpr uint64_t kNegativeTtlMs = 4000;
    if (cached != cache.end() && cached->second.valid) {
        const uint64_t age = nowMs >= cached->second.checkedMs ? nowMs - cached->second.checkedMs : 0;
        const uint64_t ttl =
            (cached->second.info.state == LocalInstallState::NotInstalled)
                ? kNegativeTtlMs
                : kPositiveTtlMs;
        if (age < ttl) return cached->second.info;
    }

    if (!consumeInstallProbeBudget(nowMs)) {
        if (cached != cache.end() && cached->second.valid) return cached->second.info;
        return unknown;
    }

    LocalInstallInfo info;
    info.state = LocalInstallState::NotInstalled;

    const std::string candidates[] = {
        std::string("ux0:app/") + tid,
        (tid == item.titleId) ? std::string() : (std::string("ux0:app/") + item.titleId),
    };

    std::string paramPath;
    bool foundAppDir = false;
    bool hasParam = false;

    for (const std::string&dir : candidates) {
        if (dir.empty()) continue;
        SceIoStat dirStat{};
        if (sceIoGetstat(dir.c_str(), &dirStat) < 0) continue;
        foundAppDir = true;
        paramPath = dir + "/sce_sys/param.sfo";
        SceIoStat paramStat{};
        hasParam = sceIoGetstat(paramPath.c_str(), &paramStat) >= 0;
        break;
    }

    if (!foundAppDir) {
        info.state = LocalInstallState::NotInstalled;
    } else {
        info.state = LocalInstallState::Installed;
        if (hasParam) {
            std::string ver;
            if (!readSfoStringKey(paramPath, "APP_VER", ver))
                readSfoStringKey(paramPath, "VERSION", ver);
            info.installedVersion = ver;
            if (!ver.empty() && !item.version.empty() &&
                compareVersionStrings(ver, item.version) < 0) {
                info.state = LocalInstallState::UpdateAvailable;
            }
        }
    }

    InstallProbeEntry& entry = cache[key];
    entry.info = info;
    entry.checkedMs = nowMs;
    entry.valid = true;

    installStatusCache_[item.titleId] = info;
    if (tid != item.titleId) installStatusCache_[tid] = info;
    return info;
}

void FullCatalogScreen::invalidateInstallStatus(const std::string& titleId) {
    if (titleId.empty()) {
        installStatusCache_.clear();
        installProbeCache().clear();
        return;
    }

    installStatusCache_.erase(titleId);
    for (auto it = installProbeCache().begin(); it != installProbeCache().end();) {
        if (it->first == titleId || it->first.rfind(titleId + "\n", 0) == 0 ||
            (!titleId.empty() && it->first.rfind(titleId, 0) == 0)) {
            it = installProbeCache().erase(it);
        } else {
            ++it;
        }
    }
}

void FullCatalogScreen::drawInstallBadge(int x, int y, const LocalInstallInfo& info, bool compact) {
    if (info.state != LocalInstallState::Installed && info.state != LocalInstallState::UpdateAvailable)
        return;
    const bool upd = (info.state == LocalInstallState::UpdateAvailable);
    const char* label = upd ? (compact ? "UPD" : "UPDATE") : (compact ? "ON" : "INSTALLED");
    const unsigned bg = upd ? RGBA8(0xE0, 0x8A, 0x10, 255) : ACCENT;
    const unsigned fg = upd ? WHITE : BG;
    const float scale = compact ? 0.48f : 0.54f;
    const int tw = vita2d_pgf_text_width(font_, scale, label);
    const int padX = compact ? 5 : 7;
    const int bh = compact ? 16 : 18;
    const int bw = tw + padX * 2;
    vita2d_draw_rectangle(x, y, bw, bh, bg);
    vita2d_pgf_draw_text(font_, x + padX, y + (compact ? 12 : 13), fg, scale, label);
}

void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, y, x + w, y + h);
    const float pulse = focus ? focusPulse() : 0.f;
    // Subtle lift / scale for focused card
    // Keep focus chrome inside the card/panel (no outward expand while scrolling).
    int ox = 0;
    int oy = 0;
    int ww = w;
    int hh = h;
    const unsigned bg = focus ? SURFACE2 : SURFACE;
    vita2d_draw_rectangle(x + ox, y + oy, ww, hh, bg);
    // Brand accent rail on every card (stronger when focused)
    vita2d_draw_rectangle(x + ox, y + oy, focus ? 3 : 2, hh, focus ? ACCENT : ACCENT_SOFT);
    if (focus) {
        const unsigned glow = withAlpha(ACCENT, static_cast<unsigned>(55 + pulse * 100));
        vita2d_draw_rectangle(x + ox - 2, y + oy - 2, ww + 4, 2, glow);
        vita2d_draw_rectangle(x + ox - 2, y + oy + hh, ww + 4, 2, glow);
        vita2d_draw_rectangle(x + ox - 2, y + oy, 2, hh, glow);
        vita2d_draw_rectangle(x + ox + ww, y + oy, 2, hh, glow);
        const int bw = 2 + static_cast<int>(pulse * 2.0f);
        vita2d_draw_rectangle(x + ox, y + oy, ww, bw, ACCENT);
        vita2d_draw_rectangle(x + ox, y + oy + hh - bw, ww, bw, ACCENT);
        vita2d_draw_rectangle(x + ox, y + oy, bw, hh, ACCENT);
        vita2d_draw_rectangle(x + ox + ww - bw, y + oy, bw, hh, ACCENT);
    } else {
        vita2d_draw_rectangle(x, y, w, 1, BORDER);
    }
    const bool compact = h < 125;
    int is = compact ? 64 : 80;
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 10 + ox, y + (compact ? 8 : 10) + oy, is, is);
    int tx = x + is + (compact ? 14 : 18) + ox;
    {
        const float nameSc = compact ? (focus ? 0.90f : 0.84f) : (focus ? 0.98f : 0.92f);
        // Reserve right side for Game/Data Files chips + size so title never underlaps.
        int rightPad = 16;
        if (itemHasLinkType(it, "game files") || itemHasLinkType(it, "data files")
            || itemHasLinkType(it, "game file") || itemHasLinkType(it, "data file")) {
            rightPad = compact ? 100 : 110;
        }
        const int nameMaxW = std::max(40, (x + ox + ww) - tx - rightPad);
        drawMarqueeText(font_, tx, y + (compact ? 24 : 28) + oy, nameMaxW, WHITE, nameSc, it.name, focus);
    }
    // Keep remaining card chrome inside the card box (marquee temporarily tightens scissor).
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + ox, y + oy, x + ox + ww, y + oy + hh);
    vita2d_pgf_draw_text(font_, tx, y + (compact ? 44 : 50) + oy, TEXT, compact ? 0.74f : 0.82f,
        ellipsize(it.author.empty() ? "Unknown author" : it.author, compact ? 16 : 18).c_str());
    vita2d_pgf_draw_text(font_, tx, y + (compact ? 64 : 72) + oy, colorForStatus(it.status), compact ? 0.72f : 0.80f,
        ellipsize(it.status, 14).c_str());
    // Version / date bottom-left; size always bottom-right when known (all catalogs).
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    if (!meta.empty())
        vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - (compact ? 10 : 12) + oy, DIM, compact ? 0.66f : 0.72f, ellipsize(meta, compact ? 16 : 18).c_str());
    {
        // Bottom-right chips: size + optional Data / Game Files tags (stacked upward)
        {
            int sy = y + h - 10 + oy;
            const int right = x + ox + ww;
            // Neutral size chip
            auto drawSizeChip = [&](const std::string& label) {
                if (label.empty()) return;
                const float sc = 0.70f;
                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());
                const int padX = 6;
                const int cw = tw + padX * 2;
                const int ch = 19;
                const int sx = right - cw - 6;
                const int cy = sy - ch + 3;
                vita2d_draw_rectangle(sx, cy, cw, ch, SURFACE2);
                vita2d_pgf_draw_text(font_, sx + padX, sy, TEXT, sc, label.c_str());
                sy -= 21;
            };
            // Folder-style amber chips so Data / Game Files stand out
            auto drawFolderChip = [&](const std::string& label) {
                if (label.empty()) return;
                const float sc = 0.76f;
                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());
                const int padX = 8;
                const int cw = tw + padX * 2;
                const int ch = 24;
                const int sx = right - cw - 6;
                const int cy = sy - ch + 4;
                const unsigned folderBg = RGBA8(0x3A, 0x2C, 0x10, 255);
                const unsigned folderEdge = RGBA8(0xE8, 0xB4, 0x3A, 255);
                const unsigned folderText = RGBA8(0xFF, 0xD2, 0x6A, 255);
                vita2d_draw_rectangle(sx, cy, cw, ch, folderBg);
                vita2d_draw_rectangle(sx, cy, cw, 2, folderEdge); // top tab highlight
                vita2d_draw_rectangle(sx, cy, 2, ch, folderEdge);
                vita2d_pgf_draw_text(font_, sx + padX, sy + 1, folderText, sc, label.c_str());
                sy -= 24;
            };
            const std::string sz = itemCardSizeLabel(it);
            // Allow a bit more room for "16 MB + 1.5 GB"
            if (!sz.empty()) drawSizeChip(ellipsize(sz, 22));
            if (itemHasLinkType(it, "data files")) drawFolderChip("Data Files");
            if (itemHasLinkType(it, "game files")) drawFolderChip("Game Files");
        }
    }

    // Installed / update badge: bottom-left on icon + top-right of card
    {
        const LocalInstallInfo li = queryLocalInstall(it);
        if (li.state == LocalInstallState::Installed || li.state == LocalInstallState::UpdateAvailable) {
            // Overlay on icon corner (always visible even when title text is long)
            drawInstallBadge(x + 10 + ox, y + 9 + oy + is - 18, li, true);
            const char* lab = (li.state == LocalInstallState::UpdateAvailable) ? "UPD" : "ON";
            const float sc = 0.56f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            const int bw = tw + 10;
            drawInstallBadge(x + ox + ww - bw - 6, y + oy + 6, li, true);
        }
    }
    (void)idx;
}
void FullCatalogScreen::drawCatalogPanel(int x,int y,int w,int h,bool split){
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, 2, h, ACCENT_SOFT);
    const unsigned dimPanel = contentFade_ < 0.99f ? RGBA8(0,0,0, static_cast<unsigned>((1.f-contentFade_)*90)) : 0;

    // Clip cards to the panel so smooth scroll never spills outside the frame
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 1, y + 1, x + w - 1, y + h - 1);

    if (!split) {
        const int vis = visibleRowsFull();
        const int rows = totalRows();
        const int cw = (w - GRID_PAD * 2 - CARD_GAP * 2) / 3;
        const float rowH = static_cast<float>(FULL_CARD_H + CARD_GAP);
        const int baseRow = std::max(0, static_cast<int>(std::floor(visualCatalogScroll_)) - 0);
        for (int r = -1; r <= vis + 1; ++r) {
            for (int c = 0; c < 3; ++c) {
                const int i = (baseRow + r) * 3 + c;
                if (i < 0 || i >= (int)catalogView().size()) continue;
                const float fy = static_cast<float>(y + GRID_PAD) + (static_cast<float>(baseRow + r) - visualCatalogScroll_) * rowH;
                if (fy + FULL_CARD_H < y || fy > y + h) continue;
                drawCatalogCard(catalogView()[i], i, x + GRID_PAD + c * (cw + CARD_GAP), static_cast<int>(fy), cw, FULL_CARD_H, i == state_.focusIndex);
                // Re-assert panel clip: focused card marquee disables global scissor.
                vita2d_enable_clipping();
                vita2d_set_clip_rectangle(x + 1, y + 1, x + w - 1, y + h - 1);
            }
        }
        vita2d_disable_clipping();
        if (rows > vis) {
            int tx = x + w - 8, ty = y + 12, th = h - 24;
            vita2d_draw_rectangle(tx, ty, 4, th, BORDER);
            int thumb = std::max(24, th * vis / std::max(1, rows));
            int mr = std::max(1, rows - vis);
            float scrollT = std::min(1.f, visualCatalogScroll_ / static_cast<float>(mr));
            int yy = ty + static_cast<int>((th - thumb) * scrollT);
            vita2d_draw_rectangle(tx, yy, 4, thumb, ACCENT);
        }
        drawScrollFades(x, y, w, h);
    } else {
        const int vis = visibleRowsSplit();
        const float rowH = static_cast<float>(SPLIT_CARD_H + CARD_GAP);
        const int baseRow = std::max(0, static_cast<int>(std::floor(visualCatalogScroll_)));
        for (int r = -1; r <= vis + 1; ++r) {
            const int i = baseRow + r;
            if (i < 0 || i >= (int)catalogView().size()) continue;
            const float fy = static_cast<float>(y + GRID_PAD) + (static_cast<float>(i) - visualCatalogScroll_) * rowH;
            if (fy + SPLIT_CARD_H < y || fy > y + h) continue;
            drawCatalogCard(catalogView()[i], i, x + GRID_PAD, static_cast<int>(fy), w - GRID_PAD * 2 - 4, SPLIT_CARD_H, i == state_.focusIndex);
            // Re-assert panel clip: focused card marquee disables global scissor.
            vita2d_enable_clipping();
            vita2d_set_clip_rectangle(x + 1, y + 1, x + w - 1, y + h - 1);
        }
        vita2d_disable_clipping();
        const int total = (int)catalogView().size();
        if (total > vis) {
            int tx = x + w - 8, ty = y + 12, th = h - 24;
            vita2d_draw_rectangle(tx, ty, 4, th, BORDER);
            int thumb = std::max(24, th * vis / std::max(1, total));
            int mr = std::max(1, total - vis);
            float scrollT = std::min(1.f, visualCatalogScroll_ / static_cast<float>(mr));
            int yy = ty + static_cast<int>((th - thumb) * scrollT);
            vita2d_draw_rectangle(tx, yy, 4, thumb, ACCENT);
        }
        drawScrollFades(x, y, w, h);
        if (state_.mode == UiMode::SPLIT_DETAIL && state_.activePanel == UiPanel::Catalog)
            drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4, "LIST");
    }
    if (dimPanel)
        vita2d_draw_rectangle(x, y, w, h, dimPanel);
}


void FullCatalogScreen::wrapText(const std::string&t,int max,std::vector<std::string>&out)const{out.clear();std::string cur;for(char c:t){if(c=='\n'){out.push_back(cur);cur.clear();continue;}if((int)cur.size()>=max&&c==' '){out.push_back(cur);cur.clear();continue;}cur.push_back(c);if((int)cur.size()>=max){out.push_back(cur);cur.clear();}}if(!cur.empty())out.push_back(cur);}void FullCatalogScreen::drawTextLines(const std::vector<std::string>&l,int x,int y,int lh,unsigned col,float sc,int start,int max,int top,int bottom){int first=std::max(0,start),last=std::min((int)l.size(),first+max),dy=y+first*lh;for(int i=first;i<last;++i){if(dy>=top&&dy<=bottom)vita2d_pgf_draw_text(font_,x,dy,col,sc,l[i].c_str());dy+=lh;}}
void FullCatalogScreen::drawDetailLinks(const CatalogItem& it, int x, int y, int w, int& heightOut) {
    heightOut = 0;
    const auto rows = buildLinkLayout(it);
    const bool showAll = itemHasDataOrGameFiles(it);
    const int focusOff = showAll ? 1 : 0;
    int yOff = 0;

    if (showAll) {
        // Own section header (same style as DOWNLOADS / GAME FILES)
        vita2d_draw_rectangle(x, y + LINK_SECTION_H - 1, w, 1, BORDER);
        vita2d_pgf_draw_text(font_, x + 4, y + 18, ACCENT, 0.76f, "INSTALL ALL");
        const int by = y + LINK_SECTION_H + 4;
        const bool fAll = state_.linkNavigation && state_.linkFocus == 0;
        // Same look as download rows (SURFACE2 / ACCENT focus) + soft border pulse
        const float pulse = 0.40f + 0.60f * focusPulse();
        const unsigned borderA = (unsigned)(120.f + 135.f * pulse);
        const unsigned borderCol = withAlpha(ACCENT, borderA > 255 ? 255 : borderA);
        const unsigned fill = fAll ? ACCENT : SURFACE2;
        const int bwPulse = fAll ? 2 : (2 + (int)(1.5f * pulse));
        vita2d_draw_rectangle(x, by, w, INSTALL_ALL_BLOCK_H, borderCol);
        vita2d_draw_rectangle(x + bwPulse, by + bwPulse, w - bwPulse * 2, INSTALL_ALL_BLOCK_H - bwPulse * 2, fill);
        vita2d_draw_rectangle(x + bwPulse, by + bwPulse, w - bwPulse * 2, 1, fAll ? ACCENT : BORDER);
        const unsigned tc = fAll ? BG : WHITE;
        const unsigned sub = fAll ? BG : TEXT;
        vita2d_pgf_draw_text(font_, x + 12, by + 24, tc, 0.84f, "INSTALL ALL");
        vita2d_pgf_draw_text(font_, x + 12, by + 48, sub, 0.66f,
            "Install app + Game/Data Files from scratch");
        yOff = LINK_SECTION_H + 4 + INSTALL_ALL_BLOCK_H + 8;
    }

    if (rows.empty() && !showAll) return;

    for (const auto& row : rows) {
        const int ry = y + yOff + row.y;
        if (row.isSection) {
            vita2d_draw_rectangle(x, ry + LINK_SECTION_H - 1, w, 1, BORDER);
            vita2d_pgf_draw_text(font_, x + 4, ry + 18, ACCENT, 0.76f, linkSectionTitle(row.section));
            continue;
        }
        const CatalogLink& l = it.linkDetails[row.detailIndex];
        const bool f = state_.linkNavigation && state_.linkFocus == (row.focusIndex + focusOff);
        const bool can = actionableLink(l);
        vita2d_draw_rectangle(x, ry, w, LINK_ROW_H, f ? ACCENT : SURFACE2);
        vita2d_draw_rectangle(x, ry, w, 1, f ? ACCENT : BORDER);
        const unsigned mc = f ? BG : (can ? WHITE : TEXT);
        std::string title = l.name.empty() ? l.type : l.name;
        const std::string sizeLabel = formatLinkSizeLabel(l, it);
        const int badgeW = l.recommended ? 96 : 0;
        { const int titleMaxW = std::max(40, w - 20 - badgeW - 8); drawMarqueeText(font_, x + 10, ry + 17, titleMaxW, mc, 0.80f, title, f); }
        std::string meta = linkSectionMetaLabel(row.section);
        if (!sizeLabel.empty()) meta += "  •  " + sizeLabel;
        if (can) meta += f ? "  •  X: install" : "  •  X";
        vita2d_pgf_draw_text(font_, x + 10, ry + 35, f ? BG : DIM, 0.70f, ellipsize(meta, badgeW ? 26 : 40).c_str());
        if (l.recommended) {
            const int bx = x + w - badgeW - 8, by = ry + 8;
            vita2d_pgf_draw_text(font_, bx, ry + 18, f ? BG : ACCENT, 0.66f, "Recommended");
        }
    }
    heightOut = linkLayoutTotalHeight(rows) + yOff;
}


void FullCatalogScreen::drawDetailContent(const CatalogItem& it, int x, int y, int w, int h) {
    if (detailCrossfade_ < 0.99f) {
        vita2d_draw_rectangle(x, y, w, h, RGBA8(0, 0, 0, static_cast<unsigned>((1.f - detailCrossfade_) * 140)));
    }

    const int cx = x + 18;
    const int cw = w - 36;
    const int mc = std::max(16, cw / 7);
    const int top = y + DETAIL_HEADER_H + 10;
    const int bottom = y + h - 10;
    const float scroll = std::max(0.f, visualDetailScroll_);

    auto normalizeBullets = [](const std::string& text) {
        std::string out;
        out.reserve(text.size() + 8);
        for (size_t p = 0; p < text.size(); ++p) {
            if (text[p] == '-' && (p == 0 || text[p - 1] == ' ') && p + 1 < text.size() && text[p + 1] == ' ') {
                if (!out.empty() && out.back() != '\n') out.push_back('\n');
            }
            out.push_back(text[p]);
        }
        return out;
    };

    auto drawSectionHeader = [&](int sx, int sy, const char* title) {
        if (sy + DETAIL_SECTION_H < top || sy > bottom) return;
        vita2d_pgf_draw_text(font_, sx, sy + 18, ACCENT, 0.72f, title);
        vita2d_draw_rectangle(sx, sy + DETAIL_SECTION_H - 4, cw, 1, BORDER);
        vita2d_draw_rectangle(sx, sy + DETAIL_SECTION_H - 4, 56, 1, ACCENT);
    };

    auto drawBody = [&](int sx, int sy, const std::vector<std::string>& lines) {
        int dy = sy;
        for (const auto& line : lines) {
            if (dy >= top - LINE_H && dy <= bottom + LINE_H) {
                vita2d_pgf_draw_text(font_, sx, dy + 16, TEXT, 0.68f, line.c_str());
            }
            dy += LINE_H;
        }
        return dy;
    };

    auto drawMetaRow = [&](int sx, int sy, int rowW, const char* label, const std::string& value) {
        if (value.empty()) return sy;
        if (sy >= top - DETAIL_META_H && sy <= bottom + DETAIL_META_H) {
            vita2d_pgf_draw_text(font_, sx + 10, sy + 18, DIM, 0.64f, label);
            const int lw = vita2d_pgf_text_width(font_, 0.64f, label);
            const int vx = sx + std::max(120, lw + 20);
            const int maxChars = std::max(8, (rowW - (vx - sx) - 12) / 8);
            vita2d_pgf_draw_text(font_, vx, sy + 18, WHITE, 0.68f, ellipsize(value, maxChars).c_str());
        }
        return sy + DETAIL_META_H;
    };

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 2, top, x + w - 18, bottom);

    int cursor = top - (int)scroll;
    int linksH = 0;
    drawDetailLinks(it, cx, cursor, cw, linksH);
    // Link-row marquee can clear scissor; restore detail body clip.
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 2, top, x + w - 18, bottom);
    cursor += linksH;

    auto emitTextSection = [&](const char* title, const std::string& body, bool bullets) {
        if (body.empty()) return;
        std::vector<std::string> lines;
        wrapText(bullets ? normalizeBullets(body) : body, mc, lines);
        if (lines.empty()) return;
        drawSectionHeader(cx, cursor, title);
        cursor += DETAIL_SECTION_H;
        cursor = drawBody(cx + 2, cursor, lines);
        cursor += DETAIL_SECTION_GAP;
    };

    emitTextSection("DESCRIPTION", it.description, false);
    emitTextSection("LONG DESCRIPTION", it.longDescription, false);

    const int sc = std::min(5, (int)it.screenshots.size());
    if (sc > 0) {
        drawSectionHeader(cx, cursor, "SCREENSHOTS");
        cursor += DETAIL_SECTION_H;
        for (int i = 0; i < sc; ++i) {
            drawImage(it.screenshots[i], "shot", cx, cursor + i * SCREENSHOT_ROW_H, cw, SCREENSHOT_ROW_H - 18);
        }
        cursor += sc * SCREENSHOT_ROW_H + DETAIL_SECTION_GAP;
    }

    emitTextSection("REQUIREMENTS", it.requirements, true);

    std::string installLine;
    {
        const LocalInstallInfo li = queryLocalInstall(it);
        if (li.state == LocalInstallState::Installed || li.state == LocalInstallState::UpdateAvailable) {
            installLine = (li.state == LocalInstallState::UpdateAvailable) ? "Update available" : "Installed";
            if (!li.installedVersion.empty()) installLine += " (v" + li.installedVersion + ")";
        } else if (!it.titleId.empty()) {
            installLine = "Not installed";
        }
    }

    const bool hasInfo = !it.titleId.empty() || !it.version.empty() || !installLine.empty() ||
                         !it.versionDate.empty() || !it.category.empty() || !it.subcategory.empty() ||
                         !it.size.empty() || !it.status.empty();
    if (hasInfo) {
        const int cardTop = cursor;
        int rows = 0;
        if (!it.titleId.empty()) ++rows;
        if (!it.version.empty()) ++rows;
        if (!installLine.empty()) ++rows;
        if (!it.versionDate.empty()) ++rows;
        if (!it.category.empty()) ++rows;
        if (!it.subcategory.empty()) ++rows;
        if (!it.size.empty()) ++rows;
        if (!it.status.empty()) ++rows;
        const int cardH = DETAIL_SECTION_H + 6 + rows * DETAIL_META_H + 10;

        if (cardTop + cardH >= top && cardTop <= bottom) {
            vita2d_draw_rectangle(cx, cardTop, cw, cardH, SURFACE2);
            vita2d_draw_rectangle(cx, cardTop, 3, cardH, ACCENT);
            vita2d_draw_rectangle(cx, cardTop, cw, 1, BORDER);
            vita2d_draw_rectangle(cx, cardTop + cardH - 1, cw, 1, BORDER);
        }
        drawSectionHeader(cx + 8, cardTop + 2, "INFORMATION");
        int my = cardTop + DETAIL_SECTION_H + 4;
        my = drawMetaRow(cx, my, cw, "Title ID", it.titleId);
        my = drawMetaRow(cx, my, cw, "Version", it.version);
        my = drawMetaRow(cx, my, cw, "Install", installLine);
        my = drawMetaRow(cx, my, cw, "Released", it.versionDate);
        my = drawMetaRow(cx, my, cw, "Category", it.category);
        my = drawMetaRow(cx, my, cw, "Subcategory", it.subcategory);
        my = drawMetaRow(cx, my, cw, "Size", it.size);
        my = drawMetaRow(cx, my, cw, "Status", it.status);
        cursor = cardTop + cardH + DETAIL_SECTION_GAP;
    }

    emitTextSection("CHANGELOG", it.changelog, false);

    vita2d_disable_clipping();

    const int total = detailContentHeight(it, w);
    const int vis = std::max(1, h - DETAIL_HEADER_H - 18);
    const int mx = std::max(0, total - vis);
    if (mx > 0) {
        const int tx = x + w - 8;
        const int ty = y + DETAIL_HEADER_H + 8;
        const int th = h - DETAIL_HEADER_H - 18;
        vita2d_draw_rectangle(tx, ty, 3, th, BORDER);
        const int thumb = std::max(20, th * vis / std::max(1, total));
        const int yy = ty + (int)((th - thumb) * (scroll / std::max(1.f, (float)mx)));
        vita2d_draw_rectangle(tx, yy, 3, thumb, ACCENT);
    }
}


void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, y, x + w, y + h);
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, 2, h, ACCENT_SOFT);
    vita2d_draw_rectangle(x, y, w, 1, ACCENT_SOFT);
    int i = selectedIndex();
    if (i < 0) return;
    const CatalogItem& it = catalogView()[i];
    const bool active = (state_.mode == UiMode::SPLIT_DETAIL && state_.activePanel == UiPanel::Detail);
    vita2d_draw_rectangle(x, y, w, DETAIL_HEADER_H, SURFACE);
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 12, y + 14, 72, 72);
    // Leave room for active-panel label chip on the left when focused
    const int titleX = active ? x + 100 : x + 96;
    {
        // Leave room for Select-links / Request-data buttons on the right.
        const int titleMaxW = std::max(60, (x + w - 160) - titleX);
        drawMarqueeText(font_, titleX, y + 32, titleMaxW, WHITE, 1.00f, it.name, active);
    }
    vita2d_pgf_draw_text(font_, titleX, y + 56, TEXT, 0.80f, ellipsize(it.author, 18).c_str());
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    vita2d_pgf_draw_text(font_, titleX, y + 78, colorForStatus(it.status), 0.70f, ellipsize(meta.empty() ? it.status : meta, 20).c_str());

    {
        const LocalInstallInfo li = queryLocalInstall(it);
        if (li.state == LocalInstallState::Installed || li.state == LocalInstallState::UpdateAvailable) {
            const char* lab = (li.state == LocalInstallState::UpdateAvailable) ? "UPDATE" : "INSTALLED";
            const float sc = 0.54f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            const int bw = tw + 14;
            drawInstallBadge(x + w - bw - 10, y + 12, li, false);
            if (!li.installedVersion.empty()) {
                char iv[48];
                sceClibSnprintf(iv, sizeof(iv), "Local v%s", li.installedVersion.c_str());
                const int lw = vita2d_pgf_text_width(font_, 0.48f, iv);
                vita2d_pgf_draw_text(font_, x + w - lw - 12, y + 36, DIM, 0.48f, iv);
            }
        }
    }
    {
        const int bx = x + w - 156, by = y + 12, bw = 142, bh = 32;
        if (!it.linkDetails.empty()) {
            const bool linkOn = state_.linkNavigation;
            const float pulse = linkOn ? focusPulse() : 0.f;
            vita2d_draw_rectangle(bx, by, bw, bh, linkOn ? ACCENT : SURFACE2);
            if (linkOn) {
                const unsigned glow = withAlpha(ACCENT, static_cast<unsigned>(40 + pulse * 80));
                vita2d_draw_rectangle(bx - 2, by - 2, bw + 4, bh + 4, glow);
                vita2d_draw_rectangle(bx, by, bw, bh, ACCENT);
            }
            vita2d_draw_rectangle(bx, by, bw, 1, ACCENT);
            vita2d_draw_rectangle(bx, by, 1, bh, ACCENT);
            vita2d_draw_rectangle(bx, by + bh - 1, bw, 1, ACCENT);
            vita2d_draw_rectangle(bx + bw - 1, by, 1, bh, ACCENT);
            vita2d_pgf_draw_text(font_, bx + 8, by + 22, linkOn ? BG : ACCENT, 0.72f, linkOn ? "△ Exit link mode" : "△ Select links");
        }
        if (itemEligibleForDataRequest(it)) {
            // Below Select links when present; same header column when no links.
            // Amber identity color so it stands out from green "Select links".
            const unsigned REQ = RGBA8(0xFF, 0xB0, 0x20, 255);
            const unsigned REQ_BG = RGBA8(0x3A, 0x2A, 0x10, 255);
            const int rby = !it.linkDetails.empty() ? (by + bh + 6) : by;
            const int rbx = bx, rbw = bw, rbh = 32;
            vita2d_draw_rectangle(rbx, rby, rbw, rbh, REQ_BG);
            vita2d_draw_rectangle(rbx, rby, rbw, 2, REQ);
            vita2d_draw_rectangle(rbx, rby, 2, rbh, REQ);
            vita2d_draw_rectangle(rbx, rby + rbh - 2, rbw, 2, REQ);
            vita2d_draw_rectangle(rbx + rbw - 2, rby, 2, rbh, REQ);
            vita2d_pgf_draw_text(font_, rbx + 8, rby + 22, REQ, 0.72f, "□ Request data");
        }
    }
    drawDetailContent(it, x, y, w, h);
    // Fade over scrollable body (below header)
    drawScrollFades(x, y + DETAIL_HEADER_H, w, std::max(1, h - DETAIL_HEADER_H));
    if (active)
        drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4, "DETAIL");
}

bool FullCatalogScreen::itemSupportsInstallAll(const CatalogItem& item) const {
    return itemHasDataOrGameFiles(item);
}

void FullCatalogScreen::collectInstallAllOptions(const CatalogItem& item, const char* kind, std::vector<int>& out) const {
    out.clear();
    for (size_t i = 0; i < item.linkDetails.size(); ++i) {
        const CatalogLink& l = item.linkDetails[i];
        if (std::strcmp(kind, "download") == 0) {
            if (isDownloadTypeLink(l)) out.push_back(static_cast<int>(i));
        } else if (std::strcmp(kind, "game") == 0) {
            if (isGameFilesTypeLink(l)) out.push_back(static_cast<int>(i));
        } else if (std::strcmp(kind, "data") == 0) {
            if (isDataFilesTypeLink(l)) out.push_back(static_cast<int>(i));
        }
    }
}

void FullCatalogScreen::openInstallAllWizard() {
    if (installProgressActive_ || catalogLoading_) {
        showToast("Wait for current operation to finish", 1600);
        return;
    }
    const int i = selectedIndex();
    if (i < 0) return;
    const CatalogItem& item = catalogView()[i];
    if (!itemSupportsInstallAll(item)) return;
    installAllItemIndex_ = i;
    installAllChosenDownload_ = -1;
    installAllChosenGameFiles_ = -1;
    installAllChosenDataFiles_ = -1;
    installAllQueue_.clear();
    installAllQueueLabels_.clear();
    installAllQueueIndex_ = 0;
    installAllLastOutcome_ = -1;
    installAllFinishedToast_ = false;
    installAllFocus_ = 0;
    installAllPhase_ = InstallAllPhase::Confirm;
    exitLinkNavigation();
    diagnostics::log("[UI] Install All wizard opened for " + item.name);
}

void FullCatalogScreen::closeInstallAllWizard(bool cancel) {
    if (installAllPhase_ == InstallAllPhase::Hidden) return;
    if (cancel && installAllPhase_ == InstallAllPhase::Running) {
        // Running installs are cancelled via installCancel_; just drop queue
        installAllQueue_.clear();
        installAllQueueLabels_.clear();
    }
    installAllPhase_ = InstallAllPhase::Hidden;
    installAllOptions_.clear();
    installAllItemIndex_ = -1;
    if (cancel) diagnostics::log("[UI] Install All wizard cancelled");
}

void FullCatalogScreen::installAllAdvancePick() {
    if (installAllItemIndex_ < 0 || installAllItemIndex_ >= (int)catalogView().size()) {
        closeInstallAllWizard(true);
        return;
    }
    const CatalogItem& item = catalogView()[installAllItemIndex_];

    // After Confirm → resolve Download, then Game Files, then Data Files.
    // After a Pick* selection, continue from the next category.
    bool needDownload = (installAllChosenDownload_ < 0);
    bool needGame = (installAllChosenGameFiles_ < 0);
    bool needData = (installAllChosenDataFiles_ < 0);

    // Mark resolved-empty categories so we don't re-prompt
    // Use -2 as "none available / skipped"
    auto autoOrPrompt = [&](bool& need, int& chosen, InstallAllPhase phase, const char* kind) -> bool {
        if (!need) return false;
        collectInstallAllOptions(item, kind, installAllOptions_);
        if (installAllOptions_.empty()) {
            chosen = -2; // skipped
            need = false;
            return false;
        }
        if (installAllOptions_.size() == 1) {
            chosen = installAllOptions_[0];
            need = false;
            return false;
        }
        // Multiple: show picker (unless we already showed this phase and user selected)
        if (installAllPhase_ == phase && chosen >= 0) {
            need = false;
            return false;
        }
        if (installAllPhase_ != phase) {
            installAllPhase_ = phase;
            installAllFocus_ = 0;
            // Prefer recommended
            for (size_t i = 0; i < installAllOptions_.size(); ++i) {
                if (item.linkDetails[installAllOptions_[i]].recommended) {
                    installAllFocus_ = (int)i;
                    break;
                }
            }
            return true; // wait for UI
        }
        return false;
    };

    // If coming from a pick phase with a selection already stored by input, clear need
    if (installAllPhase_ == InstallAllPhase::PickDownload && installAllChosenDownload_ >= 0) needDownload = false;
    if (installAllPhase_ == InstallAllPhase::PickGameFiles && installAllChosenGameFiles_ >= 0) needGame = false;
    if (installAllPhase_ == InstallAllPhase::PickDataFiles && installAllChosenDataFiles_ >= 0) needData = false;

    if (autoOrPrompt(needDownload, installAllChosenDownload_, InstallAllPhase::PickDownload, "download")) return;
    if (autoOrPrompt(needGame, installAllChosenGameFiles_, InstallAllPhase::PickGameFiles, "game")) return;
    if (autoOrPrompt(needData, installAllChosenDataFiles_, InstallAllPhase::PickDataFiles, "data")) return;

    installAllStartQueue();
}

void FullCatalogScreen::installAllStartQueue() {
    if (installAllItemIndex_ < 0 || installAllItemIndex_ >= (int)catalogView().size()) {
        closeInstallAllWizard(true);
        return;
    }
    const CatalogItem& item = catalogView()[installAllItemIndex_];
    installAllQueue_.clear();
    installAllQueueLabels_.clear();

    auto pushIdx = [&](int di, const char* label) {
        if (di < 0 || di >= (int)item.linkDetails.size()) return;
        installAllQueue_.push_back(item.linkDetails[di]);
        installAllQueueLabels_.push_back(label);
    };
    pushIdx(installAllChosenDownload_, "App (VPK)");
    pushIdx(installAllChosenGameFiles_, "Game Files");
    pushIdx(installAllChosenDataFiles_, "Data Files");

    if (installAllQueue_.empty()) {
        showToast("Nothing to install", 1600);
        closeInstallAllWizard(true);
        return;
    }

    // Pre-flight free-space estimate (2.1x sum of known sizes)
    uint64_t needSum = 0;
    for (const auto& l : installAllQueue_) {
        uint64_t b = parseUiSizeBytes(l.size);
        if (b == 0) b = parseUiSizeBytes(item.size);
        needSum += b;
    }
    if (needSum > 0) {
        // Soft check only in log; InstallController enforces hard check per job
        diagnostics::log(std::string("[UI] Install All estimated payload bytes=") + std::to_string(needSum));
    }

    installAllQueueIndex_ = 0;
    installAllLastOutcome_ = -1;
    installAllPhase_ = InstallAllPhase::Running;
    diagnostics::log(std::string("[UI] Install All start steps=") + std::to_string(installAllQueue_.size())
                     + " app=" + item.name);

    if (!linkAction_) {
        showToast("Install unavailable", 1600);
        closeInstallAllWizard(true);
        return;
    }
    const CatalogLink& first = installAllQueue_[0];
    diagnostics::log(std::string("[UI] Install All step 1/") + std::to_string(installAllQueue_.size())
                     + " " + installAllQueueLabels_[0] + " url=" + first.url);
    if (!linkAction_(item, first)) {
        showToast("Could not start install", 1800);
        closeInstallAllWizard(true);
    }
}

void FullCatalogScreen::installAllTryAdvanceFromProgress(int outcome) {
    if (installAllPhase_ != InstallAllPhase::Running) return;
    if (outcome == installAllLastOutcome_) return;
    const int prev = installAllLastOutcome_;
    installAllLastOutcome_ = outcome;

    if (outcome == 2 || outcome == 3) {
        // Failed or cancelled — stop queue; keep result panel for user
        diagnostics::log(std::string("[UI] Install All stopped at step ")
                         + std::to_string(installAllQueueIndex_ + 1)
                         + (outcome == 3 ? " (cancelled)" : " (failed)"));
        installAllQueue_.clear();
        installAllQueueLabels_.clear();
        installAllPhase_ = InstallAllPhase::Hidden;
        installAllItemIndex_ = -1;
        return;
    }

    if (outcome != 1) return;
    // Completed current step
    if (installAllQueueIndex_ + 1 < installAllQueue_.size()) {
        // Auto-ack success and start next (do not wait for user)
        if (installAcknowledge_) installAcknowledge_();
        ++installAllQueueIndex_;
        installAllLastOutcome_ = -1;
        if (installAllItemIndex_ < 0 || installAllItemIndex_ >= (int)catalogView().size()) {
            closeInstallAllWizard(true);
            return;
        }
        const CatalogItem& item = catalogView()[installAllItemIndex_];
        const CatalogLink& next = installAllQueue_[installAllQueueIndex_];
        diagnostics::log(std::string("[UI] Install All step ")
                         + std::to_string(installAllQueueIndex_ + 1) + "/"
                         + std::to_string(installAllQueue_.size()) + " "
                         + installAllQueueLabels_[installAllQueueIndex_]
                         + " url=" + next.url);
        if (!linkAction_ || !linkAction_(item, next)) {
            showToast("Install All stopped — next step failed to start", 2200);
            closeInstallAllWizard(true);
        }
        return;
    }

    // Last step completed — keep success panel; toast when user dismisses
    installAllFinishedToast_ = true;
    diagnostics::log("[UI] Install All finished all steps");
    // Clear running phase so further outcome updates don't re-enter
    installAllPhase_ = InstallAllPhase::Hidden;
    installAllQueue_.clear();
    installAllItemIndex_ = -1;
    (void)prev;
}

void FullCatalogScreen::drawInstallAllOverlay() {
    if (installAllPhase_ == InstallAllPhase::Hidden || installAllPhase_ == InstallAllPhase::Running) return;
    if (installAllItemIndex_ < 0 || installAllItemIndex_ >= (int)catalogView().size()) return;
    const CatalogItem& item = catalogView()[installAllItemIndex_];

    // Dim full screen
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 180));

    const int ow = 680, oh = 400;
    const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
    vita2d_draw_rectangle(ox, oy, ow, oh, SURFACE2);
    vita2d_draw_rectangle(ox, oy, ow, 3, ACCENT);
    vita2d_draw_rectangle(ox, oy + oh - 1, ow, 1, BORDER);
    vita2d_draw_rectangle(ox, oy, 1, oh, BORDER);
    vita2d_draw_rectangle(ox + ow - 1, oy, 1, oh, BORDER);

    if (installAllPhase_ == InstallAllPhase::Confirm) {
        vita2d_pgf_draw_text(font_, ox + 24, oy + 40, ACCENT, 0.96f, "Install All");
        vita2d_pgf_draw_text(font_, ox + 24, oy + 76, WHITE, 0.72f, ellipsize(item.name, 42).c_str());
        const char* lines[] = {
            "This installs the homebrew from scratch:",
            "1) App (VPK)  2) Game Files  3) Data Files",
            "You will pick one download source per step when needed.",
            "If you only want to update the app, use the VPK button instead.",
        };
        int ty = oy + 112;
        for (const char* ln : lines) {
            vita2d_pgf_draw_text(font_, ox + 24, ty, TEXT, 0.66f, ln);
            ty += 28;
        }
        const int bw = 220, bh = 44;
        const int by = oy + oh - 60;
        const int bxOk = ox + 28;
        const int bxCancel = ox + ow - 28 - bw;
        const bool fOk = installAllFocus_ == 0;
        const bool fCancel = installAllFocus_ == 1;
        vita2d_draw_rectangle(bxOk, by, bw, bh, fOk ? ACCENT : SURFACE2);
        {
            const char* lab = "Continue";
            const float sc = 0.72f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            vita2d_pgf_draw_text(font_, bxOk + (bw - tw) / 2, by + 30, fOk ? BG : WHITE, sc, lab);
        }
        vita2d_draw_rectangle(bxCancel, by, bw, bh, fCancel ? ACCENT : SURFACE2);
        {
            const char* lab = "Cancel";
            const float sc = 0.72f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            vita2d_pgf_draw_text(font_, bxCancel + (bw - tw) / 2, by + 30, fCancel ? BG : WHITE, sc, lab);
        }
        vita2d_pgf_draw_text(font_, ox + 24, oy + oh - 84, DIM, 0.58f, "D-Pad: move   X: select   O: cancel");
        return;
    }

    const char* title = "Choose download";
    if (installAllPhase_ == InstallAllPhase::PickGameFiles) title = "Choose Game Files";
    else if (installAllPhase_ == InstallAllPhase::PickDataFiles) title = "Choose Data Files";
    vita2d_pgf_draw_text(font_, ox + 24, oy + 40, ACCENT, 0.96f, title);
    vita2d_pgf_draw_text(font_, ox + 24, oy + 70, DIM, 0.60f, "Same content — pick one mirror / source");

    const int listTop = oy + 86;
    const int rowH = LINK_ROW_H + 6;
    const int maxVis = 5;
    int start = 0;
    if (installAllFocus_ >= maxVis) start = installAllFocus_ - maxVis + 1;
    for (int n = 0; n < maxVis; ++n) {
        const int idx = start + n;
        if (idx >= (int)installAllOptions_.size()) break;
        const int di = installAllOptions_[idx];
        const CatalogLink& l = item.linkDetails[di];
        const int ry = listTop + n * (rowH + 6);
        const bool f = (idx == installAllFocus_);
        const int rx = ox + 20, rw = ow - 40;
        // Match detail download rows: dark SURFACE2 body (not full green fill)
        vita2d_draw_rectangle(rx, ry, rw, rowH, SURFACE2);
        vita2d_draw_rectangle(rx, ry, rw, 1, f ? ACCENT : BORDER);
        if (f) {
            // Focus indicator: left neon strip + soft top/bottom edge
            vita2d_draw_rectangle(rx, ry, 3, rowH, ACCENT);
            vita2d_draw_rectangle(rx, ry + rowH - 1, rw, 1, ACCENT);
        }
        std::string name = l.name.empty() ? l.type : l.name;
        const std::string sizeLabel = l.size.empty() ? "" : l.size;
        const int badgeW = l.recommended ? 96 : 0;
        const unsigned mc = WHITE;
        { const int titleMaxW = std::max(40, rw - 24 - badgeW - 8); drawMarqueeText(font_, rx + 12, ry + 18, titleMaxW, mc, 0.80f, name, f); }
        std::string meta = l.type.empty() ? "Download" : l.type;
        if (!sizeLabel.empty()) meta += "  •  " + sizeLabel;
        if (f) meta += "  •  X: select";
        else meta += "  •  X";
        vita2d_pgf_draw_text(font_, rx + 12, ry + 36, DIM, 0.66f, ellipsize(meta, badgeW ? 28 : 44).c_str());
        if (l.recommended) {
            const int bx = rx + rw - badgeW - 8, by = ry + 9;
            vita2d_pgf_draw_text(font_, bx, ry + 17, f ? BG : ACCENT, 0.58f, "Recommended");
        }
    }
    vita2d_pgf_draw_text(font_, ox + 24, oy + oh - 28, DIM, 0.58f, "D-Pad: move   X: select   O: cancel");
}

void FullCatalogScreen::drawLoadingOverlay(){
// Catalog load/download at startup: full-screen brand image + progress (not used for installs).
if (catalogSplashAlpha_ > 0.01f && !installProgressActive_) {
    const unsigned a = (unsigned)(catalogSplashAlpha_ * 255.f);
    if (a > 255) { /* clamp */ }
    const unsigned tint = RGBA8(255, 255, 255, a > 255 ? 255 : a);
    if (catalogLoadingTex_) {
        const float tw = (float)vita2d_texture_get_width(catalogLoadingTex_);
        const float th = (float)vita2d_texture_get_height(catalogLoadingTex_);
        const float sx = (tw > 1.f) ? (SCREEN_W / tw) : 1.f;
        const float sy = (th > 1.f) ? (SCREEN_H / th) : 1.f;
        vita2d_draw_texture_tint_scale(catalogLoadingTex_, 0.f, 0.f, sx, sy, tint);
    } else {
        vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0x0A, 0x0A, 0x0A, a > 255 ? 255 : a));
    }
    // Bottom progress panel — clear hierarchy for startup / self-update / catalogs
    const int stripH = 148;
    const int stripY = SCREEN_H - stripH;
    const int barX = 48, barW = SCREEN_W - 96, barH = 16;
    const int barY = SCREEN_H - 40;
    const unsigned panelA = (unsigned)(catalogSplashAlpha_ * 230.f);
    const unsigned ta = (unsigned)(catalogSplashAlpha_ * 255.f);
    vita2d_draw_rectangle(0, stripY, SCREEN_W, stripH, RGBA8(0x08, 0x08, 0x0A, panelA > 255 ? 255 : panelA));
    vita2d_draw_rectangle(0, stripY, SCREEN_W, 3, withAlpha(ACCENT, ta > 255 ? 255 : ta));

    std::string phase = catalogLoadingLabel_.empty() ? "Startup" : catalogLoadingLabel_;
    vita2d_pgf_draw_text(font_, barX, stripY + 28, ACCENT, 0.90f, ellipsize(phase, 40).c_str());

    std::string detail = catalogLoadingMessage_.empty() ? "Please wait..." : catalogLoadingMessage_;
    vita2d_pgf_draw_text(font_, barX, stripY + 52, WHITE, 0.74f, ellipsize(detail, 78).c_str());

    if (catalogLoadingTotal_ > 0) {
        char sizeLine[96];
        if (catalogLoadingTotal_ >= 8192ULL) {
            const double curMb = (double)catalogLoadingCurrent_ / (1024.0 * 1024.0);
            const double totMb = (double)catalogLoadingTotal_ / (1024.0 * 1024.0);
            sceClibSnprintf(sizeLine, sizeof(sizeLine), "%.2f MB  /  %.2f MB", curMb, totMb);
        } else {
            sceClibSnprintf(sizeLine, sizeof(sizeLine), "%llu  /  %llu",
                (unsigned long long)catalogLoadingCurrent_,
                (unsigned long long)catalogLoadingTotal_);
        }
        vita2d_pgf_draw_text(font_, barX, stripY + 74, TEXT, 0.52f, sizeLine);
    }

    vita2d_draw_rectangle(barX, barY, barW, barH, SURFACE);
    vita2d_draw_rectangle(barX, barY, barW, 1, ACCENT_SOFT);
    vita2d_draw_rectangle(barX, barY + barH - 1, barW, 1, ACCENT_SOFT);
    float pct = 0.f;
    if (catalogLoadingTotal_ > 0) {
        pct = (float)catalogLoadingCurrent_ / (float)catalogLoadingTotal_;
        if (pct < 0.f) pct = 0.f;
        if (pct > 1.f) pct = 1.f;
    } else {
        const float t = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1200) / 1200.f;
        pct = 0.15f + 0.35f * (t < 0.5f ? (t * 2.f) : (2.f - t * 2.f));
    }
    const int fill = (int)(barW * pct);
    if (fill > 0) vita2d_draw_rectangle(barX, barY, fill, barH, ACCENT);
    char pctBuf[32];
    if (catalogLoadingTotal_ > 0)
        sceClibSnprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)(pct * 100.f + 0.5f));
    else
        sceClibSnprintf(pctBuf, sizeof(pctBuf), "...");
    const int pctW = vita2d_pgf_text_width(font_, 0.58f, pctBuf);
    vita2d_pgf_draw_text(font_, barX + barW - pctW, barY - 18, WHITE, 0.58f, pctBuf);
    return;
}


const unsigned RED=RGBA8(0xE0,0x32,0x32,255), GREEN=RGBA8(0x3B,0xD9,0x60,255), BLACK=RGBA8(0,0,0,255);
const int w=700,h=440,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;
vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,120));
vita2d_draw_rectangle(x,y,w,h,PANEL);
const unsigned edge=(installOutcome_==2)?RED:((installOutcome_==1)?GREEN:ACCENT);
vita2d_draw_rectangle(x,y,w,3,edge);
vita2d_draw_rectangle(x,y+3,3,h-6,edge);
vita2d_draw_rectangle(x+w-3,y+3,3,h-6,BORDER);
vita2d_draw_rectangle(x,y+h-3,w,3,BORDER);
vita2d_pgf_draw_text(font_,x+28,y+36,edge,.68f,"PSVitaAlive");

if(installOutcome_==1){
  const bool zipExtract =
      !installLiveAreaOk_ &&
      installResultTitleId_.empty() &&
      !installResultPath_.empty() &&
      (installResultPath_.find("ux0:app/") != 0);
  if (zipExtract) {
    vita2d_pgf_draw_text(font_,x+28,y+80,GREEN,1.05f,"ZIP extraction complete");
    std::string file=installProgressFile_.empty()?"(archive)":ellipsize(installProgressFile_,70);
    vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("File: "+file).c_str());
    vita2d_pgf_draw_text(font_,x+28,y+150,TEXT,.62f,("Extracted to: "+ellipsize(installResultPath_,58)).c_str());
    vita2d_pgf_draw_text(font_,x+28,y+182,DIM,.56f,"No LiveArea bubble — files only");
    if(!installProgressMessage_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+214,DIM,.54f,ellipsize(installProgressMessage_,78).c_str());
  } else {
    vita2d_pgf_draw_text(font_,x+28,y+80,GREEN,1.05f,"Installation complete");
    std::string file=installProgressFile_.empty()?"(file)":ellipsize(installProgressFile_,70);
    vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("File: "+file).c_str());
    if(!installResultTitleId_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+144,TEXT,.60f,("Title ID: "+installResultTitleId_).c_str());
    if(!installResultPath_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.60f,("Path: "+ellipsize(installResultPath_,62)).c_str());
    if(installLiveAreaOk_)
      vita2d_pgf_draw_text(font_,x+28,y+200,GREEN,.72f,"LiveArea: OK — app bubble verified");
    else if(!installResultPath_.empty() && installResultPath_.find("ux0:app/")==0)
      vita2d_pgf_draw_text(font_,x+28,y+200,RGBA8(0xFF,0xC0,0x40,255),.68f,"LiveArea: not confirmed yet");
    else
      vita2d_pgf_draw_text(font_,x+28,y+200,TEXT,.62f,"LiveArea: N/A for this content");
    if(!installProgressMessage_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+232,DIM,.54f,ellipsize(installProgressMessage_,78).c_str());
  }

    const int by2=y+300,bw2=280,bh2=40;
  vita2d_draw_rectangle(x+28,by2,bw2,bh2,GREEN);
  vita2d_pgf_draw_text(font_,x+100,by2+26,BLACK,.62f,"O  Continue");
  // Auto-close countdown bar only (no text)
  {
    const int barX = x + 28;
    const int barW = w - 56;
    const int barY = y + h - 22;
    const int barH = 6;
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    const uint64_t totalMs = 8000;
    uint64_t rem = installResultAutoCloseMs_;
    if (rem > totalMs) rem = totalMs;
    const int fill = (rem > 0 && totalMs > 0)
        ? (int)((barW * rem) / totalMs)
        : 0;
    if (fill > 0) vita2d_draw_rectangle(barX, barY, fill, barH, GREEN);
  }
  return;
}

if(installOutcome_==3){
  const unsigned amber = RGBA8(0xE0,0xA0,0x30,255);
  vita2d_pgf_draw_text(font_,x+28,y+80,amber,1.05f,"Download cancelled");
  std::string file=installProgressFile_.empty()?"(file)":ellipsize(installProgressFile_,70);
  vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("File: "+file).c_str());
  std::string msg=installProgressMessage_.empty()?"Download cancelled":installProgressMessage_;
  // Prefer a friendly title-aligned message
  if(msg=="Download cancelled"||msg=="Installation cancelled"||msg=="Cancelling download...")
    msg="You cancelled this download. Incomplete files were removed.";
  vita2d_pgf_draw_text(font_,x+28,y+160,TEXT,.60f,ellipsize(msg,78).c_str());
  vita2d_pgf_draw_text(font_,x+28,y+200,DIM,.54f,"No error was reported. You can try again anytime.");
  const int by2=y+300,bh2=40;
  const int bwClose=280;
  const int bxClose=x+(w-bwClose)/2;
  vita2d_draw_rectangle(bxClose,by2,bwClose,bh2,SURFACE2);
  vita2d_draw_rectangle(bxClose,by2,bwClose,1,BORDER);
  {
    const char* clab = "O  Close";
    const float sc = 0.64f;
    const int tw = vita2d_pgf_text_width(font_, sc, clab);
    vita2d_pgf_draw_text(font_, bxClose + (bwClose - tw) / 2, by2 + 27, WHITE, sc, clab);
  }
  vita2d_pgf_draw_text(font_,x+28,y+h-16,DIM,.48f,"Circle: close");
  return;
}

if(installOutcome_==2){
  vita2d_pgf_draw_text(font_,x+28,y+80,RED,1.05f,"Installation failed");
  std::string file=installProgressFile_.empty()?"(file)":ellipsize(installProgressFile_,70);
  vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("File: "+file).c_str());
  vita2d_pgf_draw_text(font_,x+28,y+152,RED,.68f,"Reason:");
  std::string err=installProgressMessage_.empty()?"Unknown error":installProgressMessage_;
  const bool spaceErr = isNonReportableInstallError(err);
  vita2d_pgf_draw_text(font_,x+28,y+180,TEXT,.60f,ellipsize(err,78).c_str());
  if(err.size()>78)
    vita2d_pgf_draw_text(font_,x+28,y+202,TEXT,.58f,ellipsize(err.substr(70),78).c_str());
  if (spaceErr) {
    vita2d_pgf_draw_text(font_,x+28,y+236,DIM,.54f,"Free up space on ux0 and try again.");
    vita2d_pgf_draw_text(font_,x+28,y+258,DIM,.52f,"This is not a bug — Report is disabled for space errors.");
  } else {
    vita2d_pgf_draw_text(font_,x+28,y+236,DIM,.54f,"Check free space, format, and session.log");
    vita2d_pgf_draw_text(font_,x+28,y+258,DIM,.52f,"ux0:data/psvitaalive/logs/session.log");
  }
  const int by2=y+300,bh2=40;
  if (spaceErr) {
    // Only Close — space issues are expected user-side, not bug reports
    const int bwClose=280;
    const int bxClose=x+(w-bwClose)/2;
    vita2d_draw_rectangle(bxClose,by2,bwClose,bh2,SURFACE2);
    vita2d_draw_rectangle(bxClose,by2,bwClose,1,BORDER);
    {
      const char* clab = "O  Close";
      const float sc = 0.64f;
      const int tw = vita2d_pgf_text_width(font_, sc, clab);
      vita2d_pgf_draw_text(font_, bxClose + (bwClose - tw) / 2, by2 + 27, WHITE, sc, clab);
    }
    vita2d_pgf_draw_text(font_,x+28,y+h-16,DIM,.48f,"Circle: close");
  } else {
    const int bwReport=200, bwClose=200;
    const int bxReport=x+28, bxClose=x+w-28-bwClose;
    // Idle/sending/fail = red; success only = green (matches footer Report chip)
    const unsigned reportCol = (reportUiState_==2) ? GREEN : RED;
    const unsigned reportText = (reportUiState_==2) ? RGBA8(0,0,0,255) : WHITE;
    vita2d_draw_rectangle(bxReport,by2,bwReport,bh2,reportCol);
    if (reportUiState_==1) {
      const int barX = bxReport + 16, barW = bwReport - 32, barH = 10;
      const int barY = by2 + (bh2 - barH) / 2;
      vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
      const float tt = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1000) / 1000.f;
      const float phase = tt < 0.5f ? (tt * 2.f) : (2.f - tt * 2.f);
      const int fillW = (int)(barW * (0.25f + 0.55f * phase));
      if (fillW > 0) vita2d_draw_rectangle(barX, barY, fillW, barH, RGBA8(0,0,0,255));
    } else {
      const char* lab = "Report";
      if (reportUiState_==2) lab = "Sent";
      else if (reportUiState_==3) lab = reportUiMsg_[0] ? reportUiMsg_ : "Failed";
      const float sc = 0.64f;
      const int tw = vita2d_pgf_text_width(font_, sc, lab);
      vita2d_pgf_draw_text(font_, bxReport + (bwReport - tw) / 2, by2 + 27, reportText, sc, lab);
    }
    vita2d_draw_rectangle(bxClose,by2,bwClose,bh2,RED);
    {
      const char* clab = "O  Close";
      const float sc = 0.64f;
      const int tw = vita2d_pgf_text_width(font_, sc, clab);
      vita2d_pgf_draw_text(font_, bxClose + (bwClose - tw) / 2, by2 + 27, WHITE, sc, clab);
    }
    vita2d_pgf_draw_text(font_,x+28,y+h-16,DIM,.48f,"Square: report   Circle: close");
  }
  return;
}

if(catalogLoading_){
  vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,catalogLoadingLabel_.empty()?"Loading catalog":catalogLoadingLabel_.c_str());
  vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.60f,catalogLoadingMessage_.empty()?"Preparing...":catalogLoadingMessage_.c_str());
  uint64_t pct=catalogLoadingTotal_?std::min<uint64_t>(100,(catalogLoadingCurrent_*100)/catalogLoadingTotal_):0;
  int bx=x+28,by=y+140,bw=w-56,bh=12;
  vita2d_draw_rectangle(bx,by,bw,bh,BORDER);
  vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);
  char st[160];
  sceClibSnprintf(st,sizeof(st),"%llu%%  %llu / %llu",(unsigned long long)pct,(unsigned long long)catalogLoadingCurrent_,(unsigned long long)catalogLoadingTotal_);
  vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,st);
  return;
}

{
  const char* title = "Installing";
  if (installProgressStage_ == "BGDL") title = "Preparing download";
  else if (installProgressStage_ == "Downloading" || installProgressStage_ == "Cancelling" || installProgressStage_.empty())
    title = "Downloading";
  else if (installProgressStage_ == "Installing") title = "Installing";
  else if (!installProgressStage_.empty()) title = installProgressStage_.c_str();
  vita2d_pgf_draw_text(font_,x+28,y+72,WHITE,1.12f,title);
}
std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,68);
vita2d_pgf_draw_text(font_,x+28,y+106,TEXT,.72f,file.c_str());
const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);
const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;
int bx=x+28,by=y+138,bw=w-56,bh=14;
vita2d_draw_rectangle(bx,by,bw,bh,BORDER);
const bool msgRetry =
    installProgressMessage_.find("retrying") != std::string::npos ||
    installProgressMessage_.find("Retry") != std::string::npos ||
    installProgressMessage_.find("retry") != std::string::npos;
const bool stageExtract =
    installProgressStage_.find("Extract") != std::string::npos ||
    installProgressStage_.find("extract") != std::string::npos ||
    installProgressStage_.find("ZIP") != std::string::npos ||
    installProgressStage_.find("Unzip") != std::string::npos;
const bool stageInstall =
    installProgressStage_ == "Installing" ||
    installProgressStage_.find("Install") != std::string::npos ||
    installProgressStage_.find("Promote") != std::string::npos ||
    installProgressStage_.find("promote") != std::string::npos;
const bool stageDownload =
    !stageExtract && !stageInstall && (
    installProgressStage_ == "Downloading" ||
    installProgressStage_ == "BGDL" ||
    installProgressStage_ == "Cancelling" ||
    installProgressStage_.empty());
// Sliding bar while connecting/retrying (download) or waiting with no bytes yet.
// Install/extract use the bar too when progress is unknown, but never the "server" copy.
const bool indeterminate =
    msgRetry ||
    total == 0 ||
    (current == 0 && installProgressSpeed_ == 0 && (stageDownload || stageInstall || stageExtract));
if (indeterminate) {
  // Bounce a segment left↔right (same idea as catalog loading strip).
  const float t = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1400) / 1400.f;
  const float u = (t < 0.5f) ? (t * 2.f) : (2.f - t * 2.f);
  const int pulse = std::max(28, (int)(bw * 0.28f));
  const int off = (int)((bw - pulse) * u);
  if (pulse > 0) vita2d_draw_rectangle(bx + off, by, pulse, bh, ACCENT);
} else {
  vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);
}
char stats[220];
if (indeterminate) {
  sceClibSnprintf(stats,sizeof(stats),"…  %s / %s  •  %s/s",
      formatBytes(current).c_str(),
      total?formatBytes(total).c_str():"?",
      formatBytes(installProgressSpeed_).c_str());
} else {
  sceClibSnprintf(stats,sizeof(stats),"%llu%%  %s / %s  •  %s/s",(unsigned long long)pct,formatBytes(current).c_str(),total?formatBytes(total).c_str():"?",formatBytes(installProgressSpeed_).c_str());
}
vita2d_pgf_draw_text(font_,x+28,y+172,TEXT,.66f,stats);
uint64_t eta=0;if(installProgressSpeed_>0&&total>current)eta=(total-current)/installProgressSpeed_;
char info[180];
if (indeterminate)
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: —");
else
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: %s",formatEta(eta).c_str());
vita2d_pgf_draw_text(font_,x+28,y+198,ACCENT,.70f,info);
if(!installProgressMessage_.empty())vita2d_pgf_draw_text(font_,x+28,y+224,DIM,.60f,ellipsize(installProgressMessage_,78).c_str());
// Footer must match the real phase — never "Connecting..." during install/extract.
const char* waitHint = nullptr;
if (msgRetry && stageDownload)
  waitHint = "Retrying connection — please wait. This is not an error.";
else if (msgRetry && (stageInstall || stageExtract))
  waitHint = "Retrying this step — please wait. This is not an error.";
else if (stageExtract)
  waitHint = "Extracting files — large archives can take a while. This is not an error.";
else if (stageInstall)
  waitHint = "Installing on the console — please wait. This is not frozen.";
else if (indeterminate && stageDownload)
  waitHint = "Connecting to the server — may take a moment. This is not an error.";
else if (stageDownload)
  waitHint = "Speed depends on your internet connection — please be patient.";
else
  waitHint = "Please wait — this step can take a moment.";
vita2d_pgf_draw_text(font_,x+28,y+248,
    (stageDownload && !indeterminate && !msgRetry) ? DIM : ACCENT,
    .58f, waitHint);
// High-visibility lock banner — larger type for Vita screen readability.
{
  const int bx = x + 16, by = y + 268, bw = w - 32, bh = 52;
  vita2d_draw_rectangle(bx, by, bw, bh, RGBA8(0x40, 0x10, 0x10, 255));
  vita2d_draw_rectangle(bx, by, bw, 2, RED);
  vita2d_draw_rectangle(bx, by + bh - 2, bw, 2, RED);
  vita2d_draw_rectangle(bx, by, 4, bh, RED);
  vita2d_draw_rectangle(bx + bw - 4, by, 4, bh, RED);
  vita2d_pgf_draw_text(font_, bx + 14, by + 20, RED, .64f,
      "LOCKED: PS button and power menu disabled");
  vita2d_pgf_draw_text(font_, bx + 14, by + 40, WHITE, .58f,
      "Screen stays ON. Do NOT force power-off until finished.");
}
const int by2=y+332,bw2=380,bh2=42;
vita2d_draw_rectangle(x+28,by2,bw2,bh2,SURFACE2);
vita2d_draw_rectangle(x+28,by2,bw2,1,BORDER);
{
  const char* clab = "CIRCLE  CANCEL DOWNLOAD";
  const float csc = 0.68f;
  const int ctw = vita2d_pgf_text_width(font_, csc, clab);
  vita2d_pgf_draw_text(font_, x + 28 + (bw2 - ctw) / 2, by2 + 28, WHITE, csc, clab);
}
vita2d_pgf_draw_text(font_,x+28,y+h-14,DIM,.52f,
    "Only CIRCLE works  |  PS / power menu blocked  |  Screen forced on");
}

void drawFooterBar(vita2d_pgf* font, const char* leftHints) {
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, 2, ACCENT);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H + 2, SCREEN_W, 1, ACCENT_SOFT);
    if (leftHints && font)
        vita2d_pgf_draw_text(font, 12, SCREEN_H - 14, TEXT, 0.64f, leftHints);
    if (!font) return;

    const Ux0SpaceInfo sp = queryUx0Space();
    // Wider panel + larger type for readability on real Vita screens.
    const int panelW = 220;
    const int panelH = FOOTER_H - 4;
    const int panelX = SCREEN_W - panelW - 6;
    const int panelY = SCREEN_H - FOOTER_H + 2;
    vita2d_draw_rectangle(panelX, panelY, panelW, panelH, SURFACE);
    vita2d_draw_rectangle(panelX, panelY, 3, panelH, ACCENT);

    if (!sp.ok) {
        vita2d_pgf_draw_text(font, panelX + 12, panelY + 22, DIM, 0.68f, "ux0 n/a");
        return;
    }
    // Line 1: UX0 + free space (primary info)
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 18, ACCENT, 0.70f, "UX0");
    char line[48];
    sceClibSnprintf(line, sizeof(line), "%s free", formatBytesShort(sp.freeBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 52, panelY + 18, WHITE, 0.70f, line);
    // Line 2: total capacity
    sceClibSnprintf(line, sizeof(line), "of %s total", formatBytesShort(sp.totalBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 34, TEXT, 0.62f, line);

    const int barX = panelX + 10, barY = panelY + panelH - 10, barW = panelW - 20, barH = 6;
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    float used = 0.f;
    if (sp.totalBytes > 0)
        used = 1.f - (float)((double)sp.freeBytes / (double)sp.totalBytes);
    if (used < 0.f) used = 0.f;
    if (used > 1.f) used = 1.f;
    const unsigned fill = used > 0.90f ? RGBA8(0xE0, 0x32, 0x32, 255)
                        : (used > 0.75f ? RGBA8(0xFF, 0xB0, 0x20, 255) : ACCENT);
    vita2d_draw_rectangle(barX, barY, std::max(1, (int)(barW * used)), barH, fill);
}
void FullCatalogScreen::drawFullCatalog(){vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);drawCatalogPanel(0,HEADER_H+TABS_H,SCREEN_W,SCREEN_H-HEADER_H-TABS_H-FOOTER_H,false);drawFooterBar(font_, "D-Pad: Nav   X: Detail   △: Search   SELECT: Settings   L/R: Catalog   START: Exit");drawReportChip();drawNewsChip();if(catalogLoading_||installProgressActive_||catalogSplashAlpha_>0.01f)drawLoadingOverlay();if(newsVisible_)drawNewsOverlay();if(reportConfirmVisible_)drawReportConfirmOverlay();if(dataRequestConfirmVisible_)drawDataRequestConfirmOverlay();if(installAllPhase_!=InstallAllPhase::Hidden&&installAllPhase_!=InstallAllPhase::Running)drawInstallAllOverlay();if(!catalogError_.empty())vita2d_pgf_draw_text(font_,18,HEADER_H+TABS_H+26,ACCENT,.66f,catalogError_.c_str());drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawSplitDetail(){vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H,lw=SCREEN_W/2;drawCatalogPanel(0,top,lw,hh,true);drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_draw_rectangle(lw-1,top,2,hh,BORDER);drawFooterBar(font_, state_.activePanel==UiPanel::Catalog?"PANEL: LIST  |  → Detail   D-Pad: Navigate   O: Back   L/R: Catalog":"PANEL: DETAIL  |  ← List   D-Pad: Scroll   △: Links   X: Action   O: Back");drawReportChip();drawNewsChip();if(catalogLoading_||installProgressActive_||catalogSplashAlpha_>0.01f)drawLoadingOverlay();if(newsVisible_)drawNewsOverlay();if(reportConfirmVisible_)drawReportConfirmOverlay();if(dataRequestConfirmVisible_)drawDataRequestConfirmOverlay();if(installAllPhase_!=InstallAllPhase::Hidden&&installAllPhase_!=InstallAllPhase::Running)drawInstallAllOverlay();drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawOpeningDetail(){float p=transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,rw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawClosingDetail(){float p=1.0f-transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::draw(){switch(state_.mode){case UiMode::FULL_CATALOG:drawFullCatalog();break;case UiMode::OPENING_DETAIL:drawOpeningDetail();break;case UiMode::SPLIT_DETAIL:drawSplitDetail();break;case UiMode::CLOSING_DETAIL:drawClosingDetail();break;case UiMode::SETTINGS:drawSettings();break;}}bool FullCatalogScreen::updateAndDraw(){
    if(!ready_)return false;
    pollReportWorker();
    pollDataRequestWorker();
    flushDeferredTextureFrees();
    if(catalogSwitchCooldownFrames_>0)--catalogSwitchCooldownFrames_;
    // Expire toast
    if(toastExpiresMs_!=0){
        const uint64_t now=sceKernelGetProcessTimeWide()/1000ULL;
        if(now>=toastExpiresMs_){toastMessage_.clear();toastExpiresMs_=0;}
    }
    handleInput();
    handleTouch();
    updateTransition();
    pollSelfUpdateProgress();
    updateAnimations();
    if(catalogSwitchCooldownFrames_==0)prepareVisibleTextures();
    draw();
    return !state_.requestExit;
}
} // namespace psvitaalive::ui
