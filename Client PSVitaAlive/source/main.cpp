/* PSVitaAlive - native client entry point. */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ime_dialog.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "ui/full_catalog_screen.hpp"
#include "ui/image_cache.hpp"
#include "catalog/catalog_manager.hpp"

namespace {
constexpr const char* DIAG_DIR="ux0:data/psvitaalive/logs";
constexpr const char* DIAG_LOG="ux0:data/psvitaalive/logs/session.log";
void ensureDiagnosticLog(){sceIoMkdir("ux0:data/psvitaalive",0777);sceIoMkdir(DIAG_DIR,0777);}
void diagnosticLog(const std::string& msg){ensureDiagnosticLog();SceUID fd=sceIoOpen(DIAG_LOG,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);if(fd<0)return;char line[1400];uint64_t ms=sceKernelGetProcessTimeWide()/1000ULL;sceClibSnprintf(line,sizeof(line),"[%llu ms] %s\n",(unsigned long long)ms,msg.c_str());sceIoWrite(fd,line,std::strlen(line));sceIoClose(fd);}
std::string installStatusText(const psvitaalive::InstallStatus&s){using S=psvitaalive::InstallStatus::State;if(s.state==S::Idle)return{};char b[384];uint64_t p=s.total?std::min<uint64_t>(100,(s.current*100)/s.total):0;sceClibSnprintf(b,sizeof(b),"%s | %s | %llu%% | %s",s.stage.c_str(),s.fileName.empty()?"file":s.fileName.c_str(),(unsigned long long)p,s.message.c_str());return b;}
bool asciiToWide(const std::string&text,SceWChar16*out,size_t cap){if(!out||!cap)return false;size_t i=0;for(;i+1<cap&&i<text.size();++i){unsigned char c=(unsigned char)text[i];out[i]=(SceWChar16)(c<128?c:'?');}out[i]=0;return true;}
std::string wideToAscii(const SceWChar16*t){if(!t)return{};std::string r;for(size_t i=0;t[i]&&i<2048;++i)r.push_back(t[i]<=0x7F?(char)t[i]:'?');return r;}
bool promptZipDestination(std::string&dst){static bool loaded=false;if(!loaded){int r=sceSysmoduleLoadModule(SCE_SYSMODULE_IME);if(r<0)return false;loaded=true;}SceWChar16 input[256]={},title[128]={};asciiToWide(dst.empty()?"ux0:data/":dst,input,256);asciiToWide("ZIP extraction path",title,128);SceImeDialogParam p={};p.type=SCE_IME_TYPE_BASIC_LATIN;p.option=SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;p.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;p.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;p.title=title;p.maxTextLength=255;p.initialText=input;p.inputTextBuffer=input;p.supportedLanguages=SCE_IME_LANGUAGE_ENGLISH|SCE_IME_LANGUAGE_SPANISH;p.enterLabel=SCE_IME_ENTER_LABEL_GO;p.commonParam.magic=SCE_COMMON_DIALOG_MAGIC_NUMBER;if(sceImeDialogInit(&p)<0)return false;while(sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING)sceKernelDelayThread(10*1000);SceImeDialogResult r={};sceImeDialogGetResult(&r);bool ok=r.button==SCE_IME_DIALOG_BUTTON_ENTER;if(ok){dst=wideToAscii(input);for(char&c:dst)if(c=='\\')c='/';while(dst.size()>1&&dst.back()=='/')dst.pop_back();}sceImeDialogTerm();return ok&&!dst.empty();}
}

int main(){
 diagnosticLog("============================================================");diagnosticLog("PSVitaAlive session BEGIN");diagnosticLog("TitleID=PSVA00001");
 psvitaalive::StorageManager storage;storage.initProjectDirs();
 psvitaalive::InstallController installer;psvitaalive::CatalogManager catalogs;psvitaalive::ui::ImageCache images;
 catalogs.init();images.init();
 psvitaalive::ui::FullCatalogScreen screen;screen.setImageCache(&images);
 screen.setCatalogChangeCallback([&](psvitaalive::ui::CatalogType next){screen.setActiveCatalog(next);return catalogs.request(next);});
 screen.setInstallCallbacks([&installer](const psvitaalive::ui::CatalogItem&item){diagnosticLog("INSTALL REQUEST name="+item.name+" title_id="+item.titleId+" url="+item.downloadUrl);if(item.downloadUrl.empty()||item.downloadFileName.empty())return false;std::string zipDestination;if(item.downloadFileName.size()>=4&&item.downloadFileName.substr(item.downloadFileName.size()-4)==".zip"){zipDestination="ux0:data/";if(!promptZipDestination(zipDestination))return false;}return installer.requestInstall(item.downloadUrl,item.downloadFileName,zipDestination);},[&installer](){return installStatusText(installer.status());});
 if(!screen.init()){installer.shutdown();catalogs.shutdown();images.shutdown();sceKernelExitProcess(1);return 1;}
 screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);catalogs.request(psvitaalive::ui::CatalogType::Homebrew);
 while(screen.updateAndDraw()){
   psvitaalive::CatalogManager::Status cs=catalogs.status();
   if(cs.state==psvitaalive::CatalogManager::State::Loading)screen.setCatalogLoading(true,cs.label,cs.current,cs.total,cs.message);
   else if(cs.state==psvitaalive::CatalogManager::State::Failed)screen.setCatalogError(cs.error.empty()?"Unable to load catalog":cs.error);
   std::vector<psvitaalive::ui::CatalogItem> ready;psvitaalive::ui::CatalogType readyCatalog;
   if(catalogs.takeReady(ready,readyCatalog)){screen.setCatalogItems(std::move(ready));screen.setActiveCatalog(readyCatalog);screen.setCatalogLoading(false,psvitaalive::ui::catalogName(readyCatalog),1,1,"Ready");}

   const psvitaalive::InstallStatus cur=installer.status();
   using InstallState=psvitaalive::InstallStatus::State;
   const bool installActive=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||cur.state==InstallState::Completed||cur.state==InstallState::Failed;
   screen.setInstallProgress(installActive,cur.current,cur.total,cur.bytesPerSecond,cur.stage,cur.fileName,cur.message);
 }
 screen.setInstallProgress(false,0,0,0,"","","");
 screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();diagnosticLog("PSVitaAlive session END");sceKernelExitProcess(0);return 0;
}
