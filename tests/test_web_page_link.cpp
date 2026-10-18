// Build on Linux/macOS with:
// c++ -std=c++17 -I"Client PSVitaAlive/include" tests/test_web_page_link.cpp "Client PSVitaAlive/source/ui/web_page_link.cpp" -o /tmp/test_web_page_link && /tmp/test_web_page_link
#include "ui/web_page_link.hpp"
#include <cassert>
#include <string>
int main() {
    using namespace psvitaalive::ui;
    const std::string base = "https://vegettosan.github.io/PSVitaAlive/";
    CatalogItem item;
    item.id = "my-game";
    item.titleId = "VITAQUAKE";
    assert(webPageUrl(CatalogType::Homebrew,item)==base+"app.html?title_id=VITAQUAKE");
    assert(webPageUrl(CatalogType::VitaGames,item)==base+"game.html?catalog=psvita&id=my-game");
    assert(webPageUrl(CatalogType::PspGames,item)==base+"game.html?catalog=psp&id=my-game");
    assert(webPageUrl(CatalogType::Ps1Games,item)==base+"game.html?catalog=ps1&id=my-game");
    item.id = "game with space";
    assert(webPageUrl(CatalogType::Ps1Games,item)==base+"game.html?catalog=ps1&id=game%20with%20space");
    item.titleId.clear();
    assert(webPageUrl(CatalogType::Homebrew,item).empty());
    item.id.clear();
    assert(webPageUrl(CatalogType::PspGames,item).empty());
    assert(webPageUrl(CatalogType::Count,item).empty());
    return 0;
}
