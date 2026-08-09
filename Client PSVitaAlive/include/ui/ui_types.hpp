#pragma once

#include <string>
#include <vector>
#include <cstdint>

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
    CLOSING_DETAIL
};

enum class UiPanel {
    Catalog = 0,
    Detail
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

    // Phase 10: the renderer keeps only normalized download information.
    // Real catalog data will populate these fields later.
    std::string downloadUrl;
    std::string downloadFileName;

    std::vector<std::string> screenshots;
    std::vector<std::string> links;
};

struct UiState {
    CatalogType catalog = CatalogType::Homebrew;

    UiMode mode = UiMode::FULL_CATALOG;
    UiPanel activePanel = UiPanel::Catalog;

    int focusIndex = 0;

    // Scroll del catálogo.
    int catalogScrollRow = 0;

    // Scroll vertical independiente del detalle.
    int detailScroll = 0;

    // Estado de la animación de apertura/cierre.
    uint64_t transitionStart = 0;

    bool requestExit = false;
};

constexpr int SCREEN_W = 960;
constexpr int SCREEN_H = 544;

constexpr int GRID_COLS = 3;

constexpr int HEADER_H = 52;
constexpr int TABS_H = 36;
constexpr int FOOTER_H = 40;

constexpr int GRID_PAD = 12;
constexpr int CARD_GAP = 10;

} // namespace ui
} // namespace psvitaalive
