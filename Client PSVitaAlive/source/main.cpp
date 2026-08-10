/* PSVitaAlive - native client entry point. */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/ime_dialog.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "diagnostic_logger.hpp"
#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "ui/full_catalog_screen.hpp"
#include "ui/image_cache.hpp"
#include "catalog/catalog_manager.hpp"

namespace {
std::string installStatusText(const psvitaalive::InstallStatus&s){using S=psvitaalive::InstallStatus::State;if(s.state==S::Idle)return{};char b[384];uint64_t p=s.total?std::min<uint64_t>(100,(s.current*100)/s.total):0;sceClibSnprintf(b,sizeof(b),"%s | %s | %llu%% | %s",s.stage.c_str(),s.fileName.empty()?"file":s.fileName.c_str(),(unsigned long long)p,s.message.c_str());return b;}
bool asciiToWide(const std::string&text,SceWChar16*out,size_t cap){if(!out||!cap)return false;size_t i=0;for(;i+1<cap&&i<text.size();++i){unsigned char c=(unsigned char)text[i];out[i]=(SceWChar16)(c<128?c:'?');}out[i]=0;return true;}
std::string wideToAscii(const SceWChar16*t){if(!t)return{};std::string r;for(size_t i=0;t[i]&&i<2048;++i)r.push_back(t[i]<=0x7F?(char)t[i]:'?');return r;}
bool promptZipDestination(std::string&dst){static bool loaded=false;if(!loaded){int r=sceSysmoduleLoadModule(SCE_SYSMODULE_IME);if(r<0)return false;loaded=true;}SceWChar16 input[256]={},title[128]={};asciiToWide(dst.empty()?"ux0:data/":dst,input,256);asciiToWide("ZIP extraction path",title,128);SceImeDialogParam p={};p.type=SCE_IME_TYPE_BASIC_LATIN;p.option=SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;p.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;p.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;p.title=title;p.maxTextLength=255;p.initialText=input;p.inputTextBuffer=input;p.supportedLanguages=SCE_IME_LANGUAGE_ENGLISH|SCE_IME_LANGUAGE_SPANISH;p.enterLabel=SCE_IME_ENTER_LABEL_GO;p.commonParam.magic=SCE_COMMON_DIALOG_MAGIC_NUMBER;if(sceImeDialogInit(&p)<0)return false;while(sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING)sceKernelDelayThread(10*1000);SceImeDialogResult r={};sceImeDialogGetResult(&r);bool ok=r.button==SCE_IME_DIALOG_BUTTON_ENTER;if(ok){dst=wideToAscii(input);for(char&c:dst)if(c=='\\')c='/';while(dst.size()>1&&dst.back()=='/')dst.pop_back();}sceImeDialogTerm();return ok&&!dst.empty();}
}

int main(){
    psvitaalive::diagnostics::init();
    psvitaalive::diagnostics::log("============================================================");
    psvitaalive::diagnostics::log("PSVitaAlive session BEGIN");
    psvitaalive::diagnostics::log("TitleID=PSVA00001");

    psvitaalive::StorageManager storage;storage.initProjectDirs();
    psvitaalive::InstallController installer;psvitaalive::CatalogManager catalogs;psvitaalive::ui::ImageCache images;
    if(!catalogs.init())psvitaalive::diagnostics::log("[System] CatalogManager init failed");
    if(!images.init())psvitaalive::diagnostics::log("[System] ImageCache init failed");

    psvitaalive::ui::FullCatalogScreen screen;screen.setImageCache(&images);
    screen.setCatalogChangeCallback([&](psvitaalive::ui::CatalogType next){psvitaalive::diagnostics::log(std::string("[UI] catalog requested: ")+psvitaalive::ui::catalogName(next));return catalogs.request(next);});
    screen.setInstallCallbacks([&installer](const psvitaalive::ui::CatalogItem&item){psvitaalive::diagnostics::log("[UI] INSTALL REQUEST name="+item.name+" title_id="+item.titleId+" url="+item.downloadUrl);if(item.downloadUrl.empty()||item.downloadFileName.empty())return false;std::string zipDestination;if(item.downloadFileName.size()>=4&&item.downloadFileName.substr(item.downloadFileName.size()-4)==".zip"){zipDestination="ux0:data/";if(!promptZipDestination(zipDestination)){psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");return false;}}return installer.requestInstall(item.downloadUrl,item.downloadFileName,zipDestination);},[&installer](){return installStatusText(installer.status());});

    if(!screen.init()){psvitaalive::diagnostics::log("[System] UI initialization failed");installer.shutdown();catalogs.shutdown();images.shutdown();psvitaalive::diagnostics::shutdown();sceKernelExitProcess(1);return 1;}

    // Startup cache verification: check every official catalog once, sequentially.
    // Cached catalogs are parsed locally when validators match; only missing or
    // changed catalogs are downloaded. The user sees one coherent overlay for
    // the whole startup verification sequence.
    const int catalogCount=(int)psvitaalive::ui::CatalogType::Count;
    int preloadIndex=0;
    bool preloading=true;
    bool homebrewReady=false;
    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(psvitaalive::ui::CatalogType::Homebrew),0,0,"Checking catalog cache...");
    if(!catalogs.request(psvitaalive::ui::CatalogType::Homebrew)){preloading=false;screen.setCatalogError("Unable to start catalog check");}

    while(screen.updateAndDraw()){
        psvitaalive::CatalogManager::Status cs=catalogs.status();
        if(cs.state==psvitaalive::CatalogManager::State::Loading){
            screen.setCatalogLoading(true,cs.label,cs.current,cs.total,cs.message);
        } else if(cs.state==psvitaalive::CatalogManager::State::Failed){
            if(preloading){
                psvitaalive::diagnostics::log(std::string("[Startup] catalog failed: ")+cs.label+" error="+cs.error);
                ++preloadIndex;
                if(preloadIndex<catalogCount){
                    const auto next=(psvitaalive::ui::CatalogType)preloadIndex;
                    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Catalog unavailable; checking next catalog...");
                    catalogs.request(next);
                } else {
                    preloading=false;
                    if(homebrewReady)screen.setCatalogLoading(false,"Homebrew",1,1,"Ready");
                    else screen.setCatalogError("Homebrew catalog unavailable");
                }
            } else {
                screen.setCatalogError(cs.error.empty()?"Unable to load catalog":cs.error);
            }
        }

        std::vector<psvitaalive::ui::CatalogItem> ready;psvitaalive::ui::CatalogType readyCatalog;
        if(catalogs.takeReady(ready,readyCatalog)){
            psvitaalive::diagnostics::log(std::string("[System] catalog ready: ")+psvitaalive::ui::catalogName(readyCatalog));
            if(preloading){
                if(readyCatalog==psvitaalive::ui::CatalogType::Homebrew){
                    screen.setCatalogItems(std::move(ready));
                    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
                    homebrewReady=true;
                }
                ++preloadIndex;
                if(preloadIndex<catalogCount){
                    const auto next=(psvitaalive::ui::CatalogType)preloadIndex;
                    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Checking next catalog...");
                    catalogs.request(next);
                } else {
                    preloading=false;
                    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
                    if(homebrewReady)screen.setCatalogLoading(false,"Homebrew",1,1,"All catalogs checked");
                }
            } else {
                screen.setCatalogItems(std::move(ready));
                screen.setActiveCatalog(readyCatalog);
                screen.setCatalogLoading(false,psvitaalive::ui::catalogName(readyCatalog),1,1,"Ready");
            }
        }

        const psvitaalive::InstallStatus cur=installer.status();
        using InstallState=psvitaalive::InstallStatus::State;
        const bool installActive=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||cur.state==InstallState::Completed||cur.state==InstallState::Failed;
        screen.setInstallProgress(installActive,cur.current,cur.total,cur.bytesPerSecond,cur.stage,cur.fileName,cur.message);
    }

    screen.setInstallProgress(false,0,0,0,"","","");
    screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();
    psvitaalive::diagnostics::log("PSVitaAlive session END");
    psvitaalive::diagnostics::shutdown();
    sceKernelExitProcess(0);return 0;
}
