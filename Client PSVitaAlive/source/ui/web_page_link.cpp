#include "ui/web_page_link.hpp"
namespace psvitaalive { namespace ui {
namespace {
std::string urlEncode(const std::string& s) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '~')
            out.push_back(static_cast<char>(c));
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
bool safeId(const std::string& s) {
    if (s.empty() || s.size() > 160) return false;
    for (unsigned char c : s) if (c < 0x21 || c == 0x7F) return false;
    return true;
}
}
std::string webPageUrl(CatalogType catalog, const CatalogItem& item) {
    constexpr const char* BASE = "https://vegettosan.github.io/PSVitaAlive/";
    if (catalog == CatalogType::Homebrew) {
        if (!safeId(item.titleId)) return {};
        return std::string(BASE) + "app.html?title_id=" + urlEncode(item.titleId);
    }
    const char* type = nullptr;
    switch(catalog) {
        case CatalogType::VitaGames: type = "psvita"; break;
        case CatalogType::PspGames: type = "psp"; break;
        case CatalogType::Ps1Games: type = "ps1"; break;
        default: return {};
    }
    if (!safeId(item.id)) return {};
    return std::string(BASE) + "game.html?catalog=" + type + "&id=" + urlEncode(item.id);
}
}} // namespace psvitaalive::ui
