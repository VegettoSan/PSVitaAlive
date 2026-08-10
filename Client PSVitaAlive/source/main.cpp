/* PSVitaAlive - native client entry point. */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/ime_dialog.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>
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
bool promptText(const std::string&initial,const std::string&title,std::string&out){static bool loaded=false;if(!loaded){int r=sceSysmoduleLoadModule(SCE_SYSMODULE_IME);if(r<0)return false;loaded=true;}SceWChar16 input[256]={},wtitle[128]={};asciiToWide(initial,input,256);asciiToWide(title,wtitle,128);SceImeDialogParam p={};p.type=SCE_IME_TYPE_BASIC_LATIN;p.option=SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;p.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;p.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;p.title=wtitle;p.maxTextLength=255;p.initialText=input;p.inputTextBuffer=input;p.supportedLanguages=SCE_IME_LANGUAGE_ENGLISH|SCE_IME_LANGUAGE_SPANISH;p.enterLabel=SCE_IME_ENTER_LABEL_SEARCH;p.commonParam.magic=SCE_COMMON_DIALOG_MAGIC_NUMBER;if(sceImeDialogInit(&p)<0)return false;while(sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING)sceKernelDelayThread(10*1000);SceImeDialogResult r={};sceImeDialogGetResult(&r);bool ok=r.button==SCE_IME_DIALOG_BUTTON_ENTER;if(ok)out=wideToAscii(input);sceImeDialogTerm();return ok;}
bool promptZipDestination(std::string&dst){static bool loaded=false;if(!loaded){int r=sceSysmoduleLoadModule(SCE_SYSMODULE_IME);if(r<0)return false;loaded=true;}SceWChar16 input[256]={},title[128]={};asciiToWide(dst.empty()?"ux0:data/":dst,input,256);asciiToWide("ZIP extraction path",title,128);SceImeDialogParam p={};p.type=SCE_IME_TYPE_BASIC_LATIN;p.option=SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;p.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;p.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;p.title=title;p.maxTextLength=255;p.initialText=input;p.inputTextBuffer=input;p.supportedLanguages=SCE_IME_LANGUAGE_ENGLISH|SCE_IME_LANGUAGE_SPANISH;p.enterLabel=SCE_IME_ENTER_LABEL_GO;p.commonParam.magic=SCE_COMMON_DIALOG_MAGIC_NUMBER;if(sceImeDialogInit(&p)<0)return false;while(sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING)sceKernelDelayThread(10*1000);SceImeDialogResult r={};sceImeDialogGetResult(&r);bool ok=r.button==SCE_IME_DIALOG_BUTTON_ENTER;if(ok){dst=wideToAscii(input);for(char&c:dst)if(c=='\\')c='/';while(dst.size()>1&&dst.back()=='/')dst.pop_back();}sceImeDialogTerm();return ok&&!dst.empty();}
std::string fileNameFromUrl(const std::string&url,const std::string&id){std::string clean=url;const size_t q=clean.find('?');if(q!=std::string::npos)clean.erase(q);const size_t f=clean.find('#');if(f!=std::string::npos)clean.erase(f);const size_t slash=clean.find_last_of('/');std::string name=slash==std::string::npos?clean:clean.substr(slash+1);return name.empty()?id+".bin":name;}
bool isZipName(const std::string&name){return name.size()>=4&&name.substr(name.size()-4)==".zip";}
struct StartupImageJob{std::string url;std::string path;std::string namespaceName;std::string fileName;};
void addStartupImage(std::vector<StartupImageJob>&jobs,std::unordered_set<std::string>&seen,psvitaalive::ui::ImageCache&images,const std::string&url,const std::string&namespaceName){if(url.empty())return;const std::string key=namespaceName+"\n"+url;if(!seen.insert(key).second)return;const std::string path=images.request(url,namespaceName);if(path.empty())return;jobs.push_back({url,path,namespaceName,fileNameFromUrl(url,namespaceName+"_image")});}
void collectCatalogImages(std::vector<StartupImageJob>&jobs,std::unordered_set<std::string>&seen,psvitaalive::ui::ImageCache&images,const std::vector<psvitaalive::ui::CatalogItem>&items){for(const auto&item:items){addStartupImage(jobs,seen,images,item.icon,"app");addStartupImage(jobs,seen,images,item.cover,"app");const size_t count=std::min<size_t>(5,item.screenshots.size());for(size_t i=0;i<count;++i)addStartupImage(jobs,seen,images,item.screenshots[i],"shot");}}
void imageWarmupProgress(const std::vector<StartupImageJob>&jobs,psvitaalive::ui::ImageCache&images,uint64_t&completed,std::string&currentFile,bool&failedCurrent){completed=0;currentFile.clear();failedCurrent=false;for(const auto&job:jobs){const bool ready=images.isReady(job.path),failed=images.isFailed(job.path);if(ready||failed){++completed;continue;}if(currentFile.empty()){currentFile=job.fileName;failedCurrent=false;}}}
std::string progressMessage(uint64_t current,uint64_t total,const std::string&prefix,const std::string&file){char b[320];const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;sceClibSnprintf(b,sizeof(b),"%s | %llu%% | %llu / %llu%s%s",prefix.c_str(),(unsigned long long)pct,(unsigned long long)current,(unsigned long long)total,file.empty()?"":" | ",file.c_str());return b;}
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
    screen.setSearchCallback([&](const std::string&current){std::string result=current;if(promptText(current,"Search catalog",result))return result;return current;});
    screen.setInstallCallbacks([&installer](const psvitaalive::ui::CatalogItem&item){psvitaalive::diagnostics::log("[UI] INSTALL REQUEST name="+item.name+" title_id="+item.titleId+" url="+item.downloadUrl);if(item.downloadUrl.empty()||item.downloadFileName.empty())return false;std::string zipDestination;if(isZipName(item.downloadFileName)){zipDestination="ux0:data/";if(!promptZipDestination(zipDestination)){psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");return false;}}return installer.requestInstall(item.downloadUrl,item.downloadFileName,zipDestination);},[&installer](){return installStatusText(installer.status());});
    screen.setLinkActionCallback([&installer](const psvitaalive::ui::CatalogItem&item,const psvitaalive::ui::CatalogLink&link){
        const std::string type=link.type;
        const bool actionable=(type=="Download"||type=="download"||type=="Downloads"||type=="Mirror"||type=="mirror"||type=="DLC"||type=="dlc"||link.url.find(".vpk")!=std::string::npos||link.url.find(".pkg")!=std::string::npos||link.url.find(".zip")!=std::string::npos||link.url.find(".pbp")!=std::string::npos||link.url.find(".iso")!=std::string::npos||link.url.find(".cso")!=std::string::npos);
        if(!actionable){psvitaalive::diagnostics::log(std::string("[UI] link is informational only: ")+link.url);return false;}
        psvitaalive::ui::CatalogItem requestItem=item;requestItem.downloadUrl=link.url;requestItem.downloadFileName=fileNameFromUrl(link.url,item.id);
        psvitaalive::diagnostics::log(std::string("[UI] LINK INSTALL name=")+item.name+" type="+link.type+" url="+link.url);
        std::string zipDestination;if(isZipName(requestItem.downloadFileName)){zipDestination="ux0:data/";if(!promptZipDestination(zipDestination)){psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");return false;}}
        return installer.requestInstall(requestItem.downloadUrl,requestItem.downloadFileName,zipDestination);
    });

    if(!screen.init()){psvitaalive::diagnostics::log("[System] UI initialization failed");installer.shutdown();catalogs.shutdown();images.shutdown();psvitaalive::diagnostics::shutdown();sceKernelExitProcess(1);return 1;}

    const int catalogCount=(int)psvitaalive::ui::CatalogType::Count;
    int preloadIndex=0;bool startupCatalogs=true;bool startupImages=false;bool homebrewReady=false;
    std::vector<std::vector<psvitaalive::ui::CatalogItem>> startupCatalogItems((size_t)catalogCount);
    std::vector<StartupImageJob> startupImagesJobs;std::unordered_set<std::string> startupImageSeen;
    uint64_t lastImageProgressPoll=0,lastImageCompleted=0;std::string lastImageFile;

    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(psvitaalive::ui::CatalogType::Homebrew),0,0,"Checking catalog cache...");
    if(!catalogs.request(psvitaalive::ui::CatalogType::Homebrew)){startupCatalogs=false;screen.setCatalogError("Unable to start catalog check");}

    while(screen.updateAndDraw()){
        const uint64_t now=sceKernelGetSystemTimeWide();
        psvitaalive::CatalogManager::Status cs=catalogs.status();
        if(startupCatalogs&&cs.state==psvitaalive::CatalogManager::State::Loading){screen.setCatalogLoading(true,cs.label,cs.current,cs.total,progressMessage(cs.current,cs.total,cs.message,""));}
        else if(startupCatalogs&&cs.state==psvitaalive::CatalogManager::State::Failed){
            psvitaalive::diagnostics::log(std::string("[Startup] catalog failed: ")+cs.label+" error="+cs.error);
            ++preloadIndex;
            if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Checking next catalog cache...");catalogs.request(next);}
            else{
                startupCatalogs=false;startupImages=true;
                for(const auto&items:startupCatalogItems)collectCatalogImages(startupImagesJobs,startupImageSeen,images,items);
                lastImageCompleted=0;lastImageFile.clear();
                screen.setCatalogLoading(true,"Preparing images",0,(uint64_t)startupImagesJobs.size(),"Preparing startup image cache...");
                psvitaalive::diagnostics::log("[Startup] all catalogs processed; starting image warmup");
            }
        }

        std::vector<psvitaalive::ui::CatalogItem> ready;psvitaalive::ui::CatalogType readyCatalog;
        if(catalogs.takeReady(ready,readyCatalog)){
            psvitaalive::diagnostics::log(std::string("[System] catalog ready: ")+psvitaalive::ui::catalogName(readyCatalog));
            startupCatalogItems[(int)readyCatalog]=ready;
            if(startupCatalogs){
                if(readyCatalog==psvitaalive::ui::CatalogType::Homebrew){screen.setCatalogItems(ready);screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);homebrewReady=true;}
                ++preloadIndex;
                if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Checking next catalog cache...");catalogs.request(next);}
                else{
                    startupCatalogs=false;startupImages=true;for(const auto&items:startupCatalogItems)collectCatalogImages(startupImagesJobs,startupImageSeen,images,items);
                    lastImageCompleted=0;lastImageFile.clear();screen.setCatalogLoading(true,"Preparing images",0,(uint64_t)startupImagesJobs.size(),"Preparing startup image cache...");
                    psvitaalive::diagnostics::log("[Startup] all catalogs ready; starting image warmup");
                }
            }else{
                screen.setCatalogItems(std::move(ready));screen.setActiveCatalog(readyCatalog);screen.setCatalogLoading(false,psvitaalive::ui::catalogName(readyCatalog),1,1,"Ready");
            }
        }

        if(startupImages&&now>=lastImageProgressPoll){
            lastImageProgressPoll=now+500000;
            uint64_t completed=0;std::string currentFile;bool failedCurrent=false;imageWarmupProgress(startupImagesJobs,images,completed,currentFile,failedCurrent);
            lastImageCompleted=completed;lastImageFile=currentFile;
            if(startupImagesJobs.empty()||completed>=(uint64_t)startupImagesJobs.size()){
                startupImages=false;screen.setCatalogLoading(false,"",completed,(uint64_t)startupImagesJobs.size(),"Image cache ready");
                psvitaalive::diagnostics::log("[Startup] image warmup complete");
                if(homebrewReady){screen.setCatalogItems(startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew]);screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);}
            }else{
                std::string msg=progressMessage(completed,(uint64_t)startupImagesJobs.size(),failedCurrent?"Retrying image":"Downloading image",currentFile);
                screen.setCatalogLoading(true,"Preparing images",completed,(uint64_t)startupImagesJobs.size(),msg);
            }
        }

        const psvitaalive::InstallStatus cur=installer.status();using InstallState=psvitaalive::InstallStatus::State;const bool active=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||cur.state==InstallState::Completed||cur.state==InstallState::Failed;screen.setInstallProgress(active,cur.current,cur.total,cur.bytesPerSecond,cur.stage,cur.fileName,cur.message);
    }

    screen.setInstallProgress(false,0,0,0,"","","");screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();psvitaalive::diagnostics::log("PSVitaAlive session END");psvitaalive::diagnostics::shutdown();sceKernelExitProcess(0);return 0;
}
