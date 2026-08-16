#include "ui/full_catalog_screen.hpp"
#include "diagnostic_logger.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <set>
#include <unordered_set>
#include <utility>
namespace psvitaalive::ui { namespace {
constexpr unsigned BG=RGBA8(0,0,0,255),SURFACE=RGBA8(0x37,0x37,0x37,255),SURFACE2=RGBA8(0x2A,0x2A,0x2A,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),PANEL=RGBA8(0x20,0x20,0x20,255);
constexpr int FULL_CARD_H=120,SPLIT_CARD_H=82,DETAIL_HEADER_H=92,LINE_H=18,TRANSITION_MS=340,LINK_ROW_H=38,LINK_GAP=6,SCREENSHOT_ROW_H=250;
constexpr size_t MAX_APP_TEXTURES=18,MAX_SCREENSHOT_TEXTURES=6;
constexpr int CATALOG_SWITCH_COOLDOWN_FRAMES=40; // ~0.66s at 60fps
constexpr uint64_t CATALOG_SWITCH_MIN_MS=700; // hard debounce against L/R spam
constexpr size_t MAX_DEFERRED_FREES_PER_FRAME=8;constexpr uint64_t DIRECTION_REPEAT_DELAY_US=320000,DIRECTION_REPEAT_INTERVAL_US=420000;
const char* extOf(const std::string&p){const size_t d=p.find_last_of('.');return d==std::string::npos?"":p.c_str()+d;}std::string formatBytes(uint64_t b){char o[64];double v=(double)b;if(b>=1024ULL*1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f GB",v/(1024.0*1024.0*1024.0));else if(b>=1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f MB",v/(1024.0*1024.0));else if(b>=1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f KB",v/1024.0);else sceClibSnprintf(o,sizeof(o),"%llu B",(unsigned long long)b);return o;}std::string lowerAscii(std::string s){for(char&c:s)c=(char)std::tolower((unsigned char)c);return s;}std::string ellipsize(const std::string&s,size_t n){if(s.size()<=n)return s;if(n<=3)return s.substr(0,n);return s.substr(0,n-3)+"...";}bool actionableLink(const CatalogLink&l){std::string t=lowerAscii(l.type);if(t=="download"||t=="downloads"||t=="mirror"||t=="dlc")return true;std::string u=lowerAscii(l.url);return u.find(".vpk")!=std::string::npos||u.find(".pkg")!=std::string::npos||u.find(".zip")!=std::string::npos||u.find(".pbp")!=std::string::npos||u.find(".iso")!=std::string::npos||u.find(".cso")!=std::string::npos;}}
std::string formatEta(uint64_t seconds){if(seconds==0)return "--";uint64_t h=seconds/3600,m=(seconds%3600)/60,sec=seconds%60;char o[64];if(h)sceClibSnprintf(o,sizeof(o),"%llu:%02llu:%02llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)sec);else sceClibSnprintf(o,sizeof(o),"%02llu:%02llu",(unsigned long long)m,(unsigned long long)sec);return o;}
FullCatalogScreen::FullCatalogScreen()=default;FullCatalogScreen::~FullCatalogScreen(){shutdown();}
void FullCatalogScreen::setInstallCallbacks(InstallRequestFn r,InstallStatusFn s){installRequest_=std::move(r);installStatusText_=std::move(s);}void FullCatalogScreen::setInstallCancelCallback(InstallCancelFn c){installCancel_=std::move(c);}void FullCatalogScreen::setInstallAcknowledgeCallback(InstallAcknowledgeFn c){installAcknowledge_=std::move(c);}void FullCatalogScreen::setCatalogChangeCallback(CatalogChangeFn c){catalogChange_=std::move(c);}void FullCatalogScreen::setSearchCallback(SearchRequestFn c){searchRequest_=std::move(c);}void FullCatalogScreen::setLinkActionCallback(LinkActionFn c){linkAction_=std::move(c);}void FullCatalogScreen::setImageCache(ImageCache*c){imageCache_=c;}
void FullCatalogScreen::setCatalogItems(std::vector<CatalogItem>items){
    // Textures for the previous tab may still be draining; only schedule free if needed.
    releaseTextures();
    allItems_=std::move(items);
    sortItemsByDate(allItems_);
    applySearch(searchQuery_);
    visualCatalogScroll_=0.f;
    visualDetailScroll_=0.f;
    visualFocusIndex_=0.f;
    detailCrossfade_=1.f;
    detailCrossfadeFrom_=-1;
    contentFade_=0.4f;
    catalogLoading_=false;
    catalogError_.clear();
    // Instant memory-cache hits clear loading quickly; keep debounce so spam L/R cannot thrash.
    if(catalogSwitchCooldownFrames_<CATALOG_SWITCH_COOLDOWN_FRAMES/2)
        catalogSwitchCooldownFrames_=CATALOG_SWITCH_COOLDOWN_FRAMES/2;
    lastCatalogSwitchMs_=sceKernelGetProcessTimeWide()/1000ULL;
}void FullCatalogScreen::setActiveCatalog(CatalogType c){
    releaseTextures();
    state_.catalog=c;
    searchQuery_.clear();
    items_=allItems_;
    state_.focusIndex=0;
    state_.catalogScrollRow=0;
    state_.detailScroll=0;
    visualCatalogScroll_=0.f;
    visualDetailScroll_=0.f;
    contentFade_=0.45f;
    detailScrollBeforeLinkMode_=0;
    state_.linkFocus=-1;
    state_.linkNavigation=false;
}void FullCatalogScreen::setCatalogLoading(bool l,const std::string&lab,uint64_t cur,uint64_t tot,const std::string&msg){catalogLoading_=l;catalogLoadingLabel_=lab;catalogLoadingCurrent_=cur;catalogLoadingTotal_=tot;catalogLoadingMessage_=msg;if(l)catalogError_.clear();}void FullCatalogScreen::setCatalogError(const std::string&e){catalogLoading_=false;catalogError_=e;}void FullCatalogScreen::setInstallProgress(
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
}
bool FullCatalogScreen::init(){vita2d_init();vita2d_set_clear_color(BG);font_=vita2d_load_default_pgf();if(!font_)return false;sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);state_=UiState{};ready_=true;diagnostics::log("[UI] initialized");return true;}void FullCatalogScreen::scheduleTextureFree(vita2d_texture* texture){
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
        return;
    }
    for(auto& e:textures_){
        if(e.second){
            scheduleTextureFree(e.second);
            e.second=nullptr;
        }
    }
    textures_.clear();
    textureOrder_.clear();
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
void FullCatalogScreen::shutdown(){releaseTextures();flushDeferredTextureFrees();if(font_){vita2d_free_pgf(font_);font_=nullptr;}if(ready_){vita2d_fini();ready_=false;}diagnostics::log("[UI] shutdown");}
int FullCatalogScreen::totalRows()const{return items_.empty()?0:(int(items_.size())+2)/3;}int FullCatalogScreen::visibleRowsFull()const{return 3;}int FullCatalogScreen::visibleRowsSplit()const{return std::max(1,(SCREEN_H-HEADER_H-TABS_H-FOOTER_H-GRID_PAD*2)/(SPLIT_CARD_H+CARD_GAP));}int FullCatalogScreen::selectedIndex()const{return items_.empty()?-1:std::max(0,std::min(state_.focusIndex,(int)items_.size()-1));}void FullCatalogScreen::clampCatalogFocus(){if(items_.empty())state_.focusIndex=0;else state_.focusIndex=std::max(0,std::min(state_.focusIndex,(int)items_.size()-1));}void FullCatalogScreen::clampCatalogScroll(){if(items_.empty()){state_.catalogScrollRow=0;return;}int v=state_.mode==UiMode::FULL_CATALOG?visibleRowsFull():visibleRowsSplit();if(state_.mode==UiMode::FULL_CATALOG){int r=state_.focusIndex/3;if(r<state_.catalogScrollRow)state_.catalogScrollRow=r;if(r>=state_.catalogScrollRow+v)state_.catalogScrollRow=r-v+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,std::max(0,totalRows()-v)));}else{int m=std::max(0,(int)items_.size()-v);if(state_.focusIndex<state_.catalogScrollRow)state_.catalogScrollRow=state_.focusIndex;if(state_.focusIndex>=state_.catalogScrollRow+v)state_.catalogScrollRow=state_.focusIndex-v+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,m));}}
void FullCatalogScreen::sortItemsByDate(std::vector<CatalogItem>&v)const{std::stable_sort(v.begin(),v.end(),[](const CatalogItem&a,const CatalogItem&b){if(a.versionDate!=b.versionDate)return a.versionDate>b.versionDate;return lowerAscii(a.name)<lowerAscii(b.name);});}bool FullCatalogScreen::matchesSearch(const CatalogItem&i,const std::string&q)const{if(q.empty())return true;std::string x=lowerAscii(q),h=lowerAscii(i.name+"\n"+i.titleId+"\n"+i.author+"\n"+i.description+"\n"+i.longDescription+"\n"+i.category+"\n"+i.subcategory);return h.find(x)!=std::string::npos;}void FullCatalogScreen::applySearch(const std::string&q){searchQuery_=q;items_.clear();for(const auto&i:allItems_)if(matchesSearch(i,q))items_.push_back(i);state_.focusIndex=0;state_.catalogScrollRow=0;state_.detailScroll=0;detailScrollBeforeLinkMode_=0;state_.linkFocus=-1;state_.linkNavigation=false;char m[256];sceClibSnprintf(m,sizeof(m),"[UI] search query='%s' results=%u",searchQuery_.c_str(),(unsigned)items_.size());diagnostics::log(m);}
int FullCatalogScreen::detailContentHeight(const CatalogItem&i,int w)const{int cw=std::max(1,w-36),mc=std::max(18,cw/7);std::vector<std::string>pre,post;auto add=[&](std::vector<std::string>&l,const char*t,const std::string&v){if(v.empty())return;l.push_back(t);std::vector<std::string>q;wrapText(v,mc,q);for(auto&s:q)l.push_back(s);l.push_back("");};add(pre,"Description",i.description);add(pre,"Long Description",i.longDescription);add(post,"Requirements",i.requirements);post.push_back("Information");post.push_back("Title ID: "+i.titleId);post.push_back("Version: "+i.version);post.push_back("Release date: "+i.versionDate);post.push_back("Category: "+i.category);post.push_back("Subcategory: "+i.subcategory);post.push_back("Size: "+i.size);post.push_back("Status: "+i.status);post.push_back("");add(post,"Changelog",i.changelog);int lh=i.linkDetails.empty()?0:10+(int)i.linkDetails.size()*(LINK_ROW_H+LINK_GAP),sc=std::min(5,(int)i.screenshots.size());return lh+((int)pre.size()+(int)post.size())*LINE_H+sc*SCREENSHOT_ROW_H+32;}int FullCatalogScreen::detailLinkScrollLimit(const CatalogItem&i,int w,int h)const{(void)w;if(i.linkDetails.empty())return 0;int lh=10+(int)i.linkDetails.size()*(LINK_ROW_H+LINK_GAP),v=std::max(1,h-DETAIL_HEADER_H-18);return std::max(0,lh-v);}void FullCatalogScreen::clampDetailScroll(){int i=selectedIndex();if(i<0){state_.detailScroll=0;return;}int vh=std::max(1,SCREEN_H-HEADER_H-TABS_H-FOOTER_H-DETAIL_HEADER_H-18),total=detailContentHeight(items_[i],SCREEN_W/2),mx=std::max(0,total-vh);state_.detailScroll=std::max(0,std::min(state_.detailScroll,mx));if(items_[i].linkDetails.empty()){state_.linkFocus=-1;state_.linkNavigation=false;}else if(state_.linkFocus>=(int)items_[i].linkDetails.size())state_.linkFocus=(int)items_[i].linkDetails.size()-1;if(state_.linkNavigation){int lim=detailLinkScrollLimit(items_[i],SCREEN_W/2,SCREEN_H-HEADER_H-TABS_H-FOOTER_H);state_.detailScroll=std::max(0,std::min(state_.detailScroll,lim));}}
void FullCatalogScreen::moveCatalogFocus(int d){if(items_.empty())return;if(state_.mode!=UiMode::FULL_CATALOG)releaseScreenshotTextures();if(state_.mode==UiMode::FULL_CATALOG){if(d<0&&state_.focusIndex>=3)state_.focusIndex-=3;if(d>0&&state_.focusIndex+3<(int)items_.size())state_.focusIndex+=3;}else{if(d<0&&state_.focusIndex>0)--state_.focusIndex;if(d>0&&state_.focusIndex+1<(int)items_.size())++state_.focusIndex;}clampCatalogFocus();clampCatalogScroll();state_.detailScroll=0;detailScrollBeforeLinkMode_=0;state_.linkFocus=-1;state_.linkNavigation=false;}void FullCatalogScreen::moveDetailScroll(int d){state_.detailScroll+=d<0?-72:72;clampDetailScroll();}void FullCatalogScreen::enterLinkNavigation(){int i=selectedIndex();if(i<0||items_[i].linkDetails.empty())return;detailScrollBeforeLinkMode_=state_.detailScroll;state_.linkNavigation=true;state_.linkFocus=0;state_.detailScroll=0;clampDetailScroll();diagnostics::log("[UI] link navigation enabled; detail scroll bounded to links");}void FullCatalogScreen::exitLinkNavigation(){if(!state_.linkNavigation)return;state_.linkNavigation=false;state_.linkFocus=-1;state_.detailScroll=detailScrollBeforeLinkMode_;detailScrollBeforeLinkMode_=0;clampDetailScroll();diagnostics::log("[UI] link navigation disabled; detail scroll restored");}void FullCatalogScreen::moveLinkFocus(int dx,int dy){(void)dx;int i=selectedIndex();if(i<0||items_[i].linkDetails.empty())return;int c=(int)items_[i].linkDetails.size();if(state_.linkFocus<0)state_.linkFocus=0;else state_.linkFocus=std::max(0,std::min(c-1,state_.linkFocus+dy));state_.linkNavigation=true;int top=DETAIL_HEADER_H+10+state_.linkFocus*(LINK_ROW_H+LINK_GAP),vis=SCREEN_H-HEADER_H-TABS_H-FOOTER_H-DETAIL_HEADER_H-18,lim=detailLinkScrollLimit(items_[i],SCREEN_W/2,SCREEN_H-HEADER_H-TABS_H-FOOTER_H);if(top<state_.detailScroll)state_.detailScroll=top;if(top+LINK_ROW_H>state_.detailScroll+vis)state_.detailScroll=top+LINK_ROW_H-vis;state_.detailScroll=std::max(0,std::min(state_.detailScroll,lim));}void FullCatalogScreen::activateFocusedLink(){int i=selectedIndex();if(i<0||state_.linkFocus<0||state_.linkFocus>=(int)items_[i].linkDetails.size())return;const CatalogLink&l=items_[i].linkDetails[state_.linkFocus];if(!linkAction_||!actionableLink(l)){diagnostics::log(std::string("[UI] non-download link selected: ")+l.url);return;}if(linkAction_(items_[i],l))exitLinkNavigation();}void FullCatalogScreen::changeCatalog(int d){
    if(catalogLoading_||installProgressActive_||isTransitioning())return;
    if(catalogSwitchCooldownFrames_>0)return;
    if(!deferredFreeTextures_.empty())return; // wait until GPU frees drain

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

    // Lock BEFORE loader callback.
    catalogLoading_=true;
    catalogLoadingLabel_=catalogName(n);
    catalogLoadingCurrent_=0;
    catalogLoadingTotal_=0;
    catalogLoadingMessage_="Checking catalog cache...";
    catalogError_.clear();
    showToast(std::string("Cargando ") + catalogName(n) + "...", 1200);
    state_.catalog=n;
    items_.clear();
    state_.focusIndex=0;
    state_.catalogScrollRow=0;
    releaseTextures();
    catalogSwitchCooldownFrames_=CATALOG_SWITCH_COOLDOWN_FRAMES;
    lastCatalogSwitchMs_=nowMs;

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
    if (isTransitioning() || catalogLoading_ || installProgressActive_) return;

    SceTouchData td{};
    const int n = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    if (n <= 0) return;

    // Vita touch reports ~1920x1088; map to 960x544 framebuffer.
    auto mapX = [](int tx) { return tx * SCREEN_W / 1920; };
    auto mapY = [](int ty) { return ty * SCREEN_H / 1088; };

    const uint64_t now = sceKernelGetProcessTimeWide();

    if (td.reportNum > 0) {
        const int x = mapX(td.report[0].x);
        const int y = mapY(td.report[0].y);
        if (!touchDown_) {
            touchDown_ = true;
            touchStartX_ = x;
            touchStartY_ = y;
            touchLastY_ = y;
            touchMoved_ = false;
            touchDownMs_ = now / 1000ULL;
        } else {
            const int dy = y - touchLastY_;
            const int adx = std::abs(x - touchStartX_);
            const int ady = std::abs(y - touchStartY_);
            if (adx > 12 || ady > 12) touchMoved_ = true;
            // Vertical drag scrolls catalog (or detail when detail panel is active)
            if (std::abs(dy) >= 18) {
                const int steps = dy / 18;
                if (state_.mode == UiMode::FULL_CATALOG) {
                    if (steps > 0) {
                        for (int i = 0; i < steps; ++i) moveCatalogFocus(1);
                    } else {
                        for (int i = 0; i < -steps; ++i) moveCatalogFocus(-1);
                    }
                } else if (state_.activePanel == UiPanel::Detail) {
                    if (steps > 0) {
                        for (int i = 0; i < steps; ++i) moveDetailScroll(1);
                    } else {
                        for (int i = 0; i < -steps; ++i) moveDetailScroll(-1);
                    }
                } else {
                    if (steps > 0) {
                        for (int i = 0; i < steps; ++i) moveCatalogFocus(1);
                    } else {
                        for (int i = 0; i < -steps; ++i) moveCatalogFocus(-1);
                    }
                }
                touchLastY_ = y;
            }
        }
    } else if (touchDown_) {
        // Finger up — treat as tap if not a drag
        const int x = touchStartX_;
        const int y = touchStartY_;
        const bool wasDrag = touchMoved_;
        touchDown_ = false;

        if (wasDrag) return;

        // Tabs strip
        if (y >= HEADER_H && y < HEADER_H + TABS_H) {
            const float tw = static_cast<float>(SCREEN_W) / static_cast<float>(CatalogType::Count);
            const int tab = std::min((int)CatalogType::Count - 1, std::max(0, (int)(x / tw)));
            const int cur = (int)state_.catalog;
            const int delta = tab - cur;
            if (delta != 0) changeCatalog(delta);
            return;
        }

        const int panelTop = HEADER_H + TABS_H;
        const int panelBottom = SCREEN_H - FOOTER_H;
        if (y < panelTop || y >= panelBottom) return;

        if (state_.mode == UiMode::FULL_CATALOG) {
            const int gridX = 0, gridY = panelTop;
            const int gridW = SCREEN_W, gridH = panelBottom - panelTop;
            const int cw = (gridW - GRID_PAD * 2 - CARD_GAP * 2) / 3;
            const float rowH = static_cast<float>(FULL_CARD_H + CARD_GAP);
            const int localX = x - (gridX + GRID_PAD);
            const int localY = y - (gridY + GRID_PAD);
            if (localX < 0 || localY < 0) return;
            const int col = localX / (cw + CARD_GAP);
            const int row = static_cast<int>((localY / rowH) + visualCatalogScroll_);
            if (col < 0 || col > 2) return;
            const int idx = row * 3 + col;
            if (idx < 0 || idx >= (int)items_.size()) return;
            if (idx == state_.focusIndex) {
                startOpeningDetail();
            } else {
                state_.focusIndex = idx;
                clampCatalogFocus();
                clampCatalogScroll();
            }
            return;
        }

        // Split: left list / right detail
        const int mid = SCREEN_W / 2;
        if (x < mid) {
            state_.activePanel = UiPanel::Catalog;
            const float rowH = static_cast<float>(SPLIT_CARD_H + CARD_GAP);
            const int localY = y - (panelTop + GRID_PAD);
            if (localY < 0) return;
            const int idx = static_cast<int>(localY / rowH + visualCatalogScroll_);
            if (idx < 0 || idx >= (int)items_.size()) return;
            state_.focusIndex = idx;
            state_.detailScroll = 0;
            visualDetailScroll_ = 0.f;
            clampCatalogFocus();
            clampCatalogScroll();
        } else {
            state_.activePanel = UiPanel::Detail;
        }
    }
}

void FullCatalogScreen::handleInput(){if(isTransitioning())return;SceCtrlData p{};sceCtrlPeekBufferPositive(0,&p,1);static uint32_t prev=0;static uint64_t repeatAt=0;uint32_t mask=SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT,pressed=p.buttons&~prev,direct=pressed&mask;uint64_t now=sceKernelGetProcessTimeWide(),repeat=0;if((p.buttons&mask)==0)repeatAt=0;else if(direct)repeatAt=now+DIRECTION_REPEAT_DELAY_US;else if(repeatAt&&now>=repeatAt){repeat=p.buttons&mask;repeatAt=now+DIRECTION_REPEAT_INTERVAL_US;}prev=p.buttons;uint32_t nav=direct|repeat;if(pressed&SCE_CTRL_START){state_.requestExit=true;return;}if((pressed&SCE_CTRL_LTRIGGER)||(pressed&SCE_CTRL_RTRIGGER)){
        const bool canSwitch=!catalogLoading_&&!installProgressActive_&&!isTransitioning()
            &&catalogSwitchCooldownFrames_<=0&&deferredFreeTextures_.empty();
        if(canSwitch){
            if(pressed&SCE_CTRL_LTRIGGER)changeCatalog(-1);else changeCatalog(1);
        }else if(catalogLoading_){
            showToast("Cambiando catalogo...", 1000);
        }else if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty()){
            showToast("Espera un momento...", 900);
        }
        return;
    }if(installProgressActive_&&(pressed&SCE_CTRL_CIRCLE)){if(installOutcome_==1||installOutcome_==2){if(installAcknowledge_)installAcknowledge_();}else if(installCancel_)installCancel_();return;}if(catalogLoading_||installProgressActive_)return;if(pressed&SCE_CTRL_SQUARE){if(!searchQuery_.empty())applySearch("");return;}if(state_.mode==UiMode::FULL_CATALOG){if(pressed&SCE_CTRL_TRIANGLE){if(searchRequest_)applySearch(searchRequest_(searchQuery_));return;}if(nav&SCE_CTRL_LEFT&&state_.focusIndex%3>0)--state_.focusIndex;if(nav&SCE_CTRL_RIGHT&&state_.focusIndex%3<2&&state_.focusIndex+1<(int)items_.size())++state_.focusIndex;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);clampCatalogScroll();if(pressed&SCE_CTRL_CROSS)startOpeningDetail();return;}if(state_.mode!=UiMode::SPLIT_DETAIL)return;if(pressed&SCE_CTRL_CIRCLE){startClosingDetail();return;}if(state_.activePanel==UiPanel::Catalog){if(pressed&SCE_CTRL_RIGHT)state_.activePanel=UiPanel::Detail;if(nav&SCE_CTRL_UP)moveCatalogFocus(-1);if(nav&SCE_CTRL_DOWN)moveCatalogFocus(1);return;}if(nav&SCE_CTRL_LEFT)state_.activePanel=UiPanel::Catalog;if(pressed&SCE_CTRL_TRIANGLE){if(state_.linkNavigation)exitLinkNavigation();else enterLinkNavigation();return;}if(state_.linkNavigation){if(nav&SCE_CTRL_UP)moveLinkFocus(0,-1);if(nav&SCE_CTRL_DOWN)moveLinkFocus(0,1);if(pressed&SCE_CTRL_CROSS)activateFocusedLink();return;}if(nav&SCE_CTRL_UP)moveDetailScroll(-1);if(nav&SCE_CTRL_DOWN)moveDetailScroll(1);}
unsigned FullCatalogScreen::colorForStatus(const std::string&s)const{if(s=="Verified")return ACCENT;if(s=="Legacy")return TEXT;if(s=="Archive")return DIM;return TEXT;}void FullCatalogScreen::drawHeader(int w){vita2d_draw_rectangle(0,0,w,HEADER_H,SURFACE2);vita2d_pgf_draw_text(font_,16,32,ACCENT,1.12f,"PSVitaAlive Store");vita2d_pgf_draw_text(font_,w-132,32,DIM,.76f,"START: Exit");if(!searchQuery_.empty()){std::string q=ellipsize(searchQuery_,22);int pw=std::max(156,std::min(w-300,(int)(q.size()*7+86))),bx=(w-pw)/2,by=11,bh=28;vita2d_draw_rectangle(bx,by,pw,bh,SURFACE);vita2d_draw_rectangle(bx,by,pw,2,ACCENT);vita2d_draw_rectangle(bx,by+bh-2,pw,2,ACCENT);vita2d_pgf_draw_text(font_,bx+12,by+19,ACCENT,.58f,"FILTER");vita2d_pgf_draw_text(font_,bx+62,by+19,WHITE,.58f,q.c_str());}}
void FullCatalogScreen::drawTabs(int w){
    vita2d_draw_rectangle(0,HEADER_H,w,TABS_H,SURFACE);
    float tw=(float)w/(int)CatalogType::Count;
    // Sliding accent under the active tab
    vita2d_draw_rectangle((int)tabIndicatorX_, HEADER_H+TABS_H-3, (int)tw, 3, ACCENT);
    vita2d_draw_rectangle((int)tabIndicatorX_, HEADER_H+TABS_H-4, (int)tw, 1, RGBA8(0x3B,0xFF,0,90));
    for(int i=0;i<(int)CatalogType::Count;++i){
        int x=(int)(i*tw);
        bool a=(int)state_.catalog==i;
        vita2d_pgf_draw_text(font_,x+12,HEADER_H+24,a?ACCENT:TEXT,a?0.84f:0.78f,catalogName((CatalogType)i));
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

    std::unordered_set<std::string> keep;
    auto markKeep=[&](const std::string& url, const char* ns){
        if(url.empty())return;
        const std::string path=imageCache_->request(url, ns);
        if(!path.empty())keep.insert(path);
    };

    constexpr int kLoadsPerFrame = 3; // fill the 3x3 grid quickly without stalling a frame

    if(state_.mode==UiMode::FULL_CATALOG){
        // Strict visible set: the 9 on-screen cells only (no huge buffer that fights the cap).
        const int first=std::max(0, state_.catalogScrollRow*3);
        const int last=std::min((int)items_.size(), first+9);
        for(int i=first;i<last;++i){
            const CatalogItem& it=items_[i];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
        }
        releaseTexturesNotIn(keep);

        int loads=0;
        for(int i=first;i<last&&loads<kLoadsPerFrame;++i){
            const CatalogItem& it=items_[i];
            const std::string& url=!it.icon.empty()?it.icon:it.cover;
            if(url.empty())continue;
            const size_t before=textures_.size();
            prepareImageTexture(url, "app");
            if(textures_.size()>before)++loads;
        }
        return;
    }

    if(state_.mode==UiMode::OPENING_DETAIL||state_.mode==UiMode::SPLIT_DETAIL||state_.mode==UiMode::CLOSING_DETAIL){
        const int first=std::max(0, state_.catalogScrollRow);
        const int last=std::min((int)items_.size(), state_.catalogScrollRow+visibleRowsSplit());
        for(int i=first;i<last;++i){
            const CatalogItem& it=items_[i];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
        }
        const int sel=selectedIndex();
        if(sel>=0){
            const CatalogItem& it=items_[sel];
            markKeep(!it.icon.empty()?it.icon:it.cover, "app");
            const int panelX=SCREEN_W/2, panelY=HEADER_H+TABS_H;
            const int panelW=SCREEN_W-panelX, panelH=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;
            const int top=panelY+DETAIL_HEADER_H+10, bottom=panelY+panelH-10;
            const int scroll=std::max(0, state_.detailScroll);
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

        int loads=0;
        auto prepareOne=[&](const std::string& url, const char* ns){
            if(loads>=kLoadsPerFrame||url.empty())return;
            const size_t before=textures_.size();
            prepareImageTexture(url, ns);
            if(textures_.size()>before)++loads;
        };
        for(int i=first;i<last;++i){
            const CatalogItem& it=items_[i];
            prepareOne(!it.icon.empty()?it.icon:it.cover, "app");
        }
        if(sel>=0){
            const CatalogItem& it=items_[sel];
            const int panelX=SCREEN_W/2, panelY=HEADER_H+TABS_H;
            const int panelW=SCREEN_W-panelX, panelH=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;
            const int top=panelY+DETAIL_HEADER_H+10, bottom=panelY+panelH-10;
            const int scroll=std::max(0, state_.detailScroll);
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
    // Compact progress only — no text (overflows small icon/cover cells).
    vita2d_draw_rectangle(x, y, w, h, SURFACE2);
    if (w < 8 || h < 6) return;

    float pct = 0.f;
    bool determinate = false;
    if (imageCache_ && !url.empty()) {
        const std::string path = imageCache_->request(url, ns);
        const auto prog = imageCache_->progress();
        if (prog.active && !prog.localPath.empty() && prog.localPath == path && prog.total > 0) {
            pct = std::min(1.f, static_cast<float>(prog.downloaded) / static_cast<float>(prog.total));
            determinate = true;
        } else if (imageCache_->isPending(path)) {
            // Indeterminate pulse
            pct = focusPulse();
            determinate = false;
        }
    }

    const int pad = 3;
    const int barH = std::max(3, std::min(6, h / 8));
    const int barX = x + pad;
    const int barY = y + h - pad - barH;
    const int barW = std::max(1, w - pad * 2);

    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    if (determinate) {
        const int fill = std::max(1, static_cast<int>(barW * pct));
        vita2d_draw_rectangle(barX, barY, fill, barH, ACCENT);
    } else {
        const int slideW = std::max(8, barW / 3);
        const int slide = static_cast<int>((barW - slideW) * pct);
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
    std::string path = imageCache_->request(url, ns);
    if (imageCache_->isFailed(path)) {
        vita2d_draw_rectangle(x, y, w, h, SURFACE2);
        if (font_) vita2d_pgf_draw_text(font_, x + 12, y + h / 2, DIM, 0.58f, "Sin imagen");
        return;
    }
    if (!imageCache_->isReady(path)) {
        drawImageLoadingPlaceholder(url, ns, x, y, w, h);
        return;
    }
    auto it = textures_.find(path);
    if (it == textures_.end()) {
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
    const unsigned glow = RGBA8(0x00, 0xFF, 0x66, static_cast<unsigned>(40 + pulse * 70));
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

void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){
    const float pulse = focus ? focusPulse() : 0.f;
    // Subtle lift / scale for focused card
    int ox = focus ? -1 : 0;
    int oy = focus ? -1 : 0;
    int ww = focus ? w + 2 : w;
    int hh = focus ? h + 2 : h;
    const unsigned bg = focus ? SURFACE2 : SURFACE;
    vita2d_draw_rectangle(x + ox, y + oy, ww, hh, bg);
    if (focus) {
        const unsigned glow = RGBA8(0x00, 0xFF, 0x66, static_cast<unsigned>(50 + pulse * 90));
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
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    if (meta.empty()) meta = it.size;
    if (!meta.empty()) vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - 10 + oy, DIM, 0.58f, ellipsize(meta, 30).c_str());
    (void)idx;
}
void FullCatalogScreen::drawCatalogPanel(int x,int y,int w,int h,bool split){
    vita2d_draw_rectangle(x, y, w, h, PANEL);
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
                if (i < 0 || i >= (int)items_.size()) continue;
                const float fy = static_cast<float>(y + GRID_PAD) + (static_cast<float>(baseRow + r) - visualCatalogScroll_) * rowH;
                if (fy + FULL_CARD_H < y || fy > y + h) continue;
                drawCatalogCard(items_[i], i, x + GRID_PAD + c * (cw + CARD_GAP), static_cast<int>(fy), cw, FULL_CARD_H, i == state_.focusIndex);
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
            if (i < 0 || i >= (int)items_.size()) continue;
            const float fy = static_cast<float>(y + GRID_PAD) + (static_cast<float>(i) - visualCatalogScroll_) * rowH;
            if (fy + SPLIT_CARD_H < y || fy > y + h) continue;
            drawCatalogCard(items_[i], i, x + GRID_PAD, static_cast<int>(fy), w - GRID_PAD * 2 - 4, SPLIT_CARD_H, i == state_.focusIndex);
        }
        vita2d_disable_clipping();
        const int total = (int)items_.size();
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
            drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4, "LISTA");
    }
    if (dimPanel)
        vita2d_draw_rectangle(x, y, w, h, dimPanel);
}


void FullCatalogScreen::wrapText(const std::string&t,int max,std::vector<std::string>&out)const{out.clear();std::string cur;for(char c:t){if(c=='\n'){out.push_back(cur);cur.clear();continue;}if((int)cur.size()>=max&&c==' '){out.push_back(cur);cur.clear();continue;}cur.push_back(c);if((int)cur.size()>=max){out.push_back(cur);cur.clear();}}if(!cur.empty())out.push_back(cur);}void FullCatalogScreen::drawTextLines(const std::vector<std::string>&l,int x,int y,int lh,unsigned col,float sc,int start,int max,int top,int bottom){int first=std::max(0,start),last=std::min((int)l.size(),first+max),dy=y+first*lh;for(int i=first;i<last;++i){if(dy>=top&&dy<=bottom)vita2d_pgf_draw_text(font_,x,dy,col,sc,l[i].c_str());dy+=lh;}}
void FullCatalogScreen::drawDetailLinks(const CatalogItem&it,int x,int y,int w,int&heightOut){heightOut=0;if(it.linkDetails.empty())return;for(size_t i=0;i<it.linkDetails.size();++i){const CatalogLink&l=it.linkDetails[i];bool f=state_.linkNavigation&&state_.linkFocus==(int)i,can=actionableLink(l);int ry=y+(int)i*(LINK_ROW_H+LINK_GAP);vita2d_draw_rectangle(x,ry,w,LINK_ROW_H,f?ACCENT:SURFACE2);vita2d_draw_rectangle(x,ry,w,1,f?ACCENT:BORDER);unsigned mc=f?BG:(can?WHITE:TEXT);std::string title=l.name.empty()?l.type:l.name;int bw=l.recommended?104:0;vita2d_pgf_draw_text(font_,x+10,ry+15,mc,.66f,ellipsize(title,bw?24:30).c_str());std::string meta=l.type;if(can)meta+="  • Cross: action";vita2d_pgf_draw_text(font_,x+10,ry+31,f?BG:DIM,.52f,ellipsize(meta,bw?30:50).c_str());if(l.recommended){int bx=x+w-bw-10,by=ry+8;vita2d_draw_rectangle(bx,by,bw,22,f?BG:ACCENT);vita2d_pgf_draw_text(font_,bx+8,by+15,f?ACCENT:BG,.52f,"Recommended");}}heightOut=10+(int)it.linkDetails.size()*(LINK_ROW_H+LINK_GAP);}
void FullCatalogScreen::drawDetailContent(const CatalogItem&it,int x,int y,int w,int h){
if(detailCrossfade_<0.99f){vita2d_draw_rectangle(x,y,w,h,RGBA8(0,0,0,static_cast<unsigned>((1.f-detailCrossfade_)*140)));}
int cx=x+18,cw=w-36,mc=std::max(18,cw/7),top=y+DETAIL_HEADER_H+10,bottom=y+h-10;float scroll=std::max(0.f,visualDetailScroll_);std::vector<std::string>pre,post;auto add=[&](std::vector<std::string>&l,const char*t,const std::string&v){if(v.empty())return;l.push_back(t);std::vector<std::string>q;wrapText(v,mc,q);for(auto&s:q)l.push_back(s);l.push_back("");};add(pre,"Description",it.description);add(pre,"Long Description",it.longDescription);add(post,"Requirements",it.requirements);post.push_back("Information");post.push_back("Title ID: "+it.titleId);post.push_back("Version: "+it.version);post.push_back("Release date: "+it.versionDate);post.push_back("Category: "+it.category);post.push_back("Subcategory: "+it.subcategory);post.push_back("Size: "+it.size);post.push_back("Status: "+it.status);post.push_back("");add(post,"Changelog",it.changelog);int sc=std::min(5,(int)it.screenshots.size()),shotH=sc*SCREENSHOT_ROW_H,links=0;vita2d_enable_clipping();vita2d_set_clip_rectangle(x+2,top,x+w-18,bottom);drawDetailLinks(it,cx,top-scroll,cw,links);int preTop=top+links-scroll;drawTextLines(pre,cx,preTop,LINE_H,TEXT,.66f,0,(int)pre.size(),top,bottom);int shotTop=preTop+(int)pre.size()*LINE_H;for(int i=0;i<sc;++i)drawImage(it.screenshots[i],"shot",cx,shotTop+i*SCREENSHOT_ROW_H,cw,SCREENSHOT_ROW_H-18);int postTop=shotTop+shotH+8;drawTextLines(post,cx,postTop,LINE_H,TEXT,.66f,0,(int)post.size(),top,bottom);vita2d_disable_clipping();int total=detailContentHeight(it,w),vis=std::max(1,h-DETAIL_HEADER_H-18),mx=std::max(0,total-vis);if(mx>0){int tx=x+w-8,ty=y+DETAIL_HEADER_H+8,th=h-DETAIL_HEADER_H-18;vita2d_draw_rectangle(tx,ty,3,th,BORDER);int thumb=std::max(20,th*vis/std::max(1,total));int yy=ty+(int)((th-thumb)*(scroll/std::max(1.f,(float)mx)));vita2d_draw_rectangle(tx,yy,3,thumb,ACCENT);}}
void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    int i = selectedIndex();
    if (i < 0) return;
    const CatalogItem& it = items_[i];
    const bool active = (state_.mode == UiMode::SPLIT_DETAIL && state_.activePanel == UiPanel::Detail);
    vita2d_draw_rectangle(x, y, w, DETAIL_HEADER_H, SURFACE);
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 12, y + 12, 68, 68);
    // Leave room for active-panel label chip on the left when focused
    const int titleX = active ? x + 100 : x + 92;
    vita2d_pgf_draw_text(font_, titleX, y + 29, WHITE, 0.82f, ellipsize(it.name, active ? 18 : 24).c_str());
    vita2d_pgf_draw_text(font_, titleX, y + 50, TEXT, 0.64f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 20).c_str());
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    vita2d_pgf_draw_text(font_, titleX, y + 70, colorForStatus(it.status), 0.60f, ellipsize(meta.empty() ? it.status : meta, 22).c_str());
    if (!it.linkDetails.empty()) {
        int bx = x + w - 142, by = y + 12, bw = 128, bh = 28;
        const bool linkOn = state_.linkNavigation;
        const float pulse = linkOn ? focusPulse() : 0.f;
        vita2d_draw_rectangle(bx, by, bw, bh, linkOn ? ACCENT : SURFACE2);
        if (linkOn) {
            const unsigned glow = RGBA8(0x00, 0xFF, 0x66, static_cast<unsigned>(40 + pulse * 80));
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
        drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4, "DETALLE");
}
void FullCatalogScreen::drawLoadingOverlay(){
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
  vita2d_pgf_draw_text(font_,x+28,y+80,GREEN,1.05f,"Instalacion finalizada");
  std::string file=installProgressFile_.empty()?"(archivo)":ellipsize(installProgressFile_,70);
  vita2d_pgf_draw_text(font_,x+28,y+118,WHITE,.66f,("Archivo: "+file).c_str());
  if(!installResultTitleId_.empty())
    vita2d_pgf_draw_text(font_,x+28,y+144,TEXT,.60f,("Title ID: "+installResultTitleId_).c_str());
  if(!installResultPath_.empty())
    vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.60f,("Destino: "+ellipsize(installResultPath_,62)).c_str());
  if(installLiveAreaOk_)
    vita2d_pgf_draw_text(font_,x+28,y+200,GREEN,.72f,"LiveArea: SI — burbuja / app verificada");
  else if(!installResultPath_.empty() && installResultPath_.find("ux0:app/")==0)
    vita2d_pgf_draw_text(font_,x+28,y+200,RGBA8(0xFF,0xC0,0x40,255),.68f,"LiveArea: no confirmado (revisa el log)");
  else
    vita2d_pgf_draw_text(font_,x+28,y+200,TEXT,.62f,"LiveArea: N/A para este tipo de contenido");
  if(!installProgressMessage_.empty())
    vita2d_pgf_draw_text(font_,x+28,y+232,DIM,.54f,ellipsize(installProgressMessage_,78).c_str());
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
void FullCatalogScreen::drawFullCatalog(){vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);drawCatalogPanel(0,HEADER_H+TABS_H,SCREEN_W,SCREEN_H-HEADER_H-TABS_H-FOOTER_H,false);vita2d_draw_rectangle(0,SCREEN_H-FOOTER_H,SCREEN_W,FOOTER_H,SURFACE2);vita2d_pgf_draw_text(font_,12,SCREEN_H-14,TEXT,.58f,"D-Pad: Navigate   X: Detail   △: Search   □: Clear Filter   L/R: Catalog   START: Exit");if(catalogLoading_||installProgressActive_)drawLoadingOverlay();if(!catalogError_.empty())vita2d_pgf_draw_text(font_,18,HEADER_H+TABS_H+26,ACCENT,.66f,catalogError_.c_str());drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawSplitDetail(){vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H,lw=SCREEN_W/2;drawCatalogPanel(0,top,lw,hh,true);drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_draw_rectangle(lw-1,top,2,hh,BORDER);vita2d_draw_rectangle(0,SCREEN_H-FOOTER_H,SCREEN_W,FOOTER_H,SURFACE2);vita2d_pgf_draw_text(font_,12,SCREEN_H-14,TEXT,.54f,state_.activePanel==UiPanel::Catalog?"PANEL: LISTA  |  → Detail   D-Pad: Navigate   O: Back   L/R: Catalog":"PANEL: DETALLE  |  ← Lista   D-Pad: Scroll   △: Links   X: Action   O: Back");if(catalogLoading_||installProgressActive_)drawLoadingOverlay();drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawOpeningDetail(){float p=transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,rw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawClosingDetail(){float p=1.0f-transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::draw(){switch(state_.mode){case UiMode::FULL_CATALOG:drawFullCatalog();break;case UiMode::OPENING_DETAIL:drawOpeningDetail();break;case UiMode::SPLIT_DETAIL:drawSplitDetail();break;case UiMode::CLOSING_DETAIL:drawClosingDetail();break;}}bool FullCatalogScreen::updateAndDraw(){
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
    updateAnimations();
    if(catalogSwitchCooldownFrames_==0)prepareVisibleTextures();
    draw();
    return !state_.requestExit;
}
} // namespace psvitaalive::ui
