#include "ui/full_catalog_screen.hpp"
#include "installer/app_settings.hpp"
#include "installer/plugin_detector.hpp"
#include "update/update_checker.hpp"

#ifndef PSVITAALIVE_VERSION
#define PSVITAALIVE_VERSION "01.00"
#endif
#include "diagnostic_logger.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/devctl.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <set>
#include <unordered_set>
#include <utility>
namespace psvitaalive::ui { namespace {


std::string currentTimeLabel() {
    SceDateTime dt{};
    if (sceRtcGetCurrentClockLocalTime(&dt) < 0) return "--:--";
    char buf[16];
    sceClibSnprintf(buf, sizeof(buf), "%02d:%02d", (int)dt.hour, (int)dt.minute);
    return buf;
}

std::string formatBytesShort(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%.2fG", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ULL * 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%.1fM", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%lluK", (unsigned long long)(bytes / 1024ULL));
    else
        sceClibSnprintf(buf, sizeof(buf), "%lluB", (unsigned long long)bytes);
    return buf;
}

struct Ux0SpaceInfo {
    bool ok = false;
    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
};

Ux0SpaceInfo queryUx0Space() {
    static Ux0SpaceInfo cached{};
    static uint64_t lastMs = 0;
    const uint64_t nowMs = sceKernelGetProcessTimeWide() / 1000ULL;
    if (lastMs != 0 && nowMs >= lastMs && (nowMs - lastMs) < 3000ULL) return cached;

    struct {
        uint64_t max_size;
        uint64_t free_size;
        uint32_t cluster_size;
        void* unk;
    } info{};
    const int ret = sceIoDevctl("ux0:", 0x3001, nullptr, 0, &info, sizeof(info));
    lastMs = nowMs;
    cached = {};
    if (ret < 0) return cached;
    cached.ok = true;
    cached.freeBytes = info.free_size;
    cached.totalBytes = info.max_size > 0 ? info.max_size : info.free_size;
    return cached;
}

std::string ux0FreeSpaceLabel() {
    const Ux0SpaceInfo s = queryUx0Space();
    if (!s.ok) return "ux0 --";
    char buf[64];
    sceClibSnprintf(buf, sizeof(buf), "%s/%s",
                    formatBytesShort(s.freeBytes).c_str(),
                    formatBytesShort(s.totalBytes).c_str());
    return buf;
}


/** Best human-readable size for a catalog card (item.size or first download link size). */
std::string itemDisplaySize(const CatalogItem& it) {
    if (!it.size.empty()) return it.size;
    for (const auto& link : it.linkDetails) {
        if (link.recommended && !link.size.empty()) return link.size;
    }
    for (const auto& link : it.linkDetails) {
        if (!link.size.empty()) return link.size;
    }
    return std::string();
}

/** Normalize link.type for comparisons (lowercase, collapse spaces). */
std::string normalizeLinkType(std::string t) {
    for (char& c : t) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '_' || c == '-') c = ' ';
    }
    std::string out;
    out.reserve(t.size());
    bool space = false;
    for (char c : t) {
        if (c == ' ') {
            if (!space && !out.empty()) {
                out.push_back(' ');
                space = true;
            }
        } else {
            out.push_back(c);
            space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool itemHasLinkType(const CatalogItem& it, const char* needle) {
    const std::string want = needle;
    for (const auto& link : it.linkDetails) {
        const std::string t = normalizeLinkType(link.type);
        if (t == want) return true;
        if (want == "data files" && (t == "data file" || t == "datafiles" || t == "data")) return true;
        if (want == "game files" && (t == "game file" || t == "gamefiles")) return true;
    }
    return false;
}



// LiveArea brand palette (PSVitaAlive Store): neon lime on near-black honeycomb look
constexpr unsigned BG=RGBA8(0x0A,0x0A,0x0A,255);
constexpr unsigned SURFACE=RGBA8(0x1A,0x1A,0x1A,255);
constexpr unsigned SURFACE2=RGBA8(0x12,0x12,0x14,255);
constexpr unsigned PANEL=RGBA8(0x0E,0x0E,0x10,255);
constexpr unsigned BORDER=RGBA8(0x2A,0x2A,0x2E,255);
constexpr unsigned TEXT=RGBA8(0xAA,0xAA,0xAA,255);
constexpr unsigned DIM=RGBA8(0x66,0x66,0x6A,255);
constexpr unsigned ACCENT=RGBA8(0x3B,0xFF,0x00,255);       // #3BFF00
constexpr unsigned ACCENT_DIM=RGBA8(0x3B,0xFF,0x00,90);
constexpr unsigned ACCENT_SOFT=RGBA8(0x3B,0xFF,0x00,40);
constexpr unsigned WHITE=RGBA8(0xF0,0xF0,0xF0,255);
constexpr unsigned SILVER=RGBA8(0xC8,0xC8,0xCC,255);

/** Thin neon frame used across cards, panels, and modals (LiveArea brand). */
void drawNeonFrame(int x, int y, int w, int h, unsigned alphaOuter = 70, unsigned alphaInner = 180) {
    const unsigned outer = RGBA8(0x3B, 0xFF, 0x00, alphaOuter);
    const unsigned inner = RGBA8(0x3B, 0xFF, 0x00, alphaInner);
    vita2d_draw_rectangle(x - 1, y - 1, w + 2, 1, outer);
    vita2d_draw_rectangle(x - 1, y + h, w + 2, 1, outer);
    vita2d_draw_rectangle(x - 1, y - 1, 1, h + 2, outer);
    vita2d_draw_rectangle(x + w, y - 1, 1, h + 2, outer);
    vita2d_draw_rectangle(x, y, w, 1, inner);
    vita2d_draw_rectangle(x, y + h - 1, w, 1, inner);
    vita2d_draw_rectangle(x, y, 1, h, inner);
    vita2d_draw_rectangle(x + w - 1, y, 1, h, inner);
}

constexpr int FULL_CARD_H=120,SPLIT_CARD_H=82,DETAIL_HEADER_H=92,LINE_H=18,TRANSITION_MS=340,LINK_ROW_H=38,LINK_GAP=6,SCREENSHOT_ROW_H=250;
constexpr size_t MAX_APP_TEXTURES=18,MAX_SCREENSHOT_TEXTURES=6;
constexpr int CATALOG_SWITCH_COOLDOWN_FRAMES=50; // ~0.83s at 60fps
constexpr uint64_t CATALOG_SWITCH_MIN_MS=900; // hard debounce against L/R spam
constexpr size_t MAX_DEFERRED_FREES_PER_FRAME=8;constexpr uint64_t DIRECTION_REPEAT_DELAY_US=320000,DIRECTION_REPEAT_INTERVAL_US=420000;
const char* extOf(const std::string&p){const size_t d=p.find_last_of('.');return d==std::string::npos?"":p.c_str()+d;}std::string formatBytes(uint64_t b){char o[64];double v=(double)b;if(b>=1024ULL*1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f GB",v/(1024.0*1024.0*1024.0));else if(b>=1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f MB",v/(1024.0*1024.0));else if(b>=1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f KB",v/1024.0);else sceClibSnprintf(o,sizeof(o),"%llu B",(unsigned long long)b);return o;}std::string lowerAscii(std::string s){for(char&c:s)c=(char)std::tolower((unsigned char)c);return s;}std::string ellipsize(const std::string&s,size_t n){if(s.size()<=n)return s;if(n<=3)return s.substr(0,n);return s.substr(0,n-3)+"...";}bool actionableLink(const CatalogLink&l){std::string t=lowerAscii(l.type);if(t=="download"||t=="downloads"||t=="mirror"||t=="dlc")return true;std::string u=lowerAscii(l.url);return u.find(".vpk")!=std::string::npos||u.find(".pkg")!=std::string::npos||u.find(".zip")!=std::string::npos||u.find(".pbp")!=std::string::npos||u.find(".iso")!=std::string::npos||u.find(".cso")!=std::string::npos;}
bool isDownloadLink(const CatalogLink&l){return lowerAscii(l.type)=="download"||lowerAscii(l.type)=="downloads"||lowerAscii(l.type)=="mirror"||lowerAscii(l.type)=="dlc";}
std::vector<int> downloadLinkIndices(const CatalogItem&it){std::vector<int> out;for(size_t i=0;i<it.linkDetails.size();++i)if(isDownloadLink(it.linkDetails[i]))out.push_back((int)i);return out;}
std::string formatLinkSizeLabel(const CatalogLink&l,const CatalogItem&it){if(!l.size.empty())return l.size;if(!it.size.empty())return it.size;return {};}
}
std::string formatEta(uint64_t seconds){if(seconds==0)return "--";uint64_t h=seconds/3600,m=(seconds%3600)/60,sec=seconds%60;char o[64];if(h)sceClibSnprintf(o,sizeof(o),"%llu:%02llu:%02llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)sec);else sceClibSnprintf(o,sizeof(o),"%02llu:%02llu",(unsigned long long)m,(unsigned long long)sec);return o;}
FullCatalogScreen::FullCatalogScreen()=default;FullCatalogScreen::~FullCatalogScreen(){shutdown();}
void FullCatalogScreen::setInstallCallbacks(InstallRequestFn r,InstallStatusFn s){installRequest_=std::move(r);installStatusText_=std::move(s);}void FullCatalogScreen::setInstallCancelCallback(InstallCancelFn c){installCancel_=std::move(c);}void FullCatalogScreen::setInstallAcknowledgeCallback(InstallAcknowledgeFn c){installAcknowledge_=std::move(c);}void FullCatalogScreen::setCatalogChangeCallback(CatalogChangeFn c){catalogChange_=std::move(c);}void FullCatalogScreen::setSearchCallback(SearchRequestFn c){searchRequest_=std::move(c);}void FullCatalogScreen::setLinkActionCallback(LinkActionFn c){linkAction_=std::move(c);}void FullCatalogScreen::setImageCache(ImageCache*c){imageCache_=c;}
void FullCatalogScreen::setCatalogItems(std::vector<CatalogItem>items){
    // Textures for the previous tab may still be draining; only schedule free if needed.
    releaseTextures();
    installStatusCache_.clear();
    allItems_=std::move(items);
    sortItemsByDate(allItems_);
    items_.clear();
    applySearch(searchQuery_);
    visualCatalogScroll_=0.f;
    visualDetailScroll_=0.f;
    visualFocusIndex_=0.f;
    detailCrossfade_=1.f;
    detailCrossfadeFrom_=-1;
    contentFade_=0.4f;
    catalogLoading_=false;
    catalogError_.clear();
    // Give the catalog/UI time to settle before any ux0:app probes begin.
    installStatusWarmupUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 1500ULL;
    // Instant memory-cache hits clear loading quickly; keep debounce so spam L/R cannot thrash.
    if(catalogSwitchCooldownFrames_<CATALOG_SWITCH_COOLDOWN_FRAMES/2)
        catalogSwitchCooldownFrames_=CATALOG_SWITCH_COOLDOWN_FRAMES/2;
    lastCatalogSwitchMs_=sceKernelGetProcessTimeWide()/1000ULL;
}void FullCatalogScreen::setActiveCatalog(CatalogType c){
    // Textures usually already released in changeCatalog; release again only if any remain.
    if(!textures_.empty() || !deferredFreeTextures_.empty()){
        releaseTextures();
    }
    state_.catalog=c;
    installStatusWarmupUntilMs_ = sceKernelGetProcessTimeWide() / 1000ULL + 1500ULL;
    searchQuery_.clear();
    items_.clear(); // browse via allItems_ (catalogView) — no duplicate
    state_.focusIndex=0;
    state_.catalogScrollRow=0;
    state_.detailScroll=0;
    visualCatalogScroll_=0.f;
    visualDetailScroll_=0.f;
    contentFade_=0.45f;
    detailScrollBeforeLinkMode_=0;
    state_.linkFocus=-1;
    state_.linkNavigation=false;
}void FullCatalogScreen::setCatalogLoading(bool l,const std::string&lab,uint64_t cur,uint64_t tot,const std::string&msg){
    if (l) {
        catalogLoading_ = true;
        catalogSplashAlpha_ = 1.f;
        catalogError_.clear();
    } else {
        catalogLoading_ = false;
        // Keep splash visible until fade-out completes in updateAnimations / drawLoadingOverlay
        if (catalogSplashAlpha_ < 0.05f) catalogSplashAlpha_ = 0.f;
    }
    catalogLoadingLabel_=lab;
    catalogLoadingCurrent_=cur;
    catalogLoadingTotal_=tot;
    catalogLoadingMessage_=msg;
}void FullCatalogScreen::setCatalogError(const std::string&e){catalogLoading_=false;catalogError_=e;}void FullCatalogScreen::setInstallProgress(
    bool active,
    uint64_t current,
    uint64_t total,
    uint64_t bytesPerSecond,
    const std::string& stage,
    const std::string& fileName,
    const std::string& message,
    int outcome,
    bool liveAreaOk,
    const std::string& installPath,
    const std::string& titleId)
{
    installProgressActive_ = active;
    installProgressCurrent_ = current;
    installProgressTotal_ = total;
    installProgressSpeed_ = bytesPerSecond;

    installProgressStage_ = stage;
    installProgressFile_ = fileName;
    installProgressMessage_ = message;

    installOutcome_ = outcome;
    installLiveAreaOk_ = liveAreaOk;
    installResultPath_ = installPath;
    installResultTitleId_ = titleId;
    // Refresh local install badges after a finished install attempt
    if (!titleId.empty() && (outcome == 1 || outcome == 2)) {
        invalidateInstallStatus(titleId);
    }
}
bool FullCatalogScreen::init(){
    vita2d_init();
    vita2d_set_clear_color(BG);
    font_=vita2d_load_default_pgf();
    if(!font_)return false;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    // Optional full-screen catalog splash (bundled in VPK as ui/catalog_loading.png)
    catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.png");
    if (!catalogLoadingTex_) {
        catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.PNG");
    }
    if (catalogLoadingTex_) {
        diagnostics::log("[UI] catalog_loading.png loaded");
    } else {
        diagnostics::log("[UI] catalog_loading.png not found (fallback overlay)");
    }
    headerLogoTex_ = vita2d_load_PNG_file("app0:ui/PSVitaAlive_Store_logo_text.png");
    if (!headerLogoTex_) {
        headerLogoTex_ = vita2d_load_PNG_file("app0:ui/logo.png");
    }
    if (headerLogoTex_) {
        diagnostics::log("[UI] header logo loaded");
    } else {
        diagnostics::log("[UI] header logo not found (text fallback)");
    }
    state_=UiState{};
    ready_=true;
    diagnostics::log("[UI] initialized");
    return true;
}void FullCatalogScreen::scheduleTextureFree(vita2d_texture* texture){
    if(!texture)return;
    deferredFreeTextures_.push_back(texture);
}
void FullCatalogScreen::flushDeferredTextureFrees(){
    if(deferredFreeTextures_.empty())return;
    // Previous frame has been presented; safe to return memory to vita2d.
    // Cap per frame so spam L/R cannot free dozens of textures in one shot (Vita3K crash).
    vita2d_wait_rendering_done();
    size_t n=0;
    while(!deferredFreeTextures_.empty() && n<MAX_DEFERRED_FREES_PER_FRAME){
        vita2d_texture* t=deferredFreeTextures_.front();
        deferredFreeTextures_.erase(deferredFreeTextures_.begin());
        if(t)vita2d_free_texture(t);
        ++n;
    }
}
void FullCatalogScreen::releaseTextures(){
    if(textures_.empty()){
        textureOrder_.clear();
    } else {
        for(auto& e:textures_){
            if(e.second){
                scheduleTextureFree(e.second);
                e.second=nullptr;
            }
        }
        textures_.clear();
        textureOrder_.clear();
    }
    // Catalog switches free many textures at once — drain immediately after GPU
    // wait so the next catalog does not pile deferred frees / UAF under load.
    if(!deferredFreeTextures_.empty()){
        vita2d_wait_rendering_done();
        while(!deferredFreeTextures_.empty()){
            vita2d_texture* t=deferredFreeTextures_.front();
            deferredFreeTextures_.erase(deferredFreeTextures_.begin());
            if(t)vita2d_free_texture(t);
        }
    }
}
void FullCatalogScreen::releaseScreenshotTextures(){
    bool any=false;
    for(const auto& kv:textures_){
        if(kv.first.find("/shot_")!=std::string::npos){any=true;break;}
    }
    if(!any)return;
    for(auto i=textures_.begin();i!=textures_.end();){
        if(i->first.find("/shot_")!=std::string::npos){
            scheduleTextureFree(i->second);
            i=textures_.erase(i);
        }else ++i;
    }
    textureOrder_.erase(std::remove_if(textureOrder_.begin(),textureOrder_.end(),[](const std::string& p){
        return p.find("/shot_")!=std::string::npos;
    }),textureOrder_.end());
}
void FullCatalogScreen::releaseTexturesNotIn(const std::unordered_set<std::string>& keep){
    bool any=false;
    for(const auto& kv:textures_){
        if(keep.find(kv.first)==keep.end()){any=true;break;}
    }
    if(!any)return;
    size_t freed=0;
    for(auto i=textures_.begin();i!=textures_.end();){
        if(keep.find(i->first)==keep.end()){
            scheduleTextureFree(i->second);
            i=textures_.erase(i);
            ++freed;
        }else ++i;
    }
    textureOrder_.erase(std::remove_if(textureOrder_.begin(),textureOrder_.end(),[&](const std::string& p){
        return keep.find(p)==keep.end();
    }),textureOrder_.end());
    if(freed>0){
        char m[96];
        sceClibSnprintf(m,sizeof(m),"[UI] deferred-free %u off-screen textures (kept %u)",
            (unsigned)freed,(unsigned)textures_.size());
        diagnostics::log(m);
    }
}
void FullCatalogScreen::touchTexture(const std::string& p){
    auto i=std::find(textureOrder_.begin(),textureOrder_.end(),p);
    if(i!=textureOrder_.end())textureOrder_.erase(i);
    textureOrder_.push_back(p);
}
void FullCatalogScreen::evictTextureIfNeeded(const std::string& ns){
    const size_t lim=(ns=="shot")?MAX_SCREENSHOT_TEXTURES:MAX_APP_TEXTURES;
    const char* marker=(ns=="shot")?"/shot_":"/app_";
    size_t c=0;
    for(const auto& p:textureOrder_)if(p.find(marker)!=std::string::npos)++c;
    while(c>=lim){
        bool removed=false;
        for(auto i=textureOrder_.begin();i!=textureOrder_.end();++i){
            if(i->find(marker)==std::string::npos)continue;
            auto t=textures_.find(*i);
            if(t!=textures_.end()){
                scheduleTextureFree(t->second);
                textures_.erase(t);
            }
            textureOrder_.erase(i);
            --c;
            removed=true;
            break;
        }
        if(!removed)break;
    }
}
void FullCatalogScreen::shutdown(){
    releaseTextures();
    flushDeferredTextureFrees();
    if (catalogLoadingTex_) {
        vita2d_free_texture(catalogLoadingTex_);
        catalogLoadingTex_ = nullptr;
    }
    if (headerLogoTex_) {
        vita2d_free_texture(headerLogoTex_);
        headerLogoTex_ = nullptr;
    }
    if(font_){vita2d_free_pgf(font_);font_=nullptr;}
    if(ready_){vita2d_fini();ready_=false;}
    diagnostics::log("[UI] shutdown");
}
int FullCatalogScreen::totalRows()const{return catalogView().empty()?0:(int(catalogView().size())+2)/3;}int FullCatalogScreen::visibleRowsFull()const{return 3;}int FullCatalogScreen::visibleRowsSplit()const{return std::max(1,(SCREEN_H-HEADER_H-TABS_H-FOOTER_H-GRID_PAD*2)/(SPLIT_CARD_H+CARD_GAP));}int FullCatalogScreen::selectedIndex()const{return catalogView().empty()?-1:std::max(0,std::min(state_.focusIndex,(int)catalogView().size()-1));}void FullCatalogScreen::clampCatalogFocus(){if(catalogView().empty())state_.focusIndex=0;else state_.focusIndex=std::max(0,std::min(state_.focusIndex,(int)catalogView().size()-1));}void FullCatalogScreen::clampCatalogScroll(){if(catalogView().empty()){state_.catalogScrollRow=0;return;}int v=state_.mode==UiMode::FULL_CATALOG?visibleRowsFull():visibleRowsSplit();if(state_.mode==UiMode::FULL_CATALOG){int r=state_.focusIndex/3;if(r<state_.catalogScrollRow)state_.catalogScrollRow=r;if(r>=state_.catalogScrollRow+v)state_.catalogScrollRow=r-v+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,std::max(0,totalRows()-v)));}else{int m=std::max(0,(int)catalogView().size()-v);if(state_.focusIndex<state_.catalogScrollRow)state_.catalogScrollRow=state_.focusIndex;if(state_.focusIndex>=state_.catalogScrollRow+v)state_.catalogScrollRow=state_.focusIndex-v+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,m));}}
void FullCatalogScreen::sortItemsByDate(std::vector<CatalogItem>&v)const{std::stable_sort(v.begin(),v.end(),[](const CatalogItem&a,const CatalogItem&b){if(a.versionDate!=b.versionDate)return a.versionDate>b.versionDate;return lowerAscii(a.name)<lowerAscii(b.name);});}bool FullCatalogScreen::matchesSearch(const CatalogItem&i,const std::string&q)const{if(q.empty())return true;std::string x=lowerAscii(q),h=lowerAscii(i.name+"\n"+i.titleId+"\n"+i.author+"\n"+i.description+"\n"+i.longDescription+"\n"+i.category+"\n"+i.subcategory);return h.find(x)!=std::string::npos;}void FullCatalogScreen::applySearch(const std::string&q){
    searchQuery_=q;
    items_.clear();
    if(!q.empty()){
        items_.reserve(allItems_.size()>256?256:allItems_.size());
        for(const auto&i:allItems_)if(matchesSearch(i,q))items_.push_back(i);
    }
    // When q empty, catalogView() uses allItems_ — no second full copy in RAM.
    state_.focusIndex=0;state_.catalogScrollRow=0;state_.detailScroll=0;detailScrollBeforeLinkMode_=0;state_.linkFocus=-1;state_.linkNavigation=false;
    char m[256];sceClibSnprintf(m,sizeof(m),"[UI] search query='%s' results=%u",q.c_str(),(unsigned)catalogView().size());diagnostics::log(m);
}
int FullCatalogScreen::detailContentHeight(const CatalogItem&i,int w)const{int cw=std::max(1,w-36),mc=std::max(18,cw/7);std::vector<std::string>pre,post;auto add=[&](std::vector<std::string>&l,const char*t,const std::string&v){if(v.empty())return;l.push_back(t);std::vector<std::string>q;wrapText(v,mc,q);for(auto&s:q)l.push_back(s);l.push_back("");};add(pre,"Description",i.description);add(pre,"Long Description",i.longDescription);add(post,"Requirements",i.requirements);post.push_back("Information");post.push_back("Title ID: "+i.titleId);post.push_back("Version: "+i.version);post.push_back("Release date: "+i.versionDate);post.push_back("Category: "+i.category);post.push_back("Subcategory: "+i.subcategory);post.push_back("Size: "+i.size);post.push_back("Status: "+i.status);post.push_back("");add(post,"Changelog",i.changelog);int lh=i.linkDetails.empty()?0:10+(int)i.linkDetails.size()*(LINK_ROW_H+LINK_GAP),sc=std::min(5,(int)i.screenshots.size());return lh+((int)pre.size()+(int)post.size())*LINE_H+sc*SCREENSHOT_ROW_H+32;}int FullCatalogScreen::detailLinkScrollLimit(const CatalogItem&i,int w,int h)const{(void)w;const int n=(int)downloadLinkIndices(i).size();if(n<=0)return 0;int lh=10+n*(LINK_ROW_H+LINK_GAP),v=std::max(1,h-DETAIL_HEADER_H-18);return std::max(0,lh-v);}void FullCatalogScreen::clampDetailScroll(){int i=selectedIndex();if(i<0){state_.detailScroll=0;return;}int vh=std::max(1,SCREEN_H-HEADER_H-TABS_H-FOOTER_H-DETAIL_HEADER_H-18),total=detailContentHeight(catalogView()[i],SCREEN_W/2),mx=std::max(0,total-vh);state_.detailScroll=std::max(0,std::min(state_.detailScroll,mx));if(catalogView()[i].linkDetails.empty()){state_.linkFocus=-1;state_.linkNavigation=false;}else if(state_.linkFocus>=(int)catalogView()[i].linkDetails.size())state_.linkFocus=(int)catalogView()[i].linkDetails.size()-1;if(state_.linkNavigation){int lim=detailLinkScrollLimit(catalogView()[i],SCREEN_W/2,SCREEN_H-HEADER_H-TABS_H-FOOTER_H);state_.detailScroll=std::max(0,std::min(state_.detailScroll,lim));}}
void FullCatalogScreen::moveCatalogFocus(int d){if(catalogView().empty())return;if(state_.mode!=UiMode::FULL_CATALOG)releaseScreenshotTextures();if(state_.mode==UiMode::FULL_CATALOG){if(d<0&&state_.focusIndex>=3)state_.focusIndex-=3;if(d>0&&state_.focusIndex+3<(int)catalogView().size())state_.focusIndex+=3;}else{if(d<0&&state_.focusIndex>0)--state_.focusIndex;if(d>0&&state_.focusIndex+1<(int)catalogView().size())++state_.focusIndex;}clampCatalogFocus();clampCatalogScroll();state_.detailScroll=0;detailScrollBeforeLinkMode_=0;state_.linkFocus=-1;state_.linkNavigation=false;}void FullCatalogScreen::moveDetailScroll(int d){state_.detailScroll+=d<0?-72:72;clampDetailScroll();}void FullCatalogScreen::enterLinkNavigation(){int i=selectedIndex();if(i<0)return;const auto idxs=downloadLinkIndices(catalogView()[i]);if(idxs.empty())return;detailScrollBeforeLinkMode_=state_.detailScroll;state_.linkNavigation=true;state_.linkFocus=0;state_.detailScroll=0;clampDetailScroll();diagnostics::log("[UI] link navigation enabled (downloads only)");}void FullCatalogScreen::exitLinkNavigation(){if(!state_.linkNavigation)return;state_.linkNavigation=false;state_.linkFocus=-1;state_.detailScroll=detailScrollBeforeLinkMode_;detailScrollBeforeLinkMode_=0;clampDetailScroll();diagnostics::log("[UI] link navigation disabled; detail scroll restored");}void FullCatalogScreen::moveLinkFocus(int dx,int dy){(void)dx;int i=selectedIndex();if(i<0)return;const auto idxs=downloadLinkIndices(catalogView()[i]);int c=(int)idxs.size();if(c<=0)return;if(state_.linkFocus<0)state_.linkFocus=0;else state_.linkFocus=std::max(0,std::min(c-1,state_.linkFocus+dy));state_.linkNavigation=true;int top=DETAIL_HEADER_H+10+state_.linkFocus*(LINK_ROW_H+LINK_GAP),vis=SCREEN_H-HEADER_H-TABS_H-FOOTER_H-DETAIL_HEADER_H-18,lim=detailLinkScrollLimit(catalogView()[i],SCREEN_W/2,SCREEN_H-HEADER_H-TABS_H-FOOTER_H);if(top<state_.detailScroll)state_.detailScroll=top;if(top+LINK_ROW_H>state_.detailScroll+vis)state_.detailScroll=top+LINK_ROW_H-vis;state_.detailScroll=std::max(0,std::min(state_.detailScroll,lim));}void FullCatalogScreen::activateFocusedLink(){int i=selectedIndex();if(i<0||state_.linkFocus<0)return;const auto idxs=downloadLinkIndices(catalogView()[i]);if(state_.linkFocus>=(int)idxs.size())return;const CatalogLink&l=catalogView()[i].linkDetails[idxs[state_.linkFocus]];if(!linkAction_||!actionableLink(l)){diagnostics::log(std::string("[UI] non-download link selected: ")+l.url);return;}if(linkAction_(catalogView()[i],l))exitLinkNavigation();}void FullCatalogScreen::changeCatalog(int d){
    if(catalogLoading_||installProgressActive_||isTransitioning())return;
    if(catalogSwitchCooldownFrames_>0)return;

    // Drain any pending texture frees before switching (do not soft-skip the input).
    if(!deferredFreeTextures_.empty()){
        vita2d_wait_rendering_done();
        while(!deferredFreeTextures_.empty()){
            vita2d_texture* t=deferredFreeTextures_.front();
            deferredFreeTextures_.erase(deferredFreeTextures_.begin());
            if(t)vita2d_free_texture(t);
        }
    }

    const uint64_t nowMs=sceKernelGetProcessTimeWide()/1000ULL;
    if(lastCatalogSwitchMs_!=0 && nowMs>=lastCatalogSwitchMs_ &&
       (nowMs-lastCatalogSwitchMs_)<CATALOG_SWITCH_MIN_MS){
        return;
    }

    int v=(int)state_.catalog+d,c=(int)CatalogType::Count;
    if(v<0)v=c-1;if(v>=c)v=0;
    CatalogType n=(CatalogType)v;
    if(n==state_.catalog)return;

    if(state_.mode!=UiMode::FULL_CATALOG){
        exitLinkNavigation();
        state_.mode=UiMode::FULL_CATALOG;
        state_.activePanel=UiPanel::Catalog;
        state_.detailScroll=0;
        releaseScreenshotTextures();
    }

    // Lock BEFORE loader callback — blocks install probes and image binds.
    catalogLoading_=true;
    catalogLoadingLabel_=catalogName(n);
    catalogLoadingCurrent_=0;
    catalogLoadingTotal_=0;
    catalogLoadingMessage_="Checking catalog cache...";
    catalogError_.clear();
    showToast(std::string("Loading ") + catalogName(n) + "...", 1200);
    state_.catalog=n;
    items_.clear();
    // Drop previous catalog payload early to free RAM before the next load.
    {
        std::vector<CatalogItem> empty;
        allItems_.swap(empty);
    }
    state_.focusIndex=0;
    state_.catalogScrollRow=0;
    releaseTextures();
    installStatusCache_.clear();
    catalogSwitchCooldownFrames_=CATALOG_SWITCH_COOLDOWN_FRAMES;
    lastCatalogSwitchMs_=nowMs;
    installStatusWarmupUntilMs_=nowMs+1500ULL;

    if(catalogChange_){
        if(!catalogChange_(n)){
            catalogLoading_=false;
            catalogError_="Could not start catalog load";
            diagnostics::log("[UI] catalogChange callback returned false");
        }
        return;
    }
    setActiveCatalog(n);
    catalogLoading_=false;
}
bool FullCatalogScreen::isTransitioning()const{return state_.mode==UiMode::OPENING_DETAIL||state_.mode==UiMode::CLOSING_DETAIL;}void FullCatalogScreen::startOpeningDetail(){if(state_.mode!=UiMode::FULL_CATALOG||catalogLoading_||installProgressActive_||selectedIndex()<0)return;state_.detailScroll=0;detailScrollBeforeLinkMode_=0;state_.linkFocus=-1;state_.linkNavigation=false;state_.transitionStart=sceKernelGetProcessTimeWide();state_.mode=UiMode::OPENING_DETAIL;diagnostics::log("[UI] opening detail");}void FullCatalogScreen::startClosingDetail(){
    if(state_.mode!=UiMode::SPLIT_DETAIL)return;
    exitLinkNavigation();
    state_.transitionStart=sceKernelGetProcessTimeWide();
    state_.mode=UiMode::CLOSING_DETAIL;
    diagnostics::log("[UI] closing detail");
}float FullCatalogScreen::transitionProgress()const{
    if(!isTransitioning())return 1.0f;
    uint64_t e=sceKernelGetProcessTimeWide()-state_.transitionStart;
    float t=std::max(0.0f,std::min(1.0f,(float)e/(float)(TRANSITION_MS*1000)));
    return easeInOut(t);
}void FullCatalogScreen::updateTransition(){
    if(!isTransitioning()||transitionProgress()<1.0f)return;
    const bool closing=(state_.mode==UiMode::CLOSING_DETAIL);
    state_.mode=state_.mode==UiMode::OPENING_DETAIL?UiMode::SPLIT_DETAIL:UiMode::FULL_CATALOG;
    state_.activePanel=UiPanel::Catalog;
    clampCatalogScroll();
    if(closing){
        releaseScreenshotTextures();
        catalogSwitchCooldownFrames_=2;
        diagnostics::log("[UI] detail closed — screenshot textures scheduled for free");
    }
}
void FullCatalogScreen::handleTouch() {
    if (isTransitioning()) return;

    SceTouchData td{};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) <= 0) return;

    // Vita front touch is typically 1920x1088 logical units.
    auto mapX = [](int tx) { return tx * SCREEN_W / 1920; };
    auto mapY = [](int ty) { return ty * SCREEN_H / 1088; };
    auto hit = [](int x, int y, int rx, int ry, int rw, int rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    };

    // --- Install overlay: only the explicit button is tappable ---
    // (Tapping the whole card used to cancel mid-download → "Download cancelled".)
    if (installProgressActive_) {
        if (td.reportNum > 0) {
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = mapX(td.report[0].x);
                touchStartY_ = mapY(td.report[0].y);
                touchMoved_ = false;
            }
        } else if (touchDown_) {
            const int x = touchStartX_, y = touchStartY_;
            touchDown_ = false;
            if (touchMoved_) return;
            const int ow = 640, oh = 380, ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
            const int by = oy + 300, bw = 280, bh = 44;
            if (!hit(x, y, ox + 28, by, bw, bh)) return;
            if (installOutcome_ == 1 || installOutcome_ == 2) {
                if (installAcknowledge_) installAcknowledge_();
            } else if (installCancel_) {
                installCancel_();
            }
        }
        return;
    }

    // --- Settings: must run before generic drag/scroll handling ---
    if (state_.mode == UiMode::SETTINGS) {
        const int margin = 20;
        const int contentTop = HEADER_H + 56;
        const int colW = SCREEN_W - margin * 2;
        const int listX = margin;
        const int listW = (colW * 58) / 100;
        struct Meta { bool sectionStart; const char* section; };
        Meta meta[5] = {
            {true, "INSTALL"}, {false, ""}, {true, "INTERFACE"}, {true, "CATALOG"}, {true, "UPDATES"}
        };
        int rowY[5];
        int y = contentTop - static_cast<int>(settingsScrollY_);
        for (int i = 0; i < 5; ++i) {
            if (meta[i].sectionStart && meta[i].section[0]) y += 22;
            rowY[i] = y;
            y += 52 + 8;
        }
        const int rowH = 52;
        const int measured = 5 * (52 + 8) + 4 * 22;
        const int listViewH = SCREEN_H - contentTop - FOOTER_H - 8;
        const float maxScroll = static_cast<float>(std::max(0, measured - listViewH));

        if (td.reportNum > 0) {
            const int x = mapX(td.report[0].x);
            const int yy = mapY(td.report[0].y);
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = x;
                touchStartY_ = yy;
                touchLastY_ = yy;
                touchMoved_ = false;
                touchAccumY_ = 0.f;
            } else {
                const int dy = yy - touchLastY_;
                touchLastY_ = yy;
                if (std::abs(x - touchStartX_) > 20 || std::abs(yy - touchStartY_) > 20)
                    touchMoved_ = true;
                // Vertical drag scrolls the settings list (finger up → content up)
                if (touchMoved_) {
                    settingsScrollY_ -= static_cast<float>(dy);
                    if (settingsScrollY_ < 0.f) settingsScrollY_ = 0.f;
                    if (settingsScrollY_ > maxScroll) settingsScrollY_ = maxScroll;
                }
            }
        } else if (touchDown_) {
            const int x = touchStartX_, yy = touchStartY_;
            touchDown_ = false;
            // Allow slight finger jitter — still treat as tap if not a long drag
            for (int i = 0; i < 5; ++i) {
                if (hit(x, yy, listX, rowY[i], listW, rowH)) {
                    if (settingsFocus_ == i) cycleSettingsOption(i, +1);
                    else settingsFocus_ = i;
                    return;
                }
            }
            if (yy < HEADER_H + 48) {
                closeSettings(true);
                return;
            }
        }
        return;
    }

    if (catalogLoading_) return;

    constexpr int kDragSlop = 20;       // px before a gesture counts as drag
    constexpr float kScrollPx = 48.f;   // pixels of drag per one catalog/detail step

    if (td.reportNum > 0) {
        const int x = mapX(td.report[0].x);
        const int y = mapY(td.report[0].y);
        if (!touchDown_) {
            touchDown_ = true;
            touchStartX_ = x;
            touchStartY_ = y;
            touchLastY_ = y;
            touchMoved_ = false;
            touchAccumY_ = 0.f;
            touchDownMs_ = sceKernelGetProcessTimeWide() / 1000ULL;
        } else {
            const int dy = y - touchLastY_;
            touchLastY_ = y;
            if (std::abs(x - touchStartX_) > kDragSlop || std::abs(y - touchStartY_) > kDragSlop)
                touchMoved_ = true;

            if (!touchMoved_) return;

            touchAccumY_ += static_cast<float>(dy);
            while (touchAccumY_ <= -kScrollPx) {
                touchAccumY_ += kScrollPx;
                // Scroll direction: finger up → content moves up → next items
                if (state_.mode == UiMode::FULL_CATALOG) {
                    moveCatalogFocus(1);
                } else if (state_.mode == UiMode::SPLIT_DETAIL) {
                    if (state_.activePanel == UiPanel::Detail || touchStartX_ >= SCREEN_W / 2) {
                        state_.activePanel = UiPanel::Detail;
                        moveDetailScroll(1);
                    } else {
                        moveCatalogFocus(1);
                    }
                }
            }
            while (touchAccumY_ >= kScrollPx) {
                touchAccumY_ -= kScrollPx;
                if (state_.mode == UiMode::FULL_CATALOG) {
                    moveCatalogFocus(-1);
                } else if (state_.mode == UiMode::SPLIT_DETAIL) {
                    if (state_.activePanel == UiPanel::Detail || touchStartX_ >= SCREEN_W / 2) {
                        state_.activePanel = UiPanel::Detail;
                        moveDetailScroll(-1);
                    } else {
                        moveCatalogFocus(-1);
                    }
                }
            }
        }
        return;
    }

    if (!touchDown_) return;

    // Finger lifted
    const int x = touchStartX_;
    const int y = touchStartY_;
    const bool wasDrag = touchMoved_;
    touchDown_ = false;
    touchAccumY_ = 0.f;
    if (wasDrag) return;

    
// --- Header search bar ---
    if (y < HEADER_H) {
        const int barX = 200, barY = 10, barH = 32;
        const int barW = SCREEN_W - barX - 140;
        if (hit(x, y, barX, barY, barW, barH)) {
            // Right edge of bar clears filter when active
            if (!searchQuery_.empty() && x > barX + barW - 56) {
                applySearch("");
                return;
            }
            if (searchRequest_) applySearch(searchRequest_(searchQuery_));
            return;
        }
        return;
    }

    // --- Tabs (L/R equivalent) ---
    if (y >= HEADER_H && y < HEADER_H + TABS_H) {
        const float tw = static_cast<float>(SCREEN_W) / static_cast<float>(CatalogType::Count);
        const int tab = std::min((int)CatalogType::Count - 1, std::max(0, (int)(x / tw)));
        const int delta = tab - (int)state_.catalog;
        if (delta != 0) changeCatalog(delta);
        return;
    }

    const int panelTop = HEADER_H + TABS_H;
    const int panelBottom = SCREEN_H - FOOTER_H;
    if (y < panelTop || y >= panelBottom) {
        // Footer: in full catalog, left area can open search
        if (y >= panelBottom && state_.mode == UiMode::FULL_CATALOG && x < SCREEN_W / 2) {
            if (searchRequest_) applySearch(searchRequest_(searchQuery_));
        }
        return;
    }

    // --- Full catalog grid ---
    if (state_.mode == UiMode::FULL_CATALOG) {
        const int cw = (SCREEN_W - GRID_PAD * 2 - CARD_GAP * 2) / 3;
        const float rowH = static_cast<float>(FULL_CARD_H + CARD_GAP);
        const int localX = x - GRID_PAD;
        const int localY = y - (panelTop + GRID_PAD);
        if (localX < 0 || localY < 0) return;
        const int col = localX / (cw + CARD_GAP);
        const int row = static_cast<int>(localY / rowH + visualCatalogScroll_);
        if (col < 0 || col > 2) return;
        const int idx = row * 3 + col;
        if (idx < 0 || idx >= (int)catalogView().size()) return;
        if (idx == state_.focusIndex) startOpeningDetail();
        else {
            state_.focusIndex = idx;
            clampCatalogFocus();
            clampCatalogScroll();
        }
        return;
    }

    if (state_.mode != UiMode::SPLIT_DETAIL) return;

    const int mid = SCREEN_W / 2;

    // --- Left list ---
    if (x < mid) {
        state_.activePanel = UiPanel::Catalog;
        const float rowH = static_cast<float>(SPLIT_CARD_H + CARD_GAP);
        const int localY = y - (panelTop + GRID_PAD);
        if (localY >= 0) {
            const int idx = static_cast<int>(localY / rowH + visualCatalogScroll_);
            if (idx >= 0 && idx < (int)catalogView().size()) {
                state_.focusIndex = idx;
                state_.detailScroll = 0;
                visualDetailScroll_ = 0.f;
                clampCatalogFocus();
                clampCatalogScroll();
            }
        }
        // Tap near left edge bottom could close — not needed
        return;
    }

    // --- Right detail panel ---
    state_.activePanel = UiPanel::Detail;
    const int dx = mid, dy = panelTop;
    const int dw = SCREEN_W - mid, dh = panelBottom - panelTop;

    // Close detail: tap top-left of detail header strip or outside-ish
    // Links button (same coords as drawDetailPanel)
    if (!catalogView().empty()) {
        const int i = selectedIndex();
        if (i >= 0 && !catalogView()[i].linkDetails.empty()) {
            const int bx = dx + dw - 142, by = dy + 12, bw = 128, bh = 28;
            if (hit(x, y, bx, by, bw, bh)) {
                if (state_.linkNavigation) exitLinkNavigation();
                else enterLinkNavigation();
                return;
            }
        }
    }

    // Tap in header left → close detail (○)
    if (y < dy + DETAIL_HEADER_H && x < dx + 90) {
        startClosingDetail();
        return;
    }

    // Download link rows (even without link mode — tap installs)
    {
        const int i = selectedIndex();
        if (i >= 0) {
            const auto idxs = downloadLinkIndices(catalogView()[i]);
            const int listTop = dy + DETAIL_HEADER_H + 10 - (int)visualDetailScroll_;
            for (int row = 0; row < (int)idxs.size(); ++row) {
                const int ry = listTop + row * (LINK_ROW_H + LINK_GAP);
                if (hit(x, y, dx + 18, ry, dw - 36, LINK_ROW_H)) {
                    state_.linkNavigation = true;
                    state_.linkFocus = row;
                    activateFocusedLink();
                    return;
                }
            }
        }
    }
}



void FullCatalogScreen::setAppSettings(const ::psvitaalive::AppSettingsData& settings) {
    settingsEdit_ = settings;
}

void FullCatalogScreen::setPluginStatus(const ::psvitaalive::PluginStatus& plugins) {
    pluginsStatus_ = plugins;
}

void FullCatalogScreen::setSettingsSaveCallback(SettingsSaveFn callback) {
    settingsSave_ = std::move(callback);
}

void FullCatalogScreen::openSettings() {
    if (installProgressActive_ || catalogLoading_ || selfUpdateBusy_.load()) {
        showToast("Wait until loading/install finishes", 1800);
        return;
    }
    if (state_.mode == UiMode::SETTINGS) return;
    settingsReturnMode_ = (state_.mode == UiMode::SPLIT_DETAIL) ? UiMode::SPLIT_DETAIL : UiMode::FULL_CATALOG;
    settingsFocus_ = 0;
    settingsEnter_ = 0.f;
    settingsFocusY_ = 0.f;
    settingsScrollY_ = 0.f;
    state_.mode = UiMode::SETTINGS;
    diagnostics::log("[UI] settings opened");
}

void FullCatalogScreen::closeSettings(bool save) {
    if (state_.mode != UiMode::SETTINGS) return;
    if (save) {
        if (settingsSave_) settingsSave_(settingsEdit_);
        showToast("Settings saved", 1600);
        diagnostics::log("[UI] settings saved");
    }
    state_.mode = settingsReturnMode_;
}


void FullCatalogScreen::pollSelfUpdateProgress() {
    if (!selfUpdateBusy_.load() && !selfUpdateDone_.load()) return;

    if (selfUpdateBusy_.load()) {
        const uint64_t cur = selfUpdateCur_.load();
        const uint64_t tot = selfUpdateTot_.load();
        setInstallProgress(
            true,
            cur,
            tot,
            0,
            "Self-update",
            "PSVitaAlive.vpk",
            selfUpdateMsg_[0] ? selfUpdateMsg_ : "Updating...",
            0,
            false
        );
        return;
    }

    if (selfUpdateDone_.load()) {
        const bool ok = selfUpdateOk_.load();
        setInstallProgress(
            true,
            1,
            1,
            0,
            "Self-update",
            "PSVitaAlive.vpk",
            selfUpdateMsg_[0] ? selfUpdateMsg_ : (ok ? "Update installed — press START to exit, then reopen" : "Update failed"),
            ok ? 1 : 2,
            false
        );
        selfUpdateDone_.store(false);
        if (selfUpdateThread_ >= 0) {
            sceKernelWaitThreadEnd(selfUpdateThread_, nullptr, nullptr);
            sceKernelDeleteThread(selfUpdateThread_);
            selfUpdateThread_ = -1;
        }
    }
}

int FullCatalogScreen::selfUpdateWorkerEntry(SceSize args, void* argp) {
    (void)args;
    FullCatalogScreen* self = *reinterpret_cast<FullCatalogScreen**>(argp);
    if (!self) return 0;

    auto onProg = [self](const ::psvitaalive::UpdateChecker::ApplyProgress& p) {
        self->selfUpdateCur_.store(p.current);
        self->selfUpdateTot_.store(p.total);
        sceClibSnprintf(self->selfUpdateMsg_, sizeof(self->selfUpdateMsg_), "%s", p.message.c_str());
    };

    const bool ok = ::psvitaalive::UpdateChecker::applyUpdate(self->selfUpdateInfo_, onProg, nullptr);
    self->selfUpdateOk_.store(ok);
    if (ok) {
        sceClibSnprintf(self->selfUpdateMsg_, sizeof(self->selfUpdateMsg_),
                        "Update installed — press START to exit, then reopen");
    } else if (self->selfUpdateMsg_[0] == 0) {
        sceClibSnprintf(self->selfUpdateMsg_, sizeof(self->selfUpdateMsg_), "Update failed");
    }
    self->selfUpdateBusy_.store(false);
    self->selfUpdateDone_.store(true);
    return 0;
}

void FullCatalogScreen::triggerSelfUpdateAction() {
    if (selfUpdateBusy_.load() || installProgressActive_) {
        showToast("Wait until the current operation finishes", 1800);
        return;
    }

    if (selfUpdateChecked_ &&
        selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable &&
        !selfUpdateInfo_.downloadUrl.empty()) {
        // Start VitaDB-style in-place install on a worker thread.
        selfUpdateBusy_.store(true);
        selfUpdateDone_.store(false);
        selfUpdateOk_.store(false);
        selfUpdateCur_.store(0);
        selfUpdateTot_.store(selfUpdateInfo_.assetSize);
        sceClibSnprintf(selfUpdateMsg_, sizeof(selfUpdateMsg_), "Starting update...");
        closeSettings(true);
        setInstallProgress(true, 0, selfUpdateInfo_.assetSize, 0, "Self-update", "PSVitaAlive.vpk",
                           "Starting update...", 0, false);

        FullCatalogScreen* self = this;
        selfUpdateThread_ = sceKernelCreateThread(
            "PSVitaAliveSelfUpdate",
            &FullCatalogScreen::selfUpdateWorkerEntry,
            0x10000100,
            64 * 1024,
            0,
            0,
            nullptr
        );
        if (selfUpdateThread_ < 0) {
            selfUpdateBusy_.store(false);
            setInstallProgress(true, 0, 0, 0, "Self-update", "PSVitaAlive.vpk",
                               "Could not start update thread", 2, false);
            showToast("Update thread failed", 2000);
            return;
        }
        const int st = sceKernelStartThread(selfUpdateThread_, sizeof(self), &self);
        if (st < 0) {
            sceKernelDeleteThread(selfUpdateThread_);
            selfUpdateThread_ = -1;
            selfUpdateBusy_.store(false);
            setInstallProgress(true, 0, 0, 0, "Self-update", "PSVitaAlive.vpk",
                               "Could not start update thread", 2, false);
            return;
        }
        diagnostics::log("[UI] self-update apply started");
        return;
    }

    showToast("Checking GitHub for updates...", 1200);
    selfUpdateInfo_ = ::psvitaalive::UpdateChecker::checkLatest(PSVITAALIVE_VERSION);
    selfUpdateChecked_ = true;
    if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable) {
        showToast(std::string("Update ") + selfUpdateInfo_.remoteVersion + " available — press X to install", 2800);
    } else if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate) {
        showToast(std::string("Already up to date (v") + selfUpdateInfo_.localVersion + ")", 2200);
    } else {
        showToast(selfUpdateInfo_.error.empty() ? "Update check failed" : selfUpdateInfo_.error, 2600);
    }
}

void FullCatalogScreen::cycleSettingsOption(int row, int delta) {
    if (delta == 0) return;
    if (row == 0) {
        int v = static_cast<int>(settingsEdit_.installMethod);
        v = (v + delta) % 3;
        if (v < 0) v += 3;
        settingsEdit_.installMethod = static_cast<::psvitaalive::InstallMethod>(v);
    } else if (row == 1) {
        settingsEdit_.pspTarget = (settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline)
            ? ::psvitaalive::PspTarget::LiveArea : ::psvitaalive::PspTarget::Adrenaline;
    } else if (row == 2) {
        settingsEdit_.warnMissingPlugins = !settingsEdit_.warnMissingPlugins;
    } else if (row == 3) {
        settingsEdit_.promptImageWarmup = !settingsEdit_.promptImageWarmup;
    } else if (row == 4) {
        triggerSelfUpdateAction();
    }
}

void FullCatalogScreen::handleSettingsInput(uint32_t pressed, uint32_t nav) {
    constexpr int kRows = 5;
    if (nav & SCE_CTRL_UP) {
        settingsFocus_ = (settingsFocus_ + kRows - 1) % kRows;
    }
    if (nav & SCE_CTRL_DOWN) {
        settingsFocus_ = (settingsFocus_ + 1) % kRows;
    }
    if ((nav & SCE_CTRL_LEFT) || (pressed & SCE_CTRL_SQUARE)) {
        cycleSettingsOption(settingsFocus_, -1);
    }
    if ((nav & SCE_CTRL_RIGHT) || (pressed & SCE_CTRL_CROSS)) {
        cycleSettingsOption(settingsFocus_, +1);
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        closeSettings(true);
        return;
    }
    if (pressed & SCE_CTRL_SELECT) {
        closeSettings(true);
        return;
    }
}

void FullCatalogScreen::drawSettings() {
    const float enter = settingsEnter_;
    const int slide = static_cast<int>((1.f - enter) * 28.f);
    const unsigned dimA = static_cast<unsigned>(enter * 255.f);

    vita2d_start_drawing();
    vita2d_set_clear_color(BG);vita2d_clear_screen();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, BG);
    drawHeader(SCREEN_W);

    vita2d_draw_rectangle(0, HEADER_H + slide, SCREEN_W, 44, SURFACE2);
    vita2d_draw_rectangle(0, HEADER_H + slide, SCREEN_W, 3, ACCENT);
    vita2d_pgf_draw_text(font_, 20, HEADER_H + 30 + slide, ACCENT, 1.00f, "Settings");
    vita2d_pgf_draw_text(font_, SCREEN_W - 300, HEADER_H + 28 + slide, DIM, 0.56f, "O / SELECT: Save & back");

    const int margin = 20;
    const int contentTop = HEADER_H + 56 + slide;
    const int contentH = SCREEN_H - contentTop - FOOTER_H - 6;
    const int listClipBottom = contentTop + contentH - 8;

    auto methodLabel = [&]() -> std::string {
        if (settingsEdit_.installMethod == ::psvitaalive::InstallMethod::Auto) return "Auto";
        if (settingsEdit_.installMethod == ::psvitaalive::InstallMethod::Direct) return "Direct";
        return "BGDL";
    };
    auto pspLabel = [&]() -> std::string {
        return settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline ? "Adrenaline" : "LiveArea";
    };
    auto updateLabel = [&]() -> std::string {
        if (selfUpdateBusy_.load()) return "Working...";
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable) {
            return std::string("Install ") + selfUpdateInfo_.remoteVersion;
        }
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate) {
            return "Up to date";
        }
        if (selfUpdateChecked_ && selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::Failed) {
            return "Check failed";
        }
        return std::string("v") + PSVITAALIVE_VERSION;
    };

    struct Opt {
        const char* section;
        const char* label;
        std::string value;
        const char* hint;
        bool sectionStart;
    };
    Opt opts[5] = {
        {"INSTALL", "Install method", methodLabel(), "Auto: BGDL for PKG when available", true},
        {"", "PSP / PS1 target", pspLabel(), "ISO/CSO/PBP under ux0:pspemu", false},
        {"INTERFACE", "Warn missing plugins", settingsEdit_.warnMissingPlugins ? "Yes" : "No", "Startup toast if NoNpDrm is missing", true},
        {"CATALOG", "Prompt image download", settingsEdit_.promptImageWarmup ? "Yes" : "No", "If you choose No once, it will not ask again", true},
        {"UPDATES", "App updates", updateLabel(), "GitHub Releases — X to check / install", true},
    };

    // List (left) + contextual help panel (right)
    const int colW = SCREEN_W - margin * 2;
    const int listX = margin;
    const int listW = (colW * 58) / 100;
    const int sideX = listX + listW + 12;
    const int sideW = colW - listW - 12;
    const int rowH = 52;
    const int sectionH = 22;
    const int rowGap = 8;

    int measured = 0;
    for (int i = 0; i < 5; ++i) {
        if (opts[i].sectionStart && opts[i].section[0]) measured += sectionH;
        measured += rowH + rowGap;
    }
    const int listViewH = listClipBottom - contentTop;
    const float maxScroll = static_cast<float>(std::max(0, measured - listViewH));
    if (settingsScrollY_ < 0.f) settingsScrollY_ = 0.f;
    if (settingsScrollY_ > maxScroll) settingsScrollY_ = maxScroll;

    {
        int fy = 0;
        for (int i = 0; i <= settingsFocus_ && i < 5; ++i) {
            if (opts[i].sectionStart && opts[i].section[0]) fy += sectionH;
            if (i < settingsFocus_) fy += rowH + rowGap;
        }
        const float rowTop = static_cast<float>(fy);
        const float rowBot = rowTop + static_cast<float>(rowH);
        if (rowTop < settingsScrollY_) settingsScrollY_ = rowTop;
        if (rowBot > settingsScrollY_ + static_cast<float>(listViewH))
            settingsScrollY_ = rowBot - static_cast<float>(listViewH);
        if (settingsScrollY_ < 0.f) settingsScrollY_ = 0.f;
        if (settingsScrollY_ > maxScroll) settingsScrollY_ = maxScroll;
    }

    int rowY[5] = {};
    int y = contentTop - static_cast<int>(settingsScrollY_);
    for (int i = 0; i < 5; ++i) {
        if (opts[i].sectionStart && opts[i].section[0]) {
            if (y + 16 >= contentTop && y + 4 <= listClipBottom)
                vita2d_pgf_draw_text(font_, listX + 6, y + 16, DIM, 0.56f, opts[i].section);
            y += sectionH;
        }
        rowY[i] = y;
        const bool focus = (settingsFocus_ == i);
        if (y + rowH >= contentTop && y <= listClipBottom) {
            vita2d_draw_rectangle(listX, y, listW, rowH, focus ? SURFACE : SURFACE2);
            if (focus) {
                vita2d_draw_rectangle(listX, y, 4, rowH, ACCENT);
                vita2d_draw_rectangle(listX, y, listW, 2, ACCENT);
                vita2d_draw_rectangle(listX, y + rowH - 2, listW, 2, ACCENT);
            } else {
                vita2d_draw_rectangle(listX, y + rowH - 1, listW, 1, BORDER);
            }
            vita2d_pgf_draw_text(font_, listX + 14, y + 20, focus ? WHITE : TEXT, 0.68f, opts[i].label);
            const int chipW = 110;
            const int chipX = listX + listW - chipW - 10;
            const int chipY = y + 12;
            vita2d_draw_rectangle(chipX, chipY, chipW, 24, focus ? ACCENT : SURFACE);
            vita2d_pgf_draw_text(font_, chipX + 8, chipY + 17, focus ? BG : ACCENT, 0.54f, opts[i].value.c_str());
            vita2d_pgf_draw_text(font_, listX + 14, y + 42, DIM, 0.48f, opts[i].hint);
            if (focus) vita2d_pgf_draw_text(font_, chipX - 32, chipY + 17, ACCENT, 0.52f, "<>");
        }
        y += rowH + rowGap;
    }

    {
        const int panelH = listClipBottom - contentTop;
        vita2d_draw_rectangle(sideX, contentTop, sideW, panelH, SURFACE2);
        vita2d_draw_rectangle(sideX, contentTop, 3, panelH, ACCENT);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 22, ACCENT, 0.62f, "INFO");

        const char* title = opts[settingsFocus_].label;
        const char* body1 = "";
        const char* body2 = "";
        const char* body3 = "";
        switch (settingsFocus_) {
        case 0:
            body1 = "How packages are installed.";
            body2 = "Auto uses BGDL for PKG when";
            body3 = "Shell supports it, else Direct.";
            break;
        case 1:
            body1 = "Where PSP/PS1 content goes.";
            body2 = "Adrenaline: ISO/CSO under";
            body3 = "ux0:pspemu. LiveArea needs plugins.";
            break;
        case 2:
            body1 = "Show a toast at startup when";
            body2 = "NoNpDrm / NoPspEmuDrm are";
            body3 = "missing from taiHEN config.";
            break;
        case 3:
            body1 = "Ask once whether to download";
            body2 = "all catalog images at startup.";
            body3 = "Off = load images on demand.";
            break;
        case 4:
            body1 = "Checks GitHub Releases for a";
            body2 = "newer PSVitaAlive.vpk and can";
            body3 = "install it in-place (VitaDB style).";
            break;
        default: break;
        }
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 48, WHITE, 0.64f, title);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 74, TEXT, 0.52f, body1);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 94, TEXT, 0.52f, body2);
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 114, TEXT, 0.52f, body3);

        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 150, DIM, 0.52f, "SYSTEM");
        char plug[96];
        sceClibSnprintf(plug, sizeof(plug), "NoNpDrm: %s", pluginsStatus_.nonpdrm ? "OK" : "missing");
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 172, TEXT, 0.52f, plug);
        sceClibSnprintf(plug, sizeof(plug), "NoPspEmuDrm: %s", pluginsStatus_.nopspemudrmKern ? "OK" : "missing");
        vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 192, TEXT, 0.52f, plug);
        if (!pluginsStatus_.configPathUsed.empty()) {
            vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 214, DIM, 0.46f,
                                 ellipsize(pluginsStatus_.configPathUsed, 28).c_str());
        }
        if (settingsFocus_ == 4) {
            char ver[64];
            sceClibSnprintf(ver, sizeof(ver), "Local: v%s", PSVITAALIVE_VERSION);
            vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 240, ACCENT, 0.52f, ver);
            if (selfUpdateChecked_) {
                if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpdateAvailable)
                    sceClibSnprintf(ver, sizeof(ver), "Remote: v%s", selfUpdateInfo_.remoteVersion.c_str());
                else if (selfUpdateInfo_.state == ::psvitaalive::UpdateChecker::State::UpToDate)
                    sceClibSnprintf(ver, sizeof(ver), "Remote: up to date");
                else
                    sceClibSnprintf(ver, sizeof(ver), "Remote: check failed");
                vita2d_pgf_draw_text(font_, sideX + 14, contentTop + 260, TEXT, 0.50f, ver);
            }
        }
    }

    if (maxScroll > 1.f) {
        const float ratio = settingsScrollY_ / maxScroll;
        const int trackH = listViewH - 8;
        const int thumbH = std::max(24, trackH / 4);
        const int thumbY = contentTop + 4 + static_cast<int>(ratio * (trackH - thumbH));
        vita2d_draw_rectangle(listX + listW + 2, contentTop + 4, 3, trackH, BORDER);
        vita2d_draw_rectangle(listX + listW + 2, thumbY, 3, thumbH, ACCENT);
    }

    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT, 0.56f, "D-Pad: move   X / <>: change   O: save & back");
    drawToast();
    vita2d_end_drawing();
    vita2d_swap_buffers();

    // Expose row geometry for touch via static (same frame layout)
    // Stored for handleTouch — simple approach: recompute same math there.
    (void)rowY;
}

void FullCatalogScreen::handleInput(){if(isTransitioning())return;SceCtrlData p{};sceCtrlPeekBufferPositive(0,&p,1);static uint32_t prev=0;static uint64_t repeatAt=0;uint32_t mask=SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT,pressed=p.buttons&~prev,direct=pressed&mask;uint64_t now=sceKernelGetProcessTimeWide(),repeat=0;if((p.buttons&mask)==0)repeatAt=0;else if(direct)repeatAt=now+DIRECTION_REPEAT_DELAY_US;else if(repeatAt&&now>=repeatAt){repeat=p.buttons&mask;repeatAt=now+DIRECTION_REPEAT_INTERVAL_US;}prev=p.buttons;uint32_t nav=direct|repeat;if(state_.mode==UiMode::SETTINGS){handleSettingsInput(pressed,nav);return;}
if(pressed&SCE_CTRL_SELECT){openSettings();return;}
if(pressed&SCE_CTRL_START){
        if(installProgressActive_ && installOutcome_==0){
            showToast("A download/install is in progress",1800);
            return;
        }
        // After a successful self-update the running binary is stale — force exit.
        if(installProgressActive_ && installOutcome_==1 &&
           installProgressStage_.find("Self-update")!=std::string::npos){
            sceKernelExitProcess(0);
            return;
        }
        state_.requestExit=true;
        return;
    }if((pressed&SCE_CTRL_LTRIGGER)||(pressed&SCE_CTRL_RTRIGGER)){
        const bool canSwitch=!catalogLoading_&&!installProgressActive_&&!isTransitioning()
            &&catalogSwitchCooldownFrames_<=0&&deferredFreeTextures_.empty();
        if(canSwitch){
            if(pressed&SCE_CTRL_LTRIGGER)changeCatalog(-1);else changeCatalog(1);
        }else if(catalogLoading_){
            showToast("Cambiando catalogo...", 1000);
        }else if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty()){
            showToast("Please wait...", 900);
        }
        return;
    }if(installProgressActive_&&(pressed&SCE_CTRL_CIRCLE)){if(installOutcome_==1||installOutcome_==2){if(installAcknowledge_)installAcknowledge_();}else if(installCancel_)installCancel_();return;}if(catalogLoading_||installProgressActive_)return;if(pressed&SCE_CTRL_SQUARE){if(!searchQuery_.empty())applySearch("");return;}if(state_.mode==UiMode::FULL_CATALOG){if(pressed&SCE_CTRL_TRIANGLE){if(searchRequest_)applySearch(searchRequest_(searchQuery_));return;}if(nav&SCE_CTRL_LEFT&&state_.focusIndex%3>0)--state_.focusIndex;if(nav&SCE_CTRL_RIGHT&&state_.focusIndex%3<2&&state_.focusIndex+1<(int)catalogView().size())++state_.focusIndex;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);clampCatalogScroll();if(pressed&SCE_CTRL_CROSS)startOpeningDetail();return;}if(state_.mode!=UiMode::SPLIT_DETAIL)return;if(pressed&SCE_CTRL_TRIANGLE){if(searchRequest_)applySearch(searchRequest_(searchQuery_));return;}if(pressed&SCE_CTRL_CIRCLE){startClosingDetail();return;}if(state_.activePanel==UiPanel::Catalog){if(pressed&SCE_CTRL_RIGHT)state_.activePanel=UiPanel::Detail;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);return;}if(nav&SCE_CTRL_LEFT)state_.activePanel=UiPanel::Catalog;if(pressed&SCE_CTRL_TRIANGLE){if(state_.linkNavigation)exitLinkNavigation();else enterLinkNavigation();return;}if(state_.linkNavigation){if(nav&SCE_CTRL_UP)moveLinkFocus(0,-1);if(nav&SCE_CTRL_DOWN)moveLinkFocus(0,1);if(pressed&SCE_CTRL_CROSS)activateFocusedLink();return;}if(nav&SCE_CTRL_UP)moveDetailScroll(-1);if(nav&SCE_CTRL_DOWN)moveDetailScroll(1);}
unsigned FullCatalogScreen::colorForStatus(const std::string&s)const{if(s=="Verified")return ACCENT;if(s=="Legacy")return TEXT;if(s=="Archive")return DIM;return TEXT;}void FullCatalogScreen::drawHeader(int w){
    // Near-black bar + dual neon edge (LiveArea brand)
    vita2d_draw_rectangle(0, 0, w, HEADER_H, SURFACE2);
    vita2d_draw_rectangle(0, 0, w, 2, ACCENT);
    vita2d_draw_rectangle(0, HEADER_H - 1, w, 1, ACCENT_SOFT);
    // Brand: logo image (preferred) or compact text fallback
    int searchLeft = 200;
    if (headerLogoTex_) {
        const float lw = (float)vita2d_texture_get_width(headerLogoTex_);
        const float lh = (float)vita2d_texture_get_height(headerLogoTex_);
        const float maxH = (float)(HEADER_H - 10);
        const float maxW = 190.f;
        float sc = maxH / (lh > 1.f ? lh : 1.f);
        if (lw * sc > maxW) sc = maxW / (lw > 1.f ? lw : 1.f);
        const float dw = lw * sc;
        const float dh = lh * sc;
        const float dx = 10.f;
        const float dy = ((float)HEADER_H - dh) * 0.5f;
        vita2d_draw_texture_scale(headerLogoTex_, dx, dy, sc, sc);
        searchLeft = (int)(dx + dw + 12.f);
        if (searchLeft < 160) searchLeft = 160;
    } else {
        vita2d_pgf_draw_text(font_, 14, 28, ACCENT, 0.88f, "PSVitaAlive");
        searchLeft = 200;
    }
    // Center search field with neon frame
    const int barX = searchLeft, barY = 10, barH = 32;
    const int barW = w - barX - 100;
    vita2d_draw_rectangle(barX, barY, barW, barH, SURFACE);
    vita2d_draw_rectangle(barX - 1, barY - 1, barW + 2, 1, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX - 1, barY + barH, barW + 2, 1, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX - 1, barY - 1, 1, barH + 2, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX + barW, barY - 1, 1, barH + 2, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX, barY, barW, 1, RGBA8(0x3B, 0xFF, 0x00, 140));
    vita2d_draw_rectangle(barX, barY + barH - 1, barW, 1, RGBA8(0x3B, 0xFF, 0x00, 140));
    vita2d_draw_rectangle(barX, barY, 1, barH, RGBA8(0x3B, 0xFF, 0x00, 140));
    vita2d_draw_rectangle(barX + barW - 1, barY, 1, barH, RGBA8(0x3B, 0xFF, 0x00, 140));
    if (searchQuery_.empty()) {
        vita2d_pgf_draw_text(font_, barX + 12, barY + 21, DIM, 0.58f, "Search...  (△)");
    } else {
        vita2d_pgf_draw_text(font_, barX + 12, barY + 21, ACCENT, 0.56f, "FILTER");
        vita2d_pgf_draw_text(font_, barX + 70, barY + 21, WHITE, 0.58f, ellipsize(searchQuery_, 28).c_str());
        vita2d_pgf_draw_text(font_, barX + barW - 52, barY + 21, DIM, 0.52f, "□ clear");
    }
    {
        const std::string clock = currentTimeLabel();
        const int clockX = w - 16 - (int)clock.size() * 11;
        vita2d_pgf_draw_text(font_, clockX, 32, SILVER, 0.78f, clock.c_str());
    }
}

void FullCatalogScreen::drawTabs(int w){
    vita2d_draw_rectangle(0, HEADER_H, w, TABS_H, SURFACE2);
    vita2d_draw_rectangle(0, HEADER_H, w, 1, BORDER);
    float tw = (float)w / (int)CatalogType::Count;
    // Sliding neon underline under active tab
    vita2d_draw_rectangle((int)tabIndicatorX_ + 8, HEADER_H + TABS_H - 3, (int)tw - 16, 3, ACCENT);
    vita2d_draw_rectangle((int)tabIndicatorX_ + 8, HEADER_H + TABS_H - 5, (int)tw - 16, 2, ACCENT_SOFT);
    for (int i = 0; i < (int)CatalogType::Count; ++i) {
        int x = (int)(i * tw);
        bool a = (int)state_.catalog == i;
        if (a) {
            vita2d_draw_rectangle(x + 4, HEADER_H + 4, (int)tw - 8, TABS_H - 8, SURFACE);
        }
        vita2d_pgf_draw_text(font_, x + 12, HEADER_H + 24, a ? ACCENT : TEXT, a ? 0.86f : 0.76f,
                             catalogName((CatalogType)i));
    }
}
void FullCatalogScreen::prepareImageTexture(const std::string&url,const std::string&ns){
    static std::set<std::string> failedTextureLoads;
    if(!imageCache_||url.empty())return;
    std::string path=imageCache_->request(url,ns);
    if(imageCache_->isFailed(path)||!imageCache_->isReady(path))return;
    if(failedTextureLoads.find(path)!=failedTextureLoads.end())return;
    auto it=textures_.find(path);
    if(it!=textures_.end()){touchTexture(path);return;}
    evictTextureIfNeeded(ns);
    // Avoid decoding a file the worker may still be writing.
    SceIoStat stCheck={};
    if(sceIoGetstat(path.c_str(),&stCheck)<0||stCheck.st_size<=0)return;
    vita2d_texture*t=nullptr;
    const char*e=extOf(path);
    if(std::strcmp(e,".jpg")==0||std::strcmp(e,".jpeg")==0)t=vita2d_load_JPEG_file(path.c_str());
    else t=vita2d_load_PNG_file(path.c_str());
    if(!t){failedTextureLoads.insert(path);SceIoStat st={};long long sz=-1;if(sceIoGetstat(path.c_str(),&st)>=0)sz=(long long)st.st_size;char m[700];sceClibSnprintf(m,sizeof(m),"[UI] texture load failed ns=%s path=%s size=%lld",ns.c_str(),path.c_str(),sz);diagnostics::log(m);return;}
    textures_[path]=t;textureOrder_.push_back(path);
}
void FullCatalogScreen::prepareVisibleTextures(){
    if(!imageCache_||catalogLoading_||installProgressActive_)return;
    // Do not thrash GPU while frees are still draining or right after a catalog switch.
    if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty())return;

    std::unordered_set<std::string> keep;
    auto pathOnly=[&](const std::string& url, const char* ns)->std::string{
        if(url.empty())return {};
        return imageCache_->pathFor(url, ns);
    };
    auto markKeep=[&](const std::string& url, const char* ns){
        const std::string path=pathOnly(url, ns);
        if(!path.empty())keep.insert(path);
    };

    // At most one new GPU texture decode per frame (avoids hitch + free/load storms).
    constexpr int kLoadsPerFrame = 1;

    if(state_.mode==UiMode::FULL_CATALOG){
        const int first=std::max(0, state_.catalogScrollRow*3);
        const int last=std::min((int)catalogView().size(), first+9);
        for(int i=first;i<last;++i){
            const CatalogItem& it=catalogView()[i];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
        }
        // Drop GPU textures that scrolled away, then drop their download jobs too.
        releaseTexturesNotIn(keep);
        imageCache_->cancelQueuedExcept(keep);

        int loads=0;
        for(int i=first;i<last&&loads<kLoadsPerFrame;++i){
            const CatalogItem& it=catalogView()[i];
            const std::string& url=!it.icon.empty()?it.icon:it.cover;
            if(url.empty())continue;
            // Only enqueue download / decode for the current viewport.
            const size_t before=textures_.size();
            prepareImageTexture(url, "app");
            if(textures_.size()>before)++loads;
        }
        return;
    }

    if(state_.mode==UiMode::OPENING_DETAIL||state_.mode==UiMode::SPLIT_DETAIL||state_.mode==UiMode::CLOSING_DETAIL){
        const int first=std::max(0, state_.catalogScrollRow);
        const int last=std::min((int)catalogView().size(), state_.catalogScrollRow+visibleRowsSplit());
        for(int i=first;i<last;++i){
            const CatalogItem& it=catalogView()[i];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
        }
        const int sel=selectedIndex();
        if(sel>=0){
            const CatalogItem& it=catalogView()[sel];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
            const int panelX=SCREEN_W/2, panelY=HEADER_H+TABS_H;
            const int panelW=SCREEN_W-panelX, panelH=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;
            const int top=panelY+DETAIL_HEADER_H+10, bottom=panelY+panelH-10;
            const int scroll=std::max(0, (int)visualDetailScroll_);
            int mc=std::max(18, (panelW-36)/7);
            std::vector<std::string> pre;
            auto add=[&](const std::string& v){
                if(v.empty())return;
                pre.push_back("");
                std::vector<std::string> q; wrapText(v, mc, q);
                pre.insert(pre.end(), q.begin(), q.end());
                pre.push_back("");
            };
            add(it.description);
            add(it.longDescription);
            const int links=it.linkDetails.empty()?0:10+(int)it.linkDetails.size()*(LINK_ROW_H+LINK_GAP);
            const int shotTop=top+links+(int)pre.size()*LINE_H-scroll;
            const int shotH=SCREENSHOT_ROW_H-12;
            const int sc=std::min((int)it.screenshots.size(), 8);
            for(int k=0;k<sc;++k){
                const int sy=shotTop+k*SCREENSHOT_ROW_H;
                if(sy+shotH>top&&sy<bottom)markKeep(it.screenshots[k], "shot");
            }
        }
        releaseTexturesNotIn(keep);
        imageCache_->cancelQueuedExcept(keep);

        int loads=0;
        auto prepareOne=[&](const std::string& url, const char* ns){
            if(loads>=kLoadsPerFrame||url.empty())return;
            const size_t before=textures_.size();
            prepareImageTexture(url, ns);
            if(textures_.size()>before)++loads;
        };
        for(int i=first;i<last;++i){
            const CatalogItem& it=catalogView()[i];
            prepareOne(!it.icon.empty()?it.icon:it.cover, "app");
        }
        if(sel>=0){
            const CatalogItem& it=catalogView()[sel];
            const int panelX=SCREEN_W/2, panelY=HEADER_H+TABS_H;
            const int panelW=SCREEN_W-panelX, panelH=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;
            const int top=panelY+DETAIL_HEADER_H+10, bottom=panelY+panelH-10;
            const int scroll=std::max(0, (int)visualDetailScroll_);
            int mc=std::max(18, (panelW-36)/7);
            std::vector<std::string> pre;
            auto add=[&](const std::string& v){
                if(v.empty())return;
                pre.push_back("");
                std::vector<std::string> q; wrapText(v, mc, q);
                pre.insert(pre.end(), q.begin(), q.end());
                pre.push_back("");
            };
            add(it.description);
            add(it.longDescription);
            const int links=it.linkDetails.empty()?0:10+(int)it.linkDetails.size()*(LINK_ROW_H+LINK_GAP);
            const int shotTop=top+links+(int)pre.size()*LINE_H-scroll;
            const int shotH=SCREENSHOT_ROW_H-12;
            const int sc=std::min((int)it.screenshots.size(), 8);
            for(int k=0;k<sc;++k){
                const int sy=shotTop+k*SCREENSHOT_ROW_H;
                if(sy+shotH>top&&sy<bottom)prepareOne(it.screenshots[k], "shot");
            }
        }
        return;
    }

    releaseScreenshotTextures();
}

void FullCatalogScreen::drawImageLoadingPlaceholder(const std::string& url, const std::string& ns, int x, int y, int w, int h) {
    vita2d_draw_rectangle(x, y, w, h, SURFACE2);
    if (w < 8 || h < 6 || !imageCache_ || url.empty()) return;

    const std::string path = imageCache_->pathFor(url, ns);
    const auto prog = imageCache_->progress();
    float pct = 0.f;
    bool determinate = false;
    if (prog.active && !prog.localPath.empty() && prog.localPath == path && prog.total > 0) {
        pct = std::min(1.f, static_cast<float>(prog.downloaded) / static_cast<float>(prog.total));
        determinate = true;
    } else if (imageCache_->isPending(path)) {
        pct = focusPulse();
    } else {
        return; // not loading — nothing to show
    }

    const int pad = 3;
    const int barH = std::max(3, std::min(6, h / 8));
    const int barX = x + pad;
    const int barY = y + h - pad - barH;
    const int barW = std::max(1, w - pad * 2);
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    if (determinate) {
        vita2d_draw_rectangle(barX, barY, std::max(1, (int)(barW * pct)), barH, ACCENT);
    } else {
        const int slideW = std::max(8, barW / 3);
        const int slide = (int)((barW - slideW) * pct);
        vita2d_draw_rectangle(barX + slide, barY, slideW, barH, ACCENT);
    }
}

void FullCatalogScreen::drawImage(const std::string& url, const std::string& ns, int x, int y, int w, int h) {
    if (!imageCache_ || url.empty()) {
        vita2d_draw_rectangle(x, y, w, h, SURFACE2);
        return;
    }
    if (ns == "shot") {
        const int clipTop = HEADER_H + TABS_H + DETAIL_HEADER_H;
        const int clipBottom = SCREEN_H - FOOTER_H;
        if (y + h <= clipTop || y >= clipBottom) return;
    }

    // Resolve path without enqueueing. Downloads are only started from prepareVisibleTextures.
    const std::string path = imageCache_->pathFor(url, ns);
    if (path.empty()) {
        vita2d_draw_rectangle(x, y, w, h, SURFACE2);
        return;
    }

    if (imageCache_->isFailed(path)) {
        vita2d_draw_rectangle(x, y, w, h, SURFACE2);
        return;
    }

    if (!imageCache_->isReady(path)) {
        // Soft nudge: if file already on disk, request() will mark ready; else may queue once.
        // prepareVisibleTextures is the primary enqueue path for visible cells only.
        drawImageLoadingPlaceholder(url, ns, x, y, w, h);
        return;
    }

    auto it = textures_.find(path);
    if (it == textures_.end() || !it->second) {
        drawImageLoadingPlaceholder(url, ns, x, y, w, h);
        return;
    }
    vita2d_draw_rectangle(x, y, w, h, SURFACE2);
    touchTexture(path);
    vita2d_texture* t = it->second;
    float tw = (float)vita2d_texture_get_width(t), th = (float)vita2d_texture_get_height(t);
    if (tw <= 0 || th <= 0) return;
    float sc = std::min((float)w / tw, (float)h / th), dw = tw * sc, dh = th * sc;
    vita2d_draw_texture_scale(t, x + (w - dw) / 2.0f, y + (h - dh) / 2.0f, sc, sc);
}

float FullCatalogScreen::focusPulse() const {
    // 0..1 soft pulse for focused UI chrome (~1.2s period)
    const double t = static_cast<double>(sceKernelGetProcessTimeWide()) / 1000000.0;
    return 0.55f + 0.45f * static_cast<float>(std::sin(t * 5.2));
}


float FullCatalogScreen::easeInOut(float t) const {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    // Smoothstep-ish cubic
    return t * t * (3.f - 2.f * t);
}

void FullCatalogScreen::showToast(const std::string& message, uint64_t durationMs) {
    toastMessage_ = message;
    toastShownMs_ = sceKernelGetProcessTimeWide() / 1000ULL;
    toastExpiresMs_ = toastShownMs_ + durationMs;
}

void FullCatalogScreen::updateAnimations() {
    // Catalog list scroll (row units)
    const float targetCat = static_cast<float>(state_.catalogScrollRow);
    visualCatalogScroll_ += (targetCat - visualCatalogScroll_) * 0.18f;
    if (std::fabs(targetCat - visualCatalogScroll_) < 0.008f)
        visualCatalogScroll_ = targetCat;

    // Detail body scroll (pixel-ish line units) — same smoothing as catalog
    const float targetDet = static_cast<float>(state_.detailScroll);
    visualDetailScroll_ += (targetDet - visualDetailScroll_) * 0.18f;
    if (std::fabs(targetDet - visualDetailScroll_) < 0.02f)
        visualDetailScroll_ = targetDet;

    // Keep visual focus in sync with logical focus (no laggy card handoff).
    visualFocusIndex_ = static_cast<float>(state_.focusIndex);

    // When selection jumps to another app in detail mode, fade content briefly
    if (state_.mode == UiMode::SPLIT_DETAIL || state_.mode == UiMode::OPENING_DETAIL) {
        if (detailCrossfadeFrom_ != state_.focusIndex) {
            if (detailCrossfadeFrom_ >= 0)
                detailCrossfade_ = 0.25f;
            detailCrossfadeFrom_ = state_.focusIndex;
        }
        detailCrossfade_ += (1.f - detailCrossfade_) * 0.14f;
        if (detailCrossfade_ > 0.995f) detailCrossfade_ = 1.f;
    } else {
        detailCrossfade_ = 1.f;
        detailCrossfadeFrom_ = state_.focusIndex;
    }

    // Tab underline slides toward active catalog
    const float tabW = static_cast<float>(SCREEN_W) / static_cast<float>(CatalogType::Count);
    const float targetTabX = tabW * static_cast<float>(static_cast<int>(state_.catalog));
    if (tabIndicatorReady_ < 0.5f) {
        tabIndicatorX_ = targetTabX;
        tabIndicatorReady_ = 1.f;
    } else {
        tabIndicatorX_ += (targetTabX - tabIndicatorX_) * 0.18f;
    }

    // Soft fade-in after catalog data arrives
    if (catalogLoading_) {
        contentFade_ = 0.35f;
    } else {
        contentFade_ += (1.f - contentFade_) * 0.12f;
        if (contentFade_ > 0.995f) contentFade_ = 1.f;
    }


    // Catalog splash fade-out when loading finished
    if (catalogLoading_) {
        if (catalogSplashAlpha_ < 1.f) {
            catalogSplashAlpha_ += (1.f - catalogSplashAlpha_) * 0.25f;
            if (catalogSplashAlpha_ > 0.995f) catalogSplashAlpha_ = 1.f;
        }
    } else if (catalogSplashAlpha_ > 0.f) {
        catalogSplashAlpha_ *= 0.88f;
        catalogSplashAlpha_ -= 0.02f;
        if (catalogSplashAlpha_ < 0.02f) catalogSplashAlpha_ = 0.f;
    }

    // Settings open fade/slide
    if (state_.mode == UiMode::SETTINGS) {
        settingsEnter_ += (1.f - settingsEnter_) * 0.18f;
        if (settingsEnter_ > 0.995f) settingsEnter_ = 1.f;
        // target focus Y roughly matches row layout (updated in draw too)
        const float targetY = static_cast<float>(settingsFocus_);
        settingsFocusY_ += (targetY - settingsFocusY_) * 0.25f;
    } else {
        settingsEnter_ = 0.f;
    }
}

void FullCatalogScreen::drawToast() const {
    if (toastMessage_.empty() || toastExpiresMs_ == 0) return;
    const uint64_t now = sceKernelGetProcessTimeWide() / 1000ULL;
    if (now >= toastExpiresMs_) return;
    const uint64_t life = toastExpiresMs_ - toastShownMs_;
    const uint64_t age = now - toastShownMs_;
    float a = 1.f;
    if (age < 120) a = static_cast<float>(age) / 120.f;
    else if (toastExpiresMs_ - now < 200)
        a = static_cast<float>(toastExpiresMs_ - now) / 200.f;
    const unsigned alpha = static_cast<unsigned>(std::max(0.f, std::min(1.f, a)) * 230.f);
    const int tw = std::min(520, 40 + static_cast<int>(toastMessage_.size()) * 8);
    const int th = 40;
    const int x = (SCREEN_W - tw) / 2;
    const int y = SCREEN_H - FOOTER_H - th - 16;
    vita2d_draw_rectangle(x, y, tw, th, RGBA8(0x18, 0x18, 0x18, alpha));
    vita2d_draw_rectangle(x, y, tw, 2, RGBA8(0x3B, 0xFF, 0x00, alpha));
    vita2d_draw_rectangle(x, y + th - 1, tw, 1, RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(alpha * 0.5f)));
    if (font_)
        vita2d_pgf_draw_text(font_, x + 16, y + 26, RGBA8(255, 255, 255, alpha), 0.64f, toastMessage_.c_str());
}

void FullCatalogScreen::drawScrollFades(int x, int y, int width, int height) const {
    // Soft top/bottom vignette so scrolling content does not hard-clip against the panel edge.
    constexpr int steps = 14;
    for (int i = 0; i < steps; ++i) {
        const unsigned a = static_cast<unsigned>((steps - i) * (steps - i) * 180 / (steps * steps));
        const unsigned col = RGBA8(0, 0, 0, a);
        vita2d_draw_rectangle(x, y + i, width, 1, col);
        vita2d_draw_rectangle(x, y + height - 1 - i, width, 1, col);
    }
}

void FullCatalogScreen::drawActivePanelFrame(int x, int y, int width, int height, const char* label) const {
    const float pulse = focusPulse();
    const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(40 + pulse * 70));
    const unsigned solid = ACCENT;
    // Outer soft glow
    vita2d_draw_rectangle(x - 2, y - 2, width + 4, 2, glow);
    vita2d_draw_rectangle(x - 2, y + height, width + 4, 2, glow);
    vita2d_draw_rectangle(x - 2, y, 2, height, glow);
    vita2d_draw_rectangle(x + width, y, 2, height, glow);
    // Solid frame
    vita2d_draw_rectangle(x, y, width, 3, solid);
    vita2d_draw_rectangle(x, y + height - 3, width, 3, solid);
    vita2d_draw_rectangle(x, y, 3, height, solid);
    vita2d_draw_rectangle(x + width - 3, y, 3, height, solid);
    // Corner ticks
    const int tick = 14;
    vita2d_draw_rectangle(x, y, tick, 3, WHITE);
    vita2d_draw_rectangle(x, y, 3, tick, WHITE);
    vita2d_draw_rectangle(x + width - tick, y, tick, 3, WHITE);
    vita2d_draw_rectangle(x + width - 3, y, 3, tick, WHITE);
    vita2d_draw_rectangle(x, y + height - 3, tick, 3, WHITE);
    vita2d_draw_rectangle(x, y + height - tick, 3, tick, WHITE);
    vita2d_draw_rectangle(x + width - tick, y + height - 3, tick, 3, WHITE);
    vita2d_draw_rectangle(x + width - 3, y + height - tick, 3, tick, WHITE);
    // Label chip
    if (label && font_) {
        const int lw = 78, lh = 22;
        const int lx = x + 10, ly = y + 8;
        vita2d_draw_rectangle(lx, ly, lw, lh, solid);
        vita2d_draw_rectangle(lx, ly, lw, 1, WHITE);
        vita2d_pgf_draw_text(font_, lx + 10, ly + 16, BG, 0.58f, label);
    }
}




namespace {
bool readSfoStringKey(const std::string& path, const char* keyName, std::string& out) {
    out.clear();
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceIoStat st{};
    if (sceIoGetstat(path.c_str(), &st) < 0 || st.st_size < 0x14 || st.st_size > 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }
    std::string data(static_cast<size_t>(st.st_size), '\0');
    size_t done = 0;
    while (done < data.size()) {
        const int r = sceIoRead(fd, &data[done], data.size() - done);
        if (r <= 0) { sceIoClose(fd); return false; }
        done += static_cast<size_t>(r);
    }
    sceIoClose(fd);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
    if (std::memcmp(p, "\0PSF", 4) != 0) return false;
    auto u16 = [](const unsigned char* q) -> uint16_t {
        return static_cast<uint16_t>(q[0]) | static_cast<uint16_t>(q[1] << 8);
    };
    auto u32 = [](const unsigned char* q) -> uint32_t {
        return static_cast<uint32_t>(q[0]) | (static_cast<uint32_t>(q[1]) << 8) |
               (static_cast<uint32_t>(q[2]) << 16) | (static_cast<uint32_t>(q[3]) << 24);
    };
    const uint32_t keyTable = u32(p + 8);
    const uint32_t dataTable = u32(p + 12);
    const uint32_t count = u32(p + 16);
    if (keyTable >= data.size() || dataTable >= data.size()) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t entry = 0x14 + static_cast<size_t>(i) * 16;
        if (entry + 16 > data.size()) break;
        const uint16_t keyOff = u16(p + entry);
        const uint32_t dataLen = u32(p + entry + 4);
        const uint32_t dataOff = u32(p + entry + 12);
        const size_t keyPos = static_cast<size_t>(keyTable) + keyOff;
        const size_t valPos = static_cast<size_t>(dataTable) + dataOff;
        if (keyPos >= data.size() || valPos >= data.size()) continue;
        const char* key = reinterpret_cast<const char*>(p + keyPos);
        if (std::strcmp(key, keyName) != 0) continue;
        const size_t n = std::min(static_cast<size_t>(dataLen), data.size() - valPos);
        out.assign(reinterpret_cast<const char*>(p + valPos), n);
        while (!out.empty() && (out.back() == '\0' || out.back() == ' ')) out.pop_back();
        return !out.empty();
    }
    return false;
}

void versionParts(const std::string& s, int* parts, int maxParts) {
    for (int i = 0; i < maxParts; ++i) parts[i] = 0;
    int idx = 0;
    int cur = -1;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            if (cur < 0) cur = 0;
            cur = cur * 10 + (ch - '0');
        } else if (cur >= 0) {
            if (idx < maxParts) parts[idx++] = cur;
            cur = -1;
        }
    }
    if (cur >= 0 && idx < maxParts) parts[idx] = cur;
}

int compareVersionStrings(const std::string& installed, const std::string& catalog) {
    int a[4], b[4];
    versionParts(installed, a, 4);
    versionParts(catalog, b, 4);
    for (int i = 0; i < 4; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}
struct InstallProbeEntry {
    LocalInstallInfo info{};
    uint64_t checkedMs = 0;
    bool valid = false;
};

std::unordered_map<std::string, InstallProbeEntry>& installProbeCache() {
    static std::unordered_map<std::string, InstallProbeEntry> cache;
    return cache;
}

bool consumeInstallProbeBudget(uint64_t nowMs) {
    static uint64_t windowMs = 0;
    static unsigned probes = 0;
    constexpr uint64_t kWindowMs = 250;
    constexpr unsigned kMaxProbesPerWindow = 2;
    if (windowMs == 0 || nowMs < windowMs || (nowMs - windowMs) >= kWindowMs) {
        windowMs = nowMs;
        probes = 0;
    }
    if (probes >= kMaxProbesPerWindow) return false;
    ++probes;
    return true;
}

std::string installProbeKey(const std::string&titleId, const std::string&version) {
    return titleId + "\n" + version;
}

} // namespace (install-status helpers)

LocalInstallInfo FullCatalogScreen::queryLocalInstall(const CatalogItem& item) {
    LocalInstallInfo unknown;
    unknown.state = LocalInstallState::Unknown;
    if (item.titleId.empty()) return unknown;

    std::string tid = item.titleId;
    for (char& c : tid) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }

    const std::string key = installProbeKey(tid, item.version);
    const uint64_t nowMs = sceKernelGetProcessTimeWide() / 1000ULL;
    // Never probe the Vita filesystem while catalogs are loading or immediately after.
    if (catalogLoading_ || nowMs < installStatusWarmupUntilMs_) {
        return unknown;
    }
    auto& cache = installProbeCache();
    auto cached = cache.find(key);

    constexpr uint64_t kPositiveTtlMs = 8000;
    constexpr uint64_t kNegativeTtlMs = 4000;
    if (cached != cache.end() && cached->second.valid) {
        const uint64_t age = nowMs >= cached->second.checkedMs ? nowMs - cached->second.checkedMs : 0;
        const uint64_t ttl =
            (cached->second.info.state == LocalInstallState::NotInstalled)
                ? kNegativeTtlMs
                : kPositiveTtlMs;
        if (age < ttl) return cached->second.info;
    }

    if (!consumeInstallProbeBudget(nowMs)) {
        if (cached != cache.end() && cached->second.valid) return cached->second.info;
        return unknown;
    }

    LocalInstallInfo info;
    info.state = LocalInstallState::NotInstalled;

    const std::string candidates[] = {
        std::string("ux0:app/") + tid,
        (tid == item.titleId) ? std::string() : (std::string("ux0:app/") + item.titleId),
    };

    std::string paramPath;
    bool foundAppDir = false;
    bool hasParam = false;

    for (const std::string&dir : candidates) {
        if (dir.empty()) continue;
        SceIoStat dirStat{};
        if (sceIoGetstat(dir.c_str(), &dirStat) < 0) continue;
        foundAppDir = true;
        paramPath = dir + "/sce_sys/param.sfo";
        SceIoStat paramStat{};
        hasParam = sceIoGetstat(paramPath.c_str(), &paramStat) >= 0;
        break;
    }

    if (!foundAppDir) {
        info.state = LocalInstallState::NotInstalled;
    } else {
        info.state = LocalInstallState::Installed;
        if (hasParam) {
            std::string ver;
            if (!readSfoStringKey(paramPath, "APP_VER", ver))
                readSfoStringKey(paramPath, "VERSION", ver);
            info.installedVersion = ver;
            if (!ver.empty() && !item.version.empty() &&
                compareVersionStrings(ver, item.version) < 0) {
                info.state = LocalInstallState::UpdateAvailable;
            }
        }
    }

    InstallProbeEntry& entry = cache[key];
    entry.info = info;
    entry.checkedMs = nowMs;
    entry.valid = true;

    installStatusCache_[item.titleId] = info;
    if (tid != item.titleId) installStatusCache_[tid] = info;
    return info;
}

void FullCatalogScreen::invalidateInstallStatus(const std::string& titleId) {
    if (titleId.empty()) {
        installStatusCache_.clear();
        installProbeCache().clear();
        return;
    }

    installStatusCache_.erase(titleId);
    for (auto it = installProbeCache().begin(); it != installProbeCache().end();) {
        if (it->first == titleId || it->first.rfind(titleId + "\n", 0) == 0 ||
            (!titleId.empty() && it->first.rfind(titleId, 0) == 0)) {
            it = installProbeCache().erase(it);
        } else {
            ++it;
        }
    }
}

void FullCatalogScreen::drawInstallBadge(int x, int y, const LocalInstallInfo& info, bool compact) {
    if (info.state != LocalInstallState::Installed && info.state != LocalInstallState::UpdateAvailable)
        return;
    const bool upd = (info.state == LocalInstallState::UpdateAvailable);
    const char* label = upd ? (compact ? "UPD" : "UPDATE") : (compact ? "ON" : "INSTALLED");
    const unsigned bg = upd ? RGBA8(0xE0, 0x8A, 0x10, 255) : ACCENT;
    const unsigned fg = upd ? WHITE : BG;
    const float scale = compact ? 0.48f : 0.54f;
    const int tw = vita2d_pgf_text_width(font_, scale, label);
    const int padX = compact ? 5 : 7;
    const int bh = compact ? 16 : 18;
    const int bw = tw + padX * 2;
    vita2d_draw_rectangle(x, y, bw, bh, bg);
    vita2d_pgf_draw_text(font_, x + padX, y + (compact ? 12 : 13), fg, scale, label);
}

void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){
    const float pulse = focus ? focusPulse() : 0.f;
    // Subtle lift / scale for focused card
    int ox = focus ? -1 : 0;
    int oy = focus ? -1 : 0;
    int ww = focus ? w + 2 : w;
    int hh = focus ? h + 2 : h;
    const unsigned bg = focus ? SURFACE2 : SURFACE;
    vita2d_draw_rectangle(x + ox, y + oy, ww, hh, bg);
    // Brand accent rail on every card (stronger when focused)
    vita2d_draw_rectangle(x + ox, y + oy, focus ? 3 : 2, hh, focus ? ACCENT : ACCENT_SOFT);
    if (focus) {
        const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(55 + pulse * 100));
        vita2d_draw_rectangle(x + ox - 2, y + oy - 2, ww + 4, 2, glow);
        vita2d_draw_rectangle(x + ox - 2, y + oy + hh, ww + 4, 2, glow);
        vita2d_draw_rectangle(x + ox - 2, y + oy, 2, hh, glow);
        vita2d_draw_rectangle(x + ox + ww, y + oy, 2, hh, glow);
        const int bw = 2 + static_cast<int>(pulse * 2.0f);
        vita2d_draw_rectangle(x + ox, y + oy, ww, bw, ACCENT);
        vita2d_draw_rectangle(x + ox, y + oy + hh - bw, ww, bw, ACCENT);
        vita2d_draw_rectangle(x + ox, y + oy, bw, hh, ACCENT);
        vita2d_draw_rectangle(x + ox + ww - bw, y + oy, bw, hh, ACCENT);
    } else {
        vita2d_draw_rectangle(x, y, w, 1, BORDER);
    }
    int is = h >= 100 ? 76 : 54;
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 10 + ox, y + 9 + oy, is, is);
    int tx = x + is + 20 + ox;
    vita2d_pgf_draw_text(font_, tx, y + 25 + oy, WHITE, focus ? 0.80f : 0.76f, ellipsize(it.name, h >= 100 ? 24 : 22).c_str());
    vita2d_pgf_draw_text(font_, tx, y + 45 + oy, TEXT, 0.64f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 20).c_str());
    vita2d_pgf_draw_text(font_, tx, y + 64 + oy, colorForStatus(it.status), 0.62f, ellipsize(it.status, 16).c_str());
    // Version / date bottom-left; size always bottom-right when known (all catalogs).
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    if (!meta.empty())
        vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - 10 + oy, DIM, 0.56f, ellipsize(meta, 22).c_str());
    {
        // Bottom-right chips: size + optional Data / Game Files tags (stacked upward)
        {
            int sy = y + h - 10 + oy;
            const int right = x + ox + ww;
            const float sc = 0.54f;
            auto drawChip = [&](const std::string& label) {
                if (label.empty()) return;
                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());
                const int sx = right - tw - 8;
                vita2d_draw_rectangle(sx - 4, sy - 12, tw + 8, 16, SURFACE2);
                vita2d_pgf_draw_text(font_, sx, sy, TEXT, sc, label.c_str());
                sy -= 18;
            };
            const std::string sz = itemDisplaySize(it);
            if (!sz.empty()) drawChip(ellipsize(sz, 14));
            if (itemHasLinkType(it, "data files")) drawChip("Data");
            if (itemHasLinkType(it, "game files")) drawChip("Game Files");
        }
    }

    // Installed / update badge: bottom-left on icon + top-right of card
    {
        const LocalInstallInfo li = queryLocalInstall(it);
        if (li.state == LocalInstallState::Installed || li.state == LocalInstallState::UpdateAvailable) {
            // Overlay on icon corner (always visible even when title text is long)
            drawInstallBadge(x + 10 + ox, y + 9 + oy + is - 18, li, true);
            const char* lab = (li.state == LocalInstallState::UpdateAvailable) ? "UPD" : "ON";
            const float sc = 0.48f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            const int bw = tw + 10;
            drawInstallBadge(x + ox + ww - bw - 6, y + oy + 6, li, true);
        }
    }
    (void)idx;
}
void FullCatalogScreen::drawCatalogPanel(int x,int y,int w,int h,bool split){
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, 2, h, ACCENT_SOFT);
    const unsigned dimPanel = contentFade_ < 0.99f ? RGBA8(0,0,0, static_cast<unsigned>((1.f-contentFade_)*90)) : 0;

    // Clip cards to the panel so smooth scroll never spills outside the frame
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 1, y + 1, x + w - 1, y + h - 1);

    if (!split) {
        const int vis = visibleRowsFull();
        const int rows = totalRows();
        const int cw = (w - GRID_PAD * 2 - CARD_GAP * 2) / 3;
        const float rowH = static_cast<float>(FULL_CARD_H + CARD_GAP);
        const int baseRow = std::max(0, static_cast<int>(std::floor(visualCatalogScroll_)) - 0);
        for (int r = -1; r <= vis; ++r) {
            for (int c = 0; c < 3; ++c) {
                const int i = (baseRow + r) * 3 + c;
                if (i < 0 || i >= (int)catalogView().size()) continue;
                const float fy = static_cast<float>(y + GRID_PAD) + (static_cast<float>(baseRow + r) - visualCatalogScroll_) * rowH;
                if (fy + FULL_CARD_H < y || fy > y + h) continue;
                drawCatalogCard(catalogView()[i], i, x + GRID_PAD + c * (cw + CARD_GAP), static_cast<int>(fy), cw, FULL_CARD_H, i == state_.focusIndex);
            }
        }
        vita2d_disable_clipping();
        if (rows > vis) {
            int tx = x + w - 8, ty = y + 12, th = h - 24;
            vita2d_draw_rectangle(tx, ty, 4, th, BORDER);
            int thumb = std::max(24, th * vis / std::max(1, rows));
            int mr = std::max(1, rows - vis);
            float scrollT = std::min(1.f, visualCatalogScroll_ / static_cast<float>(mr));
            int yy = ty + static_cast<int>((th - thumb) * scrollT);
            vita2d_draw_rectangle(tx, yy, 4, thumb, ACCENT);
        }
        drawScrollFades(x, y, w, h);
    } else {
        const int vis = visibleRowsSplit();
        const float rowH = static_cast<float>(SPLIT_CARD_H + CARD_GAP);
        const int baseRow = std::max(0, static_cast<int>(std::floor(visualCatalogScroll_)));
        for (int r = -1; r <= vis; ++r) {
            const int i = baseRow + r;
            if (i < 0 || i >= (int)catalogView().size()) continue;
            const float fy = static_cast<float>(y + GRID_PAD) + (static_cast<float>(i) - visualCatalogScroll_) * rowH;
            if (fy + SPLIT_CARD_H < y || fy > y + h) continue;
            drawCatalogCard(catalogView()[i], i, x + GRID_PAD, static_cast<int>(fy), w - GRID_PAD * 2 - 4, SPLIT_CARD_H, i == state_.focusIndex);
        }
        vita2d_disable_clipping();
        const int total = (int)catalogView().size();
        if (total > vis) {
            int tx = x + w - 8, ty = y + 12, th = h - 24;
            vita2d_draw_rectangle(tx, ty, 4, th, BORDER);
            int thumb = std::max(24, th * vis / std::max(1, total));
            int mr = std::max(1, total - vis);
            float scrollT = std::min(1.f, visualCatalogScroll_ / static_cast<float>(mr));
            int yy = ty + static_cast<int>((th - thumb) * scrollT);
            vita2d_draw_rectangle(tx, yy, 4, thumb, ACCENT);
        }
        drawScrollFades(x, y, w, h);
        if (state_.mode == UiMode::SPLIT_DETAIL && state_.activePanel == UiPanel::Catalog)
            drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4, "LIST");
    }
    if (dimPanel)
        vita2d_draw_rectangle(x, y, w, h, dimPanel);
}


void FullCatalogScreen::wrapText(const std::string&t,int max,std::vector<std::string>&out)const{out.clear();std::string cur;for(char c:t){if(c=='\n'){out.push_back(cur);cur.clear();continue;}if((int)cur.size()>=max&&c==' '){out.push_back(cur);cur.clear();continue;}cur.push_back(c);if((int)cur.size()>=max){out.push_back(cur);cur.clear();}}if(!cur.empty())out.push_back(cur);}void FullCatalogScreen::drawTextLines(const std::vector<std::string>&l,int x,int y,int lh,unsigned col,float sc,int start,int max,int top,int bottom){int first=std::max(0,start),last=std::min((int)l.size(),first+max),dy=y+first*lh;for(int i=first;i<last;++i){if(dy>=top&&dy<=bottom)vita2d_pgf_draw_text(font_,x,dy,col,sc,l[i].c_str());dy+=lh;}}
void FullCatalogScreen::drawDetailLinks(const CatalogItem& it, int x, int y, int w, int& heightOut) {
    heightOut = 0;
    const auto idxs = downloadLinkIndices(it);
    if (idxs.empty()) return;
    for (size_t row = 0; row < idxs.size(); ++row) {
        const int li = idxs[row];
        const CatalogLink& l = it.linkDetails[li];
        const bool f = state_.linkNavigation && state_.linkFocus == (int)row;
        const bool can = actionableLink(l);
        const int ry = y + (int)row * (LINK_ROW_H + LINK_GAP);
        vita2d_draw_rectangle(x, ry, w, LINK_ROW_H, f ? ACCENT : SURFACE2);
        vita2d_draw_rectangle(x, ry, w, 1, f ? ACCENT : BORDER);
        const unsigned mc = f ? BG : (can ? WHITE : TEXT);
        std::string title = l.name.empty() ? l.type : l.name;
        const std::string sizeLabel = formatLinkSizeLabel(l, it);
        const int badgeW = l.recommended ? 96 : 0;
        vita2d_pgf_draw_text(font_, x + 10, ry + 15, mc, 0.64f, ellipsize(title, badgeW ? 18 : 26).c_str());
        std::string meta = "Download";
        if (!sizeLabel.empty()) meta += "  •  " + sizeLabel;
        if (can) meta += f ? "  •  X: instalar" : "  •  X";
        vita2d_pgf_draw_text(font_, x + 10, ry + 31, f ? BG : DIM, 0.50f, ellipsize(meta, badgeW ? 28 : 42).c_str());
        if (l.recommended) {
            const int bx = x + w - badgeW - 8, by = ry + 8;
            vita2d_draw_rectangle(bx, by, badgeW, 22, f ? BG : ACCENT);
            vita2d_pgf_draw_text(font_, bx + 6, by + 15, f ? ACCENT : BG, 0.50f, "Recommended");
        }
    }
    heightOut = 10 + (int)idxs.size() * (LINK_ROW_H + LINK_GAP);
}
void FullCatalogScreen::drawDetailContent(const CatalogItem&it,int x,int y,int w,int h){
if(detailCrossfade_<0.99f){vita2d_draw_rectangle(x,y,w,h,RGBA8(0,0,0,static_cast<unsigned>((1.f-detailCrossfade_)*140)));}
int cx=x+18,cw=w-36,mc=std::max(18,cw/7),top=y+DETAIL_HEADER_H+10,bottom=y+h-10;float scroll=std::max(0.f,visualDetailScroll_);std::vector<std::string>pre,post;auto add=[&](std::vector<std::string>&l,const char*t,const std::string&v){if(v.empty())return;l.push_back(t);std::vector<std::string>q;wrapText(v,mc,q);for(auto&s:q)l.push_back(s);l.push_back("");};add(pre,"Description",it.description);add(pre,"Long Description",it.longDescription);add(post,"Requirements",it.requirements);post.push_back("Information");post.push_back("Title ID: "+it.titleId);post.push_back("Version: "+it.version);{const LocalInstallInfo li=queryLocalInstall(it);if(li.state==LocalInstallState::Installed||li.state==LocalInstallState::UpdateAvailable){std::string line=li.state==LocalInstallState::UpdateAvailable?"Install: update available":"Install: installed";if(!li.installedVersion.empty())line+=" (local v"+li.installedVersion+")";post.push_back(line);}else if(!it.titleId.empty())post.push_back("Install: not installed");}post.push_back("Release date: "+it.versionDate);post.push_back("Category: "+it.category);post.push_back("Subcategory: "+it.subcategory);post.push_back("Size: "+it.size);post.push_back("Status: "+it.status);post.push_back("");add(post,"Changelog",it.changelog);int sc=std::min(5,(int)it.screenshots.size()),shotH=sc*SCREENSHOT_ROW_H,links=0;vita2d_enable_clipping();vita2d_set_clip_rectangle(x+2,top,x+w-18,bottom);drawDetailLinks(it,cx,top-scroll,cw,links);int preTop=top+links-scroll;drawTextLines(pre,cx,preTop,LINE_H,TEXT,.66f,0,(int)pre.size(),top,bottom);int shotTop=preTop+(int)pre.size()*LINE_H;for(int i=0;i<sc;++i)drawImage(it.screenshots[i],"shot",cx,shotTop+i*SCREENSHOT_ROW_H,cw,SCREENSHOT_ROW_H-18);int postTop=shotTop+shotH+8;drawTextLines(post,cx,postTop,LINE_H,TEXT,.66f,0,(int)post.size(),top,bottom);vita2d_disable_clipping();int total=detailContentHeight(it,w),vis=std::max(1,h-DETAIL_HEADER_H-18),mx=std::max(0,total-vis);if(mx>0){int tx=x+w-8,ty=y+DETAIL_HEADER_H+8,th=h-DETAIL_HEADER_H-18;vita2d_draw_rectangle(tx,ty,3,th,BORDER);int thumb=std::max(20,th*vis/std::max(1,total));int yy=ty+(int)((th-thumb)*(scroll/std::max(1.f,(float)mx)));vita2d_draw_rectangle(tx,yy,3,thumb,ACCENT);}}
void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, 2, h, ACCENT_SOFT);
    vita2d_draw_rectangle(x, y, w, 1, ACCENT_SOFT);
    int i = selectedIndex();
    if (i < 0) return;
    const CatalogItem& it = catalogView()[i];
    const bool active = (state_.mode == UiMode::SPLIT_DETAIL && state_.activePanel == UiPanel::Detail);
    vita2d_draw_rectangle(x, y, w, DETAIL_HEADER_H, SURFACE);
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 12, y + 12, 68, 68);
    // Leave room for active-panel label chip on the left when focused
    const int titleX = active ? x + 100 : x + 92;
    vita2d_pgf_draw_text(font_, titleX, y + 29, WHITE, 0.82f, ellipsize(it.name, active ? 18 : 24).c_str());
    vita2d_pgf_draw_text(font_, titleX, y + 50, TEXT, 0.64f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 20).c_str());
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    vita2d_pgf_draw_text(font_, titleX, y + 70, colorForStatus(it.status), 0.60f, ellipsize(meta.empty() ? it.status : meta, 22).c_str());

    {
        const LocalInstallInfo li = queryLocalInstall(it);
        if (li.state == LocalInstallState::Installed || li.state == LocalInstallState::UpdateAvailable) {
            const char* lab = (li.state == LocalInstallState::UpdateAvailable) ? "UPDATE" : "INSTALLED";
            const float sc = 0.54f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            const int bw = tw + 14;
            drawInstallBadge(x + w - bw - 10, y + 12, li, false);
            if (!li.installedVersion.empty()) {
                char iv[48];
                sceClibSnprintf(iv, sizeof(iv), "Local v%s", li.installedVersion.c_str());
                const int lw = vita2d_pgf_text_width(font_, 0.48f, iv);
                vita2d_pgf_draw_text(font_, x + w - lw - 12, y + 36, DIM, 0.48f, iv);
            }
        }
    }
    if (!it.linkDetails.empty()) {
        int bx = x + w - 142, by = y + 12, bw = 128, bh = 28;
        const bool linkOn = state_.linkNavigation;
        const float pulse = linkOn ? focusPulse() : 0.f;
        vita2d_draw_rectangle(bx, by, bw, bh, linkOn ? ACCENT : SURFACE2);
        if (linkOn) {
            const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(40 + pulse * 80));
            vita2d_draw_rectangle(bx - 2, by - 2, bw + 4, bh + 4, glow);
            vita2d_draw_rectangle(bx, by, bw, bh, ACCENT);
        }
        vita2d_draw_rectangle(bx, by, bw, 1, ACCENT);
        vita2d_draw_rectangle(bx, by, 1, bh, ACCENT);
        vita2d_draw_rectangle(bx, by + bh - 1, bw, 1, ACCENT);
        vita2d_draw_rectangle(bx + bw - 1, by, 1, bh, ACCENT);
        vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");
    }
    drawDetailContent(it, x, y, w, h);
    // Fade over scrollable body (below header)
    drawScrollFades(x, y + DETAIL_HEADER_H, w, std::max(1, h - DETAIL_HEADER_H));
    if (active)
        drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4, "DETAIL");
}
void FullCatalogScreen::drawLoadingOverlay(){
// Catalog load/download at startup: full-screen brand image + progress (not used for installs).
if (catalogSplashAlpha_ > 0.01f && !installProgressActive_) {
    const unsigned a = (unsigned)(catalogSplashAlpha_ * 255.f);
    if (a > 255) { /* clamp */ }
    const unsigned tint = RGBA8(255, 255, 255, a > 255 ? 255 : a);
    if (catalogLoadingTex_) {
        const float tw = (float)vita2d_texture_get_width(catalogLoadingTex_);
        const float th = (float)vita2d_texture_get_height(catalogLoadingTex_);
        const float sx = (tw > 1.f) ? (SCREEN_W / tw) : 1.f;
        const float sy = (th > 1.f) ? (SCREEN_H / th) : 1.f;
        vita2d_draw_texture_tint_scale(catalogLoadingTex_, 0.f, 0.f, sx, sy, tint);
    } else {
        vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0x0A, 0x0A, 0x0A, a > 255 ? 255 : a));
    }
    // Bottom progress panel — clear hierarchy for startup / self-update / catalogs
    const int stripH = 148;
    const int stripY = SCREEN_H - stripH;
    const int barX = 48, barW = SCREEN_W - 96, barH = 16;
    const int barY = SCREEN_H - 40;
    const unsigned panelA = (unsigned)(catalogSplashAlpha_ * 230.f);
    const unsigned ta = (unsigned)(catalogSplashAlpha_ * 255.f);
    vita2d_draw_rectangle(0, stripY, SCREEN_W, stripH, RGBA8(0x08, 0x08, 0x0A, panelA > 255 ? 255 : panelA));
    vita2d_draw_rectangle(0, stripY, SCREEN_W, 3, RGBA8(0x3B, 0xFF, 0x00, ta > 255 ? 255 : ta));

    std::string phase = catalogLoadingLabel_.empty() ? "Startup" : catalogLoadingLabel_;
    vita2d_pgf_draw_text(font_, barX, stripY + 28, ACCENT, 0.78f, ellipsize(phase, 40).c_str());

    std::string detail = catalogLoadingMessage_.empty() ? "Please wait..." : catalogLoadingMessage_;
    vita2d_pgf_draw_text(font_, barX, stripY + 52, WHITE, 0.58f, ellipsize(detail, 78).c_str());

    if (catalogLoadingTotal_ > 0) {
        char sizeLine[96];
        if (catalogLoadingTotal_ >= 8192ULL) {
            const double curMb = (double)catalogLoadingCurrent_ / (1024.0 * 1024.0);
            const double totMb = (double)catalogLoadingTotal_ / (1024.0 * 1024.0);
            sceClibSnprintf(sizeLine, sizeof(sizeLine), "%.2f MB  /  %.2f MB", curMb, totMb);
        } else {
            sceClibSnprintf(sizeLine, sizeof(sizeLine), "%llu  /  %llu",
                (unsigned long long)catalogLoadingCurrent_,
                (unsigned long long)catalogLoadingTotal_);
        }
        vita2d_pgf_draw_text(font_, barX, stripY + 74, TEXT, 0.52f, sizeLine);
    }

    vita2d_draw_rectangle(barX, barY, barW, barH, SURFACE);
    vita2d_draw_rectangle(barX, barY, barW, 1, ACCENT_SOFT);
    vita2d_draw_rectangle(barX, barY + barH - 1, barW, 1, ACCENT_SOFT);
    float pct = 0.f;
    if (catalogLoadingTotal_ > 0) {
        pct = (float)catalogLoadingCurrent_ / (float)catalogLoadingTotal_;
        if (pct < 0.f) pct = 0.f;
        if (pct > 1.f) pct = 1.f;
    } else {
        const float t = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1200) / 1200.f;
        pct = 0.15f + 0.35f * (t < 0.5f ? (t * 2.f) : (2.f - t * 2.f));
    }
    const int fill = (int)(barW * pct);
    if (fill > 0) vita2d_draw_rectangle(barX, barY, fill, barH, ACCENT);
    char pctBuf[32];
    if (catalogLoadingTotal_ > 0)
        sceClibSnprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)(pct * 100.f + 0.5f));
    else
        sceClibSnprintf(pctBuf, sizeof(pctBuf), "...");
    const int pctW = vita2d_pgf_text_width(font_, 0.58f, pctBuf);
    vita2d_pgf_draw_text(font_, barX + barW - pctW, barY - 18, WHITE, 0.58f, pctBuf);
    return;
}


const unsigned RED=RGBA8(0xE0,0x32,0x32,255), GREEN=RGBA8(0x3B,0xD9,0x60,255), BLACK=RGBA8(0,0,0,255);
const int w=640,h=380,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;
vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,120));
vita2d_draw_rectangle(x,y,w,h,PANEL);
const unsigned edge=(installOutcome_==2)?RED:((installOutcome_==1)?GREEN:ACCENT);
vita2d_draw_rectangle(x,y,w,3,edge);
vita2d_draw_rectangle(x,y+3,3,h-6,edge);
vita2d_draw_rectangle(x+w-3,y+3,3,h-6,BORDER);
vita2d_draw_rectangle(x,y+h-3,w,3,BORDER);
vita2d_pgf_draw_text(font_,x+28,y+36,edge,.68f,"PSVitaAlive");

if(installOutcome_==1){
  const bool zipExtract =
      !installLiveAreaOk_ &&
      installResultTitleId_.empty() &&
      !installResultPath_.empty() &&
      (installResultPath_.find("ux0:app/") != 0);
  if (zipExtract) {
    vita2d_pgf_draw_text(font_,x+28,y+80,GREEN,1.05f,"ZIP extraction complete");
    std::string file=installProgressFile_.empty()?"(archive)":ellipsize(installProgressFile_,70);
    vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("File: "+file).c_str());
    vita2d_pgf_draw_text(font_,x+28,y+150,TEXT,.62f,("Extracted to: "+ellipsize(installResultPath_,58)).c_str());
    vita2d_pgf_draw_text(font_,x+28,y+182,DIM,.56f,"No LiveArea bubble — files only");
    if(!installProgressMessage_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+214,DIM,.54f,ellipsize(installProgressMessage_,78).c_str());
  } else {
    vita2d_pgf_draw_text(font_,x+28,y+80,GREEN,1.05f,"Installation complete");
    std::string file=installProgressFile_.empty()?"(file)":ellipsize(installProgressFile_,70);
    vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("File: "+file).c_str());
    if(!installResultTitleId_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+144,TEXT,.60f,("Title ID: "+installResultTitleId_).c_str());
    if(!installResultPath_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.60f,("Path: "+ellipsize(installResultPath_,62)).c_str());
    if(installLiveAreaOk_)
      vita2d_pgf_draw_text(font_,x+28,y+200,GREEN,.72f,"LiveArea: OK — app bubble verified");
    else if(!installResultPath_.empty() && installResultPath_.find("ux0:app/")==0)
      vita2d_pgf_draw_text(font_,x+28,y+200,RGBA8(0xFF,0xC0,0x40,255),.68f,"LiveArea: not confirmed yet");
    else
      vita2d_pgf_draw_text(font_,x+28,y+200,TEXT,.62f,"LiveArea: N/A for this content");
    if(!installProgressMessage_.empty())
      vita2d_pgf_draw_text(font_,x+28,y+232,DIM,.54f,ellipsize(installProgressMessage_,78).c_str());
  }

    const int by2=y+300,bw2=280,bh2=40;
  vita2d_draw_rectangle(x+28,by2,bw2,bh2,GREEN);
  vita2d_pgf_draw_text(font_,x+100,by2+26,BLACK,.62f,"O  Continuar");
  vita2d_pgf_draw_text(font_,x+28,y+h-16,DIM,.50f,"Circle: cerrar este mensaje");
  return;
}

if(installOutcome_==2){
  vita2d_pgf_draw_text(font_,x+28,y+80,RED,1.05f,"No se pudo instalar");
  std::string file=installProgressFile_.empty()?"(archivo)":ellipsize(installProgressFile_,70);
  vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("Archivo: "+file).c_str());
  vita2d_pgf_draw_text(font_,x+28,y+152,RED,.68f,"Motivo:");
  std::string err=installProgressMessage_.empty()?"Error desconocido":installProgressMessage_;
  vita2d_pgf_draw_text(font_,x+28,y+180,TEXT,.60f,ellipsize(err,78).c_str());
  if(err.size()>78)
    vita2d_pgf_draw_text(font_,x+28,y+202,TEXT,.58f,ellipsize(err.substr(70),78).c_str());
  vita2d_pgf_draw_text(font_,x+28,y+236,DIM,.54f,"Revisa espacio libre, formato y session.log");
  vita2d_pgf_draw_text(font_,x+28,y+258,DIM,.52f,"ux0:data/psvitaalive/logs/session.log");
  const int by2=y+300,bw2=280,bh2=40;
  vita2d_draw_rectangle(x+28,by2,bw2,bh2,RED);
  vita2d_pgf_draw_text(font_,x+110,by2+26,WHITE,.62f,"O  Cerrar");
  vita2d_pgf_draw_text(font_,x+28,y+h-16,DIM,.50f,"Circle: cerrar este mensaje");
  return;
}

if(catalogLoading_){
  vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,catalogLoadingLabel_.empty()?"Loading catalog":catalogLoadingLabel_.c_str());
  vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.60f,catalogLoadingMessage_.empty()?"Preparing...":catalogLoadingMessage_.c_str());
  uint64_t pct=catalogLoadingTotal_?std::min<uint64_t>(100,(catalogLoadingCurrent_*100)/catalogLoadingTotal_):0;
  int bx=x+28,by=y+140,bw=w-56,bh=12;
  vita2d_draw_rectangle(bx,by,bw,bh,BORDER);
  vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);
  char st[160];
  sceClibSnprintf(st,sizeof(st),"%llu%%  %llu / %llu",(unsigned long long)pct,(unsigned long long)catalogLoadingCurrent_,(unsigned long long)catalogLoadingTotal_);
  vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,st);
  return;
}

vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,(installProgressStage_=="Downloading"||installProgressStage_=="Cancelling"||installProgressStage_.empty())?"Downloading":"Installing");
std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,72);
vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.62f,file.c_str());
const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);
const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;
int bx=x+28,by=y+140,bw=w-56,bh=12;
vita2d_draw_rectangle(bx,by,bw,bh,BORDER);
vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);
char stats[220];
sceClibSnprintf(stats,sizeof(stats),"%llu%%  %s / %s  •  %s/s",(unsigned long long)pct,formatBytes(current).c_str(),total?formatBytes(total).c_str():"?",formatBytes(installProgressSpeed_).c_str());
vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,stats);
uint64_t eta=0;if(installProgressSpeed_>0&&total>current)eta=(total-current)/installProgressSpeed_;
char info[180];
sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: %s",formatEta(eta).c_str());
vita2d_pgf_draw_text(font_,x+28,y+194,ACCENT,.62f,info);
if(!installProgressMessage_.empty())vita2d_pgf_draw_text(font_,x+28,y+222,DIM,.54f,ellipsize(installProgressMessage_,82).c_str());
const int by2=y+268,bw2=330,bh2=40;
vita2d_draw_rectangle(x+28,by2,bw2,bh2,SURFACE2);
vita2d_draw_rectangle(x+28,by2,bw2,1,BORDER);
vita2d_pgf_draw_text(font_,x+92,by2+26,WHITE,.62f,"CIRCLE  CANCEL DOWNLOAD");
vita2d_pgf_draw_text(font_,x+28,y+h-14,DIM,.50f,"Circle: Cancel download and remove incomplete file");
}

void drawFooterBar(vita2d_pgf* font, const char* leftHints) {
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, 2, ACCENT);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H + 2, SCREEN_W, 1, ACCENT_SOFT);
    if (leftHints && font)
        vita2d_pgf_draw_text(font, 12, SCREEN_H - 14, TEXT, 0.50f, leftHints);
    if (!font) return;

    const Ux0SpaceInfo sp = queryUx0Space();
    // Wider panel + larger type for readability on real Vita screens.
    const int panelW = 220;
    const int panelH = FOOTER_H - 6;
    const int panelX = SCREEN_W - panelW - 6;
    const int panelY = SCREEN_H - FOOTER_H + 3;
    vita2d_draw_rectangle(panelX, panelY, panelW, panelH, SURFACE);
    vita2d_draw_rectangle(panelX, panelY, 3, panelH, ACCENT);

    if (!sp.ok) {
        vita2d_pgf_draw_text(font, panelX + 12, panelY + 20, DIM, 0.58f, "ux0 n/a");
        return;
    }
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 15, ACCENT, 0.56f, "UX0");
    char line[48];
    sceClibSnprintf(line, sizeof(line), "%s free", formatBytesShort(sp.freeBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 48, panelY + 15, WHITE, 0.56f, line);
    sceClibSnprintf(line, sizeof(line), "of %s total", formatBytesShort(sp.totalBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 30, TEXT, 0.52f, line);

    const int barX = panelX + 10, barY = panelY + panelH - 8, barW = panelW - 20, barH = 5;
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    float used = 0.f;
    if (sp.totalBytes > 0)
        used = 1.f - (float)((double)sp.freeBytes / (double)sp.totalBytes);
    if (used < 0.f) used = 0.f;
    if (used > 1.f) used = 1.f;
    const unsigned fill = used > 0.90f ? RGBA8(0xE0, 0x32, 0x32, 255)
                        : (used > 0.75f ? RGBA8(0xFF, 0xB0, 0x20, 255) : ACCENT);
    vita2d_draw_rectangle(barX, barY, std::max(1, (int)(barW * used)), barH, fill);
}
void FullCatalogScreen::drawFullCatalog(){vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);drawCatalogPanel(0,HEADER_H+TABS_H,SCREEN_W,SCREEN_H-HEADER_H-TABS_H-FOOTER_H,false);drawFooterBar(font_, "D-Pad: Nav   X: Detail   △: Search   SELECT: Settings   L/R: Catalog   START: Exit");if(catalogLoading_||installProgressActive_||catalogSplashAlpha_>0.01f)drawLoadingOverlay();if(!catalogError_.empty())vita2d_pgf_draw_text(font_,18,HEADER_H+TABS_H+26,ACCENT,.66f,catalogError_.c_str());drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawSplitDetail(){vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H,lw=SCREEN_W/2;drawCatalogPanel(0,top,lw,hh,true);drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_draw_rectangle(lw-1,top,2,hh,BORDER);drawFooterBar(font_, state_.activePanel==UiPanel::Catalog?"PANEL: LIST  |  → Detail   D-Pad: Navigate   O: Back   L/R: Catalog":"PANEL: DETAIL  |  ← List   D-Pad: Scroll   △: Links   X: Action   O: Back");if(catalogLoading_||installProgressActive_||catalogSplashAlpha_>0.01f)drawLoadingOverlay();drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawOpeningDetail(){float p=transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,rw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawClosingDetail(){float p=1.0f-transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_set_clear_color(BG);vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::draw(){switch(state_.mode){case UiMode::FULL_CATALOG:drawFullCatalog();break;case UiMode::OPENING_DETAIL:drawOpeningDetail();break;case UiMode::SPLIT_DETAIL:drawSplitDetail();break;case UiMode::CLOSING_DETAIL:drawClosingDetail();break;case UiMode::SETTINGS:drawSettings();break;}}bool FullCatalogScreen::updateAndDraw(){
    if(!ready_)return false;
    flushDeferredTextureFrees();
    if(catalogSwitchCooldownFrames_>0)--catalogSwitchCooldownFrames_;
    // Expire toast
    if(toastExpiresMs_!=0){
        const uint64_t now=sceKernelGetProcessTimeWide()/1000ULL;
        if(now>=toastExpiresMs_){toastMessage_.clear();toastExpiresMs_=0;}
    }
    handleInput();
    handleTouch();
    updateTransition();
    pollSelfUpdateProgress();
    updateAnimations();
    if(catalogSwitchCooldownFrames_==0)prepareVisibleTextures();
    draw();
    return !state_.requestExit;
}
} // namespace psvitaalive::ui
