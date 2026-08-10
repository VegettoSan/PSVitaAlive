/* PSVitaAlive - native client entry point. */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/message_dialog.h>
#include <psp2/ime_dialog.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "ui/full_catalog_screen.hpp"
#include "ui/image_cache.hpp"
#include "catalog/catalog_parser.hpp"
#include "catalog/catalog_manager.hpp"
#include "network/http_client.hpp"

namespace {
constexpr const char* DIAG_DIR="ux0:data/psvitaalive/logs";
constexpr const char* DIAG_LOG="ux0:data/psvitaalive/logs/session.log";
bool gProgressDialogOpen=false;
void ensureDiagnosticLog(){sceIoMkdir("ux0:data/psvitaalive",0777);sceIoMkdir(DIAG_DIR,0777);}
void diagnosticLog(const std::string& msg){ensureDiagnosticLog();SceUID fd=sceIoOpen(DIAG_LOG,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);if(fd<0)return;char line[1400];uint64_t ms=sceKernelGetProcessTimeWide()/1000ULL;sceClibSnprintf(line,sizeof(line),"[%llu ms] %s\n",(unsigned long long)ms,msg.c_str());sceIoWrite(fd,line,std::strlen(line));sceIoClose(fd);}
double mib(uint64_t v){return(double)v/(1024.0*1024.0);}
const char* stateName(psvitaalive::InstallStatus::State s){using S=psvitaalive::InstallStatus::State;switch(s){case S::Downloading:return"Downloading";case S::Installing:return"Installing";case S::Completed:return"Completed";case S::Failed:return"Failed";default:return"Idle";}}
std::string installStatusText(const psvitaalive::InstallStatus&s){if(s.state==psvitaalive::InstallStatus::State::Idle)return{};char b[320];uint64_t p=s.total?std::min<uint64_t>(100,(s.current*100)/s.total):0;sceClibSnprintf(b,sizeof(b),"%s | %s | %llu%% | %.2f MiB/s",stateName(s.state),s.fileName.empty()?"file":s.fileName.c_str(),(unsigned long long)p,mib(s.bytesPerSecond));return b;}
void closeProgressDialog(){if(!gProgressDialogOpen)return;if(sceMsgDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_NONE){sceMsgDialogClose();sceMsgDialogTerm();}gProgressDialogOpen=false;}
void updateProgressDialog(const psvitaalive::InstallStatus&s){using S=psvitaalive::InstallStatus::State;bool show=s.state==S::Downloading||s.state==S::Installing||s.state==S::Completed||s.state==S::Failed;if(!show){closeProgressDialog();return;}static char msg[512];sceClibSnprintf(msg,sizeof(msg),"%s\n%s\nSpeed: %.2f MiB/s",stateName(s.state),s.fileName.empty()?"Preparing...":s.fileName.c_str(),mib(s.bytesPerSecond));const SceChar8*vm=(const SceChar8*)msg;if(!gProgressDialogOpen){SceMsgDialogProgressBarParam bar={};bar.barType=SCE_MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE;bar.sysMsgParam.sysMsgType=SCE_MSG_DIALOG_SYSMSG_TYPE_WAIT_SMALL;bar.msg=vm;SceMsgDialogParam p={};p.mode=SCE_MSG_DIALOG_MODE_PROGRESS_BAR;p.progBarParam=&bar;p.flag=SCE_MSG_DIALOG_ENV_FLAG_DEFAULT;p.commonParam.magic=SCE_COMMON_DIALOG_MAGIC_NUMBER;if(sceMsgDialogInit(&p)<0)return;gProgressDialogOpen=true;}uint32_t pct=s.total?(uint32_t)std::min<uint64_t>(100,(s.current*100)/s.total):0;sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT,pct);sceMsgDialogProgressBarSetMsg(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT,vm);}
bool asciiToWide(const std::string&text,SceWChar16*out,size_t cap){if(!out||!cap)return false;size_t i=0;for(;i+1<cap&&i<text.size();++i){unsigned char c=(unsigned char)text[i];out[i]=(SceWChar16)(c<128?c:'?');}out[i]=0;return true;}
std::string wideToAscii(const SceWChar16*t){if(!t)return{};std::string r;for(size_t i=0;t[i]&&i<2048;++i)r.push_back(t[i]<=0x7F?(char)t[i]:'?');return r;}
bool promptZipDestination(std::string&dst){static bool loaded=false;if(!loaded){int r=sceSysmoduleLoadModule(SCE_SYSMODULE_IME);if(r<0)return false;loaded=true;}SceWChar16 input[256]={},title[128]={};asciiToWide(dst.empty()?"ux0:data/":dst,input,256);asciiToWide("ZIP extraction path",title,128);SceImeDialogParam p={};p.type=SCE_IME_TYPE_BASIC_LATIN;p.option=SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;p.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;p.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;p.title=title;p.maxTextLength=255;p.initialText=input;p.inputTextBuffer=input;p.supportedLanguages=SCE_IME_LANGUAGE_ENGLISH|SCE_IME_LANGUAGE_SPANISH;p.enterLabel=SCE_IME_ENTER_LABEL_GO;p.commonParam.magic=SCE_COMMON_DIALOG_MAGIC_NUMBER;if(sceImeDialogInit(&p)<0)return false;while(sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING)sceKernelDelayThread(10*1000);SceImeDialogResult r={};sceImeDialogGetResult(&r);bool ok=r.button==SCE_IME_DIALOG_BUTTON_ENTER;if(ok){dst=wideToAscii(input);for(char&c:dst)if(c=='\\')c='/';while(dst.size()>1&&dst.back()=='/')dst.pop_back();}sceImeDialogTerm();return ok&&!dst.empty();}
}

int main(){
 diagnosticLog("============================================================");diagnosticLog("PSVitaAlive session BEGIN");diagnosticLog("TitleID=PSVA00001");
 psvitaalive::StorageManager storage;storage.initProjectDirs();
 constexpr const char* HOME_URL="https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog.json";const std::string homePath=std::string(psvitaalive::StorageManager::CACHE_DIR)+"/catalog.json";storage.createDirectories(psvitaalive::StorageManager::CACHE_DIR);
 std::vector<psvitaalive::ui::CatalogItem> homeItems;psvitaalive::HttpClient homeHttp;if(homeHttp.init()==psvitaalive::HttpResult::Ok){psvitaalive::HttpResult r=homeHttp.downloadToFile(HOME_URL,homePath);if(r==psvitaalive::HttpResult::Ok)psvitaalive::CatalogParser::parseFile(homePath,homeItems);homeHttp.shutdown();}
 psvitaalive::InstallController installer;psvitaalive::CatalogManager catalogs;psvitaalive::ui::ImageCache images;catalogs.init();images.init();
 psvitaalive::ui::FullCatalogScreen screen;screen.setCatalogItems(homeItems);screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);screen.setImageCache(&images);
 screen.setCatalogChangeCallback([&](psvitaalive::ui::CatalogType next){if(next==psvitaalive::ui::CatalogType::Homebrew){screen.setCatalogItems(homeItems);screen.setActiveCatalog(next);return true;}screen.setActiveCatalog(next);return catalogs.request(next);});
 screen.setInstallCallbacks([&installer](const psvitaalive::ui::CatalogItem&item){diagnosticLog("INSTALL REQUEST name="+item.name+" title_id="+item.titleId+" url="+item.downloadUrl);if(item.downloadUrl.empty()||item.downloadFileName.empty())return false;std::string zipDestination;if(item.downloadFileName.size()>=4&&item.downloadFileName.substr(item.downloadFileName.size()-4)==".zip"){zipDestination="ux0:data/";if(!promptZipDestination(zipDestination))return false;}return installer.requestInstall(item.downloadUrl,item.downloadFileName,zipDestination);},[&installer](){return installStatusText(installer.status());});
 if(!screen.init()){installer.shutdown();catalogs.shutdown();images.shutdown();sceKernelExitProcess(1);return 1;}
 psvitaalive::InstallStatus previous;previous.state=psvitaalive::InstallStatus::State::Idle;
 while(screen.updateAndDraw()){
   psvitaalive::CatalogManager::Status cs=catalogs.status();
   if(cs.state==psvitaalive::CatalogManager::State::Loading)screen.setCatalogLoading(true,cs.label,cs.current,cs.total,cs.message);
   else if(cs.state==psvitaalive::CatalogManager::State::Failed)screen.setCatalogError(cs.error.empty()?"Unable to load catalog":cs.error);
   std::vector<psvitaalive::ui::CatalogItem> ready;psvitaalive::ui::CatalogType readyCatalog;
   if(catalogs.takeReady(ready,readyCatalog)){screen.setCatalogItems(std::move(ready));screen.setActiveCatalog(readyCatalog);screen.setCatalogLoading(false,psvitaalive::ui::catalogName(readyCatalog),1,1,"Ready");}
   const psvitaalive::InstallStatus cur=installer.status();if(cur.state!=previous.state||cur.current!=previous.current||cur.total!=previous.total||cur.stage!=previous.stage||cur.message!=previous.message){previous=cur;}updateProgressDialog(cur);
 }
 closeProgressDialog();screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();diagnosticLog("PSVitaAlive session END");sceKernelExitProcess(0);return 0;
}
