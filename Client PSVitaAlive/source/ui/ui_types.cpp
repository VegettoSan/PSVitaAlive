#include "ui/ui_types.hpp"

namespace psvitaalive {
namespace ui {

const char* catalogName(CatalogType t) {
    switch (t) {
        case CatalogType::Homebrew:
            return "Homebrew";

        case CatalogType::VitaGames:
            return "Vita Games";

        case CatalogType::PspGames:
            return "PSP";

        case CatalogType::Ps1Games:
            return "PS1";

        default:
            return "Unknown";
    }
}

} // namespace ui
} // namespace psvitaalive
