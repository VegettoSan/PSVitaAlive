#include "ui/storage_catalog_policy.hpp"
#include <cassert>
#include <cstring>
using namespace psvitaalive::ui;
int main() {
    assert(storageShowsSavedZips(CatalogType::Homebrew));
    assert(storageCatalogCleanupAction(CatalogType::Homebrew,-2) == -2);
    assert(storageCleanupItemAllowed(CatalogType::Homebrew,true));
    for (const auto catalog : {CatalogType::VitaGames, CatalogType::PspGames, CatalogType::Ps1Games}) {
        assert(!storageShowsSavedZips(catalog));
        assert(!storageCleanupItemAllowed(catalog,true));
        assert(storageCleanupItemAllowed(catalog,false));
        assert(storageCatalogCleanupAction(catalog,-2) == -3);
        assert(storageCatalogCleanupAction(catalog,-4) == -1);
        assert(storageCatalogCleanupAction(catalog,-1) == -1);
        assert(storageCatalogCleanupAction(catalog,0) == 0);
    }
    assert(std::strcmp(storageInstalledSectionKey(CatalogType::Homebrew),"ID_SECTION")==0);
    assert(std::strcmp(storageInstalledSectionKey(CatalogType::PspGames),"ID_SECTION_PSP")==0);
    assert(std::strcmp(storageInstalledSectionKey(CatalogType::Ps1Games),"ID_SECTION_PS1")==0);
    assert(std::strcmp(storageInstalledSectionKey(CatalogType::VitaGames),"ID_SECTION_VITA")==0);
}
