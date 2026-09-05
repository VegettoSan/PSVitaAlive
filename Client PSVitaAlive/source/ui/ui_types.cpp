#include "ui/ui_types.hpp"
#include "localization/localization.hpp"

namespace psvitaalive {
namespace ui {

const char* catalogName(CatalogType t) {
    switch (t) {
        case CatalogType::Homebrew:
            return L(TextId::CatalogHomebrew);
        case CatalogType::VitaGames:
            return L(TextId::CatalogVitaGames);
        case CatalogType::PspGames:
            return L(TextId::CatalogPsp);
        case CatalogType::Ps1Games:
            return L(TextId::CatalogPs1);
        default:
            return L(TextId::CatalogUnknown);
    }
}

} // namespace ui
} // namespace psvitaalive
