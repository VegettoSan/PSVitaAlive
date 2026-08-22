#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive {
namespace ui {

enum class CatalogType {
    Homebrew = 0,
    VitaGames,
    PspGames,
    Ps1Games,
    Count
};

const char* catalogName(CatalogType t);

enum class UiMode {
    FULL_CATALOG = 0,
    OPENING_DETAIL,
    SPLIT_DETAIL,
    CLOSING_DETAIL,
    SETTINGS
};

enum class UiPanel {
    Catalog = 0,
    Detail
};

struct CatalogLink {
    std::string type;
    std::string name;
    std::string url;
    std::string size; // human-readable, e.g. "1.4 GB"
    std::string zrif; // Vita NoPayStation zRIF (optional)
    std::string contentId; // NPS Content ID (Vita/PSP/PS1) for license / BGDL
    bool recommended = false;
};

struct CatalogItem {
    std::string id;
    std::string titleId;
    std::string name;
    std::string author;

    std::string description;
    std::string longDescription;

    std::string status;
    std::string version;
    std::string versionDate;

    std::string requirements;
    std::string size;

    std::string category;
    std::string subcategory;

    std::string changelog;

    // Visual assets. These may be remote URLs and are loaded asynchronously.
    std::string icon;
    std::string cover;
    std::vector<std::string> screenshots;

    // Normalized primary download used by InstallController.
    std::string downloadUrl;
    std::string downloadFileName;

    // Full link information for the detail page.
    std::vector<CatalogLink> linkDetails;
    std::vector<std::string> links;
};

struct UiState {
    CatalogType catalog = CatalogType::Homebrew;

    UiMode mode = UiMode::FULL_CATALOG;
    UiPanel activePanel = UiPanel::Catalog;

    int focusIndex = 0;
    int catalogScrollRow = 0;
    int detailScroll = 0;

    // Detail link/action navigation. -1 means no action is focused.
    int linkFocus = -1;
    bool linkNavigation = false;

    uint64_t transitionStart = 0;

    bool requestExit = false;
};

constexpr int SCREEN_W = 960;
constexpr int SCREEN_H = 544;
constexpr int GRID_COLS = 3;
constexpr int HEADER_H = 52;
constexpr int TABS_H = 36;
constexpr int FOOTER_H = 48;
constexpr int GRID_PAD = 12;
constexpr int CARD_GAP = 10;

} // namespace ui
} // namespace psvitaalive
