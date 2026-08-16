/* PSVitaAlive - native client entry point. */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/ime_dialog.h>
#include <psp2/message_dialog.h>
#include <psp2/ctrl.h>
#include <vita2d.h>
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
// In-app confirmation panel (matches PSVitaAlive look) before the system IME.
// Vita has no custom folder browser; IME is still required for the path string.
bool confirmZipExtractDialog() {
    constexpr unsigned BG = RGBA8(0, 0, 0, 255);
    constexpr unsigned SURFACE = RGBA8(0x37, 0x37, 0x37, 255);
    constexpr unsigned SURFACE2 = RGBA8(0x2A, 0x2A, 0x2A, 255);
    constexpr unsigned ACCENT = RGBA8(0x3B, 0xFF, 0, 255);
    constexpr unsigned TEXT = RGBA8(0xAA, 0xAA, 0xAA, 255);
    constexpr unsigned WHITE = RGBA8(255, 255, 255, 255);
    constexpr unsigned DIM = RGBA8(0x6E, 0x6E, 0x6E, 255);
    constexpr int SW = 960, SH = 544;

    vita2d_pgf* font = vita2d_load_default_pgf();
    if (!font) return false;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    uint32_t prev = 0;
    // Swallow the button that triggered install so it does not auto-confirm.
    {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        prev = pad.buttons;
    }

    bool decided = false;
    bool accepted = false;
    while (!decided) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const uint32_t pressed = pad.buttons & ~prev;
        prev = pad.buttons;

        if (pressed & SCE_CTRL_CROSS) {
            accepted = true;
            decided = true;
        } else if (pressed & SCE_CTRL_CIRCLE) {
            accepted = false;
            decided = true;
        }

        const int boxW = 560, boxH = 220;
        const int boxX = (SW - boxW) / 2, boxY = (SH - boxH) / 2;

        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_draw_rectangle(0, 0, SW, SH, RGBA8(0, 0, 0, 200));
        vita2d_draw_rectangle(boxX, boxY, boxW, boxH, SURFACE2);
        vita2d_draw_rectangle(boxX, boxY, boxW, 3, ACCENT);
        vita2d_draw_rectangle(boxX, boxY + boxH - 1, boxW, 1, ACCENT);
        vita2d_draw_rectangle(boxX, boxY, 1, boxH, ACCENT);
        vita2d_draw_rectangle(boxX + boxW - 1, boxY, 1, boxH, ACCENT);

        vita2d_pgf_draw_text(font, boxX + 24, boxY + 36, ACCENT, 1.05f, "Archivo ZIP detectado");
        vita2d_pgf_draw_text(font, boxX + 24, boxY + 72, WHITE, 0.72f,
            "Listo. A continuacion se pedira la ruta de la");
        vita2d_pgf_draw_text(font, boxX + 24, boxY + 94, WHITE, 0.72f,
            "carpeta donde se extraera el contenido.");
        vita2d_pgf_draw_text(font, boxX + 24, boxY + 116, TEXT, 0.64f,
            "Ejemplo: ux0:data/MiApp  o  ux0:data/");

        const int btnY = boxY + boxH - 56;
        const int btnW = 150, btnH = 34;
        const int okX = boxX + boxW - btnW - 24;
        const int cancelX = okX - btnW - 12;
        vita2d_draw_rectangle(cancelX, btnY, btnW, btnH, SURFACE);
        vita2d_draw_rectangle(okX, btnY, btnW, btnH, SURFACE);
        vita2d_draw_rectangle(okX, btnY, btnW, 2, ACCENT);
        vita2d_pgf_draw_text(font, cancelX + 28, btnY + 23, TEXT, 0.68f, "O Cancelar");
        vita2d_pgf_draw_text(font, okX + 36, btnY + 23, ACCENT, 0.68f, "X Continuar");
        vita2d_pgf_draw_text(font, boxX + 24, boxY + boxH - 18, DIM, 0.55f,
            "Luego escribe la ruta y confirma en el teclado");

        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(16 * 1000);
    }

    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    return accepted;
}

bool promptZipDestination(std::string& dst) {
    if (!confirmZipExtractDialog()) {
        psvitaalive::diagnostics::log("[UI] ZIP destination cancelled at confirm dialog");
        return false;
    }

    static bool loaded = false;
    if (!loaded) {
        const int r = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        if (r < 0) return false;
        loaded = true;
    }

    SceWChar16 input[256] = {}, title[128] = {};
    asciiToWide(dst.empty() ? "ux0:data/" : dst, input, 256);
    asciiToWide("Ruta de extraccion ZIP", title, 128);

    SceImeDialogParam p = {};
    p.type = SCE_IME_TYPE_BASIC_LATIN;
    p.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
    p.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    p.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    p.title = title;
    p.maxTextLength = 255;
    p.initialText = input;
    p.inputTextBuffer = input;
    p.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH | SCE_IME_LANGUAGE_SPANISH;
    p.enterLabel = SCE_IME_ENTER_LABEL_GO;
    p.commonParam.magic = SCE_COMMON_DIALOG_MAGIC_NUMBER;

    if (sceImeDialogInit(&p) < 0) return false;
    while (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        sceKernelDelayThread(10 * 1000);
    }
    SceImeDialogResult r = {};
    sceImeDialogGetResult(&r);
    const bool ok = r.button == SCE_IME_DIALOG_BUTTON_ENTER;
    if (ok) {
        dst = wideToAscii(input);
        for (char& c : dst) if (c == '\\') c = '/';
        while (dst.size() > 1 && dst.back() == '/') dst.pop_back();
    }
    sceImeDialogTerm();
    return ok && !dst.empty();
}

std::string formatEta(uint64_t seconds){if(seconds==0)return "--";uint64_t h=seconds/3600,m=(seconds%3600)/60,sec=seconds%60;char o[64];if(h)sceClibSnprintf(o,sizeof(o),"%llu:%02llu:%02llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)sec);else sceClibSnprintf(o,sizeof(o),"%02llu:%02llu",(unsigned long long)m,(unsigned long long)sec);return o;}
bool promptDownloadAllImages(size_t totalImages){vita2d_wait_rendering_done();vita2d_pgf*font=vita2d_load_default_pgf();if(!font)return false;bool yes=false,done=false;int selected=0;uint32_t prev=0;while(!done){SceCtrlData pad={};sceCtrlPeekBufferPositive(0,&pad,1);uint32_t pressed=pad.buttons&~prev;prev=pad.buttons;if(pressed&SCE_CTRL_LEFT)selected=0;if(pressed&SCE_CTRL_RIGHT)selected=1;if(pressed&SCE_CTRL_CROSS){yes=selected==0;done=true;}if(pressed&SCE_CTRL_CIRCLE){yes=false;done=true;}vita2d_start_drawing();vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,96));const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);const int w=620,h=360,x=(960-w)/2,y=(544-h)/2;vita2d_draw_rectangle(0,0,960,544,RGBA8(0x18,0x18,0x18,255));vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,70));vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);vita2d_draw_rectangle(x,y+2,2,h-4,ACCENT);vita2d_draw_rectangle(x+w-2,y+2,2,h-4,BORDER);vita2d_draw_rectangle(x,y+h-2,w,2,BORDER);vita2d_pgf_draw_text(font,x+28,y+36,ACCENT,.68f,"PSVitaAlive");vita2d_pgf_draw_text(font,x+28,y+78,WHITE,1.02f,"Download catalog images?");vita2d_pgf_draw_text(font,x+28,y+108,TEXT,.58f,"Images are downloaded once and kept in the local cache.");vita2d_pgf_draw_text(font,x+28,y+130,DIM,.54f,"This can take a very long time and use network data.");char count[96];sceClibSnprintf(count,sizeof(count),"Pending images: %u",(unsigned)totalImages);vita2d_pgf_draw_text(font,x+28,y+160,ACCENT,.68f,count);vita2d_pgf_draw_text(font,x+28,y+190,TEXT,.54f,"Already cached images are excluded from this count.");vita2d_pgf_draw_text(font,x+28,y+212,TEXT,.54f,"Total size is calculated while downloading.");vita2d_pgf_draw_text(font,x+28,y+234,TEXT,.54f,"Speed, ETA and overall progress appear in the next panel.");int by=y+266,bw=220,bh=38;vita2d_draw_rectangle(x+58,by,bw,bh,selected==0?ACCENT:SURFACE);vita2d_draw_rectangle(x+342,by,bw,bh,selected==1?ACCENT:SURFACE);vita2d_pgf_draw_text(font,x+125,by+25,selected==0?BLACK:WHITE,.62f,"DOWNLOAD ALL");vita2d_pgf_draw_text(font,x+425,by+25,selected==1?BLACK:WHITE,.62f,"LATER");vita2d_pgf_draw_text(font,x+28,y+h-14,DIM,.52f,"Left/Right: Select    Cross: Confirm    Circle: Later");vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);}vita2d_wait_rendering_done();vita2d_free_pgf(font);return yes;}
std::string formatBytes(uint64_t b){char o[64];double v=(double)b;if(b>=1024ULL*1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f GB",v/(1024.0*1024.0*1024.0));else if(b>=1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f MB",v/(1024.0*1024.0));else if(b>=1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f KB",v/1024.0);else sceClibSnprintf(o,sizeof(o),"%llu B",(unsigned long long)b);return o;}
std::string twoLineFileName(const std::string&name){if(name.size()<=42)return name;if(name.size()<=84)return name.substr(0,42)+"\n"+name.substr(42);return name.substr(0,42)+"\n..."+name.substr(name.size()-38);}
std::string fileNameFromUrl(const std::string&url,const std::string&id){std::string clean=url;const size_t q=clean.find('?');if(q!=std::string::npos)clean.erase(q);const size_t f=clean.find('#');if(f!=std::string::npos)clean.erase(f);const size_t slash=clean.find_last_of('/');std::string name=slash==std::string::npos?clean:clean.substr(slash+1);return name.empty()?id+".bin":name;}
bool isZipName(const std::string&name){return name.size()>=4&&name.substr(name.size()-4)==".zip";}
struct StartupImageJob{std::string url;std::string path;std::string namespaceName;std::string fileName;};
void addStartupImage(std::vector<StartupImageJob>&jobs,std::unordered_set<std::string>&seen,psvitaalive::ui::ImageCache&images,const std::string&url,const std::string&namespaceName,bool queueNow){if(url.empty()||images.isCached(url,namespaceName))return;const std::string key=namespaceName+"\n"+url;if(!seen.insert(key).second)return;std::string path;if(queueNow)path=images.request(url,namespaceName);jobs.push_back({url,path,namespaceName,fileNameFromUrl(url,namespaceName+"_image")});}
void collectCatalogImages(std::vector<StartupImageJob>&jobs,std::unordered_set<std::string>&seen,psvitaalive::ui::ImageCache&images,const std::vector<psvitaalive::ui::CatalogItem>&items,bool queueNow){for(const auto&item:items){addStartupImage(jobs,seen,images,item.icon,"app",queueNow);addStartupImage(jobs,seen,images,item.cover,"app",queueNow);const size_t count=std::min<size_t>(5,item.screenshots.size());for(size_t i=0;i<count;++i)addStartupImage(jobs,seen,images,item.screenshots[i],"shot",queueNow);}}
void queueStartupImages(std::vector<StartupImageJob>&jobs,psvitaalive::ui::ImageCache&images){for(auto&job:jobs){if(job.path.empty())job.path=images.request(job.url,job.namespaceName);}}
void imageWarmupProgress(const std::vector<StartupImageJob>&jobs,psvitaalive::ui::ImageCache&images,uint64_t&completed,std::string&currentFile,bool&failedCurrent){completed=0;currentFile.clear();failedCurrent=false;for(const auto&job:jobs){const bool ready=images.isReady(job.path),failed=images.isFailed(job.path);if(ready||failed){++completed;continue;}if(currentFile.empty())currentFile=job.fileName;}}
bool runImageWarmup(std::vector<StartupImageJob>&jobs,psvitaalive::ui::ImageCache&images){
    if(jobs.empty())return true;
    images.cancelAll();
    while(images.progress().active)sceKernelDelayThread(10*1000);
    images.resetProgress();vita2d_pgf*prepFont=vita2d_load_default_pgf();if(prepFont){vita2d_start_drawing();vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,96));const int pw=620,ph=280,px=(960-pw)/2,py=(544-ph)/2;vita2d_draw_rectangle(px,py,pw,ph,RGBA8(0x20,0x20,0x20,255));vita2d_draw_rectangle(px,py,pw,2,RGBA8(0x3B,0xFF,0,255));vita2d_pgf_draw_text(prepFont,px+28,py+44,RGBA8(0x3B,0xFF,0,255),.68f,"PSVitaAlive");vita2d_pgf_draw_text(prepFont,px+28,py+86,RGBA8(255,255,255,255),1.0f,"Preparing image downloads");vita2d_pgf_draw_text(prepFont,px+28,py+122,RGBA8(0xAA,0xAA,0xAA,255),.58f,"Building the download queue...");vita2d_pgf_draw_text(prepFont,px+28,py+160,RGBA8(0x3B,0xFF,0,255),.62f,"Please wait");vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);vita2d_free_pgf(prepFont);}
    queueStartupImages(jobs,images);
    vita2d_pgf* font=vita2d_load_default_pgf();
    if(!font){images.cancelAll();return false;}
    const uint64_t started=sceKernelGetSystemTimeWide();uint64_t lastPoll=0;bool cancelled=false;
    while(true){
        const uint64_t now=sceKernelGetSystemTimeWide();
        uint64_t completed=0;std::string currentFile;bool failedCurrent=false;imageWarmupProgress(jobs,images,completed,currentFile,failedCurrent);
        const auto p=images.progress();
        if(completed>=(uint64_t)jobs.size()&&!p.active)break;
        SceCtrlData pad={};sceCtrlPeekBufferPositive(0,&pad,1);if(pad.buttons&SCE_CTRL_CIRCLE&&!cancelled){images.cancelAll();cancelled=true;}if(cancelled){vita2d_start_drawing();vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,96));vita2d_draw_rectangle(170,142,620,260,RGBA8(0x20,0x20,0x20,255));vita2d_draw_rectangle(170,142,620,2,RGBA8(0x3B,0xFF,0,255));vita2d_pgf_draw_text(font,198,184,RGBA8(0x3B,0xFF,0,255),.68f,"PSVitaAlive");vita2d_pgf_draw_text(font,198,226,RGBA8(255,255,255,255),1.0f,"Cancelling image downloads");vita2d_pgf_draw_text(font,198,262,RGBA8(0xAA,0xAA,0xAA,255),.58f,"Stopping transfer and removing incomplete file...");vita2d_end_drawing();vita2d_swap_buffers();if(!images.progress().active)break;sceKernelDelayThread(16*1000);continue;}
        if(now<lastPoll){sceKernelDelayThread(16*1000);continue;}lastPoll=now+100000;
        const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);
        const int w=620,h=320,x=(960-w)/2,y=(544-h)/2;
        vita2d_start_drawing();vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,96));vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);vita2d_draw_rectangle(x,y+2,2,h-2,ACCENT);
        vita2d_pgf_draw_text(font,x+28,y+32,ACCENT,.68f,"PSVitaAlive");vita2d_pgf_draw_text(font,x+28,y+68,WHITE,1.02f,"Downloading Images");
        std::string fn=twoLineFileName(p.fileName.empty()?currentFile:p.fileName);const size_t nl=fn.find('\n');if(nl==std::string::npos)vita2d_pgf_draw_text(font,x+28,y+98,TEXT,.62f,fn.c_str());else{vita2d_pgf_draw_text(font,x+28,y+98,TEXT,.62f,fn.substr(0,nl).c_str());vita2d_pgf_draw_text(font,x+28,y+120,TEXT,.62f,fn.substr(nl+1).c_str());}
        const uint64_t current=p.downloaded,total=p.total;const uint64_t filePct=total?std::min<uint64_t>(100,(current*100)/total):0;const uint64_t overallPct=jobs.empty()?100:std::min<uint64_t>(100,((completed*10000)+(filePct*100))/jobs.size()/100);int bx=x+28,by=y+140,bw=w-56,bh=12;vita2d_draw_rectangle(bx,by,bw,bh,BORDER);vita2d_draw_rectangle(bx,by,bw*(int)overallPct/100,bh,ACCENT);char stats[220]={};sceClibSnprintf(stats,sizeof(stats),"%llu%%  Files: %llu / %u  •  %s/s",(unsigned long long)overallPct,(unsigned long long)completed,(unsigned)jobs.size(),formatBytes(p.speed).c_str());vita2d_pgf_draw_text(font,x+28,y+168,TEXT,.58f,stats);uint64_t eta=0;if(p.speed>0){uint64_t avg=completed?p.completedBytes/completed:(p.total?p.total:0);uint64_t futureFiles=jobs.size()>completed+(p.active?1:0)?jobs.size()-completed-(p.active?1:0):0;uint64_t remaining=(p.total>p.downloaded?p.total-p.downloaded:0)+avg*futureFiles;eta=remaining/p.speed;}char etaText[96];sceClibSnprintf(etaText,sizeof(etaText),"ETA: %s",formatEta(eta).c_str());vita2d_pgf_draw_text(font,x+28,y+194,ACCENT,.62f,etaText);
        char known[220]={};sceClibSnprintf(known,sizeof(known),"Known size: %s",formatBytes(p.knownTotalBytes).c_str());vita2d_pgf_draw_text(font,x+28,y+218,TEXT,.56f,known);
        vita2d_draw_rectangle(x+w-190,y+h-52,162,38,cancelled?ACCENT:SURFACE);vita2d_draw_rectangle(x+w-190,y+h-52,162,1,ACCENT);vita2d_pgf_draw_text(font,x+w-176,y+h-27,cancelled?BLACK:WHITE,.54f,"CIRCLE  CANCEL DOWNLOAD");
        vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);
        if(cancelled&&!images.progress().active)break;
    }
    vita2d_wait_rendering_done();vita2d_free_pgf(font);return !cancelled;
}
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
    screen.setCatalogChangeCallback([&](psvitaalive::ui::CatalogType next){psvitaalive::diagnostics::log(std::string("[UI] catalog requested: ")+psvitaalive::ui::catalogName(next));images.cancelQueuedRequests();return catalogs.request(next);});
    screen.setSearchCallback([&](const std::string&current){std::string result=current;if(promptText(current,"Search catalog",result))return result;return current;});
    screen.setInstallCancelCallback([&installer](){ installer.cancel(); });
    screen.setInstallAcknowledgeCallback([&installer](){ installer.acknowledgeResult(); });
    screen.setInstallCallbacks([&installer](const psvitaalive::ui::CatalogItem&item){psvitaalive::diagnostics::log("[UI] INSTALL REQUEST name="+item.name+" title_id="+item.titleId+" url="+item.downloadUrl);
    if(item.downloadUrl.empty()||item.downloadFileName.empty())return false;std::string zipDestination;if(isZipName(item.downloadFileName)){zipDestination="ux0:data/";if(!promptZipDestination(zipDestination)){psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");return false;}}return installer.requestInstall(item.downloadUrl,item.downloadFileName,zipDestination);},[&installer](){return installStatusText(installer.status());});
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
    int preloadIndex=0;bool startupCatalogs=true;bool homebrewReady=false;bool startupImageChoicePending=false;
    std::vector<std::vector<psvitaalive::ui::CatalogItem>> startupCatalogItems((size_t)catalogCount);
    std::vector<StartupImageJob> startupImagesJobs;std::unordered_set<std::string> startupImageSeen;

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
                startupCatalogs=false;startupImageChoicePending=true;startupImagesJobs.clear();startupImageSeen.clear();for(const auto&items:startupCatalogItems)collectCatalogImages(startupImagesJobs,startupImageSeen,images,items,false);screen.setCatalogLoading(false,"",0,(uint64_t)startupImagesJobs.size(),"Catalogs ready");psvitaalive::diagnostics::log("[Startup] all catalogs processed; waiting for image warmup choice");
            }
        }

        std::vector<psvitaalive::ui::CatalogItem> ready;psvitaalive::ui::CatalogType readyCatalog;
        if(catalogs.takeReady(ready,readyCatalog)){
            psvitaalive::diagnostics::log(std::string("[System] catalog ready: ")+psvitaalive::ui::catalogName(readyCatalog));startupCatalogItems[(int)readyCatalog]=ready;
            if(startupCatalogs){
                if(readyCatalog==psvitaalive::ui::CatalogType::Homebrew){screen.setCatalogItems(ready);screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);homebrewReady=true;}
                ++preloadIndex;
                if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Checking next catalog cache...");catalogs.request(next);}
                else{startupCatalogs=false;startupImageChoicePending=true;startupImagesJobs.clear();startupImageSeen.clear();for(const auto&items:startupCatalogItems)collectCatalogImages(startupImagesJobs,startupImageSeen,images,items,false);screen.setCatalogLoading(false,"",0,(uint64_t)startupImagesJobs.size(),"Catalogs ready");psvitaalive::diagnostics::log("[Startup] all catalogs ready; waiting for image warmup choice");}
            }else{
                // Only clear loading if nothing else is still in flight (e.g. user
                // already requested another tab before this result was published).
                screen.setCatalogItems(std::move(ready));
                screen.setActiveCatalog(readyCatalog);
                if(catalogs.isBusy()){
                    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(readyCatalog),0,0,"Loading next catalog...");
                }else{
                    screen.setCatalogLoading(false,psvitaalive::ui::catalogName(readyCatalog),1,1,"Ready");
                }
            }
        }

        if(startupImageChoicePending){
            startupImageChoicePending=false;
            if(startupImagesJobs.empty()){
                screen.setCatalogLoading(false,"",0,0,"All catalog images are already cached");psvitaalive::diagnostics::log("[Startup] no pending catalog images");
                if(homebrewReady){screen.setCatalogItems(startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew]);screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);}
            }else if(promptDownloadAllImages(startupImagesJobs.size())){
                const bool completed=runImageWarmup(startupImagesJobs,images);screen.setCatalogLoading(false,"",completed?startupImagesJobs.size():0,(uint64_t)startupImagesJobs.size(),completed?"Image cache ready":"Image download cancelled");psvitaalive::diagnostics::log(std::string("[Startup] full image warmup finished result=")+(completed?"COMPLETE":"CANCELLED"));
                if(homebrewReady){screen.setCatalogItems(startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew]);screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);}
            }else{
                screen.setCatalogLoading(false,"",0,0,"Images will download while browsing");startupImagesJobs.clear();startupImageSeen.clear();psvitaalive::diagnostics::log("[Startup] user selected on-demand image loading");
                if(homebrewReady){screen.setCatalogItems(startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew]);screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);}
            }
        }

        
        // Keep L/R locked for the whole CatalogManager busy window (not only startup).
        if(!startupCatalogs){
            const auto csBusy=catalogs.status();
            if(catalogs.isBusy()||csBusy.state==psvitaalive::CatalogManager::State::Loading){
                screen.setCatalogLoading(true,
                    csBusy.label.empty()?psvitaalive::ui::catalogName(csBusy.catalog):csBusy.label,
                    csBusy.current,csBusy.total,
                    csBusy.message.empty()?"Loading catalog...":csBusy.message);
            }
        }

const psvitaalive::InstallStatus cur=installer.status();using InstallState=psvitaalive::InstallStatus::State;const bool active=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||cur.state==InstallState::Completed||cur.state==InstallState::Failed;int outcome=0;if(cur.state==InstallState::Completed)outcome=1;else if(cur.state==InstallState::Failed)outcome=2;screen.setInstallProgress(active,cur.current,cur.total,cur.bytesPerSecond,cur.stage,cur.fileName,cur.message,outcome,cur.liveAreaOk,cur.installPath,cur.titleId);
    }

    screen.setInstallProgress(false,0,0,0,"","","",0,false,"","");screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();psvitaalive::diagnostics::log("PSVitaAlive session END");psvitaalive::diagnostics::shutdown();sceKernelExitProcess(0);return 0;
}
