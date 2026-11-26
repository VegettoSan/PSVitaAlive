#pragma once
#include "ui/ui_types.hpp"
namespace psvitaalive { namespace ui {
inline bool storageShowsSavedZips(CatalogType catalog) {
    return catalog == CatalogType::Homebrew;
}
inline const char* storageInstalledSectionKey(CatalogType catalog) {
    switch (catalog) {
        case CatalogType::VitaGames: return "ID_SECTION_VITA";
        case CatalogType::PspGames: return "ID_SECTION_PSP";
        case CatalogType::Ps1Games: return "ID_SECTION_PS1";
        default: return "ID_SECTION";
    }
}
inline bool storageCleanupItemAllowed(CatalogType catalog, bool savedZip) {
    return !savedZip || storageShowsSavedZips(catalog);
}
// Stable action IDs: -1 scan, -2 all, -3 temporaries, -4 saved ZIP.
inline int storageCatalogCleanupAction(CatalogType catalog, int action) {
    if (storageShowsSavedZips(catalog)) return action;
    if (action == -2) return -3;
    if (action == -4) return -1;
    return action;
}
}} // namespace psvitaalive::ui
