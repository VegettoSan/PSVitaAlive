#include "ui/full_catalog_screen.hpp"
#include "ui/news_markdown.hpp"
#include "installer/app_settings.hpp"
#include "installer/plugin_detector.hpp"
#include "installer/tai_config_editor.hpp"
#include "localization/localization.hpp"
#include "ui/ui_font.hpp"
#include "update/update_checker.hpp"

#ifndef PSVITAALIVE_VERSION
#define PSVITAALIVE_VERSION "01.00"
#endif
#include "diagnostic_logger.hpp"
#include "network/error_reporter.hpp"
#include "network/http_client.hpp"
#include "network/news_manager.hpp"
#include <psp2/ctrl.h>
#include <psp2/power.h>
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
        if (want == "dlc" && (t == "dlcs" || t == "downloadable content")) return true;
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

/** Full UI palette snapshot for smooth theme cross-fades. */
struct ThemePalette {
    unsigned bg = 0, surface = 0, surface2 = 0, panel = 0, border = 0;
    unsigned text = 0, dim = 0, white = 0, silver = 0;
    unsigned accent = 0, accentDim = 0, accentSoft = 0;
};

ThemePalette g_themeFrom{};
ThemePalette g_themeTo{};
bool g_themeBlending = false;
uint64_t g_themeBlendStartMs = 0;
constexpr float kThemeBlendMs = 420.f; // ~0.42s ease

ThemePalette captureThemePalette() {
    ThemePalette p;
    p.bg = BG; p.surface = SURFACE; p.surface2 = SURFACE2; p.panel = PANEL;
    p.border = BORDER; p.text = TEXT; p.dim = DIM; p.white = WHITE; p.silver = SILVER;
    p.accent = ACCENT; p.accentDim = ACCENT_DIM; p.accentSoft = ACCENT_SOFT;
    return p;
}

void applyThemePalette(const ThemePalette& p) {
    BG = p.bg; SURFACE = p.surface; SURFACE2 = p.surface2; PANEL = p.panel;
    BORDER = p.border; TEXT = p.text; DIM = p.dim; WHITE = p.white; SILVER = p.silver;
    ACCENT = p.accent; ACCENT_DIM = p.accentDim; ACCENT_SOFT = p.accentSoft;
}

unsigned lerpColorU(unsigned a, unsigned b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    // vita2d RGBA8 layout: A<<24 | B<<16 | G<<8 | R
    const int ar = (int)(a & 0xFFu), ag = (int)((a >> 8) & 0xFFu), ab = (int)((a >> 16) & 0xFFu), aa = (int)((a >> 24) & 0xFFu);
    const int br = (int)(b & 0xFFu), bg = (int)((b >> 8) & 0xFFu), bb = (int)((b >> 16) & 0xFFu), ba = (int)((b >> 24) & 0xFFu);
    const int r = ar + (int)((br - ar) * t + 0.5f);
    const int g = ag + (int)((bg - ag) * t + 0.5f);
    const int bl = ab + (int)((bb - ab) * t + 0.5f);
    const int al = aa + (int)((ba - aa) * t + 0.5f);
    return RGBA8((unsigned)r, (unsigned)g, (unsigned)bl, (unsigned)al);
}

ThemePalette lerpThemePalette(const ThemePalette& a, const ThemePalette& b, float t) {
    ThemePalette o;
    o.bg = lerpColorU(a.bg, b.bg, t);
    o.surface = lerpColorU(a.surface, b.surface, t);
    o.surface2 = lerpColorU(a.surface2, b.surface2, t);
    o.panel = lerpColorU(a.panel, b.panel, t);
    o.border = lerpColorU(a.border, b.border, t);
    o.text = lerpColorU(a.text, b.text, t);
    o.dim = lerpColorU(a.dim, b.dim, t);
    o.white = lerpColorU(a.white, b.white, t);
    o.silver = lerpColorU(a.silver, b.silver, t);
    o.accent = lerpColorU(a.accent, b.accent, t);
    o.accentDim = lerpColorU(a.accentDim, b.accentDim, t);
    o.accentSoft = lerpColorU(a.accentSoft, b.accentSoft, t);
    return o;
}

void tickThemeBlend() {
    if (!g_themeBlending) return;
    const uint64_t now = sceKernelGetProcessTimeWide() / 1000ULL;
    float t = (float)(now - g_themeBlendStartMs) / kThemeBlendMs;
    if (t >= 1.f) {
        applyThemePalette(g_themeTo);
        g_themeBlending = false;
        return;
    }
    if (t < 0.f) t = 0.f;
    // Smoothstep ease-in-out
    t = t * t * (3.f - 2.f * t);
    applyThemePalette(lerpThemePalette(g_themeFrom, g_themeTo, t));
}

/** Brand art (full-color logo/splash) only for the original PSVitaAlive theme. */
bool isBrandColorTheme(::psvitaalive::ColorTheme t) {
    return t == ::psvitaalive::ColorTheme::NeonLime;
}

const char* colorThemeDisplayName(::psvitaalive::ColorTheme t) {
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan: return "Cyan";
        case ::psvitaalive::ColorTheme::Rose: return "Rose";
        case ::psvitaalive::ColorTheme::Amber: return "Amber";
        case ::psvitaalive::ColorTheme::Violet: return "Violet";
        case ::psvitaalive::ColorTheme::Mono: return "Mono";
        case ::psvitaalive::ColorTheme::Oled: return "OLED";
        case ::psvitaalive::ColorTheme::PsVita: return "PS Vita";
        case ::psvitaalive::ColorTheme::Crimson: return "Crimson";
        case ::psvitaalive::ColorTheme::Coffee: return "Coffee";
        case ::psvitaalive::ColorTheme::Gold: return "Gold";
        case ::psvitaalive::ColorTheme::Emerald: return "Emerald";
        case ::psvitaalive::ColorTheme::Coral: return "Coral";
        case ::psvitaalive::ColorTheme::Teal: return "Teal";
        case ::psvitaalive::ColorTheme::Indigo: return "Indigo";
        case ::psvitaalive::ColorTheme::Sky: return "Sky";
        case ::psvitaalive::ColorTheme::Magenta: return "Magenta";
        case ::psvitaalive::ColorTheme::Mint: return "Mint";
        case ::psvitaalive::ColorTheme::Sunset: return "Sunset";
        case ::psvitaalive::ColorTheme::Ocean: return "Ocean";
        case ::psvitaalive::ColorTheme::Lavender: return "Lavender";
        case ::psvitaalive::ColorTheme::Cherry: return "Cherry";
        case ::psvitaalive::ColorTheme::Sand: return "Sand";
        case ::psvitaalive::ColorTheme::Forest: return "Forest";
        case ::psvitaalive::ColorTheme::Ice: return "Ice";
        case ::psvitaalive::ColorTheme::Grape: return "Grape";
        case ::psvitaalive::ColorTheme::Peach: return "Peach";
        case ::psvitaalive::ColorTheme::Azure: return "Azure";
        case ::psvitaalive::ColorTheme::Steel: return "Steel";
        case ::psvitaalive::ColorTheme::Honey: return "Honey";
        case ::psvitaalive::ColorTheme::Midnight: return "Midnight";
        case ::psvitaalive::ColorTheme::Sakura: return "Sakura";
        case ::psvitaalive::ColorTheme::Matrix: return "Matrix";
        case ::psvitaalive::ColorTheme::Scarlet: return "Scarlet";
        case ::psvitaalive::ColorTheme::Orange: return "Orange";
        case ::psvitaalive::ColorTheme::White: return "White";
        case ::psvitaalive::ColorTheme::Snow: return "Snow";
        case ::psvitaalive::ColorTheme::Ivory: return "Ivory";
        case ::psvitaalive::ColorTheme::Khaki: return "Khaki";
        case ::psvitaalive::ColorTheme::Terracotta: return "Terracotta";
        case ::psvitaalive::ColorTheme::Ruby: return "Ruby";
        case ::psvitaalive::ColorTheme::Copper: return "Copper";
        case ::psvitaalive::ColorTheme::Olive: return "Olive";
        case ::psvitaalive::ColorTheme::Maroon: return "Maroon";
        case ::psvitaalive::ColorTheme::Turquoise: return "Turquoise";
        case ::psvitaalive::ColorTheme::Lemon: return "Lemon";
        case ::psvitaalive::ColorTheme::Plum: return "Plum";
        case ::psvitaalive::ColorTheme::Navy: return "Navy";
        case ::psvitaalive::ColorTheme::Rust: return "Rust";
        case ::psvitaalive::ColorTheme::Champagne: return "Champagne";
        case ::psvitaalive::ColorTheme::Graphite: return "Graphite";
        case ::psvitaalive::ColorTheme::NeonLime:
        default: return "PSVitaAlive";
    }
}

/** Accent RGB for a theme (does not mutate global palette). */
void colorThemeAccentRgb(::psvitaalive::ColorTheme t, unsigned& ar, unsigned& ag, unsigned& ab) {
    ar = 0x3B; ag = 0xFF; ab = 0x00;
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan:       ar=0x00; ag=0xE5; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Rose:       ar=0xFF; ag=0x5C; ab=0xA8; break;
        case ::psvitaalive::ColorTheme::Amber:      ar=0xFF; ag=0xB0; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Violet:     ar=0xB2; ag=0x4D; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Mono:       ar=0xC8; ag=0xC8; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Oled:       ar=0x5C; ag=0xFF; ab=0x9A; break;
        case ::psvitaalive::ColorTheme::PsVita:     ar=0x00; ag=0x9A; ab=0xDE; break;
        case ::psvitaalive::ColorTheme::Crimson:    ar=0xDC; ag=0x14; ab=0x3C; break;
        case ::psvitaalive::ColorTheme::Coffee:     ar=0xA0; ag=0x72; ab=0x3C; break;
        case ::psvitaalive::ColorTheme::Gold:       ar=0xFF; ag=0xD0; ab=0x00; break;
        case ::psvitaalive::ColorTheme::Emerald:    ar=0x00; ag=0xD4; ab=0x7A; break;
        case ::psvitaalive::ColorTheme::Coral:      ar=0xFF; ag=0x7A; ab=0x66; break;
        case ::psvitaalive::ColorTheme::Teal:       ar=0x00; ag=0xC4; ab=0xB0; break;
        case ::psvitaalive::ColorTheme::Indigo:     ar=0x5A; ag=0x4C; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Sky:        ar=0x7E; ag=0xD0; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Magenta:    ar=0xFF; ag=0x00; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Mint:       ar=0x98; ag=0xFF; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Sunset:     ar=0xFF; ag=0x55; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Ocean:      ar=0x00; ag=0x6A; ab=0xB8; break;
        case ::psvitaalive::ColorTheme::Lavender:   ar=0xC8; ag=0xA8; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Cherry:     ar=0xFF; ag=0x2D; ab=0x6A; break;
        case ::psvitaalive::ColorTheme::Sand:       ar=0xE0; ag=0xC2; ab=0x7A; break;
        case ::psvitaalive::ColorTheme::Forest:     ar=0x1E; ag=0x6B; ab=0x3A; break;
        case ::psvitaalive::ColorTheme::Ice:        ar=0xD0; ag=0xF0; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Grape:      ar=0x8E; ag=0x2D; ab=0xE2; break;
        case ::psvitaalive::ColorTheme::Peach:      ar=0xFF; ag=0xB3; ab=0x8A; break;
        case ::psvitaalive::ColorTheme::Azure:      ar=0x1E; ag=0x90; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Steel:      ar=0x8A; ag=0x9B; ab=0xB0; break;
        case ::psvitaalive::ColorTheme::Honey:      ar=0xFF; ag=0xB3; ab=0x00; break;
        case ::psvitaalive::ColorTheme::Midnight:   ar=0x3A; ag=0x5C; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Sakura:     ar=0xFF; ag=0x8A; ab=0xC4; break;
        case ::psvitaalive::ColorTheme::Matrix:     ar=0x00; ag=0xFF; ab=0x66; break;
        case ::psvitaalive::ColorTheme::Scarlet:    ar=0xFF; ag=0x00; ab=0x00; break;
        case ::psvitaalive::ColorTheme::Orange:     ar=0xFF; ag=0x7A; ab=0x00; break;
        case ::psvitaalive::ColorTheme::White:      ar=0xF5; ag=0xF5; ab=0xF5; break;
        case ::psvitaalive::ColorTheme::Snow:       ar=0xFF; ag=0xFF; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Ivory:      ar=0xFF; ag=0xF5; ab=0xE0; break;
        case ::psvitaalive::ColorTheme::Khaki:      ar=0xC3; ag=0xB0; ab=0x91; break;
        case ::psvitaalive::ColorTheme::Terracotta: ar=0xE2; ag=0x72; ab=0x5B; break;
        case ::psvitaalive::ColorTheme::Ruby:       ar=0x9B; ag=0x00; ab=0x2E; break;
        case ::psvitaalive::ColorTheme::Copper:     ar=0xB8; ag=0x73; ab=0x33; break;
        case ::psvitaalive::ColorTheme::Olive:      ar=0x6B; ag=0x8E; ab=0x23; break;
        case ::psvitaalive::ColorTheme::Maroon:     ar=0x6B; ag=0x1E; ab=0x2A; break;
        case ::psvitaalive::ColorTheme::Turquoise:  ar=0x40; ag=0xE0; ab=0xD0; break;
        case ::psvitaalive::ColorTheme::Lemon:      ar=0xF7; ag=0xE7; ab=0x33; break;
        case ::psvitaalive::ColorTheme::Plum:       ar=0x8E; ag=0x45; ab=0x85; break;
        case ::psvitaalive::ColorTheme::Navy:       ar=0x2A; ag=0x5C; ab=0x9E; break;
        case ::psvitaalive::ColorTheme::Rust:       ar=0xB7; ag=0x41; ab=0x0E; break;
        case ::psvitaalive::ColorTheme::Champagne:  ar=0xE8; ag=0xC8; ab=0x8A; break;
        case ::psvitaalive::ColorTheme::Graphite:   ar=0x90; ag=0x94; ab=0x98; break;
        case ::psvitaalive::ColorTheme::NeonLime:
        default: ar=0x3B; ag=0xFF; ab=0x00; break;
    }
}

void applyColorTheme(::psvitaalive::ColorTheme t, bool animate = true) {
    // Snapshot current on-screen colors as blend source (supports interrupting mid-fade).
    const ThemePalette fromPal = captureThemePalette();

    // Neutral baseline (overridden per theme for stronger identity).
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
    colorThemeAccentRgb(t, ar, ag, ab);

    switch (t) {
        case ::psvitaalive::ColorTheme::Oled:
            BG = RGBA8(0x00,0x00,0x00,255);
            SURFACE = RGBA8(0x0A,0x0A,0x0A,255);
            SURFACE2 = RGBA8(0x06,0x06,0x06,255);
            PANEL = RGBA8(0x03,0x03,0x03,255);
            BORDER = RGBA8(0x22,0x22,0x22,255);
            break;
        case ::psvitaalive::ColorTheme::Matrix:
            // Terminal phosphor: pure black + green-tinted chrome (not brand lime).
            BG = RGBA8(0x00,0x05,0x00,255);
            SURFACE = RGBA8(0x05,0x10,0x08,255);
            SURFACE2 = RGBA8(0x03,0x0C,0x06,255);
            PANEL = RGBA8(0x02,0x08,0x04,255);
            BORDER = RGBA8(0x10,0x38,0x1C,255);
            TEXT = RGBA8(0x8A,0xC8,0x9A,255);
            DIM = RGBA8(0x3A,0x6A,0x48,255);
            break;
        case ::psvitaalive::ColorTheme::PsVita:
            BG = RGBA8(0x08,0x0C,0x14,255);
            SURFACE = RGBA8(0x12,0x18,0x24,255);
            SURFACE2 = RGBA8(0x0E,0x14,0x1E,255);
            PANEL = RGBA8(0x0A,0x10,0x18,255);
            BORDER = RGBA8(0x28,0x38,0x4C,255);
            break;
        case ::psvitaalive::ColorTheme::Ocean:
        case ::psvitaalive::ColorTheme::Navy:
            BG = RGBA8(0x06,0x0A,0x14,255);
            SURFACE = RGBA8(0x0E,0x16,0x28,255);
            SURFACE2 = RGBA8(0x0A,0x12,0x20,255);
            PANEL = RGBA8(0x08,0x0E,0x1A,255);
            BORDER = RGBA8(0x24,0x34,0x50,255);
            break;
        case ::psvitaalive::ColorTheme::Azure:
        case ::psvitaalive::ColorTheme::Sky:
            BG = RGBA8(0x0A,0x10,0x16,255);
            SURFACE = RGBA8(0x14,0x1C,0x28,255);
            SURFACE2 = RGBA8(0x10,0x18,0x22,255);
            PANEL = RGBA8(0x0C,0x14,0x1C,255);
            BORDER = RGBA8(0x30,0x40,0x52,255);
            break;
        case ::psvitaalive::ColorTheme::Ice:
        case ::psvitaalive::ColorTheme::Snow:
            BG = RGBA8(0x0C,0x10,0x14,255);
            SURFACE = RGBA8(0x18,0x1E,0x24,255);
            SURFACE2 = RGBA8(0x12,0x18,0x1E,255);
            PANEL = RGBA8(0x0E,0x14,0x18,255);
            BORDER = RGBA8(0x3A,0x48,0x54,255);
            TEXT = RGBA8(0xC8,0xD4,0xE0,255);
            break;
        case ::psvitaalive::ColorTheme::White:
            BG = RGBA8(0x12,0x12,0x12,255);
            SURFACE = RGBA8(0x22,0x22,0x22,255);
            SURFACE2 = RGBA8(0x1A,0x1A,0x1A,255);
            PANEL = RGBA8(0x16,0x16,0x16,255);
            BORDER = RGBA8(0x48,0x48,0x48,255);
            TEXT = RGBA8(0xD8,0xD8,0xD8,255);
            DIM = RGBA8(0x88,0x88,0x88,255);
            break;
        case ::psvitaalive::ColorTheme::Ivory:
            // Soft cream / paper — light warm gray surfaces
            BG = RGBA8(0x12,0x10,0x0E,255);
            SURFACE = RGBA8(0x22,0x1E,0x1A,255);
            SURFACE2 = RGBA8(0x1A,0x18,0x14,255);
            PANEL = RGBA8(0x16,0x14,0x12,255);
            BORDER = RGBA8(0x4A,0x42,0x38,255);
            TEXT = RGBA8(0xE0,0xD8,0xC8,255);
            DIM = RGBA8(0x90,0x86,0x78,255);
            break;
        case ::psvitaalive::ColorTheme::Champagne:
            // Golden toast — stronger gold/amber wash (not cream)
            BG = RGBA8(0x10,0x0C,0x06,255);
            SURFACE = RGBA8(0x20,0x18,0x0C,255);
            SURFACE2 = RGBA8(0x18,0x12,0x08,255);
            PANEL = RGBA8(0x14,0x0E,0x06,255);
            BORDER = RGBA8(0x50,0x3C,0x1C,255);
            TEXT = RGBA8(0xE0,0xC8,0x90,255);
            DIM = RGBA8(0x8A,0x72,0x48,255);
            break;
        case ::psvitaalive::ColorTheme::Scarlet:
            BG = RGBA8(0x12,0x04,0x04,255);
            SURFACE = RGBA8(0x22,0x0A,0x0A,255);
            SURFACE2 = RGBA8(0x1A,0x08,0x08,255);
            PANEL = RGBA8(0x14,0x06,0x06,255);
            BORDER = RGBA8(0x50,0x18,0x18,255);
            break;
        case ::psvitaalive::ColorTheme::Crimson:
            // Classic crimson — cool red-purple base
            BG = RGBA8(0x10,0x04,0x08,255);
            SURFACE = RGBA8(0x20,0x08,0x12,255);
            SURFACE2 = RGBA8(0x18,0x06,0x0E,255);
            PANEL = RGBA8(0x12,0x05,0x0A,255);
            BORDER = RGBA8(0x4C,0x14,0x28,255);
            break;
        case ::psvitaalive::ColorTheme::Cherry:
            // Candy cherry — warmer pink-red surfaces
            BG = RGBA8(0x12,0x06,0x0C,255);
            SURFACE = RGBA8(0x24,0x0E,0x18,255);
            SURFACE2 = RGBA8(0x1C,0x0A,0x12,255);
            PANEL = RGBA8(0x16,0x08,0x10,255);
            BORDER = RGBA8(0x58,0x20,0x38,255);
            TEXT = RGBA8(0xE0,0xA0,0xB4,255);
            break;
        case ::psvitaalive::ColorTheme::Ruby:
            // Jewel ruby — very dark, slight blue-red
            BG = RGBA8(0x08,0x02,0x06,255);
            SURFACE = RGBA8(0x14,0x04,0x0C,255);
            SURFACE2 = RGBA8(0x0E,0x03,0x08,255);
            PANEL = RGBA8(0x0A,0x02,0x06,255);
            BORDER = RGBA8(0x38,0x08,0x18,255);
            break;
        case ::psvitaalive::ColorTheme::Maroon:
            // Brown-maroon — earthy, less saturated
            BG = RGBA8(0x0C,0x06,0x06,255);
            SURFACE = RGBA8(0x18,0x0C,0x0C,255);
            SURFACE2 = RGBA8(0x12,0x0A,0x0A,255);
            PANEL = RGBA8(0x0E,0x08,0x08,255);
            BORDER = RGBA8(0x3A,0x1E,0x1E,255);
            TEXT = RGBA8(0xC0,0x98,0x98,255);
            DIM = RGBA8(0x70,0x50,0x50,255);
            break;
        case ::psvitaalive::ColorTheme::Orange:
        case ::psvitaalive::ColorTheme::Sunset:
            BG = RGBA8(0x12,0x08,0x04,255);
            SURFACE = RGBA8(0x22,0x12,0x08,255);
            SURFACE2 = RGBA8(0x1A,0x0E,0x06,255);
            PANEL = RGBA8(0x14,0x0A,0x04,255);
            BORDER = RGBA8(0x50,0x2C,0x14,255);
            break;
        case ::psvitaalive::ColorTheme::Amber:
        case ::psvitaalive::ColorTheme::Honey:
        case ::psvitaalive::ColorTheme::Gold:
        case ::psvitaalive::ColorTheme::Lemon:
            BG = RGBA8(0x0E,0x0C,0x04,255);
            SURFACE = RGBA8(0x1C,0x18,0x08,255);
            SURFACE2 = RGBA8(0x14,0x12,0x06,255);
            PANEL = RGBA8(0x10,0x0E,0x04,255);
            BORDER = RGBA8(0x48,0x3C,0x14,255);
            break;
        case ::psvitaalive::ColorTheme::Coffee:
        case ::psvitaalive::ColorTheme::Copper:
        case ::psvitaalive::ColorTheme::Rust:
            BG = RGBA8(0x0E,0x0A,0x06,255);
            SURFACE = RGBA8(0x1C,0x14,0x0C,255);
            SURFACE2 = RGBA8(0x16,0x10,0x0A,255);
            PANEL = RGBA8(0x12,0x0C,0x08,255);
            BORDER = RGBA8(0x42,0x30,0x1C,255);
            TEXT = RGBA8(0xC4,0xA8,0x88,255);
            break;
        case ::psvitaalive::ColorTheme::Terracotta:
        case ::psvitaalive::ColorTheme::Sand:
        case ::psvitaalive::ColorTheme::Khaki:
            BG = RGBA8(0x10,0x0C,0x08,255);
            SURFACE = RGBA8(0x1E,0x18,0x12,255);
            SURFACE2 = RGBA8(0x18,0x14,0x0E,255);
            PANEL = RGBA8(0x14,0x10,0x0C,255);
            BORDER = RGBA8(0x46,0x38,0x28,255);
            TEXT = RGBA8(0xC8,0xB8,0xA0,255);
            DIM = RGBA8(0x80,0x70,0x58,255);
            break;
        case ::psvitaalive::ColorTheme::Peach:
        case ::psvitaalive::ColorTheme::Coral:
            BG = RGBA8(0x12,0x0A,0x0A,255);
            SURFACE = RGBA8(0x22,0x14,0x12,255);
            SURFACE2 = RGBA8(0x1A,0x10,0x0E,255);
            PANEL = RGBA8(0x14,0x0C,0x0C,255);
            BORDER = RGBA8(0x4C,0x30,0x28,255);
            break;
        case ::psvitaalive::ColorTheme::Rose:
        case ::psvitaalive::ColorTheme::Sakura:
        case ::psvitaalive::ColorTheme::Magenta:
            BG = RGBA8(0x10,0x08,0x0E,255);
            SURFACE = RGBA8(0x20,0x10,0x1A,255);
            SURFACE2 = RGBA8(0x18,0x0C,0x14,255);
            PANEL = RGBA8(0x12,0x0A,0x10,255);
            BORDER = RGBA8(0x48,0x28,0x3C,255);
            break;
        case ::psvitaalive::ColorTheme::Emerald:
        case ::psvitaalive::ColorTheme::Mint:
            BG = RGBA8(0x06,0x0E,0x0A,255);
            SURFACE = RGBA8(0x0C,0x1A,0x14,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x10,255);
            PANEL = RGBA8(0x08,0x10,0x0C,255);
            BORDER = RGBA8(0x24,0x40,0x32,255);
            break;
        case ::psvitaalive::ColorTheme::Forest:
        case ::psvitaalive::ColorTheme::Olive:
            BG = RGBA8(0x06,0x0A,0x06,255);
            SURFACE = RGBA8(0x0E,0x16,0x0E,255);
            SURFACE2 = RGBA8(0x0A,0x12,0x0A,255);
            PANEL = RGBA8(0x08,0x0E,0x08,255);
            BORDER = RGBA8(0x2A,0x3A,0x22,255);
            break;
        case ::psvitaalive::ColorTheme::Teal:
        case ::psvitaalive::ColorTheme::Turquoise:
            BG = RGBA8(0x04,0x0C,0x0C,255);
            SURFACE = RGBA8(0x0C,0x1A,0x1A,255);
            SURFACE2 = RGBA8(0x08,0x14,0x14,255);
            PANEL = RGBA8(0x06,0x10,0x10,255);
            BORDER = RGBA8(0x24,0x40,0x40,255);
            break;
        case ::psvitaalive::ColorTheme::Violet:
        case ::psvitaalive::ColorTheme::Grape:
        case ::psvitaalive::ColorTheme::Plum:
            BG = RGBA8(0x0C,0x08,0x12,255);
            SURFACE = RGBA8(0x18,0x10,0x22,255);
            SURFACE2 = RGBA8(0x12,0x0C,0x1A,255);
            PANEL = RGBA8(0x0E,0x0A,0x14,255);
            BORDER = RGBA8(0x3A,0x28,0x4C,255);
            break;
        case ::psvitaalive::ColorTheme::Indigo:
        case ::psvitaalive::ColorTheme::Midnight:
            BG = RGBA8(0x08,0x08,0x14,255);
            SURFACE = RGBA8(0x12,0x12,0x24,255);
            SURFACE2 = RGBA8(0x0E,0x0E,0x1C,255);
            PANEL = RGBA8(0x0A,0x0A,0x16,255);
            BORDER = RGBA8(0x2C,0x2C,0x4C,255);
            break;
        case ::psvitaalive::ColorTheme::Lavender:
            BG = RGBA8(0x0E,0x0C,0x14,255);
            SURFACE = RGBA8(0x1A,0x16,0x24,255);
            SURFACE2 = RGBA8(0x14,0x12,0x1C,255);
            PANEL = RGBA8(0x10,0x0E,0x16,255);
            BORDER = RGBA8(0x3C,0x34,0x50,255);
            break;
        case ::psvitaalive::ColorTheme::Steel:
        case ::psvitaalive::ColorTheme::Graphite:
        case ::psvitaalive::ColorTheme::Mono:
            BG = RGBA8(0x0C,0x0C,0x0E,255);
            SURFACE = RGBA8(0x1A,0x1A,0x1E,255);
            SURFACE2 = RGBA8(0x14,0x14,0x16,255);
            PANEL = RGBA8(0x10,0x10,0x12,255);
            BORDER = RGBA8(0x3A,0x3A,0x40,255);
            break;
        case ::psvitaalive::ColorTheme::Cyan:
            BG = RGBA8(0x06,0x0C,0x10,255);
            SURFACE = RGBA8(0x0C,0x18,0x1E,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x18,255);
            PANEL = RGBA8(0x08,0x10,0x14,255);
            BORDER = RGBA8(0x28,0x40,0x48,255);
            break;
        case ::psvitaalive::ColorTheme::NeonLime:
            // Brand PSVitaAlive — neutral near-black (not green-tinted like Matrix)
            BG = RGBA8(0x0A,0x0A,0x0A,255);
            SURFACE = RGBA8(0x1A,0x1A,0x1A,255);
            SURFACE2 = RGBA8(0x12,0x12,0x14,255);
            PANEL = RGBA8(0x0E,0x0E,0x10,255);
            BORDER = RGBA8(0x2A,0x2A,0x2E,255);
            TEXT = RGBA8(0xAA,0xAA,0xAA,255);
            DIM = RGBA8(0x66,0x66,0x6A,255);
            break;
        default:
            break;
    }

    ACCENT = RGBA8(ar, ag, ab, 255);
    ACCENT_DIM = RGBA8(ar, ag, ab, 90);
    ACCENT_SOFT = RGBA8(ar, ag, ab, 40);

    const ThemePalette toPal = captureThemePalette();
    if (!animate) {
        g_themeBlending = false;
        applyThemePalette(toPal);
        return;
    }
    // Cross-fade: restore "from", then tickThemeBlend interpolates each frame.
    g_themeFrom = fromPal;
    g_themeTo = toPal;
    g_themeBlendStartMs = sceKernelGetProcessTimeWide() / 1000ULL;
    g_themeBlending = true;
    applyThemePalette(fromPal);
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

/** Soft horizontal marquee for long titles when focused (list card + detail header).
 *  parentClip* = outer panel/card scissor; animated path intersects name strip with it
 *  so glyphs never paint outside the catalog panel while scrolling. */
void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,
                     const std::string& text, bool animate,
                     int parentClipL = 0, int parentClipT = 0,
                     int parentClipR = SCREEN_W, int parentClipB = SCREEN_H) {
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
    // Animated marquee: scissor = name strip ∩ parent panel (never leave panel).
    const int gap = 48;
    const int cycle = tw + gap;
    const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
    const uint64_t period = static_cast<uint64_t>(cycle) * 28ULL + 1200ULL;
    const uint64_t t = ms % period;
    int offset = 0;
    if (t > 1200ULL) offset = static_cast<int>((t - 1200ULL) / 28ULL);
    if (offset > cycle) offset = cycle;
    int x0 = x;
    int y0 = y - 20;
    int x1 = x + maxW;
    int y1 = y + 6;
    if (x0 < parentClipL) x0 = parentClipL;
    if (y0 < parentClipT) y0 = parentClipT;
    if (x1 > parentClipR) x1 = parentClipR;
    if (y1 > parentClipB) y1 = parentClipB;
    if (x1 <= x0 || y1 <= y0) {
        // Fully outside parent panel — restore parent scissor and skip draw.
        vita2d_enable_clipping();
        vita2d_set_clip_rectangle(parentClipL, parentClipT, parentClipR, parentClipB);
        return;
    }
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x0, y0, x1, y1);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    // Restore parent panel/card scissor (never full-screen).
    vita2d_set_clip_rectangle(parentClipL, parentClipT, parentClipR, parentClipB);
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
    if(t=="plugin"||t=="plugins")return true;
    // Fallback: actionable by file extension in URL.
    std::string u=lowerAscii(l.url);
    return u.find(".vpk")!=std::string::npos||u.find(".pkg")!=std::string::npos||u.find(".zip")!=std::string::npos||u.find(".pbp")!=std::string::npos||u.find(".iso")!=std::string::npos||u.find(".cso")!=std::string::npos||u.find(".suprx")!=std::string::npos||u.find(".skprx")!=std::string::npos;
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
    if(t=="plugin"||t=="plugins")return true;
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
    Plugins,
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
    if (t == "plugin" || t == "plugins") return LinkSection::Plugins;
    return LinkSection::Other;
}

const char* linkSectionTitle(LinkSection s) {
    using TID = ::psvitaalive::TextId;
    switch (s) {
        case LinkSection::Downloads: return ::psvitaalive::L(TID::SectionDownloads);
        case LinkSection::DataFiles: return ::psvitaalive::L(TID::SectionDataFiles);
        case LinkSection::GameFiles: return ::psvitaalive::L(TID::SectionGameFiles);
        case LinkSection::Mods: return ::psvitaalive::L(TID::SectionMods);
        case LinkSection::Dlc: return ::psvitaalive::L(TID::SectionDlc);
        case LinkSection::Updates: return ::psvitaalive::L(TID::SectionUpdatesLinks);
        case LinkSection::Pkg: return ::psvitaalive::L(TID::SectionPkg);
        case LinkSection::Plugins: return ::psvitaalive::L(TID::SectionPlugins);
        default: return ::psvitaalive::L(TID::SectionOther);
    }
}

const char* linkSectionMetaLabel(LinkSection s) {
    using TID = ::psvitaalive::TextId;
    switch (s) {
        case LinkSection::Downloads: return ::psvitaalive::L(TID::MetaDownload);
        case LinkSection::DataFiles: return ::psvitaalive::L(TID::MetaDataFiles);
        case LinkSection::GameFiles: return ::psvitaalive::L(TID::MetaGameFiles);
        case LinkSection::Mods: return ::psvitaalive::L(TID::MetaMod);
        case LinkSection::Dlc: return ::psvitaalive::L(TID::MetaDlc);
        case LinkSection::Updates: return ::psvitaalive::L(TID::MetaUpdate);
        case LinkSection::Pkg: return ::psvitaalive::L(TID::MetaPkg);
        case LinkSection::Plugins: return ::psvitaalive::L(TID::MetaPlugin);
        default: return ::psvitaalive::L(TID::MetaDownload);
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

bool itemHasDlc(const CatalogItem& it) {
    return itemHasLinkType(it, "dlc");
}

/** Homebrew: G/D Files chip. Vita Games + PSP: DLC chip. */
bool catalogSupportsContentFilter(CatalogType cat) {
    return cat == CatalogType::Homebrew
        || cat == CatalogType::VitaGames
        || cat == CatalogType::PspGames;
}

bool itemMatchesContentFilter(const CatalogItem& it, CatalogType cat) {
    if (cat == CatalogType::Homebrew) return itemHasDataOrGameFiles(it);
    if (cat == CatalogType::VitaGames || cat == CatalogType::PspGames) return itemHasDlc(it);
    return false;
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


bool isPluginTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    return t == "plugin" || t == "plugins";
}

bool isDlcTypeLink(const CatalogLink& l) {
    const std::string t = normalizeLinkType(l.type);
    return t == "dlc";
}

/** Resolved on-disk path for a Plugin link (line path preferred, else extract_path + basename). */
std::string pluginInstallFilePath(const CatalogLink& l) {
    auto trim = [](std::string s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
        return s;
    };
    const std::string line = trim(l.line);
    if (line.find(':') != std::string::npos) return line;

    std::string dir = trim(l.extractPath);
    if (dir.empty()) dir = "ur0:tai/";
    if (!dir.empty() && dir.back() != '/' && dir.back() != ':') dir.push_back('/');

    std::string name;
    if (!line.empty()) {
        const size_t slash = line.find_last_of("/\\");
        name = (slash == std::string::npos) ? line : line.substr(slash + 1);
    }
    if (name.empty() && !l.url.empty()) {
        std::string u = l.url;
        const size_t q = u.find('?');
        if (q != std::string::npos) u.resize(q);
        const size_t slash = u.find_last_of('/');
        name = (slash == std::string::npos) ? u : u.substr(slash + 1);
    }
    if (name.empty()) return {};
    return dir + name;
}

bool isPluginAlreadyInstalled(const CatalogLink& l) {
    if (!isPluginTypeLink(l)) return false;
    const std::string path = pluginInstallFilePath(l);
    if (path.empty()) return false;
    SceIoStat st{};
    if (sceIoGetstat(path.c_str(), &st) < 0) return false;
    return st.st_size > 0;
}

bool itemHasPluginLinks(const CatalogItem& it) {
    for (const auto& l : it.linkDetails) {
        if (isPluginTypeLink(l) && !l.url.empty()) return true;
    }
    return false;
}

bool essentialFilePresent(const std::vector<std::string>& paths) {
    SceIoStat st{};
    for (const auto& path : paths) {
        if (path.empty()) continue;
        if (sceIoGetstat(path.c_str(), &st) >= 0 && st.st_size > 0) return true;
    }
    return false;
}

/** File on disk, and for taiHEN plugins also the config.txt line must exist. */
bool essentialPluginFullyInstalled(const std::string& section,
                                   const std::string& line,
                                   const std::vector<std::string>& paths) {
    if (!essentialFilePresent(paths)) return false;
    // libshacccg and anything with section=none: file presence only
    std::string sec = section;
    for (char& c : sec) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (sec.empty() || sec == "none") return true;
    if (line.empty()) return true;
    return ::psvitaalive::TaiConfigEditor::configContainsLine(line);
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



/** Keep theme picker focus row inside the visible window (like clampCatalogScroll). */
static void clampThemePickerScroll(int focus, int& scrollRow, int themeCount, int cols, int visibleRows) {
    if (themeCount <= 0) { scrollRow = 0; return; }
    const int rows = (themeCount + cols - 1) / cols;
    const int maxScroll = std::max(0, rows - visibleRows);
    if (focus >= 0 && focus < themeCount) {
        const int fr = focus / cols;
        if (fr < scrollRow) scrollRow = fr;
        if (fr >= scrollRow + visibleRows) scrollRow = fr - visibleRows + 1;
    }
    if (scrollRow < 0) scrollRow = 0;
    if (scrollRow > maxScroll) scrollRow = maxScroll;
}

void FullCatalogScreen::openThemePicker() {
    themeSetupVisible_ = true;
    const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
    themeSetupFocus_ = static_cast<int>(settingsEdit_.colorTheme);
    if (themeSetupFocus_ < 0 || themeSetupFocus_ >= n) themeSetupFocus_ = 0;
    themeSetupScrollRow_ = 0;
    visualThemeSetupScroll_ = 0.f;
    themeSetupAppliedFocus_ = themeSetupFocus_; // current theme already active
    // Keep focused row roughly in view
    const int cols = 3;
    themeSetupScrollRow_ = std::max(0, themeSetupFocus_ / cols - 1);
    applyColorTheme(settingsEdit_.colorTheme, false);
    diagnostics::log("[UI] theme picker opened");
}

void FullCatalogScreen::openThemeSetupIfNeeded() {
    if (themeSetupChecked_) return;
    themeSetupChecked_ = true;
    if (settingsEdit_.themeSetupDone) {
        diagnostics::log("[UI] theme setup skipped (already done)");
        return;
    }
    openThemePicker();
    diagnostics::log("[UI] theme setup modal shown (first run)");
}

void FullCatalogScreen::applyThemeSetupFocus() {
    const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
    if (themeSetupFocus_ < 0 || themeSetupFocus_ >= n) return;
    const auto t = static_cast<::psvitaalive::ColorTheme>(themeSetupFocus_);
    settingsEdit_.colorTheme = t;
    applyColorTheme(t);
    showToast(std::string(::psvitaalive::L(::psvitaalive::TextId::ThemeAppliedPrefix)) + ": " +
              colorThemeDisplayName(t), 1400);
}

void FullCatalogScreen::closeThemeSetup(bool save) {
    if (!themeSetupVisible_) return;
    if (save) {
        settingsEdit_.themeSetupDone = true;
        applyColorTheme(settingsEdit_.colorTheme, false);
        if (settingsSave_) settingsSave_(settingsEdit_);
        diagnostics::log(std::string("[UI] theme setup saved theme=") +
                         ::psvitaalive::AppSettings::toString(settingsEdit_.colorTheme));
        showToast(::psvitaalive::L(::psvitaalive::TextId::ThemeSavedToast), 2200);
    }
    themeSetupVisible_ = false;
}

void FullCatalogScreen::drawThemeSetupOverlay() {
    if (!themeSetupVisible_ || !font_) return;

    using TID = ::psvitaalive::TextId;
    const int themeCount = static_cast<int>(::psvitaalive::ColorTheme::Count);
    const int cols = 3;
    const int rows = (themeCount + cols - 1) / cols;
    const int btnW = 250;
    const int btnH = 52;
    const int gapX = 14;
    const int gapY = 12;
    const int gridW = cols * btnW + (cols - 1) * gapX;

    // Near full-screen panel for readability on 960x544.
    const int w = 920, h = 508;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;

    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 170));
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, w, 3, ACCENT);
    vita2d_draw_rectangle(x, y + 3, 3, h - 6, ACCENT);
    vita2d_draw_rectangle(x + w - 3, y + 3, 3, h - 6, BORDER);
    vita2d_draw_rectangle(x, y + h - 3, w, 3, BORDER);

    vita2d_pgf_draw_text(font_, x + 24, y + 36, ACCENT, 1.12f,
                         ::psvitaalive::L(TID::ThemeSetupTitle));
    vita2d_pgf_draw_text(font_, x + 24, y + 68, TEXT, 0.84f,
                         ::psvitaalive::L(TID::ThemeSetupBody1));
    vita2d_pgf_draw_text(font_, x + 24, y + 96, DIM, 0.80f,
                         ::psvitaalive::L(TID::ThemeSetupBody2));

    const int gridTop = y + 118;
    const int gridBottom = y + h - 88;
    const int gridH = gridBottom - gridTop;
    const int rowH = btnH + gapY;
    const int visibleRows = std::max(1, gridH / rowH);
    const int maxScroll = std::max(0, rows - visibleRows);
    if (themeSetupScrollRow_ < 0) themeSetupScrollRow_ = 0;
    if (themeSetupScrollRow_ > maxScroll) themeSetupScrollRow_ = maxScroll;
    if (visualThemeSetupScroll_ < 0.f) visualThemeSetupScroll_ = 0.f;
    if (visualThemeSetupScroll_ > (float)maxScroll) visualThemeSetupScroll_ = (float)maxScroll;

    if (themeSetupFocus_ < themeCount) {
        const int fr = themeSetupFocus_ / cols;
        if (fr < themeSetupScrollRow_) themeSetupScrollRow_ = fr;
        if (fr >= themeSetupScrollRow_ + visibleRows)
            themeSetupScrollRow_ = fr - visibleRows + 1;
    }

    const float vs = visualThemeSetupScroll_;
    const int startRow = (int)std::floor(vs);
    const int gridX = x + (w - gridW) / 2;

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 12, gridTop, x + w - 12, gridBottom);

    for (int r = startRow - 1; r <= startRow + visibleRows + 1; ++r) {
        if (r < 0 || r >= rows) continue;
        const int ry = gridTop + (int)(((float)r - vs) * (float)rowH);
        for (int c = 0; c < cols; ++c) {
            const int idx = r * cols + c;
            if (idx < 0 || idx >= themeCount) continue;
            const auto th = static_cast<::psvitaalive::ColorTheme>(idx);
            unsigned ar, ag, ab;
            colorThemeAccentRgb(th, ar, ag, ab);
            const unsigned accent = RGBA8(ar, ag, ab, 255);
            const unsigned soft = RGBA8(ar, ag, ab, 55);
            const int bx = gridX + c * (btnW + gapX);
            const int by = ry;
            const bool focused = (themeSetupFocus_ == idx);
            const bool selected = (settingsEdit_.colorTheme == th);

            vita2d_draw_rectangle(bx, by, btnW, btnH, SURFACE);
            vita2d_draw_rectangle(bx, by, 6, btnH, accent);
            if (focused) {
                vita2d_draw_rectangle(bx, by, btnW, 3, accent);
                vita2d_draw_rectangle(bx, by + btnH - 3, btnW, 3, accent);
                vita2d_draw_rectangle(bx, by, 3, btnH, accent);
                vita2d_draw_rectangle(bx + btnW - 3, by, 3, btnH, accent);
            } else {
                vita2d_draw_rectangle(bx, by, btnW, 1, BORDER);
            }
            if (selected) {
                vita2d_draw_rectangle(bx + 10, by + 8, btnW - 20, btnH - 16, soft);
            }
            const char* name = colorThemeDisplayName(th);
            const float sc = 0.88f;
            const int tw = vita2d_pgf_text_width(font_, sc, name);
            vita2d_pgf_draw_text(font_, bx + (btnW - tw) / 2 + 4, by + 34,
                                 focused ? WHITE : TEXT, sc, name);
        }
    }
    vita2d_disable_clipping();

    if (maxScroll > 0) {
        const int tx = x + w - 14, ty = gridTop, th = gridH;
        vita2d_draw_rectangle(tx, ty, 4, th, BORDER);
        const int thumb = std::max(20, th * visibleRows / std::max(1, rows));
        const float scrollT = std::min(1.f, visualThemeSetupScroll_ / (float)std::max(1, maxScroll));
        const int yy = ty + (int)((th - thumb) * scrollT);
        vita2d_draw_rectangle(tx, yy, 4, thumb, ACCENT);
    }

    const int saveW = 260, saveH = 48;
    const int saveX = x + (w - saveW) / 2;
    const int saveY = y + h - 72;
    const bool saveFocus = (themeSetupFocus_ == themeCount);
    vita2d_draw_rectangle(saveX, saveY, saveW, saveH, saveFocus ? ACCENT : SURFACE2);
    if (saveFocus) {
        vita2d_draw_rectangle(saveX, saveY, saveW, 2, WHITE);
        vita2d_draw_rectangle(saveX, saveY + saveH - 2, saveW, 2, WHITE);
    }
    {
        const char* saveLab = ::psvitaalive::L(TID::BtnSave);
        const float ssc = 0.94f;
        const int stw = vita2d_pgf_text_width(font_, ssc, saveLab);
        vita2d_pgf_draw_text(font_, saveX + (saveW - stw) / 2, saveY + 32,
                             saveFocus ? RGBA8(0,0,0,255) : WHITE, ssc, saveLab);
    }

    vita2d_pgf_draw_text(font_, x + 24, y + h - 18, DIM, 0.72f,
                         ::psvitaalive::L(TID::ThemeSetupNavHint));
}

void FullCatalogScreen::runNewsCheck(bool forceShow) {
    if (newsVisible_ || themeSetupVisible_) return;
    if (!forceShow && newsCheckedOnce_) return;

    ::psvitaalive::NewsItem item = ::psvitaalive::NewsManager::fetchRemote();
    if (!item.valid) {
        item = ::psvitaalive::NewsManager::loadCached();
    }
    if (!item.valid || !item.enabled) {
        if (forceShow) {
            newsCheckedOnce_ = true;
            showToast(::psvitaalive::L(::psvitaalive::TextId::ToastNoNews), 1600);
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
    newsTitle_ = item.title.empty() ? ::psvitaalive::L(::psvitaalive::TextId::ChipNews) : item.title;
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
    const char* lab = ::psvitaalive::L(::psvitaalive::TextId::ChipNews);
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

    vita2d_pgf_draw_text(font_, x + 24, y + 36, ACCENT, 0.82f, ::psvitaalive::L(::psvitaalive::TextId::NewsTitle));
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
        const char* clab = ::psvitaalive::L(::psvitaalive::TextId::BtnOClose);
        const float sc = 0.68f;
        const int tw = vita2d_pgf_text_width(font_, sc, clab);
        vita2d_pgf_draw_text(font_, x + (w - bw) / 2 + (bw - tw) / 2, by + 25, BLACK, sc, clab);
    }
    vita2d_pgf_draw_text(font_, x + 24, y + h - 14, DIM, 0.72f, ::psvitaalive::L(::psvitaalive::TextId::NewsNavHint));
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
    const char* lab = ::psvitaalive::L(::psvitaalive::TextId::ChipReport);
    if (reportUiState_ == 1) {
        fill = SURFACE;
        textCol = WHITE;
        lab = ""; // progress bar instead
    } else if (reportUiState_ == 2) {
        fill = GREEN;
        textCol = BLACK;
        lab = ::psvitaalive::L(::psvitaalive::TextId::ChipSent);
    } else if (reportUiState_ == 3) {
        fill = RED;
        textCol = WHITE;
        lab = ::psvitaalive::L(::psvitaalive::TextId::ChipFail);
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
    if (newsVisible_ || themeSetupVisible_) return;
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

    vita2d_pgf_draw_text(font_, x + 24, y + 40, ACC, 0.86f, ::psvitaalive::L(::psvitaalive::TextId::DataRequestTitle));
    vita2d_pgf_draw_text(font_, x + 24, y + 78, WHITE, 0.78f, ::psvitaalive::L(::psvitaalive::TextId::DataRequestBody1));
    vita2d_pgf_draw_text(font_, x + 24, y + 110, TEXT, 0.72f, ::psvitaalive::L(::psvitaalive::TextId::DataRequestBody2));
    vita2d_pgf_draw_text(font_, x + 24, y + 136, TEXT, 0.72f, ::psvitaalive::L(::psvitaalive::TextId::DataRequestBody3));
    vita2d_pgf_draw_text(font_, x + 24, y + 162, TEXT, 0.72f, ::psvitaalive::L(::psvitaalive::TextId::DataRequestBody4));

    const int by = y + h - 56, bh = 40, bw = 180, gap = 24;
    const int bxCancel = x + (w - (bw * 2 + gap)) / 2;
    const int bxSend = bxCancel + bw + gap;

    vita2d_draw_rectangle(bxCancel, by, bw, bh, SURFACE2);
    vita2d_draw_rectangle(bxCancel, by, bw, 1, BORDER);
    vita2d_draw_rectangle(bxCancel, by + bh - 1, bw, 1, BORDER);
    {
        const char* lab = ::psvitaalive::L(::psvitaalive::TextId::BtnOCancel);
        const float sc = 0.74f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, bxCancel + (bw - tw) / 2, by + 27, WHITE, sc, lab);
    }
    vita2d_draw_rectangle(bxSend, by, bw, bh, ACC);
    {
        const char* lab = ::psvitaalive::L(::psvitaalive::TextId::DataRequestSend);
        const float sc = 0.74f;
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
        showToast(::psvitaalive::L(::psvitaalive::TextId::ToastRequestInProgress), 1500);
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
        showToast(::psvitaalive::L(::psvitaalive::TextId::ToastCouldNotStartRequest), 1800);
        return;
    }
    sceKernelStartThread(dataRequestThread_, sizeof(self), &self);
    showToast(::psvitaalive::L(::psvitaalive::TextId::ToastSendingRequest), 1200);
}

void FullCatalogScreen::pollDataRequestWorker() {
    if (!dataRequestDone_.load()) return;
    dataRequestDone_.store(false);
    if (dataRequestThread_ >= 0) {
        sceKernelWaitThreadEnd(dataRequestThread_, nullptr, nullptr);
        sceKernelDeleteThread(dataRequestThread_);
        dataRequestThread_ = -1;
    }
    showToast(dataRequestResultMsg_[0] ? dataRequestResultMsg_ : (dataRequestOk_.load() ? ::psvitaalive::L(::psvitaalive::TextId::ToastRequestSent) : ::psvitaalive::L(::psvitaalive::TextId::ToastRequestFailed)),
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

    vita2d_pgf_draw_text(font_, x + 24, y + 40, RED, 0.86f, ::psvitaalive::L(::psvitaalive::TextId::ReportTitle));
    vita2d_pgf_draw_text(font_, x + 24, y + 78, WHITE, 0.80f, ::psvitaalive::L(::psvitaalive::TextId::ReportSubtitle));
    vita2d_pgf_draw_text(font_, x + 24, y + 112, TEXT, 0.72f, ::psvitaalive::L(::psvitaalive::TextId::ReportBody1));
    vita2d_pgf_draw_text(font_, x + 24, y + 138, TEXT, 0.72f, ::psvitaalive::L(::psvitaalive::TextId::ReportBody2));

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
        const char* lab = ::psvitaalive::L(::psvitaalive::TextId::BtnXReport);
        const float sc = 0.74f;
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
                    res.message.empty() ? (res.ok ? ::psvitaalive::L(::psvitaalive::TextId::ToastReportSent) : ::psvitaalive::L(::psvitaalive::TextId::ToastReportFailed)) : res.message.c_str());
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
    showToast(ok ? (reportResultMsg_[0] ? reportResultMsg_ : ::psvitaalive::L(::psvitaalive::TextId::ToastReportSent))
                 : (reportResultMsg_[0] ? reportResultMsg_ : ::psvitaalive::L(::psvitaalive::TextId::ToastReportFailed)),
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
        showToast(res.ok ? ::psvitaalive::L(::psvitaalive::TextId::ToastReportSent) : (res.message.empty() ? ::psvitaalive::L(::psvitaalive::TextId::ToastReportFailed) : res.message), 2200);
        return;
    }
    FullCatalogScreen* self = this;
    if (sceKernelStartThread(reportThread_, sizeof(self), &self) < 0) {
        sceKernelDeleteThread(reportThread_);
        reportThread_ = -1;
        reportBusy_.store(false);
        reportUiState_ = 0;
        showToast(::psvitaalive::L(::psvitaalive::TextId::ToastReportFailedStart), 1800);
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
    uint64_t resultAutoCloseRemainingMs,
    bool needsReboot)
{
    installProgressActive_ = active;
    if (needsReboot && outcome == 1) {
        if (essentialInstallRunning_) {
            // Sequential essential-plugin installs: reboot only after the last one.
            essentialPluginsTryAdvanceFromProgress(outcome);
        } else {
            pluginRebootModal_ = true;
        }
    }
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
    if (essentialInstallRunning_ && outcome != 1) {
        essentialPluginsTryAdvanceFromProgress(outcome);
    }
}
bool FullCatalogScreen::init(){
    vita2d_init();
    vita2d_set_clear_color(BG);
    font_=::psvitaalive::ui::loadUiFont(settingsEdit_.uiFontStyle);
    if(!font_) font_=vita2d_load_default_pgf();
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
    catalogLoadingMonoTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading_mono.png");
    if (catalogLoadingMonoTex_) {
        diagnostics::log("[UI] catalog_loading_mono.png loaded");
    } else {
        diagnostics::log("[UI] catalog_loading_mono.png not found (tint fallback to color art)");
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
    headerLogoMonoTex_ = vita2d_load_PNG_file("app0:ui/PSVitaAlive_Store_logo_text_mono.png");
    if (headerLogoMonoTex_) {
        diagnostics::log("[UI] header logo mono loaded");
    } else {
        diagnostics::log("[UI] header logo mono not found (tint fallback to color art)");
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
    if (catalogLoadingMonoTex_) {
        vita2d_free_texture(catalogLoadingMonoTex_);
        catalogLoadingMonoTex_ = nullptr;
    }
    if (headerLogoTex_) {
        vita2d_free_texture(headerLogoTex_);
        headerLogoTex_ = nullptr;
    }
    if (headerLogoMonoTex_) {
        vita2d_free_texture(headerLogoMonoTex_);
        headerLogoMonoTex_ = nullptr;
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
        if (dataFilesFilter_ && !itemMatchesContentFilter(i, state_.catalog)) continue;
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
    sceClibSnprintf(m, sizeof(m), "[UI] content filter=%d catalog=%d results=%u",
                    dataFilesFilter_ ? 1 : 0, (int)state_.catalog, (unsigned)catalogView().size());
    diagnostics::log(m);
    if (!dataFilesFilter_) {
        showToast(::psvitaalive::L(::psvitaalive::TextId::FilterCleared), 2600);
    } else if (state_.catalog == CatalogType::Homebrew) {
        showToast(::psvitaalive::L(::psvitaalive::TextId::FilterGdOnly), 2600);
    } else {
        showToast(::psvitaalive::L(::psvitaalive::TextId::FilterDlcOnly), 2600);
    }
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
    if(isPluginTypeLink(l) && isPluginAlreadyInstalled(l)){
        showToast(::psvitaalive::L(::psvitaalive::TextId::AlreadyInstalled), 2800);
        diagnostics::log(std::string("[UI] plugin already installed: ")+pluginInstallFilePath(l));
        return;
    }
    // PSP DLC only works with LiveArea BGDL or Adrenaline Folder — not Adrenaline ISO.
    if (state_.catalog == CatalogType::PspGames && isDlcTypeLink(l)
        && settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline
        && settingsEdit_.pspMediaFormat == ::psvitaalive::PspMediaFormat::Iso) {
        showToast(::psvitaalive::L(::psvitaalive::TextId::PspDlcBlocked), 3600);
        diagnostics::log("[UI] blocked PSP DLC install: Adrenaline + ISO mode");
        return;
    }
    // PSP/PS1 LiveArea requires NoPspEmuDrm (kern + user). Adrenaline path is unchanged.
    if ((state_.catalog == CatalogType::PspGames || state_.catalog == CatalogType::Ps1Games)
        && settingsEdit_.pspTarget == ::psvitaalive::PspTarget::LiveArea) {
        const bool noPspEmuDrmReady =
            pluginsStatus_.nopspemudrmKern && pluginsStatus_.nopspemudrmUser;
        if (!noPspEmuDrmReady) {
            showToast(::psvitaalive::L(::psvitaalive::TextId::PspLiveAreaNeedsNoPspEmuDrm), 3600);
            diagnostics::log(std::string("[UI] blocked PSP/PS1 LiveArea download: NoPspEmuDrm incomplete")
                             + " kern=" + std::to_string(pluginsStatus_.nopspemudrmKern ? 1 : 0)
                             + " user=" + std::to_string(pluginsStatus_.nopspemudrmUser ? 1 : 0));
            return;
        }
    }
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
    showToast(std::string(::psvitaalive::L(::psvitaalive::TextId::LoadingCatalog)) + " " + catalogName(n) + "...", 2200);
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

    // Block all underlying UI while reboot modal is up (only Restart button is live).
    if (pluginRebootModal_) {
        if (td.reportNum <= 0) {
            if (touchDown_ && !touchMoved_) {
                const int w = 720, h = 320;
                const int ox = (SCREEN_W - w) / 2, oy = (SCREEN_H - h) / 2;
                const int bx = ox + 28, by = oy + h - 72, bw = w - 56, bh = 56;
                if (hit(touchStartX_, touchStartY_, bx, by, bw, bh)) {
                    diagnostics::log("[UI] Plugin reboot modal: soft reset (touch release)");
                    scePowerRequestColdReset();
                }
            }
            touchDown_ = false;
            return;
        }
        const int x = mapX(td.report[0].x);
        const int y = mapY(td.report[0].y);
        if (!touchDown_) {
            touchDown_ = true;
            touchStartX_ = x;
            touchStartY_ = y;
            touchMoved_ = false;
        } else {
            if (std::abs(x - touchStartX_) > 18 || std::abs(y - touchStartY_) > 18) touchMoved_ = true;
        }
        return;
    }

    // Essential plugins prompt
    if (essentialPluginsModal_) {
        if (td.reportNum <= 0) {
            if (touchDown_ && !touchMoved_) {
                const int w = 820, h = 460;
                const int ox = (SCREEN_W - w) / 2, oy = (SCREEN_H - h) / 2;
                const int btnH = 58, gap = 14;
                const int btnY = oy + h - 82;
                const int btnW = (w - 56 - gap) / 2;
                const int x0 = ox + 28;
                const int x1 = x0 + btnW + gap;
                if (hit(touchStartX_, touchStartY_, x0, btnY, btnW, btnH)) {
                    closeEssentialPluginsPrompt(true);
                } else if (hit(touchStartX_, touchStartY_, x1, btnY, btnW, btnH)) {
                    closeEssentialPluginsPrompt(false);
                }
            }
            touchDown_ = false;
            return;
        }
        const int x = mapX(td.report[0].x);
        const int y = mapY(td.report[0].y);
        if (!touchDown_) {
            touchDown_ = true;
            touchStartX_ = x;
            touchStartY_ = y;
            touchMoved_ = false;
        } else {
            if (std::abs(x - touchStartX_) > 18 || std::abs(y - touchStartY_) > 18) touchMoved_ = true;
        }
        return;
    }

    // --- First-run theme setup: scroll grid + tap theme / Save ---
    if (themeSetupVisible_) {
        const int themeCount = static_cast<int>(::psvitaalive::ColorTheme::Count);
        const int cols = 3;
        const int rows = (themeCount + cols - 1) / cols;
        const int btnW = 220, btnH = 44, gapX = 12, gapY = 10;
        const int gridW = cols * btnW + (cols - 1) * gapX;
        const int ow = 760, oh = 460;
        const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
        const int gridTop = oy + 118;
        const int gridBottom = oy + oh - 88;
        const int gridH = gridBottom - gridTop;
        const int rowH = btnH + gapY;
        const int visibleRows = std::max(1, gridH / rowH);
        const int maxScroll = std::max(0, rows - visibleRows);
        const int gridX = ox + (ow - gridW) / 2;
        const int saveW = 260, saveH = 48;
        const int saveX = ox + (ow - saveW) / 2;
        const int saveY = oy + oh - 72;

        if (td.reportNum > 0) {
            const int px = mapX(td.report[0].x);
            const int py = mapY(td.report[0].y);
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = px;
                touchStartY_ = py;
                touchLastY_ = py;
                touchMoved_ = false;
                touchAccumY_ = 0.f;
            } else {
                const int dy = py - touchLastY_;
                touchLastY_ = py;
                if (std::abs(px - touchStartX_) > 14 || std::abs(py - touchStartY_) > 14)
                    touchMoved_ = true;
                // Same as main catalog / D-Pad: drag steps move focus (grid rows + Save)
                if (touchMoved_) {
                    constexpr float kScrollPx = 48.f;
                    touchAccumY_ += static_cast<float>(dy);
                    auto afterMove = [&]() {
                        clampThemePickerScroll(themeSetupFocus_, themeSetupScrollRow_, themeCount, cols, visibleRows);
                    };
                    while (touchAccumY_ <= -kScrollPx) {
                        touchAccumY_ += kScrollPx;
                        // finger up → next row (like D-Pad DOWN)
                        if (themeSetupFocus_ < themeCount) {
                            const int n = themeSetupFocus_ + cols;
                            if (n < themeCount) themeSetupFocus_ = n;
                            else themeSetupFocus_ = themeCount; // Save button
                        }
                        afterMove();
                    }
                    while (touchAccumY_ >= kScrollPx) {
                        touchAccumY_ -= kScrollPx;
                        // finger down → previous row (like D-Pad UP)
                        if (themeSetupFocus_ == themeCount) {
                            themeSetupFocus_ = std::max(0, themeCount - 1);
                        } else if (themeSetupFocus_ >= cols) {
                            themeSetupFocus_ -= cols;
                        }
                        afterMove();
                    }
                }
                (void)maxScroll;
                (void)rowH;
            }
        } else if (touchDown_) {
            const int px = touchStartX_, py = touchStartY_;
            const bool wasMoved = touchMoved_;
            touchDown_ = false;
            touchAccumY_ = 0.f;
            if (!wasMoved) {
                if (hit(px, py, saveX - 8, saveY - 8, saveW + 16, saveH + 16)) {
                    closeThemeSetup(true);
                } else {
                    for (int r = 0; r < rows; ++r) {
                        for (int c = 0; c < cols; ++c) {
                            const int idx = r * cols + c;
                            if (idx >= themeCount) continue;
                            const int bx = gridX + c * (btnW + gapX);
                            const int by = gridTop + (int)(((float)r - visualThemeSetupScroll_) * (float)rowH);
                            if (hit(px, py, bx, by, btnW, btnH)) {
                                themeSetupFocus_ = idx;
                                if (themeSetupAppliedFocus_ == themeSetupFocus_) {
                                    closeThemeSetup(true);
                                } else {
                                    applyThemeSetupFocus();
                                    themeSetupAppliedFocus_ = themeSetupFocus_;
                                    showToast(::psvitaalive::L(::psvitaalive::TextId::ThemePreviewToast), 1800);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        return;
    }

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
            const int ow = 820, oh = 460;
            const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
            if (installAllPhase_ == InstallAllPhase::Confirm) {
                const int bw = 240, bh = 50;
                const int by = oy + oh - 68;
                const int bxOk = ox + 28;
                const int bxCancel = ox + ow - 28 - bw;
                if (hit(x, y, bxOk - 12, by - 12, bw + 24, bh + 24)) { installAllFocus_ = 0; installAllAdvancePick(); return; }
                if (hit(x, y, bxCancel - 12, by - 12, bw + 24, bh + 24)) { closeInstallAllWizard(true); return; }
                return;
            }
            if (installAllItemIndex_ >= 0 && installAllItemIndex_ < (int)catalogView().size()) {
                const int listTop = oy + 98;
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
            const int ow = 900, oh = 508, ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
            const int resultBy = oy + 360, runningBy = oy + 410, bh = 50;
            if (installOutcome_ == 3) {
                // Cancelled: only Close (centered), no Report
                const int bwClose = 280;
                const int bxClose = ox + (ow - bwClose) / 2;
                if (hit(x, y, bxClose - 8, resultBy - 8, bwClose + 16, 40 + 16)) {
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
                    if (hit(x, y, bxClose - 8, resultBy - 8, bwClose + 16, 40 + 16)) {
                        if (installAcknowledge_) installAcknowledge_();
                        reportUiState_ = 0;
                    }
                    return;
                }
                const int bwReport = 200, bwClose = 200;
                const int bxReport = ox + 28, bxClose = ox + ow - 28 - bwClose;
                if (hit(x, y, bxReport - 8, resultBy - 8, bwReport + 16, 40 + 16)) {
                    trySendErrorReport(
                        "Installation failed",
                        installProgressMessage_ + " | file=" + installProgressFile_);
                    return;
                }
                if (hit(x, y, bxClose - 8, resultBy - 8, bwClose + 16, 40 + 16)) {
                    if (installAcknowledge_) installAcknowledge_();
                    reportUiState_ = 0;
                    return;
                }
                return;
            }
            const int bw = 280;
            if (installOutcome_ == 1) {
                if (!hit(x, y, ox + 20, resultBy - 8, bw + 16, 40 + 16)) return;
                if (installAcknowledge_) installAcknowledge_();
                if (installAllFinishedToast_) {
                    installAllFinishedToast_ = false;
                    showToast(::psvitaalive::L(::psvitaalive::TextId::ToastAllInstalled), 2800);
                }
            } else {
                const int cancelW = 520;
                const int cancelX = ox + (ow - cancelW) / 2;
                if (!hit(x, y, cancelX - 12, runningBy - 12, cancelW + 24, 50 + 24)) return;
                if (installCancel_) installCancel_();
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
        Meta meta[9] = {
            {true, "INSTALL"}, {false, ""}, {false, ""},
            {true, "INTERFACE"}, {false, ""}, {false, ""}, {false, ""},
            {true, "CATALOG"}, {true, "UPDATES"}
        };
        int rowY[8];
        int y = contentTop - static_cast<int>(settingsScrollY_);
        for (int i = 0; i < 9; ++i) {
            if (meta[i].sectionStart && meta[i].section[0]) y += 22;
            rowY[i] = y;
            y += 52 + 8;
        }
        const int rowH = 52;
        const int measured = 9 * (52 + 8) + 4 * 22;
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
                // Same as main catalog: drag steps move focus like D-Pad up/down
                if (touchMoved_) {
                    constexpr float kScrollPx = 48.f;
                    constexpr int kRows = 9;
                    touchAccumY_ += static_cast<float>(dy);
                    while (touchAccumY_ <= -kScrollPx) {
                        touchAccumY_ += kScrollPx;
                        settingsFocus_ = (settingsFocus_ + 1) % kRows;
                    }
                    while (touchAccumY_ >= kScrollPx) {
                        touchAccumY_ -= kScrollPx;
                        settingsFocus_ = (settingsFocus_ + kRows - 1) % kRows;
                    }
                    (void)maxScroll; // auto-scroll in drawSettings follows focus
                }
            }
        } else if (touchDown_) {
            const int x = touchStartX_, yy = touchStartY_;
            touchDown_ = false;
            // Allow slight finger jitter — still treat as tap if not a long drag
            for (int i = 0; i < 9; ++i) {
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
        const int reportW = 100;
        const int newsW = 100;
        const int chipH = FOOTER_H - 8;
        const int reportX = panelX - reportW - 8;
        const int newsX = reportX - newsW - 8;
        const int chipY = SCREEN_H - FOOTER_H + 4;
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
        const int clockReserve = 100;
        const int filterGap = 8;
        // Match drawHeader exactly so the visual search box and touch hitbox stay aligned.
        int barX = 200;
        if (headerLogoTex_) {
            const float lw = (float)vita2d_texture_get_width(headerLogoTex_);
            const float lh = (float)vita2d_texture_get_height(headerLogoTex_);
            const float maxH = (float)(HEADER_H - 10);
            const float maxW = 190.f;
            float sc = maxH / (lh > 1.f ? lh : 1.f);
            if (lw * sc > maxW) sc = maxW / (lw > 1.f ? lw : 1.f);
            barX = (int)(10.f + lw * sc + 12.f);
            if (barX < 160) barX = 160;
        }
        const bool showContentFilter = catalogSupportsContentFilter(state_.catalog);
        const char* filterLab = (state_.catalog == CatalogType::Homebrew) ? "G/D Files" : "DLC";
        int gdW = 118;
        if (showContentFilter && font_) {
            gdW = vita2d_pgf_text_width(font_, 0.70f, filterLab) + 28;
            if (gdW < 72) gdW = 72;
            if (gdW > 128) gdW = 128;
        }
        int barW = std::max(100, SCREEN_W - barX - clockReserve - (showContentFilter ? (gdW + filterGap) : 0));
        if (showContentFilter) {
            const int chipEnd = barX + barW + filterGap + gdW;
            if (chipEnd > SCREEN_W - clockReserve + 4)
                barW = std::max(80, SCREEN_W - barX - clockReserve - gdW - filterGap);
        }
        const int gdX = barX + barW + filterGap;
        if (showContentFilter && hit(x, y, gdX, barY, gdW, barH)) {
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
    applyColorTheme(settingsEdit_.colorTheme, false);
    // Apply saved typeface (init may have run with defaults before settings were injected).
    vita2d_wait_rendering_done();
    if (vita2d_pgf* nf = ::psvitaalive::ui::loadUiFont(settingsEdit_.uiFontStyle)) {
        if (font_) vita2d_free_pgf(font_);
        font_ = nf;
    }
}

void FullCatalogScreen::setPluginStatus(const ::psvitaalive::PluginStatus& plugins) {
    pluginsStatus_ = plugins;
}

void FullCatalogScreen::setSettingsSaveCallback(SettingsSaveFn callback) {
    settingsSave_ = std::move(callback);
}

void FullCatalogScreen::openSettings() {
    if (installProgressActive_ || catalogLoading_ || selfUpdateBusy_.load()) {
        showToast(::psvitaalive::L(::psvitaalive::TextId::LockedFinishJob), 2800);
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
        showToast(::psvitaalive::L(::psvitaalive::TextId::SettingsSaved), 1600);
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
            ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStage),
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
            ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStage),
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
        showToast(::psvitaalive::L(::psvitaalive::TextId::LockedStillRunning), 2800);
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
        sceClibSnprintf(selfUpdateMsg_, sizeof(selfUpdateMsg_), "%s", ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStarting));
        closeSettings(true);
        setInstallProgress(true, 0, selfUpdateInfo_.assetSize, 0, ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStage), "PSVitaAlive.vpk",
                           ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStarting), 0, false);

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
            setInstallProgress(true, 0, 0, 0, ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStage), "PSVitaAlive.vpk",
                               ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateThreadFailed), 2, false);
            showToast(::psvitaalive::L(::psvitaalive::TextId::ToastUpdateThreadFailed), 2000);
            return;
        }
        const int st = sceKernelStartThread(selfUpdateThread_, sizeof(self), &self);
        if (st < 0) {
            sceKernelDeleteThread(selfUpdateThread_);
            selfUpdateThread_ = -1;
            selfUpdateBusy_.store(false);
            setInstallProgress(true, 0, 0, 0, ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStage), "PSVitaAlive.vpk",
                               ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateThreadFailed), 2, false);
            return;
        }
        diagnostics::log("[UI] self-update apply started");
        return;
    }

    showToast(::psvitaalive::L(::psvitaalive::TextId::ToastCheckingUpdates), 1200);
    selfUpdateInfo_ = ::psvitaalive::UpdateChecker::checkLatest(PSVITAALIVE_VERSION);
    selfUpdateChecked_ = true;
    if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable) {
        showToast(std::string(::psvitaalive::L(::psvitaalive::TextId::ToastUpdateAvailablePrefix)) + selfUpdateInfo_.remoteVersion + ::psvitaalive::L(::psvitaalive::TextId::ToastUpdateAvailableSuffix), 2800);
    } else if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate) {
        showToast(std::string(::psvitaalive::L(::psvitaalive::TextId::ToastUpToDatePrefix)) + selfUpdateInfo_.localVersion + ::psvitaalive::L(::psvitaalive::TextId::ToastUpToDateSuffix), 2200);
    } else {
        showToast(selfUpdateInfo_.error.empty() ? ::psvitaalive::L(::psvitaalive::TextId::UpdateCheckFailed) : selfUpdateInfo_.error, 2600);
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
        settingsEdit_.pspMediaFormat = (settingsEdit_.pspMediaFormat == ::psvitaalive::PspMediaFormat::Folder)
            ? ::psvitaalive::PspMediaFormat::Iso : ::psvitaalive::PspMediaFormat::Folder;
    } else if (row == 3) {
        // Language: System → each available language → System …
        auto& loc = ::psvitaalive::LocalizationManager::instance();
        std::vector<::psvitaalive::Language> langs = loc.availableLanguages();
        // Build cycle entries: index 0 = system, then manual codes
        const int n = 1 + static_cast<int>(langs.size());
        int idx = 0;
        if (settingsEdit_.languageMode == ::psvitaalive::LanguageMode::Manual) {
            idx = 1;
            for (size_t i = 0; i < langs.size(); ++i) {
                if (settingsEdit_.language == ::psvitaalive::languageCode(langs[i])) {
                    idx = 1 + static_cast<int>(i);
                    break;
                }
            }
        }
        idx = (idx + delta) % n;
        if (idx < 0) idx += n;
        if (idx == 0) {
            settingsEdit_.languageMode = ::psvitaalive::LanguageMode::System;
            settingsEdit_.language = "en";
        } else {
            settingsEdit_.languageMode = ::psvitaalive::LanguageMode::Manual;
            settingsEdit_.language = ::psvitaalive::languageCode(langs[static_cast<size_t>(idx - 1)]);
        }
        loc.setMode(settingsEdit_.languageMode, settingsEdit_.language);
        diagnostics::log(std::string("[UI] language set mode=") +
                         ::psvitaalive::AppSettings::toString(settingsEdit_.languageMode) +
                         " code=" + settingsEdit_.language);
    } else if (row == 4) {
        int v = static_cast<int>(settingsEdit_.uiFontStyle);
        const int n = static_cast<int>(::psvitaalive::UiFontStyle::Count);
        v = (v + delta) % n;
        if (v < 0) v += n;
        settingsEdit_.uiFontStyle = static_cast<::psvitaalive::UiFontStyle>(v);
        // Live preview: swap PGF after GPU idle so mid-frame draws stay safe.
        vita2d_wait_rendering_done();
        vita2d_pgf* nf = ::psvitaalive::ui::loadUiFont(settingsEdit_.uiFontStyle);
        if (nf) {
            if (font_) vita2d_free_pgf(font_);
            font_ = nf;
            // If requested style missing, loader already fell back to default.
            if (settingsEdit_.uiFontStyle != ::psvitaalive::UiFontStyle::Default) {
                // Heuristic toast only when still default path? Skip — silent fallback is OK.
            }
            diagnostics::log(std::string("[UI] font style=") +
                             ::psvitaalive::AppSettings::toString(settingsEdit_.uiFontStyle));
        }
    } else if (row == 5) {
        (void)delta;
        openThemePicker(); // same palette window as first-run setup
    } else if (row == 6) {
        settingsEdit_.warnMissingPlugins = !settingsEdit_.warnMissingPlugins;
    } else if (row == 7) {
        settingsEdit_.promptImageWarmup = !settingsEdit_.promptImageWarmup;
    } else if (row == 8) {
        triggerSelfUpdateAction();
    }
}

void FullCatalogScreen::handleSettingsInput(uint32_t pressed, uint32_t nav) {
    constexpr int kRows = 9;
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
    vita2d_pgf_draw_text(font_, 20, HEADER_H + 30 + slide, ACCENT, 1.00f, ::psvitaalive::L(::psvitaalive::TextId::Settings));
    vita2d_pgf_draw_text(font_, SCREEN_W - 300, HEADER_H + 28 + slide, DIM, 0.56f, ::psvitaalive::L(::psvitaalive::TextId::SettingsSaveBack));

    const int margin = 20;
    const int contentTop = HEADER_H + 56 + slide;
    const int contentH = SCREEN_H - contentTop - FOOTER_H - 6;
    const int listClipBottom = contentTop + contentH - 8;

    using TID = ::psvitaalive::TextId;
    auto methodLabel = [&]() -> std::string {
        if (settingsEdit_.installMethod == ::psvitaalive::InstallMethod::Auto)
            return ::psvitaalive::L(TID::Auto);
        if (settingsEdit_.installMethod == ::psvitaalive::InstallMethod::Direct)
            return ::psvitaalive::L(TID::Direct);
        return "BGDL";
    };
    auto pspLabel = [&]() -> std::string {
        return settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline
            ? ::psvitaalive::L(TID::Adrenaline) : ::psvitaalive::L(TID::LiveArea);
    };
    auto mediaFormatLabel = [&]() -> std::string {
        return settingsEdit_.pspMediaFormat == ::psvitaalive::PspMediaFormat::Iso
            ? ::psvitaalive::L(TID::Iso) : ::psvitaalive::L(TID::Folder);
    };
    auto languageLabel = [&]() -> std::string {
        if (settingsEdit_.languageMode == ::psvitaalive::LanguageMode::System)
            return ::psvitaalive::L(TID::SystemAutomatic);
        if (settingsEdit_.language == "es")
            return ::psvitaalive::L(TID::Spanish);
        return ::psvitaalive::L(TID::English);
    };
    auto updateLabel = [&]() -> std::string {
        if (selfUpdateBusy_.load()) return ::psvitaalive::L(TID::UpdateWorking);
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable) {
            return std::string(::psvitaalive::L(TID::UpdateInstallPrefix)) + " " + selfUpdateInfo_.remoteVersion;
        }
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate) {
            return ::psvitaalive::L(TID::UpdateUpToDate);
        }
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::Failed) {
            return ::psvitaalive::L(TID::UpdateCheckFailed);
        }
        return std::string("v") + PSVITAALIVE_VERSION;
    };
    auto themeLabel = [&]() -> std::string {
        return std::string(colorThemeDisplayName(settingsEdit_.colorTheme));
    };
    auto yesNo = [&](bool v) -> std::string {
        return v ? ::psvitaalive::L(TID::Yes) : ::psvitaalive::L(TID::No);
    };

    struct Opt {
        const char* section;
        const char* label;
        std::string value;
        const char* hint;
        bool sectionStart;
    };
    auto fontLabel = [&]() -> std::string {
        switch (settingsEdit_.uiFontStyle) {
            case ::psvitaalive::UiFontStyle::Serif: return ::psvitaalive::L(TID::FontSerif);
            case ::psvitaalive::UiFontStyle::Sans: return ::psvitaalive::L(TID::FontSans);
            case ::psvitaalive::UiFontStyle::SerifBold: return ::psvitaalive::L(TID::FontSerifBold);
            case ::psvitaalive::UiFontStyle::SansBold: return ::psvitaalive::L(TID::FontSansBold);
            default: return ::psvitaalive::L(TID::FontDefault);
        }
    };
    Opt opts[9] = {
        {::psvitaalive::L(TID::SectionInstall), ::psvitaalive::L(TID::InstallMethod), methodLabel(), ::psvitaalive::L(TID::HintInstallMethod), true},
        {"", ::psvitaalive::L(TID::PspPs1Target), pspLabel(), ::psvitaalive::L(TID::HintPspTarget), false},
        {"", ::psvitaalive::L(TID::PspMediaAdrenaline), mediaFormatLabel(), ::psvitaalive::L(TID::HintPspMedia), false},
        {::psvitaalive::L(TID::SectionInterface), ::psvitaalive::L(TID::Language), languageLabel(), ::psvitaalive::L(TID::HintLanguage), true},
        {"", ::psvitaalive::L(TID::UiFont), fontLabel(), ::psvitaalive::L(TID::HintUiFont), false},
        {"", ::psvitaalive::L(TID::ColorTheme), themeLabel() + "  >", ::psvitaalive::L(TID::HintColorTheme), false},
        {"", ::psvitaalive::L(TID::WarnMissingPlugins), yesNo(settingsEdit_.warnMissingPlugins), ::psvitaalive::L(TID::HintWarnPlugins), false},
        {::psvitaalive::L(TID::SectionCatalog), ::psvitaalive::L(TID::PromptImageDownload), yesNo(settingsEdit_.promptImageWarmup), ::psvitaalive::L(TID::HintImageWarmup), true},
        {::psvitaalive::L(TID::SectionUpdates), ::psvitaalive::L(TID::CheckForUpdates), updateLabel(), ::psvitaalive::L(TID::HintSelfUpdate), true},
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
    for (int i = 0; i < 9; ++i) {
        if (opts[i].sectionStart && opts[i].section[0]) measured += sectionH;
        measured += rowH + rowGap;
    }
    const int listViewH = listClipBottom - contentTop;
    const float maxScroll = static_cast<float>(std::max(0, measured - listViewH));
    if (settingsScrollY_ < 0.f) settingsScrollY_ = 0.f;
    if (settingsScrollY_ > maxScroll) settingsScrollY_ = maxScroll;

    {
        int fy = 0;
        for (int i = 0; i <= settingsFocus_ && i < 9; ++i) {
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

    /* Clip list so scrolled rows never paint over the Settings title bar or footer. */
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(listX, contentTop, listX + listW, listClipBottom);

    int rowY[8] = {};
    int y = contentTop - static_cast<int>(settingsScrollY_);
    for (int i = 0; i < 9; ++i) {
        if (opts[i].sectionStart && opts[i].section[0]) {
            vita2d_pgf_draw_text(font_, listX + 6, y + 16, DIM, 0.56f, opts[i].section);
            y += sectionH;
        }
        rowY[i] = y;
        const bool focus = (settingsFocus_ == i);
        {
            vita2d_draw_rectangle(listX, y, listW, rowH, focus ? SURFACE : SURFACE2);
            if (focus) {
                vita2d_draw_rectangle(listX, y, 4, rowH, ACCENT);
                vita2d_draw_rectangle(listX, y, listW, 2, ACCENT);
                vita2d_draw_rectangle(listX, y + rowH - 2, listW, 2, ACCENT);
            } else {
                vita2d_draw_rectangle(listX, y + rowH - 1, listW, 1, BORDER);
            }
            /* Keep label/value inside the row card; ellipsize long values. */
            const int chipW = 110;
            const int chipX = listX + listW - chipW - 10;
            const int chipY = y + 12;
            const int labelMaxW = chipX - (listX + 14) - 8;
            (void)labelMaxW;
            vita2d_pgf_draw_text(font_, listX + 14, y + 22, focus ? WHITE : TEXT, 0.76f, opts[i].label);
            vita2d_draw_rectangle(chipX, chipY, chipW, 24, focus ? ACCENT : SURFACE);
            vita2d_pgf_draw_text(font_, chipX + 8, chipY + 17, focus ? BG : ACCENT, 0.54f,
                                 ellipsize(opts[i].value, 14).c_str());
            vita2d_pgf_draw_text(font_, listX + 14, y + 42, DIM, 0.48f,
                                 ellipsize(std::string(opts[i].hint), 42).c_str());
            if (focus) vita2d_pgf_draw_text(font_, chipX - 32, chipY + 17, ACCENT, 0.52f, "<>");
        }
        y += rowH + rowGap;
    }
    vita2d_disable_clipping();

    /* Repaint Settings title strip so nothing from the list can sit on top of it. */
    vita2d_draw_rectangle(0, HEADER_H + slide, SCREEN_W, 44, SURFACE2);
    vita2d_draw_rectangle(0, HEADER_H + slide, SCREEN_W, 3, ACCENT);
    vita2d_pgf_draw_text(font_, 20, HEADER_H + 30 + slide, ACCENT, 1.00f, ::psvitaalive::L(::psvitaalive::TextId::Settings));
    vita2d_pgf_draw_text(font_, SCREEN_W - 300, HEADER_H + 28 + slide, DIM, 0.56f, ::psvitaalive::L(::psvitaalive::TextId::SettingsSaveBack));

    {
        const int panelH = listClipBottom - contentTop;
        vita2d_draw_rectangle(sideX, contentTop, sideW, panelH, SURFACE2);
        vita2d_draw_rectangle(sideX, contentTop, 3, panelH, ACCENT);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 24, ACCENT, 0.90f, ::psvitaalive::L(TID::Info));

        const char* title = opts[settingsFocus_].label;
        const char* body1 = "";
        const char* body2 = "";
        const char* body3 = "";
        switch (settingsFocus_) {
        case 0:
            body1 = ::psvitaalive::L(TID::InfoInstallMethod1);
            body2 = ::psvitaalive::L(TID::InfoInstallMethod2);
            body3 = ::psvitaalive::L(TID::InfoInstallMethod3);
            break;
        case 1:
            body1 = ::psvitaalive::L(TID::InfoPspTarget1);
            body2 = ::psvitaalive::L(TID::InfoPspTarget2);
            body3 = ::psvitaalive::L(TID::InfoPspTarget3);
            break;
        case 2:
            body1 = ::psvitaalive::L(TID::InfoPspMedia1);
            body2 = ::psvitaalive::L(TID::InfoPspMedia2);
            body3 = ::psvitaalive::L(TID::InfoPspMedia3);
            break;
        case 3:
            body1 = ::psvitaalive::L(TID::InfoLanguage1);
            body2 = ::psvitaalive::L(TID::InfoLanguage2);
            body3 = ::psvitaalive::L(TID::InfoLanguage3);
            break;
        case 4:
            body1 = ::psvitaalive::L(TID::InfoFont1);
            body2 = ::psvitaalive::L(TID::InfoFont2);
            body3 = ::psvitaalive::L(TID::InfoFont3);
            break;
        case 5:
            body1 = ::psvitaalive::L(TID::InfoColorTheme1);
            body2 = ::psvitaalive::L(TID::InfoColorTheme2);
            body3 = ::psvitaalive::L(TID::InfoColorTheme3);
            break;
        case 6:
            body1 = ::psvitaalive::L(TID::InfoWarnPlugins1);
            body2 = ::psvitaalive::L(TID::InfoWarnPlugins2);
            body3 = ::psvitaalive::L(TID::InfoWarnPlugins3);
            break;
        case 7:
            body1 = ::psvitaalive::L(TID::InfoImageWarmup1);
            body2 = ::psvitaalive::L(TID::InfoImageWarmup2);
            body3 = ::psvitaalive::L(TID::InfoImageWarmup3);
            break;
        case 8:
            body1 = ::psvitaalive::L(TID::InfoSelfUpdate1);
            body2 = ::psvitaalive::L(TID::InfoSelfUpdate2);
            body3 = ::psvitaalive::L(TID::InfoSelfUpdate3);
            break;
        default: break;
        }
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 52, WHITE, 0.88f, title);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 80, TEXT, 0.72f, body1);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 102, TEXT, 0.72f, body2);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 124, TEXT, 0.72f, body3);

        // SYSTEM: DRM plugins + essential homebrew plugins (file + config when required)
        int sy = contentTop + 158;
        vita2d_pgf_draw_text(font_, sideX + 14, sy, DIM, 0.78f, ::psvitaalive::L(TID::System));
        sy += 24;
        char plug[96];
        auto drawPlugLine = [&](const char* label, bool ok) {
            sceClibSnprintf(plug, sizeof(plug), "%s: %s", label,
                            ok ? ::psvitaalive::L(TID::StatusOk) : ::psvitaalive::L(TID::StatusMissing));
            const unsigned col = ok ? TEXT : ACCENT;
            vita2d_pgf_draw_text(font_, sideX + 14, sy, col, 0.72f, plug);
            sy += 22;
        };
        drawPlugLine("NoNpDrm", pluginsStatus_.nonpdrm);
        drawPlugLine("NoPspEmuDrm", pluginsStatus_.nopspemudrmKern);
        {
            const bool kub = essentialPluginFullyInstalled(
                "*KERNEL", "ur0:tai/kubridge.skprx",
                {"ur0:tai/kubridge.skprx", "ux0:tai/kubridge.skprx"});
            // FdFix requirement is satisfied by FdFix itself OR by RePatch (compatibility).
            const bool fdfFile = essentialPluginFullyInstalled(
                "*KERNEL", "ur0:tai/fd_fix.skprx",
                {"ur0:tai/fd_fix.skprx", "ux0:tai/fd_fix.skprx"});
            const bool fdf = fdfFile || pluginsStatus_.fdFix || pluginsStatus_.repatch;
            const bool sha = essentialPluginFullyInstalled(
                "none", "ur0:data/libshacccg.suprx",
                {"ur0:data/libshacccg.suprx", "ur0:/data/libshacccg.suprx"});
            drawPlugLine("kubridge", kub);
            drawPlugLine("RePatch", pluginsStatus_.repatch);
            drawPlugLine("fd_fix", fdf);
            drawPlugLine("libshacccg", sha);
        }
        if (!pluginsStatus_.configPathUsed.empty()) {
            vita2d_pgf_draw_text(font_, sideX + 14, sy, DIM, 0.64f,
                                 ellipsize(pluginsStatus_.configPathUsed, 26).c_str());
            sy += 20;
        }
        if (settingsFocus_ == 8) {
            char ver[64];
            sceClibSnprintf(ver, sizeof(ver), "%s: v%s", ::psvitaalive::L(TID::LocalVersion), PSVITAALIVE_VERSION);
            vita2d_pgf_draw_text(font_, sideX + 14, sy, ACCENT, 0.72f, ver);
            sy += 22;
            if (selfUpdateChecked_) {
                if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable)
                    sceClibSnprintf(ver, sizeof(ver), "%s: v%s", ::psvitaalive::L(TID::RemoteVersion),
                                    selfUpdateInfo_.remoteVersion.c_str());
                else if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate)
                    sceClibSnprintf(ver, sizeof(ver), "%s: %s", ::psvitaalive::L(TID::RemoteVersion),
                                    ::psvitaalive::L(TID::RemoteUpToDate));
                else
                    sceClibSnprintf(ver, sizeof(ver), "%s: %s", ::psvitaalive::L(TID::RemoteVersion),
                                    ::psvitaalive::L(TID::RemoteCheckFailed));
                vita2d_pgf_draw_text(font_, sideX + 14, sy, TEXT, 0.70f, ver);
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
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT, 0.56f, ::psvitaalive::L(TID::SettingsFooter));
    drawToast();
    if (themeSetupVisible_) drawThemeSetupOverlay();
    if (essentialPluginsModal_) drawEssentialPluginsOverlay();
    if (pluginRebootModal_) drawPluginRebootOverlay();
    vita2d_end_drawing();
    vita2d_swap_buffers();

    // Expose row geometry for touch via static (same frame layout)
    // Stored for handleTouch — simple approach: recompute same math there.
    (void)rowY;
}

void FullCatalogScreen::handleInput(){
    if (pluginRebootModal_) {
        // Soft restart only — do not allow LiveArea exit or underlying UI.
        // Touch is handled exclusively in handleTouch() so it cannot leak through.
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        static uint32_t prevButtonsReboot = 0;
        const uint32_t pressed = pad.buttons & ~prevButtonsReboot;
        prevButtonsReboot = pad.buttons;
        if (pressed & SCE_CTRL_CROSS) {
            diagnostics::log("[UI] Plugin reboot modal: soft reset requested");
            scePowerRequestColdReset();
        }
        return;
    }
    if (essentialPluginsModal_) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        static uint32_t prevButtonsEss = 0;
        const uint32_t pressed = pad.buttons & ~prevButtonsEss;
        prevButtonsEss = pad.buttons;
        if (pressed & SCE_CTRL_LEFT) essentialPluginsFocus_ = 0;
        if (pressed & SCE_CTRL_RIGHT) essentialPluginsFocus_ = 1;
        if (pressed & SCE_CTRL_CROSS) closeEssentialPluginsPrompt(essentialPluginsFocus_ == 0);
        if (pressed & SCE_CTRL_CIRCLE) closeEssentialPluginsPrompt(false);
        return;
    }
if(isTransitioning())return;SceCtrlData p{};sceCtrlPeekBufferPositive(0,&p,1);static uint32_t prev=0;static uint64_t repeatAt=0;uint32_t mask=SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT,pressed=p.buttons&~prev,direct=pressed&mask;uint64_t now=sceKernelGetProcessTimeWide(),repeat=0;if((p.buttons&mask)==0)repeatAt=0;else if(direct)repeatAt=now+DIRECTION_REPEAT_DELAY_US;else if(repeatAt&&now>=repeatAt){repeat=p.buttons&mask;repeatAt=now+DIRECTION_REPEAT_INTERVAL_US;}prev=p.buttons;uint32_t nav=direct|repeat;if(themeSetupVisible_){const int themeCount=static_cast<int>(::psvitaalive::ColorTheme::Count);const int cols=3;const int visibleRows=5;auto afterMove=[&](){clampThemePickerScroll(themeSetupFocus_,themeSetupScrollRow_,themeCount,cols,visibleRows);};if(nav&SCE_CTRL_LEFT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c>0){--themeSetupFocus_;afterMove();}}return;}if(nav&SCE_CTRL_RIGHT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c<cols-1&&themeSetupFocus_+1<themeCount){++themeSetupFocus_;afterMove();}}return;}if(nav&SCE_CTRL_UP){  if(themeSetupFocus_==themeCount){themeSetupFocus_=std::max(0,themeCount-1);}  else if(themeSetupFocus_>=cols)themeSetupFocus_-=cols;  else if(themeSetupScrollRow_>0)--themeSetupScrollRow_;  afterMove();return;}if(nav&SCE_CTRL_DOWN){  if(themeSetupFocus_<themeCount){int n=themeSetupFocus_+cols;if(n<themeCount)themeSetupFocus_=n;else themeSetupFocus_=themeCount;}  afterMove();return;}if(pressed&SCE_CTRL_CROSS){if(themeSetupFocus_==themeCount){closeThemeSetup(true);}else if(themeSetupAppliedFocus_==themeSetupFocus_){closeThemeSetup(true);}else{applyThemeSetupFocus();themeSetupAppliedFocus_=themeSetupFocus_;showToast(::psvitaalive::L(::psvitaalive::TextId::ThemePreviewToast),1800);}return;}return;}if(state_.mode==UiMode::SETTINGS){handleSettingsInput(pressed,nav);return;}
if(pressed&SCE_CTRL_SELECT){openSettings();return;}
if(pressed&SCE_CTRL_START){
        if(installProgressActive_ && installOutcome_==0){
            showToast(::psvitaalive::L(::psvitaalive::TextId::LockedCannotExit), 2800);
            return;
        }
        // After a successful self-update the running binary is stale — force exit.
        if(installProgressActive_ && installOutcome_==1 &&
           installProgressStage_.find(::psvitaalive::L(::psvitaalive::TextId::SelfUpdateStage))!=std::string::npos){
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
            showToast(::psvitaalive::L(::psvitaalive::TextId::LockedCannotSwitchCatalog), 2600);
        }else if(catalogLoading_){
            showToast(::psvitaalive::L(::psvitaalive::TextId::ChangingCatalog), 2200);
        }else if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty()){
            showToast(::psvitaalive::L(::psvitaalive::TextId::ToastPleaseWait), 900);
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
    }if(dataRequestConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeDataRequestConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeDataRequestConfirm();trySendDataRequest();return;}return;}if(reportConfirmVisible_){if(pressed&SCE_CTRL_CIRCLE){closeReportConfirm();return;}if(pressed&SCE_CTRL_CROSS){closeReportConfirm();trySendErrorReport("Manual report from UI","User confirmed report from footer");return;}return;}if(themeSetupVisible_){const int themeCount=static_cast<int>(::psvitaalive::ColorTheme::Count);const int cols=3;const int totalFocus=themeCount+1;if(nav&SCE_CTRL_LEFT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c>0)--themeSetupFocus_;}return;}if(nav&SCE_CTRL_RIGHT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c<cols-1&&themeSetupFocus_+1<themeCount)++themeSetupFocus_;}return;}if(nav&SCE_CTRL_UP||(pressed&SCE_CTRL_UP)){  if(themeSetupFocus_==themeCount){themeSetupFocus_=std::max(0,themeCount-cols);}  else if(themeSetupFocus_>=cols)themeSetupFocus_-=cols;  return;}if(nav&SCE_CTRL_DOWN||(pressed&SCE_CTRL_DOWN)){  if(themeSetupFocus_<themeCount){int n=themeSetupFocus_+cols;if(n<themeCount)themeSetupFocus_=n;else themeSetupFocus_=themeCount;}  return;}if(pressed&SCE_CTRL_CROSS){  if(themeSetupFocus_==themeCount){closeThemeSetup(true);}else if(themeSetupAppliedFocus_==themeSetupFocus_){closeThemeSetup(true);}else{applyThemeSetupFocus();themeSetupAppliedFocus_=themeSetupFocus_;showToast(::psvitaalive::L(::psvitaalive::TextId::ThemePreviewToast),1800);}  return;}return;}if(newsVisible_){if(pressed&SCE_CTRL_CIRCLE){closeNewsModal(newsMarkSeenOnClose_);return;}if(pressed&SCE_CTRL_UP||(nav&SCE_CTRL_UP)){if(newsScrollLine_>0)--newsScrollLine_;return;}if(pressed&SCE_CTRL_DOWN||(nav&SCE_CTRL_DOWN)){const int mv=std::max(1,(420-56-88)/22);const int ms=std::max(0,(int)newsLines_.size()-mv);if(newsScrollLine_<ms)++newsScrollLine_;return;}return;}if(installProgressActive_&&(pressed&SCE_CTRL_SQUARE)&&(installOutcome_==2)&&!isNonReportableInstallError(installProgressMessage_)){trySendErrorReport("Installation failed",installProgressMessage_+" | file="+installProgressFile_);return;}if(installProgressActive_&&(pressed&SCE_CTRL_CIRCLE)){if(installOutcome_==1||installOutcome_==2||installOutcome_==3){if(installAcknowledge_)installAcknowledge_();if(installOutcome_==1&&installAllFinishedToast_){installAllFinishedToast_=false;showToast(::psvitaalive::L(::psvitaalive::TextId::ToastAllInstalled),2800);}reportUiState_=0;}else if(installCancel_)installCancel_();return;}if(installProgressActive_){if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE|SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT)){if(installOutcome_==0)showToast(::psvitaalive::L(::psvitaalive::TextId::ToastLockedCircleOnly),2400);}return;}if(catalogLoading_)return;if(pressed&SCE_CTRL_SQUARE){if(state_.mode==UiMode::FULL_CATALOG){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast(::psvitaalive::L(::psvitaalive::TextId::ToastFiltersCleared),1200);}return;}if(state_.mode==UiMode::SPLIT_DETAIL&&state_.activePanel==UiPanel::Detail&&!state_.linkNavigation){const int si=selectedIndex();if(si>=0&&itemEligibleForDataRequest(catalogView()[si])){openDataRequestConfirm();return;}}return;}if(state_.mode==UiMode::FULL_CATALOG){if(pressed&SCE_CTRL_TRIANGLE){if(searchRequest_)applySearch(searchRequest_(searchQuery_));return;}if(nav&SCE_CTRL_LEFT&&state_.focusIndex%3>0)--state_.focusIndex;if(nav&SCE_CTRL_RIGHT&&state_.focusIndex%3<2&&state_.focusIndex+1<(int)catalogView().size())++state_.focusIndex;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);clampCatalogScroll();if(pressed&SCE_CTRL_CROSS)startOpeningDetail();return;}if(state_.mode!=UiMode::SPLIT_DETAIL)return;if(pressed&SCE_CTRL_CIRCLE){startClosingDetail();return;}if(state_.activePanel==UiPanel::Catalog){if(pressed&SCE_CTRL_RIGHT)state_.activePanel=UiPanel::Detail;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);return;}if(nav&SCE_CTRL_LEFT)state_.activePanel=UiPanel::Catalog;if(pressed&SCE_CTRL_TRIANGLE){if(state_.linkNavigation)exitLinkNavigation();else enterLinkNavigation();return;}if(state_.linkNavigation){if(nav&SCE_CTRL_UP)moveLinkFocus(0,-1);if(nav&SCE_CTRL_DOWN)moveLinkFocus(0,1);if(pressed&SCE_CTRL_CROSS)activateFocusedLink();return;}if(nav&SCE_CTRL_UP)moveDetailScroll(-1);if(nav&SCE_CTRL_DOWN)moveDetailScroll(1);}
unsigned FullCatalogScreen::colorForStatus(const std::string&s)const{if(s=="Verified")return ACCENT;if(s=="Legacy")return TEXT;if(s=="Archive")return DIM;return TEXT;}void FullCatalogScreen::drawHeader(int w){
    // Near-black bar + dual neon edge (LiveArea brand)
    vita2d_draw_rectangle(0, 0, w, HEADER_H, SURFACE2);
    vita2d_draw_rectangle(0, 0, w, 2, ACCENT);
    vita2d_draw_rectangle(0, HEADER_H - 1, w, 1, ACCENT_SOFT);
    // Brand: logo image (preferred) or compact text fallback
    int searchLeft = 200;
    {
        const bool brand = isBrandColorTheme(settingsEdit_.colorTheme);
        vita2d_texture* logoTex = (brand || !headerLogoMonoTex_) ? headerLogoTex_ : headerLogoMonoTex_;
        if (logoTex) {
        const float lw = (float)vita2d_texture_get_width(logoTex);
        const float lh = (float)vita2d_texture_get_height(logoTex);
        const float maxH = (float)(HEADER_H - 10);
        const float maxW = 190.f;
        float sc = maxH / (lh > 1.f ? lh : 1.f);
        if (lw * sc > maxW) sc = maxW / (lw > 1.f ? lw : 1.f);
        const float dw = lw * sc;
        const float dh = lh * sc;
        const float dx = 10.f;
        const float dy = ((float)HEADER_H - dh) * 0.5f;
        if (brand || !headerLogoMonoTex_)
            vita2d_draw_texture_scale(logoTex, dx, dy, sc, sc);
        else
            vita2d_draw_texture_tint_scale(logoTex, dx, dy, sc, sc, ACCENT);
        searchLeft = (int)(dx + dw + 12.f);
        if (searchLeft < 160) searchLeft = 160;
        } else {
        vita2d_pgf_draw_text(font_, 14, 30, ACCENT, 0.98f, "PSVitaAlive");
        searchLeft = 200;
        }
    }
    // Search + content filter to the RIGHT of the bar (same as Homebrew), then clock.
    // [logo][ search ][ gap ][ G/D Files | DLC ][ clock ] — chip never overlaps search.
    const int barY = 10, barH = 32;
    const int barX = searchLeft;
    const bool showContentFilter = catalogSupportsContentFilter(state_.catalog);
    const char* filterLab = (state_.catalog == CatalogType::Homebrew) ? "G/D Files" : "DLC";
    const float filterSc = 0.70f;
    int gdW = 118;
    if (showContentFilter && font_) {
        gdW = vita2d_pgf_text_width(font_, filterSc, filterLab) + 28;
        if (gdW < 72) gdW = 72;
        if (gdW > 128) gdW = 128;
    }
    const int filterGap = 8;
    const int clockReserve = 100;
    int barW = std::max(100, w - barX - clockReserve - (showContentFilter ? (gdW + filterGap) : 0));
    if (showContentFilter) {
        const int chipEnd = barX + barW + filterGap + gdW;
        if (chipEnd > w - clockReserve + 4)
            barW = std::max(80, w - barX - clockReserve - gdW - filterGap);
    }
    const int gdX = barX + barW + filterGap;
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
        vita2d_pgf_draw_text(font_, barX + 12, barY + 22, DIM, 0.66f, ::psvitaalive::L(::psvitaalive::TextId::SearchPlaceholder));
    } else {
        vita2d_pgf_draw_text(font_, barX + 12, barY + 22, ACCENT, 0.64f, ::psvitaalive::L(::psvitaalive::TextId::FilterActiveLabel));
        vita2d_pgf_draw_text(font_, barX + 78, barY + 22, WHITE, 0.66f, ellipsize(searchQuery_, 20).c_str());
        vita2d_pgf_draw_text(font_, barX + barW - 52, barY + 21, DIM, 0.52f, ::psvitaalive::L(::psvitaalive::TextId::FilterClearHint));
    }
    // Content filter chip immediately right of search (never drawn on top of the bar)
    if (showContentFilter) {
        const unsigned folderBg = dataFilesFilter_ ? RGBA8(0x5A, 0x42, 0x12, 255) : RGBA8(0x3A, 0x2C, 0x10, 255);
        const unsigned folderEdge = RGBA8(0xE8, 0xB4, 0x3A, 255);
        const unsigned folderText = RGBA8(0xFF, 0xD2, 0x6A, 255);
        vita2d_draw_rectangle(gdX, barY, gdW, barH, folderBg);
        vita2d_draw_rectangle(gdX, barY, gdW, 3, folderEdge);
        vita2d_draw_rectangle(gdX, barY, 3, barH, folderEdge);
        vita2d_draw_rectangle(gdX + gdW - 1, barY, 1, barH, folderEdge);
        vita2d_draw_rectangle(gdX, barY + barH - 1, gdW, 1, folderEdge);
        if (dataFilesFilter_) {
            vita2d_draw_rectangle(gdX, barY, gdW, 3, RGBA8(0xFF, 0xD2, 0x6A, 255));
        }
        const int tw = vita2d_pgf_text_width(font_, filterSc, filterLab);
        vita2d_pgf_draw_text(font_, gdX + (gdW - tw) / 2, barY + 22, folderText, filterSc, filterLab);
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
    // Floor short toasts so important hints remain readable on real hardware.
    if (durationMs < 2200ULL) durationMs = 2200ULL;
    toastMessage_ = message;
    toastShownMs_ = sceKernelGetProcessTimeWide() / 1000ULL;
    toastExpiresMs_ = toastShownMs_ + durationMs;
}

void FullCatalogScreen::updateAnimations() {
    // Theme palette cross-fade (BG / surfaces / accent)
    tickThemeBlend();

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

    // Theme picker grid scroll (row units) — same smoothing as catalog cards
    if (themeSetupVisible_) {
        const float targetTheme = static_cast<float>(themeSetupScrollRow_);
        visualThemeSetupScroll_ += (targetTheme - visualThemeSetupScroll_) * 0.18f;
        if (std::fabs(targetTheme - visualThemeSetupScroll_) < 0.008f)
            visualThemeSetupScroll_ = targetTheme;
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

void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus,int clipL,int clipT,int clipR,int clipB){
    const int cardL = std::max(x, clipL), cardT = std::max(y, clipT);
    const int cardR = std::min(x + w, clipR), cardB = std::min(y + h, clipB);
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(cardL, cardT, cardR, cardB);
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
        // Let the title use the full right side unless an actual badge occupies it.
        // The file badges are drawn lower in the card, so reserving 100+ px here
        // unnecessarily made titles look cramped.
        const int rightPad = compact ? 10 : 14;
        const int nameMaxW = std::max(40, (x + ox + ww) - tx - rightPad);
        drawMarqueeText(font_, tx, y + (compact ? 24 : 28) + oy, nameMaxW, WHITE, nameSc, it.name, focus,
                         clipL, clipT, clipR, clipB);
    }
    // Keep remaining card chrome inside the card box (marquee temporarily tightens scissor).
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(std::max(x + ox, clipL), std::max(y + oy, clipT),
                              std::min(x + ox + ww, clipR), std::min(y + oy + hh, clipB));
    vita2d_pgf_draw_text(font_, tx, y + (compact ? 44 : 50) + oy, TEXT, compact ? 0.74f : 0.82f,
        ellipsize(it.author.empty() ? ::psvitaalive::L(::psvitaalive::TextId::UnknownAuthor) : it.author, compact ? 16 : 18).c_str());
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
            // Amber folder-style marks (same visual language as Homebrew G/D Files)
            if (itemHasLinkType(it, "data files")) drawFolderChip(::psvitaalive::L(::psvitaalive::TextId::MetaDataFiles));
            if (itemHasLinkType(it, "game files")) drawFolderChip(::psvitaalive::L(::psvitaalive::TextId::MetaGameFiles));
            if (itemHasDlc(it)) drawFolderChip(::psvitaalive::L(::psvitaalive::TextId::MetaDlc));
        }
    }

    // Top-right DLC pill (always visible, same folder style as Homebrew marks)
    if (itemHasDlc(it)) {
        const char* dlcLab = ::psvitaalive::L(::psvitaalive::TextId::MetaDlc);
        const float dsc = 0.60f;
        const int dtw = vita2d_pgf_text_width(font_, dsc, dlcLab);
        const int dpad = 6, dch = 18;
        const int dcw = dtw + dpad * 2;
        const int dsx = x + ox + ww - dcw - 8;
        const int dsy = y + oy + 8;
        vita2d_draw_rectangle(dsx, dsy, dcw, dch, RGBA8(0x3A, 0x2C, 0x10, 255));
        vita2d_draw_rectangle(dsx, dsy, dcw, 2, RGBA8(0xE8, 0xB4, 0x3A, 255));
        vita2d_draw_rectangle(dsx, dsy, 2, dch, RGBA8(0xE8, 0xB4, 0x3A, 255));
        vita2d_pgf_draw_text(font_, dsx + dpad, dsy + 13, RGBA8(0xFF, 0xD2, 0x6A, 255), dsc, dlcLab);
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
            // Leave room for the top-right DLC pill when both are present
            int dlcShift = 0;
            if (itemHasDlc(it)) {
                const char* dlcLab = ::psvitaalive::L(::psvitaalive::TextId::MetaDlc);
                dlcShift = vita2d_pgf_text_width(font_, 0.60f, dlcLab) + 12 + 8;
            }
            drawInstallBadge(x + ox + ww - bw - 6 - dlcShift, y + oy + 6, li, true);
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
                drawCatalogCard(catalogView()[i], i, x + GRID_PAD + c * (cw + CARD_GAP), static_cast<int>(fy), cw, FULL_CARD_H, i == state_.focusIndex,
                                x + 1, y + 1, x + w - 1, y + h - 1);
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
            drawCatalogCard(catalogView()[i], i, x + GRID_PAD, static_cast<int>(fy), w - GRID_PAD * 2 - 4, SPLIT_CARD_H, i == state_.focusIndex,
                            x + 1, y + 1, x + w - 1, y + h - 1);
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
        vita2d_pgf_draw_text(font_, x + 4, y + 18, ACCENT, 0.76f, ::psvitaalive::L(::psvitaalive::TextId::InstallAllHeader));
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
        vita2d_pgf_draw_text(font_, x + 12, by + 24, tc, 0.84f, ::psvitaalive::L(::psvitaalive::TextId::InstallAllHeader));
        vita2d_pgf_draw_text(font_, x + 12, by + 48, sub, 0.66f,
            ::psvitaalive::L(::psvitaalive::TextId::InstallAllSubtitle));
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
        const bool pluginDone = isPluginTypeLink(l) && isPluginAlreadyInstalled(l);
        vita2d_draw_rectangle(x, ry, w, LINK_ROW_H, f ? ACCENT : SURFACE2);
        vita2d_draw_rectangle(x, ry, w, 1, f ? ACCENT : BORDER);
        const unsigned mc = f ? BG : (can ? WHITE : TEXT);
        std::string title = l.name.empty() ? l.type : l.name;
        const std::string sizeLabel = formatLinkSizeLabel(l, it);
        // Prefer "Installed" over "Recommended" for plugins already on disk
        const int badgeW = pluginDone ? 72 : (l.recommended ? 96 : 0);
        { const int titleMaxW = std::max(40, w - 20 - badgeW - 8); drawMarqueeText(font_, x + 10, ry + 17, titleMaxW, mc, 0.80f, title, f); }
        std::string meta = linkSectionMetaLabel(row.section);
        if (!sizeLabel.empty()) meta += "  •  " + sizeLabel;
        if (pluginDone)
            meta += std::string("  •  ") + (f ? ::psvitaalive::L(::psvitaalive::TextId::MetaAlreadyInstalled)
                                              : ::psvitaalive::L(::psvitaalive::TextId::MetaInstalled));
        else if (can)
            meta += std::string("  •  ") + (f ? ::psvitaalive::L(::psvitaalive::TextId::MetaXInstall)
                                              : ::psvitaalive::L(::psvitaalive::TextId::MetaX));
        vita2d_pgf_draw_text(font_, x + 10, ry + 35, f ? BG : DIM, 0.70f, ellipsize(meta, badgeW ? 26 : 40).c_str());
        if (pluginDone) {
            const int bx = x + w - badgeW - 8;
            vita2d_pgf_draw_text(font_, bx, ry + 18, f ? BG : ACCENT, 0.66f,
                                 ::psvitaalive::L(::psvitaalive::TextId::BadgeInstalled));
        } else if (l.recommended) {
            const int bx = x + w - badgeW - 8;
            vita2d_pgf_draw_text(font_, bx, ry + 18, f ? BG : ACCENT, 0.66f,
                                 ::psvitaalive::L(::psvitaalive::TextId::BadgeRecommended));
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

    emitTextSection(::psvitaalive::L(::psvitaalive::TextId::SectionDescription), it.description, false);
    emitTextSection(::psvitaalive::L(::psvitaalive::TextId::SectionLongDescription), it.longDescription, false);

    const int sc = std::min(5, (int)it.screenshots.size());
    if (sc > 0) {
        drawSectionHeader(cx, cursor, ::psvitaalive::L(::psvitaalive::TextId::SectionScreenshots));
        cursor += DETAIL_SECTION_H;
        for (int i = 0; i < sc; ++i) {
            drawImage(it.screenshots[i], "shot", cx, cursor + i * SCREENSHOT_ROW_H, cw, SCREENSHOT_ROW_H - 18);
        }
        cursor += sc * SCREENSHOT_ROW_H + DETAIL_SECTION_GAP;
    }

    emitTextSection(::psvitaalive::L(::psvitaalive::TextId::SectionRequirements), it.requirements, true);

    std::string installLine;
    {
        const LocalInstallInfo li = queryLocalInstall(it);
        if (li.state == LocalInstallState::Installed || li.state == LocalInstallState::UpdateAvailable) {
            installLine = (li.state == LocalInstallState::UpdateAvailable)
                ? ::psvitaalive::L(::psvitaalive::TextId::InstallStateUpdateAvailable)
                : ::psvitaalive::L(::psvitaalive::TextId::InstallStateInstalled);
            if (!li.installedVersion.empty()) installLine += " (v" + li.installedVersion + ")";
        } else if (!it.titleId.empty()) {
            installLine = ::psvitaalive::L(::psvitaalive::TextId::InstallStateNotInstalled);
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
        drawSectionHeader(cx + 8, cardTop + 2, ::psvitaalive::L(::psvitaalive::TextId::DetailInformation));
        int my = cardTop + DETAIL_SECTION_H + 4;
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaTitleId), it.titleId);
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaVersion), it.version);
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaInstall), installLine);
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaReleased), it.versionDate);
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaCategory), it.category);
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaSubcategory), it.subcategory);
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaSize), it.size);
        my = drawMetaRow(cx, my, cw, ::psvitaalive::L(::psvitaalive::TextId::MetaStatus), it.status);
        cursor = cardTop + cardH + DETAIL_SECTION_GAP;
    }

    emitTextSection(::psvitaalive::L(::psvitaalive::TextId::SectionChangelog), it.changelog, false);

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
            {
                const char* linkLab = linkOn
                    ? ::psvitaalive::L(::psvitaalive::TextId::ExitLinkMode)
                    : ::psvitaalive::L(::psvitaalive::TextId::SelectLinks);
                drawMarqueeText(font_, bx + 8, by + 22, bw - 16, linkOn ? BG : ACCENT, 0.70f,
                                linkLab, true, bx + 4, by, bx + bw - 4, by + bh);
            }
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
            drawMarqueeText(font_, rbx + 8, rby + 22, rbw - 16, REQ, 0.70f,
                            ::psvitaalive::L(::psvitaalive::TextId::RequestData), true,
                            rbx + 4, rby, rbx + rbw - 4, rby + rbh);
        }
    }
    drawDetailContent(it, x, y, w, h);
    // Fade over scrollable body (below header)
    drawScrollFades(x, y + DETAIL_HEADER_H, w, std::max(1, h - DETAIL_HEADER_H));
    if (active)
        drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4, ::psvitaalive::L(::psvitaalive::TextId::PanelDetail));
}

bool FullCatalogScreen::itemSupportsInstallAll(const CatalogItem& item) const {
    return itemHasDataOrGameFiles(item) || itemHasPluginLinks(item);
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
        showToast(::psvitaalive::L(::psvitaalive::TextId::ToastWaitOperation), 1600);
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
    pushIdx(installAllChosenDownload_, ::psvitaalive::L(::psvitaalive::TextId::LabelAppVpk));
    pushIdx(installAllChosenGameFiles_, ::psvitaalive::L(::psvitaalive::TextId::MetaGameFiles));
    pushIdx(installAllChosenDataFiles_, ::psvitaalive::L(::psvitaalive::TextId::MetaDataFiles));
    installAllHadPlugin_ = false;
    for (size_t i = 0; i < item.linkDetails.size(); ++i) {
        if (!isPluginTypeLink(item.linkDetails[i])) continue;
        if (item.linkDetails[i].url.empty()) continue;
        if (isPluginAlreadyInstalled(item.linkDetails[i])) {
            diagnostics::log(std::string("[UI] Install All skip installed plugin: ")
                             + pluginInstallFilePath(item.linkDetails[i]));
            continue;
        }
        installAllQueue_.push_back(item.linkDetails[i]);
        installAllQueueLabels_.push_back(item.linkDetails[i].name.empty() ? ::psvitaalive::L(::psvitaalive::TextId::MetaPlugin) : item.linkDetails[i].name);
        installAllHadPlugin_ = true;
    }

    if (installAllQueue_.empty()) {
        showToast(::psvitaalive::L(::psvitaalive::TextId::ToastNothingToInstall), 1600);
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
        showToast(::psvitaalive::L(::psvitaalive::TextId::ToastInstallUnavailable), 1600);
        closeInstallAllWizard(true);
        return;
    }
    const CatalogLink& first = installAllQueue_[0];
    diagnostics::log(std::string("[UI] Install All step 1/") + std::to_string(installAllQueue_.size())
                     + " " + installAllQueueLabels_[0] + " url=" + first.url);
    if (!linkAction_(item, first)) {
        showToast(::psvitaalive::L(::psvitaalive::TextId::ToastCouldNotStartInstall), 1800);
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
            showToast(::psvitaalive::L(::psvitaalive::TextId::ToastInstallAllStopped), 2200);
            closeInstallAllWizard(true);
        }
        return;
    }

    // Last step completed — keep success panel; toast when user dismisses
    installAllFinishedToast_ = true;
    diagnostics::log("[UI] Install All finished all steps");
    if (installAllHadPlugin_) {
        pluginRebootModal_ = true;
        diagnostics::log("[UI] Install All included plugins — show reboot modal");
    }
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

    const int ow = 820, oh = 460;
    const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
    vita2d_draw_rectangle(ox, oy, ow, oh, SURFACE2);
    vita2d_draw_rectangle(ox, oy, ow, 3, ACCENT);
    vita2d_draw_rectangle(ox, oy + oh - 1, ow, 1, BORDER);
    vita2d_draw_rectangle(ox, oy, 1, oh, BORDER);
    vita2d_draw_rectangle(ox + ow - 1, oy, 1, oh, BORDER);

    if (installAllPhase_ == InstallAllPhase::Confirm) {
        vita2d_pgf_draw_text(font_, ox + 28, oy + 42, ACCENT, 1.22f, ::psvitaalive::L(::psvitaalive::TextId::InstallAll));
        vita2d_pgf_draw_text(font_, ox + 28, oy + 82, WHITE, 0.96f, ellipsize(item.name, 36).c_str());
        const char* lines[] = {
            ::psvitaalive::L(::psvitaalive::TextId::InstallAllConfirm1),
            ::psvitaalive::L(::psvitaalive::TextId::InstallAllConfirm2),
            ::psvitaalive::L(::psvitaalive::TextId::InstallAllConfirm3),
            ::psvitaalive::L(::psvitaalive::TextId::InstallAllConfirm4),
        };
        int ty = oy + 124;
        for (const char* ln : lines) {
            vita2d_pgf_draw_text(font_, ox + 28, ty, TEXT, 0.86f, ln);
            ty += 34;
        }
        const int bw = 240, bh = 50;
        const int by = oy + oh - 68;
        const int bxOk = ox + 28;
        const int bxCancel = ox + ow - 28 - bw;
        const bool fOk = installAllFocus_ == 0;
        const bool fCancel = installAllFocus_ == 1;
        vita2d_draw_rectangle(bxOk, by, bw, bh, fOk ? ACCENT : SURFACE2);
        {
            const char* lab = ::psvitaalive::L(::psvitaalive::TextId::BtnContinue);
            const float sc = 0.90f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            vita2d_pgf_draw_text(font_, bxOk + (bw - tw) / 2, by + 34, fOk ? BG : WHITE, sc, lab);
        }
        vita2d_draw_rectangle(bxCancel, by, bw, bh, fCancel ? ACCENT : SURFACE2);
        {
            const char* lab = ::psvitaalive::L(::psvitaalive::TextId::BtnCancel);
            const float sc = 0.90f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            vita2d_pgf_draw_text(font_, bxCancel + (bw - tw) / 2, by + 30, fCancel ? BG : WHITE, sc, lab);
        }
        vita2d_pgf_draw_text(font_, ox + 28, oy + oh - 92, DIM, 0.74f, ::psvitaalive::L(::psvitaalive::TextId::InstallAllNavHint));
        return;
    }

    const char* title = ::psvitaalive::L(::psvitaalive::TextId::ChooseDownload);
    if (installAllPhase_ == InstallAllPhase::PickGameFiles) title = ::psvitaalive::L(::psvitaalive::TextId::ChooseGameFiles);
    else if (installAllPhase_ == InstallAllPhase::PickDataFiles) title = ::psvitaalive::L(::psvitaalive::TextId::ChooseDataFiles);
    vita2d_pgf_draw_text(font_, ox + 28, oy + 42, ACCENT, 1.18f, title);
    vita2d_pgf_draw_text(font_, ox + 28, oy + 78, DIM, 0.82f, ::psvitaalive::L(::psvitaalive::TextId::ChooseMirrorHint));

    const int listTop = oy + 98;
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
        { const int titleMaxW = std::max(40, rw - 24 - badgeW - 8); drawMarqueeText(font_, rx + 12, ry + 20, titleMaxW, mc, 0.88f, name, f); }
        std::string meta = l.type.empty()
            ? ::psvitaalive::L(::psvitaalive::TextId::MetaDownload)
            : l.type;
        if (!sizeLabel.empty()) meta += "  •  " + sizeLabel;
        if (f) meta += std::string("  •  ") + ::psvitaalive::L(::psvitaalive::TextId::MetaXSelect);
        else meta += std::string("  •  ") + ::psvitaalive::L(::psvitaalive::TextId::MetaX);
        vita2d_pgf_draw_text(font_, rx + 12, ry + 38, DIM, 0.78f, ellipsize(meta, badgeW ? 24 : 40).c_str());
        if (l.recommended) {
            const int bx = rx + rw - badgeW - 8;
            vita2d_pgf_draw_text(font_, bx, ry + 18, f ? BG : ACCENT, 0.70f,
                                 ::psvitaalive::L(::psvitaalive::TextId::BadgeRecommended));
        }
    }
    vita2d_pgf_draw_text(font_, ox + 28, oy + oh - 28, DIM, 0.74f, ::psvitaalive::L(::psvitaalive::TextId::InstallAllNavHint));
}

void FullCatalogScreen::drawLoadingOverlay(){
// Catalog load/download at startup: full-screen brand image + progress (not used for installs).
if (catalogSplashAlpha_ > 0.01f && !installProgressActive_) {
    const unsigned a = (unsigned)(catalogSplashAlpha_ * 255.f);
    if (a > 255) { /* clamp */ }
    {
        const bool brand = isBrandColorTheme(settingsEdit_.colorTheme);
        vita2d_texture* splash = (brand || !catalogLoadingMonoTex_) ? catalogLoadingTex_ : catalogLoadingMonoTex_;
        const unsigned aa = a > 255 ? 255u : a;
        const unsigned tint = (brand || !catalogLoadingMonoTex_)
            ? RGBA8(255, 255, 255, aa)
            : withAlpha(ACCENT, aa);
        if (splash) {
            const float tw = (float)vita2d_texture_get_width(splash);
            const float th = (float)vita2d_texture_get_height(splash);
            const float sx = (tw > 1.f) ? (SCREEN_W / tw) : 1.f;
            const float sy = (th > 1.f) ? (SCREEN_H / th) : 1.f;
            vita2d_draw_texture_tint_scale(splash, 0.f, 0.f, sx, sy, tint);
        } else {
            vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0x0A, 0x0A, 0x0A, aa));
        }
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

    std::string detail = catalogLoadingMessage_.empty() ? ::psvitaalive::L(::psvitaalive::TextId::PleaseWaitFallback) : catalogLoadingMessage_;
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
const int w=900,h=508,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;
vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,120));
vita2d_draw_rectangle(x,y,w,h,PANEL);
const unsigned edge=(installOutcome_==2)?RED:((installOutcome_==1)?GREEN:ACCENT);
vita2d_draw_rectangle(x,y,w,3,edge);
vita2d_draw_rectangle(x,y+3,3,h-6,edge);
vita2d_draw_rectangle(x+w-3,y+3,3,h-6,BORDER);
vita2d_draw_rectangle(x,y+h-3,w,3,BORDER);
vita2d_pgf_draw_text(font_,x+28,y+34,edge,0.92f,"PSVitaAlive");

if(installOutcome_==1){
  using TID = ::psvitaalive::TextId;
  const bool zipExtract =
      !installLiveAreaOk_ &&
      installResultTitleId_.empty() &&
      !installResultPath_.empty() &&
      (installResultPath_.find("ux0:app/") != 0);
  if (zipExtract) {
    vita2d_pgf_draw_text(font_,x+28,y+72,GREEN,1.28f,::psvitaalive::L(TID::ZipExtractComplete));
    std::string file=installProgressFile_.empty()?"(archive)":ellipsize(installProgressFile_,48);
    {
      char fl[160];
      sceClibSnprintf(fl,sizeof(fl),"%s: %s",::psvitaalive::L(TID::LabelFile),file.c_str());
      vita2d_pgf_draw_text(font_,x+28,y+116,WHITE,0.92f,fl);
    }
    {
      char pathLine[200];
      sceClibSnprintf(pathLine,sizeof(pathLine),"%s: %s",
          ::psvitaalive::L(TID::ExtractedTo), ellipsize(installResultPath_,48).c_str());
      vita2d_pgf_draw_text(font_,x+28,y+154,TEXT,0.88f,pathLine);
    }
    vita2d_pgf_draw_text(font_,x+28,y+192,DIM,0.84f,::psvitaalive::L(TID::NoLiveAreaFilesOnly));
    // Avoid showing raw English progress message under localized status.
  } else {
    vita2d_pgf_draw_text(font_,x+28,y+72,GREEN,1.28f,::psvitaalive::L(TID::InstallComplete));
    std::string file=installProgressFile_.empty()?"(file)":ellipsize(installProgressFile_,48);
    {
      char fl[160];
      sceClibSnprintf(fl,sizeof(fl),"%s: %s",::psvitaalive::L(TID::LabelFile),file.c_str());
      vita2d_pgf_draw_text(font_,x+28,y+116,WHITE,0.92f,fl);
    }
    int ly = 154;
    if(!installResultTitleId_.empty()) {
      char tid[96];
      sceClibSnprintf(tid,sizeof(tid),"%s: %s",::psvitaalive::L(TID::MetaTitleId),installResultTitleId_.c_str());
      vita2d_pgf_draw_text(font_,x+28,y+ly,TEXT,0.88f,tid);
      ly += 34;
    }
    if(!installResultPath_.empty()) {
      char pathLine[200];
      sceClibSnprintf(pathLine,sizeof(pathLine),"%s: %s",
          ::psvitaalive::L(TID::LabelPath), ellipsize(installResultPath_,48).c_str());
      vita2d_pgf_draw_text(font_,x+28,y+ly,TEXT,0.86f,pathLine);
      ly += 34;
    }
    if(installLiveAreaOk_)
      vita2d_pgf_draw_text(font_,x+28,y+ly,GREEN,0.90f,::psvitaalive::L(TID::LiveAreaOk));
    else if(!installResultPath_.empty() && installResultPath_.find("ux0:app/")==0)
      vita2d_pgf_draw_text(font_,x+28,y+ly,RGBA8(0xFF,0xC0,0x40,255),0.88f,::psvitaalive::L(TID::LiveAreaNotConfirmed));
    else
      vita2d_pgf_draw_text(font_,x+28,y+ly,TEXT,0.86f,::psvitaalive::L(TID::LiveAreaNa));
  }

  const int by2=y+370,bw2=320,bh2=50;
  vita2d_draw_rectangle(x+28,by2,bw2,bh2,GREEN);
  {
    const char* clab = ::psvitaalive::L(TID::OContinue);
    const float sc = 0.90f;
    const int tw = vita2d_pgf_text_width(font_, sc, clab);
    vita2d_pgf_draw_text(font_, x+28+(bw2-tw)/2, by2+34, BLACK, sc, clab);
  }
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
  using TID = ::psvitaalive::TextId;
  const unsigned amber = RGBA8(0xE0,0xA0,0x30,255);
  vita2d_pgf_draw_text(font_,x+28,y+72,amber,1.28f,::psvitaalive::L(TID::DownloadCancelled));
  std::string file=installProgressFile_.empty()?"(file)":ellipsize(installProgressFile_,48);
  {
    char fl[160];
    sceClibSnprintf(fl,sizeof(fl),"%s: %s",::psvitaalive::L(TID::LabelFile),file.c_str());
    vita2d_pgf_draw_text(font_,x+28,y+116,WHITE,0.92f,fl);
  }
  std::string msg=installProgressMessage_.empty()?::psvitaalive::L(::psvitaalive::TextId::DownloadCancelled):installProgressMessage_;
  if(msg=="Download cancelled"||msg=="Installation cancelled"||msg=="Cancelling download...")
    msg=::psvitaalive::L(TID::CancelledFriendlyMsg);
  vita2d_pgf_draw_text(font_,x+28,y+160,TEXT,0.86f,ellipsize(msg,52).c_str());
  vita2d_pgf_draw_text(font_,x+28,y+200,DIM,0.80f,::psvitaalive::L(TID::CancelledNoError));
  const int by2=y+370,bh2=50;
  const int bwClose=280;
  const int bxClose=x+(w-bwClose)/2;
  vita2d_draw_rectangle(bxClose,by2,bwClose,bh2,SURFACE2);
  vita2d_draw_rectangle(bxClose,by2,bwClose,1,BORDER);
  {
    const char* clab = ::psvitaalive::L(::psvitaalive::TextId::BtnOClose);
    const float sc = 0.64f;
    const int tw = vita2d_pgf_text_width(font_, sc, clab);
    vita2d_pgf_draw_text(font_, bxClose + (bwClose - tw) / 2, by2 + 27, WHITE, sc, clab);
  }
  vita2d_pgf_draw_text(font_,x+28,y+h-18,DIM,0.70f,::psvitaalive::L(::psvitaalive::TextId::CircleCloseHint));
  return;
}

if(installOutcome_==2){
  using TID = ::psvitaalive::TextId;
  vita2d_pgf_draw_text(font_,x+28,y+72,RED,1.28f,::psvitaalive::L(TID::InstallFailed));
  std::string file=installProgressFile_.empty()?"(file)":ellipsize(installProgressFile_,48);
  {
    char fl[160];
    sceClibSnprintf(fl,sizeof(fl),"%s: %s",::psvitaalive::L(TID::LabelFile),file.c_str());
    vita2d_pgf_draw_text(font_,x+28,y+116,WHITE,0.92f,fl);
  }
  vita2d_pgf_draw_text(font_,x+28,y+156,RED,0.90f,::psvitaalive::L(TID::LabelReason));
  std::string err=installProgressMessage_.empty()?::psvitaalive::L(TID::UnknownError):installProgressMessage_;
  const bool spaceErr = isNonReportableInstallError(err);
  vita2d_pgf_draw_text(font_,x+28,y+192,TEXT,0.84f,ellipsize(err,52).c_str());
  if(err.size()>52)
    vita2d_pgf_draw_text(font_,x+28,y+222,TEXT,0.82f,ellipsize(err.substr(48),52).c_str());
  const int hintY = (err.size()>52) ? 260 : 236;
  if (spaceErr) {
    vita2d_pgf_draw_text(font_,x+28,y+hintY,DIM,0.78f,::psvitaalive::L(TID::FreeSpaceHint1));
    vita2d_pgf_draw_text(font_,x+28,y+hintY+30,DIM,0.76f,::psvitaalive::L(TID::FreeSpaceHint2));
  } else {
    vita2d_pgf_draw_text(font_,x+28,y+hintY,DIM,0.78f,::psvitaalive::L(TID::CheckLogsHint1));
    vita2d_pgf_draw_text(font_,x+28,y+hintY+30,DIM,0.76f,::psvitaalive::L(TID::CheckLogsHint2));
  }
  const int by2=y+370,bh2=50;
  if (spaceErr) {
    // Only Close — space issues are expected user-side, not bug reports
    const int bwClose=280;
    const int bxClose=x+(w-bwClose)/2;
    vita2d_draw_rectangle(bxClose,by2,bwClose,bh2,SURFACE2);
    vita2d_draw_rectangle(bxClose,by2,bwClose,1,BORDER);
    {
      const char* clab = ::psvitaalive::L(::psvitaalive::TextId::BtnOClose);
      const float sc = 0.86f;
      const int tw = vita2d_pgf_text_width(font_, sc, clab);
      vita2d_pgf_draw_text(font_, bxClose + (bwClose - tw) / 2, by2 + 34, WHITE, sc, clab);
    }
    vita2d_pgf_draw_text(font_,x+28,y+h-18,DIM,0.70f,::psvitaalive::L(::psvitaalive::TextId::CircleCloseHint));
  } else {
    const int bwReport=240, bwClose=240;
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
      const char* lab = ::psvitaalive::L(::psvitaalive::TextId::ChipReport);
      if (reportUiState_==2) lab = ::psvitaalive::L(::psvitaalive::TextId::ChipSent);
      else if (reportUiState_==3) lab = reportUiMsg_[0] ? reportUiMsg_ : ::psvitaalive::L(::psvitaalive::TextId::ChipFail);
      const float sc = 0.86f;
      const int tw = vita2d_pgf_text_width(font_, sc, lab);
      vita2d_pgf_draw_text(font_, bxReport + (bwReport - tw) / 2, by2 + 34, reportText, sc, lab);
    }
    vita2d_draw_rectangle(bxClose,by2,bwClose,bh2,RED);
    {
      const char* clab = ::psvitaalive::L(::psvitaalive::TextId::BtnOClose);
      const float sc = 0.86f;
      const int tw = vita2d_pgf_text_width(font_, sc, clab);
      vita2d_pgf_draw_text(font_, bxClose + (bwClose - tw) / 2, by2 + 34, WHITE, sc, clab);
    }
    vita2d_pgf_draw_text(font_,x+28,y+h-18,DIM,0.70f,::psvitaalive::L(::psvitaalive::TextId::SquareReportHint));
  }
  return;
}

if(catalogLoading_){
  vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,catalogLoadingLabel_.empty()?::psvitaalive::L(::psvitaalive::TextId::LoadingCatalogFallback):catalogLoadingLabel_.c_str());
  vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.60f,catalogLoadingMessage_.empty()?::psvitaalive::L(::psvitaalive::TextId::PreparingFallback):catalogLoadingMessage_.c_str());
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
  using TID = ::psvitaalive::TextId;
  const char* title = ::psvitaalive::L(TID::StageInstalling);
  if (installProgressStage_ == "BGDL") title = ::psvitaalive::L(TID::StagePreparingDownload);
  else if (installProgressStage_ == "Downloading" || installProgressStage_ == "Cancelling" || installProgressStage_.empty())
    title = ::psvitaalive::L(TID::StageDownloading);
  else if (installProgressStage_ == "Installing") title = ::psvitaalive::L(TID::StageInstalling);
  else if (installProgressStage_ == "Retrying") title = ::psvitaalive::L(TID::StageRetrying);
  else if (installProgressStage_ == "Network") title = ::psvitaalive::L(TID::StageNetwork);
  else if (installProgressStage_ == "Space") title = ::psvitaalive::L(TID::StageSpace);
  else if (installProgressStage_ == "Plugin") title = ::psvitaalive::L(TID::StagePlugin);
  else if (installProgressStage_ == "Completed" || installProgressStage_ == "Done") title = ::psvitaalive::L(TID::StageCompleted);
  else if (installProgressStage_ == "Cancelled") title = ::psvitaalive::L(TID::StageCancelled);
  else if (installProgressStage_ == "Error") title = ::psvitaalive::L(TID::StageError);
  else if (installProgressStage_.find("Extract") != std::string::npos || installProgressStage_.find("Unzip") != std::string::npos)
    title = ::psvitaalive::L(TID::StageExtracting);
  else if (!installProgressStage_.empty()) title = installProgressStage_.c_str();
  // Large type for Vita readability (similar hierarchy to essential-plugins modal).
  vita2d_pgf_draw_text(font_, x + 28, y + 68, WHITE, 1.28f, title);
}
std::string file = installProgressFile_.empty()
    ? ::psvitaalive::L(::psvitaalive::TextId::StagePreparing)
    : ellipsize(installProgressFile_, 52);
vita2d_pgf_draw_text(font_, x + 28, y + 106, TEXT, 0.90f, file.c_str());
const uint64_t total = installProgressTotal_,
               current = std::min<uint64_t>(installProgressCurrent_, total ? total : installProgressCurrent_);
const uint64_t pct = total ? std::min<uint64_t>(100, (current * 100) / total) : 0;
int bx = x + 28, by = y + 138, bw = w - 56, bh = 22;
vita2d_draw_rectangle(bx, by, bw, bh, BORDER);
const bool msgRetry =
    installProgressStage_ == "Retrying" ||
    installProgressMessage_.find("retrying") != std::string::npos ||
    installProgressMessage_.find("Retry") != std::string::npos ||
    installProgressMessage_.find("retry") != std::string::npos ||
    installProgressMessage_.find("reintent") != std::string::npos;
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
const bool indeterminate =
    msgRetry ||
    total == 0 ||
    (current == 0 && installProgressSpeed_ == 0 && (stageDownload || stageInstall || stageExtract));
if (indeterminate) {
  const float t = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1400) / 1400.f;
  const float uu = (t < 0.5f) ? (t * 2.f) : (2.f - t * 2.f);
  const int pulse = std::max(36, (int)(bw * 0.28f));
  const int off = (int)((bw - pulse) * uu);
  if (pulse > 0) vita2d_draw_rectangle(bx + off, by, pulse, bh, ACCENT);
} else {
  vita2d_draw_rectangle(bx, by, bw * (int)pct / 100, bh, ACCENT);
}
char stats[220];
if (indeterminate) {
  sceClibSnprintf(stats, sizeof(stats), "…  %s / %s  •  %s/s",
      formatBytes(current).c_str(),
      total ? formatBytes(total).c_str() : "?",
      formatBytes(installProgressSpeed_).c_str());
} else {
  sceClibSnprintf(stats, sizeof(stats), "%llu%%  %s / %s  •  %s/s",
      (unsigned long long)pct, formatBytes(current).c_str(),
      total ? formatBytes(total).c_str() : "?",
      formatBytes(installProgressSpeed_).c_str());
}
vita2d_pgf_draw_text(font_, x + 28, y + 180, TEXT, 0.88f, stats);
uint64_t eta = 0;
if (installProgressSpeed_ > 0 && total > current) eta = (total - current) / installProgressSpeed_;
char info[180];
if (indeterminate)
  sceClibSnprintf(info, sizeof(info), "%s: 1 / 1   %s: —",
      ::psvitaalive::L(::psvitaalive::TextId::LabelFile),
      ::psvitaalive::L(::psvitaalive::TextId::LabelEta));
else
  sceClibSnprintf(info, sizeof(info), "%s: 1 / 1   %s: %s",
      ::psvitaalive::L(::psvitaalive::TextId::LabelFile),
      ::psvitaalive::L(::psvitaalive::TextId::LabelEta),
      formatEta(eta).c_str());
vita2d_pgf_draw_text(font_, x + 28, y + 212, ACCENT, 0.86f, info);
if (!installProgressMessage_.empty())
  vita2d_pgf_draw_text(font_, x + 28, y + 244, DIM, 0.78f, ellipsize(installProgressMessage_, 58).c_str());
const char* waitHint = nullptr;
if (msgRetry && stageDownload)
  waitHint = ::psvitaalive::L(::psvitaalive::TextId::HintRetryConnection);
else if (msgRetry && (stageInstall || stageExtract))
  waitHint = ::psvitaalive::L(::psvitaalive::TextId::HintRetryStep);
else if (stageExtract)
  waitHint = ::psvitaalive::L(::psvitaalive::TextId::HintExtracting);
else if (stageInstall)
  waitHint = ::psvitaalive::L(::psvitaalive::TextId::HintInstalling);
else if (indeterminate && stageDownload)
  waitHint = ::psvitaalive::L(::psvitaalive::TextId::HintConnecting);
else if (stageDownload)
  waitHint = ::psvitaalive::L(::psvitaalive::TextId::HintDownloadSpeed);
else
  waitHint = ::psvitaalive::L(::psvitaalive::TextId::HintPleaseWait);
vita2d_pgf_draw_text(font_, x + 28, y + 276,
    (stageDownload && !indeterminate && !msgRetry) ? DIM : ACCENT,
    0.80f, waitHint);
// LOCKED banner — tall + large type, no overlap with button below.
{
  const int lbX = x + 20, lbY = y + 308, lbW = w - 40, lbH = 78;
  vita2d_draw_rectangle(lbX, lbY, lbW, lbH, RGBA8(0x40, 0x10, 0x10, 255));
  vita2d_draw_rectangle(lbX, lbY, lbW, 3, RED);
  vita2d_draw_rectangle(lbX, lbY + lbH - 3, lbW, 3, RED);
  vita2d_draw_rectangle(lbX, lbY, 5, lbH, RED);
  vita2d_draw_rectangle(lbX + lbW - 5, lbY, 5, lbH, RED);
  vita2d_pgf_draw_text(font_, lbX + 16, lbY + 28, RED, 0.92f,
      ::psvitaalive::L(::psvitaalive::TextId::LockedBanner1));
  vita2d_pgf_draw_text(font_, lbX + 16, lbY + 56, WHITE, 0.84f,
      ::psvitaalive::L(::psvitaalive::TextId::LockedBanner2));
}
const int cancelW = 520, cancelH = 50;
const int cancelX = x + (w - cancelW) / 2;
const int cancelY = y + 410;
vita2d_draw_rectangle(cancelX, cancelY, cancelW, cancelH, SURFACE2);
vita2d_draw_rectangle(cancelX, cancelY, cancelW, 2, BORDER);
vita2d_draw_rectangle(cancelX, cancelY + cancelH - 2, cancelW, 2, BORDER);
{
  const char* clab = ::psvitaalive::L(::psvitaalive::TextId::CircleCancelDownload);
  const float csc = 0.88f;
  const int ctw = vita2d_pgf_text_width(font_, csc, clab);
  vita2d_pgf_draw_text(font_, cancelX + (cancelW - ctw) / 2, cancelY + 34, WHITE, csc, clab);
}
vita2d_pgf_draw_text(font_, x + 28, y + h - 18, DIM, 0.70f,
    ::psvitaalive::L(::psvitaalive::TextId::ProgressFooterHint));
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


void FullCatalogScreen::tryShowEssentialPluginsPrompt() {
    if (essentialPluginsPromptDone_) return;
    if (themeSetupVisible_ || newsVisible_) return;
    if (catalogLoading_ || installProgressActive_ || pluginRebootModal_) return;
    if (essentialInstallRunning_) return;

    essentialMissing_.clear();
    struct Def {
        const char* name;
        const char* desc;
        const char* url;
        const char* extractPath;
        const char* section;
        const char* line;
        const char* pathA;
        const char* pathB;
    };
    static const Def kDefs[] = {
        {
            "kubridge.skprx",
            "Kernel bridge used by many ports and advanced homebrew.",
            "https://archive.org/download/plugins-ps-vita/kubridge.skprx",
            "ur0:tai/",
            "*KERNEL",
            "ur0:tai/kubridge.skprx",
            "ur0:tai/kubridge.skprx",
            "ux0:tai/kubridge.skprx"
        },
        {
            "fd_fix.skprx",
            "File-descriptor fix that improves stability of some homebrew.",
            "https://archive.org/download/plugins-ps-vita/fd_fix.skprx",
            "ur0:tai/",
            "*KERNEL",
            "ur0:tai/fd_fix.skprx",
            "ur0:tai/fd_fix.skprx",
            "ux0:tai/fd_fix.skprx"
        },
        {
            "libshacccg.suprx",
            "Offline shader compiler required by some OpenGL ES ports (not listed in config.txt).",
            "https://archive.org/download/plugins-ps-vita/libshacccg.suprx",
            "ur0:data/",
            "none",
            "ur0:data/libshacccg.suprx",
            "ur0:data/libshacccg.suprx",
            "ur0:/data/libshacccg.suprx"
        },
    };
    // Refresh detector so RePatch / FdFix flags match current taiHEN state.
    pluginsStatus_ = ::psvitaalive::PluginDetector::scan();

    for (const Def& d : kDefs) {
        std::vector<std::string> paths;
        if (d.pathA) paths.emplace_back(d.pathA);
        if (d.pathB) paths.emplace_back(d.pathB);

        // FdFix is not needed when RePatch is active (same IO redirect role).
        // Prefer never proposing both; RePatch satisfies the FdFix requirement.
        const bool isFdFix = (d.name && std::strstr(d.name, "fd_fix") != nullptr);
        if (isFdFix && (pluginsStatus_.repatch || pluginsStatus_.fdFix)) {
            diagnostics::log(pluginsStatus_.repatch
                ? "[UI] essential plugins: skip fd_fix (RePatch active)"
                : "[UI] essential plugins: skip fd_fix (FdFix already installed)");
            continue;
        }

        if (essentialPluginFullyInstalled(d.section, d.line, paths)) continue;
        EssentialPluginSpec s;
        s.name = d.name;
        // Localized description (names stay as filenames)
        if (std::strstr(d.name, "kubridge"))
            s.desc = ::psvitaalive::L(::psvitaalive::TextId::EssentialPluginKubridgeDesc);
        else if (std::strstr(d.name, "fd_fix"))
            s.desc = ::psvitaalive::L(::psvitaalive::TextId::EssentialPluginFdFixDesc);
        else if (std::strstr(d.name, "libshacccg") || std::strstr(d.name, "shacccg"))
            s.desc = ::psvitaalive::L(::psvitaalive::TextId::EssentialPluginShacccgDesc);
        else
            s.desc = d.desc ? d.desc : "";
        s.url = d.url;
        s.extractPath = d.extractPath;
        s.section = d.section;
        s.line = d.line;
        s.checkPaths = std::move(paths);
        essentialMissing_.push_back(std::move(s));
    }

    essentialPluginsPromptDone_ = true;
    if (essentialMissing_.empty()) {
        diagnostics::log("[UI] essential plugins: all present");
        return;
    }
    essentialPluginsModal_ = true;
    essentialPluginsFocus_ = 0;
    diagnostics::log(std::string("[UI] essential plugins missing count=") + std::to_string(essentialMissing_.size()));
}

void FullCatalogScreen::closeEssentialPluginsPrompt(bool install) {
    if (!essentialPluginsModal_) return;
    essentialPluginsModal_ = false;
    if (!install) {
        diagnostics::log("[UI] essential plugins: remind later");
        showToast(::psvitaalive::L(::psvitaalive::TextId::EssentialRemindToast), 2200);
        return;
    }
    if (!linkAction_) {
        showToast(::psvitaalive::L(::psvitaalive::TextId::InstallerNotReady), 1800);
        return;
    }
    essentialInstallQueue_ = essentialMissing_;
    essentialInstallIndex_ = 0;
    essentialInstallRunning_ = true;
    essentialInstallLastOutcome_ = -1;
    diagnostics::log(std::string("[UI] essential plugins: installing ") + std::to_string(essentialInstallQueue_.size()));
    kickNextEssentialPluginInstall();
}

void FullCatalogScreen::kickNextEssentialPluginInstall() {
    if (!essentialInstallRunning_) return;
    if (essentialInstallIndex_ >= essentialInstallQueue_.size()) {
        essentialInstallRunning_ = false;
        essentialInstallQueue_.clear();
        pluginRebootModal_ = true;
        diagnostics::log("[UI] essential plugins: all done — reboot modal");
        return;
    }
    if (!linkAction_) {
        essentialInstallRunning_ = false;
        showToast(::psvitaalive::L(::psvitaalive::TextId::InstallerNotReady), 1800);
        return;
    }
    const EssentialPluginSpec& s = essentialInstallQueue_[essentialInstallIndex_];
    // Re-scan: skip fd_fix if RePatch appeared, or any plugin now fully present.
    pluginsStatus_ = ::psvitaalive::PluginDetector::scan();
    const bool isFdFix = (s.name.find("fd_fix") != std::string::npos);
    if (isFdFix && pluginsStatus_.repatch) {
        diagnostics::log("[UI] essential plugins skip fd_fix (RePatch active)");
        ++essentialInstallIndex_;
        kickNextEssentialPluginInstall();
        return;
    }
    if (isFdFix && pluginsStatus_.fdFix) {
        diagnostics::log("[UI] essential plugins skip fd_fix (already installed)");
        ++essentialInstallIndex_;
        kickNextEssentialPluginInstall();
        return;
    }
    // Skip if it appeared on disk since the prompt (e.g. user installed elsewhere)
    if (essentialPluginFullyInstalled(s.section, s.line, s.checkPaths)) {
        diagnostics::log(std::string("[UI] essential plugins skip already present: ") + s.name);
        ++essentialInstallIndex_;
        kickNextEssentialPluginInstall();
        return;
    }
    CatalogItem dummy;
    dummy.name = s.name;
    CatalogLink link;
    link.type = "Plugin";
    link.name = s.name;
    link.url = s.url;
    link.extractPath = s.extractPath;
    link.section = s.section;
    link.line = s.line;
    link.recommended = true;
    diagnostics::log(std::string("[UI] essential plugins install ")
                     + std::to_string(essentialInstallIndex_ + 1) + "/"
                     + std::to_string(essentialInstallQueue_.size()) + " " + s.name);
    essentialInstallLastOutcome_ = 0;
    if (!linkAction_(dummy, link)) {
        showToast(std::string(::psvitaalive::L(::psvitaalive::TextId::CouldNotStartPrefix)) + " " + s.name, 2000);
        essentialInstallRunning_ = false;
        essentialInstallQueue_.clear();
    }
}

void FullCatalogScreen::essentialPluginsTryAdvanceFromProgress(int outcome) {
    if (!essentialInstallRunning_) return;
    if (outcome == essentialInstallLastOutcome_) return;
    essentialInstallLastOutcome_ = outcome;

    if (outcome == 2 || outcome == 3) {
        diagnostics::log(std::string("[UI] essential plugins stopped at ")
                         + std::to_string(essentialInstallIndex_ + 1)
                         + (outcome == 3 ? " (cancelled)" : " (failed)"));
        essentialInstallRunning_ = false;
        essentialInstallQueue_.clear();
        // If at least one plugin may have been installed earlier in the queue, still offer reboot.
        if (essentialInstallIndex_ > 0) {
            pluginRebootModal_ = true;
        }
        return;
    }
    if (outcome != 1) return;

    // Acknowledge completed result so the controller returns to Idle before next job.
    if (installAcknowledge_) installAcknowledge_();
    ++essentialInstallIndex_;
    // Small deferral: kick next on next progress tick path — call immediately.
    kickNextEssentialPluginInstall();
}

void FullCatalogScreen::drawEssentialPluginsOverlay() {
    if (!essentialPluginsModal_ || !font_) return;
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 200));
    // Large type for real Vita (960x544) — avoid the tiny scales that look fine on desktop.
    const int w = 820, h = 460;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    vita2d_draw_rectangle(x, y, w, h, SURFACE);
    vita2d_draw_rectangle(x, y, w, 4, ACCENT);
    vita2d_draw_rectangle(x, y, 4, h, ACCENT);
    vita2d_draw_rectangle(x + w - 4, y, 4, h, ACCENT);
    vita2d_draw_rectangle(x, y + h - 4, w, 4, ACCENT);

    vita2d_pgf_draw_text(font_, x + 28, y + 44, WHITE, 1.18f, ::psvitaalive::L(::psvitaalive::TextId::EssentialPluginsTitle));
    vita2d_pgf_draw_text(font_, x + 28, y + 78, TEXT, 0.88f,
        ::psvitaalive::L(::psvitaalive::TextId::EssentialPluginsSubtitle));

    int ty = y + 118;
    for (const auto& s : essentialMissing_) {
        vita2d_pgf_draw_text(font_, x + 28, ty, ACCENT, 1.02f, s.name.c_str());
        ty += 30;
        vita2d_pgf_draw_text(font_, x + 36, ty, DIM, 0.84f, s.desc.c_str());
        ty += 36;
        if (ty > y + h - 110) break;
    }

    const int btnH = 58, gap = 14;
    const int btnY = y + h - 82;
    const int btnW = (w - 56 - gap) / 2;
    const int x0 = x + 28;
    const int x1 = x0 + btnW + gap;

    // Install button — same pulse border as Install All
    {
        const float pulse = 0.40f + 0.60f * focusPulse();
        const unsigned borderA = (unsigned)(120.f + 135.f * pulse);
        const unsigned borderCol = (essentialPluginsFocus_ == 0)
            ? withAlpha(ACCENT, borderA)
            : BORDER;
        const int bwPulse = (essentialPluginsFocus_ == 0) ? (2 + (int)(1.5f * pulse)) : 2;
        vita2d_draw_rectangle(x0, btnY, btnW, btnH, borderCol);
        vita2d_draw_rectangle(x0 + bwPulse, btnY + bwPulse, btnW - bwPulse * 2, btnH - bwPulse * 2,
                              essentialPluginsFocus_ == 0 ? ACCENT : SURFACE2);
        const char* lab = ::psvitaalive::L(::psvitaalive::TextId::EssentialInstallPlugins);
        const float sc = 0.98f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, x0 + (btnW - tw) / 2, btnY + 38,
                             essentialPluginsFocus_ == 0 ? BG : WHITE, sc, lab);
    }
    // Remind later
    {
        vita2d_draw_rectangle(x1, btnY, btnW, btnH, essentialPluginsFocus_ == 1 ? ACCENT : SURFACE2);
        vita2d_draw_rectangle(x1, btnY, btnW, 1, essentialPluginsFocus_ == 1 ? ACCENT : BORDER);
        const char* lab = ::psvitaalive::L(::psvitaalive::TextId::EssentialRemindLater);
        const float sc = 0.98f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, x1 + (btnW - tw) / 2, btnY + 38,
                             essentialPluginsFocus_ == 1 ? BG : WHITE, sc, lab);
    }
    vita2d_pgf_draw_text(font_, x + 28, y + h - 16, DIM, 0.72f, ::psvitaalive::L(::psvitaalive::TextId::EssentialNavHint));
}

void FullCatalogScreen::drawPluginRebootOverlay() {
    if (!pluginRebootModal_) return;
    // Dim full screen
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 200));
    const int w = 720, h = 320;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    vita2d_draw_rectangle(x, y, w, h, SURFACE);
    vita2d_draw_rectangle(x, y, w, 4, ACCENT);
    vita2d_draw_rectangle(x, y, 4, h, ACCENT);
    vita2d_draw_rectangle(x + w - 4, y, 4, h, ACCENT);
    vita2d_draw_rectangle(x, y + h - 4, w, 4, ACCENT);
    vita2d_pgf_draw_text(font_, x + 28, y + 42, WHITE, 0.92f,
        ::psvitaalive::L(::psvitaalive::TextId::PluginRebootTitle));
    const char* lines[] = {
        ::psvitaalive::L(::psvitaalive::TextId::PluginRebootLine1),
        ::psvitaalive::L(::psvitaalive::TextId::PluginRebootLine2),
        "",
        ::psvitaalive::L(::psvitaalive::TextId::PluginRebootLine3),
        ::psvitaalive::L(::psvitaalive::TextId::PluginRebootLine4),
    };
    int ty = y + 78;
    for (const char* ln : lines) {
        vita2d_pgf_draw_text(font_, x + 28, ty, TEXT, 0.68f, ln);
        ty += 26;
    }
    const int bw = w - 56, bh = 56;
    const int bx = x + 28, by = y + h - 72;
    vita2d_draw_rectangle(bx, by, bw, bh, ACCENT);
    const char* lab = ::psvitaalive::L(::psvitaalive::TextId::PluginRebootButton);
    const float sc = 0.88f;
    const int tw = vita2d_pgf_text_width(font_, sc, lab);
    vita2d_pgf_draw_text(font_, bx + (bw - tw) / 2, by + 38, BG, sc, lab);
    vita2d_pgf_draw_text(font_, x + 28, y + h - 18, DIM, 0.55f,
        ::psvitaalive::L(::psvitaalive::TextId::PluginRebootFooter));
}

void FullCatalogScreen::drawFullCatalog(){vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);drawCatalogPanel(0,HEADER_H+TABS_H,SCREEN_W,SCREEN_H-HEADER_H-TABS_H-FOOTER_H,false);drawFooterBar(font_, ::psvitaalive::L(::psvitaalive::TextId::FooterCatalog));drawReportChip();drawNewsChip();if(catalogLoading_||installProgressActive_||catalogSplashAlpha_>0.01f)drawLoadingOverlay();if(newsVisible_)drawNewsOverlay();if(themeSetupVisible_)drawThemeSetupOverlay();if(reportConfirmVisible_)drawReportConfirmOverlay();if(dataRequestConfirmVisible_)drawDataRequestConfirmOverlay();if(installAllPhase_!=InstallAllPhase::Hidden&&installAllPhase_!=InstallAllPhase::Running)drawInstallAllOverlay();if(essentialPluginsModal_)drawEssentialPluginsOverlay();if(pluginRebootModal_)drawPluginRebootOverlay();if(!catalogError_.empty())vita2d_pgf_draw_text(font_,18,HEADER_H+TABS_H+26,ACCENT,.66f,catalogError_.c_str());drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawSplitDetail(){vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H,lw=SCREEN_W/2;drawCatalogPanel(0,top,lw,hh,true);drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_draw_rectangle(lw-1,top,2,hh,BORDER);drawFooterBar(font_, state_.activePanel==UiPanel::Catalog ? ::psvitaalive::L(::psvitaalive::TextId::FooterDetailList) : ::psvitaalive::L(::psvitaalive::TextId::FooterDetailPanel));drawReportChip();drawNewsChip();if(catalogLoading_||installProgressActive_||catalogSplashAlpha_>0.01f)drawLoadingOverlay();if(newsVisible_)drawNewsOverlay();if(themeSetupVisible_)drawThemeSetupOverlay();if(reportConfirmVisible_)drawReportConfirmOverlay();if(dataRequestConfirmVisible_)drawDataRequestConfirmOverlay();if(installAllPhase_!=InstallAllPhase::Hidden&&installAllPhase_!=InstallAllPhase::Running)drawInstallAllOverlay();if(essentialPluginsModal_)drawEssentialPluginsOverlay();if(pluginRebootModal_)drawPluginRebootOverlay();drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawOpeningDetail(){float p=transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,rw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawClosingDetail(){float p=1.0f-transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::draw(){switch(state_.mode){case UiMode::FULL_CATALOG:drawFullCatalog();break;case UiMode::OPENING_DETAIL:drawOpeningDetail();break;case UiMode::SPLIT_DETAIL:drawSplitDetail();break;case UiMode::CLOSING_DETAIL:drawClosingDetail();break;case UiMode::SETTINGS:drawSettings();break;}}bool FullCatalogScreen::updateAndDraw(){
    if(!ready_)return false;
    {
        static bool once = false;
        if (!once) {
            once = true;
            diagnostics::log("[UI] first updateAndDraw frame");
        }
    }
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
