#include "ui/storage_catalog_policy.hpp"
#include "storage/installed_data_policy.hpp"
#include <cassert>
#include <cstring>
using namespace psvitaalive::ui;
using namespace psvitaalive::storage;
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
    }
    assert(std::strcmp(storageInstalledSectionKey(CatalogType::PspGames),"ID_SECTION_PSP")==0);
    assert(std::strcmp(storageInstalledSectionKey(CatalogType::Ps1Games),"ID_SECTION_PS1")==0);
    assert(std::strcmp(storageInstalledSectionKey(CatalogType::VitaGames),"ID_SECTION_VITA")==0);
    const std::string psp = "ULUS12345";
    const std::string ps1 = "NPUJ12345";
    for (const std::string mount : {"ux0","uma0","imc0","xmc0"}) {
        const std::string base = mount + ":pspemu/";
        assert(safeGameMediaPath(GameMediaKind::Psp,psp,base+"ISO/Example [ULUS12345].iso"));
        assert(safeGameMediaPath(GameMediaKind::Psp,psp,base+"ISO/ulus12345.cso"));
        assert(safeGameMediaPath(GameMediaKind::Psp,psp,base+"PSP/GAME/ULUS12345"));
        assert(safeGameMediaPath(GameMediaKind::Ps1,ps1,base+"PSP/GAME/SLUS12345"));
        assert(!safeGameMediaPath(GameMediaKind::Ps1,ps1,base+"ISO/ULUS12345.iso"));
        assert(!safeGameMediaPath(GameMediaKind::Psp,psp,base+"PSP/GAME/NPUJ12345"));
        assert(!safeGameMediaPath(GameMediaKind::Psp,psp,base+"ISO/"));
        assert(!safeGameMediaPath(GameMediaKind::Psp,psp,base+"PSP/GAME/../ULUS12345"));
        assert(!safeGameMediaPath(GameMediaKind::Ps1,ps1,base+"PSP/SAVEDATA/SLUS12345"));
        assert(!safeGameMediaPath(GameMediaKind::None,psp,base+"ISO/ULUS12345.iso"));
    }
    assert(!safeGameMediaPath(GameMediaKind::Psp,psp,"ur0:pspemu/ISO/ULUS12345.iso"));
    assert(!safeGameMediaPath(GameMediaKind::Psp,psp,"ux0:data/ULUS12345"));
    assert(!safeGameMediaPath(GameMediaKind::Psp,"PSVAS1178","ux0:pspemu/ISO/PSVAS1178.iso"));
    InstalledDataApp app;
    app.titleId=psp;
    InstalledDataPath path;
    path.path="ux0:pspemu/ISO/ULUS12345.iso";
    path.exists=true;
    app.paths.push_back(path);
    assert(!installedDataCanOfferExternalDelete(app));
    app.commercial=true;
    app.mediaKind=GameMediaKind::Psp;
    assert(installedDataCanOfferExternalDelete(app));
    app.mediaKind=GameMediaKind::Ps1;
    assert(!installedDataCanOfferExternalDelete(app));
}
