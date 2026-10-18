// Standalone host test of dynamic instructions:
// c++ -std=c++17 -Wall -Wextra -Werror -I"Client PSVitaAlive/include" tests/test_web_qr_guide.cpp "Client PSVitaAlive/source/ui/web_qr_guide.cpp" -o /tmp/test_web_qr_guide && /tmp/test_web_qr_guide
#include "ui/web_qr_guide.hpp"
#include <cassert>
using namespace psvitaalive::ui;

CatalogLink makeLink(const char* type, const char* name, const char* url,
                     const char* extractPath = "") {
    CatalogLink l;
    l.type = type;
    l.name = name;
    l.url = url;
    l.extractPath = extractPath;
    return l;
}
bool contains(const std::vector<WebQrGuideStep>& steps, WebQrGuideKind kind,
              const std::string& path = {}) {
    for (const auto& s : steps)
        if (s.kind == kind && s.destination == path) return true;
    return false;
}
int main() {
    CatalogItem home;
    home.linkDetails = {
        makeLink("Download", "VPK", "https://example.org/game.vpk"),
        makeLink("Game Files", "Game Files English", "https://example.org/data.zip", "ux0:data/"),
        makeLink("Game Files", "Game Files Spanish", "https://example.org/data2.zip", "ux0:data/"),
        makeLink("Data Files", "Extra", "https://example.org/extra.zip", "ux0:custom/"),
    };
    auto g = makeWebQrGuide(CatalogType::Homebrew, home);
    assert(g.size() == 4);
    assert(g[0].kind == WebQrGuideKind::Start);
    assert(contains(g, WebQrGuideKind::Vpk));
    assert(contains(g, WebQrGuideKind::GameFiles, "ux0:data/"));
    assert(contains(g, WebQrGuideKind::DataFiles, "ux0:custom/"));
    home.linkDetails = {makeLink("Game Files", "Game Files", "https://example.org/g.zip")};
    g = makeWebQrGuide(CatalogType::Homebrew, home);
    assert(g.size() == 2);
    assert(contains(g, WebQrGuideKind::GameFiles));
    home.linkDetails = {makeLink("Download", "Downloads", "https://mediafire.com/download?id=3")};
    g = makeWebQrGuide(CatalogType::Homebrew, home);
    assert(contains(g, WebQrGuideKind::Vpk));
    CatalogItem psp;
    psp.linkDetails = {makeLink("Download", "Game", "https://example.org/foo.iso")};
    g = makeWebQrGuide(CatalogType::PspGames, psp);
    assert(contains(g, WebQrGuideKind::PspIso));
    assert(!contains(g, WebQrGuideKind::Vpk));
    CatalogItem ps1;
    ps1.linkDetails = {makeLink("Download", "EBOOT", "https://example.org/EBOOT.PBP")};
    g = makeWebQrGuide(CatalogType::Ps1Games, ps1);
    assert(contains(g, WebQrGuideKind::Ps1Pbp));
    CatalogItem vita;
    vita.linkDetails = {makeLink("Pkg", "Game", "https://example.org/game.pkg")};
    g = makeWebQrGuide(CatalogType::VitaGames, vita);
    assert(contains(g, WebQrGuideKind::Pkg));
    CatalogItem empty;
    g = makeWebQrGuide(CatalogType::Homebrew, empty);
    assert(contains(g, WebQrGuideKind::Other));
    home.linkDetails = {makeLink("Data Files", "unsafe", "https://example.org/data.zip", "ux0:data/../bad")};
    g = makeWebQrGuide(CatalogType::Homebrew, home);
    assert(contains(g, WebQrGuideKind::DataFiles));
    assert(!contains(g, WebQrGuideKind::DataFiles, "ux0:data/../bad"));
    return 0;
}
