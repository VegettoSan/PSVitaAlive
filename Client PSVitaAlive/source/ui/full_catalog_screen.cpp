#include "ui/full_catalog_screen.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace psvitaalive::ui {
namespace {
constexpr unsigned BG = RGBA8(0,0,0,255);
constexpr unsigned SURFACE = RGBA8(0x37,0x37,0x37,255);
constexpr unsigned SURFACE2 = RGBA8(0x2A,0x2A,0x2A,255);
constexpr unsigned BORDER = RGBA8(0x6E,0x6E,0x6E,255);
constexpr unsigned TEXT = RGBA8(0xAA,0xAA,0xAA,255);
constexpr unsigned DIM = RGBA8(0x6E,0x6E,0x6E,255);
constexpr unsigned ACCENT = RGBA8(0x3B,0xFF,0,255);
constexpr unsigned WHITE = RGBA8(255,255,255,255);
constexpr unsigned PANEL = RGBA8(0x20,0x20,0x20,255);
constexpr int FULL_CARD_H=120, SPLIT_CARD_H=82, DETAIL_HEADER_H=92, LINE_H=18, TRANSITION_MS=200;
constexpr size_t MAX_APP_TEXTURES=18;
constexpr size_t MAX_SCREENSHOT_TEXTURES=3;

const char* extOf(const std::string& p){const size_t dot=p.find_last_of('.');return dot==std::string::npos?"":p.c_str()+dot;}
std::string formatBytes(uint64_t bytes){const double b=static_cast<double>(bytes);char out[64];if(bytes>=1024ULL*1024ULL*1024ULL)sceClibSnprintf(out,sizeof(out),"%.2f GB",b/(1024.0*1024.0*1024.0));else if(bytes>=1024ULL*1024ULL)sceClibSnprintf(out,sizeof(out),"%.2f MB",b/(1024.0*1024.0));else if(bytes>=1024ULL)sceClibSnprintf(out,sizeof(out),"%.2f KB",b/1024.0);else sceClibSnprintf(out,sizeof(out),"%llu B",static_cast<unsigned long long>(bytes));return out;}
}

FullCatalogScreen::FullCatalogScreen()=default;
FullCatalogScreen::~FullCatalogScreen(){shutdown();}
void FullCatalogScreen::setInstallCallbacks(InstallRequestFn r,InstallStatusFn s){installRequest_=std::move(r);installStatusText_=std::move(s);}
void FullCatalogScreen::setCatalogChangeCallback(CatalogChangeFn c){catalogChange_=std::move(c);}
void FullCatalogScreen::setImageCache(ImageCache* c){imageCache_=c;}
void FullCatalogScreen::setCatalogItems(std::vector<CatalogItem> items){items_=std::move(items);state_.focusIndex=0;state_.catalogScrollRow=0;state_.detailScroll=0;catalogLoading_=false;catalogError_.clear();}
void FullCatalogScreen::setActiveCatalog(CatalogType catalog){state_.catalog=catalog;state_.focusIndex=0;state_.catalogScrollRow=0;state_.detailScroll=0;}
void FullCatalogScreen::setCatalogLoading(bool loading,const std::string& label,uint64_t current,uint64_t total,const std::string& message){catalogLoading_=loading;catalogLoadingLabel_=label;catalogLoadingCurrent_=current;catalogLoadingTotal_=total;catalogLoadingMessage_=message;if(loading)catalogError_.clear();}
void FullCatalogScreen::setCatalogError(const std::string& e){catalogLoading_=false;catalogError_=e;}
void FullCatalogScreen::setInstallProgress(bool active,uint64_t current,uint64_t total,uint64_t bytesPerSecond,const std::string& stage,const std::string& fileName,const std::string& message){installProgressActive_=active;installProgressCurrent_=current;installProgressTotal_=total;installProgressSpeed_=bytesPerSecond;installProgressStage_=stage;installProgressFile_=fileName;installProgressMessage_=message;}

bool FullCatalogScreen::init(){vita2d_init();vita2d_set_clear_color(BG);font_=vita2d_load_default_pgf();if(!font_)return false;sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);state_=UiState{};ready_=true;diagnostics::log("[UI] initialized");return true;}
void FullCatalogScreen::releaseTextures(){for(auto& e:textures_)if(e.second)vita2d_free_texture(e.second);textures_.clear();textureOrder_.clear();}
void FullCatalogScreen::touchTexture(const std::string& path){auto it=std::find(textureOrder_.begin(),textureOrder_.end(),path);if(it!=textureOrder_.end())textureOrder_.erase(it);textureOrder_.push_back(path);}
void FullCatalogScreen::evictTextureIfNeeded(const std::string& ns){const size_t limit=ns=="shot"?MAX_SCREENSHOT_TEXTURES:MAX_APP_TEXTURES;size_t count=0;for(const auto& p:textureOrder_)if(p.find("/"+ns+"_")!=std::string::npos)++count;if(count<limit)return;for(auto it=textureOrder_.begin();it!=textureOrder_.end();++it){if(it->find("/"+ns+"_")==std::string::npos)continue;auto tex=textures_.find(*it);if(tex!=textures_.end()){if(tex->second)vita2d_free_texture(tex->second);textures_.erase(tex);}textureOrder_.erase(it);return;}}
void FullCatalogScreen::shutdown(){releaseTextures();if(font_){vita2d_free_pgf(font_);font_=nullptr;}if(ready_){vita2d_fini();ready_=false;}diagnostics::log("[UI] shutdown");}

int FullCatalogScreen::totalRows()const{return items_.empty()?0:(static_cast<int>(items_.size())+2)/3;}
int FullCatalogScreen::visibleRowsFull()const{return std::max(1,(SCREEN_H-HEADER_H-TABS_H-FOOTER_H-GRID_PAD*2)/(FULL_CARD_H+CARD_GAP));}
int FullCatalogScreen::visibleRowsSplit()const{return std::max(1,(SCREEN_H-HEADER_H-TABS_H-FOOTER_H-GRID_PAD*2)/(SPLIT_CARD_H+CARD_GAP));}
int FullCatalogScreen::selectedIndex()const{return items_.empty()?-1:std::max(0,std::min(state_.focusIndex,(int)items_.size()-1));}
void FullCatalogScreen::clampCatalogFocus(){if(items_.empty())state_.focusIndex=0;else state_.focusIndex=std::max(0,std::min(state_.focusIndex,(int)items_.size()-1));}
void FullCatalogScreen::clampCatalogScroll(){if(items_.empty()){state_.catalogScrollRow=0;return;}const int vis=state_.mode==UiMode::FULL_CATALOG?visibleRowsFull():visibleRowsSplit();if(state_.mode==UiMode::FULL_CATALOG){int row=state_.focusIndex/3;if(row<state_.catalogScrollRow)state_.catalogScrollRow=row;if(row>=state_.catalogScrollRow+vis)state_.catalogScrollRow=row-vis+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,std::max(0,totalRows()-vis)));}else{int maxs=std::max(0,(int)items_.size()-vis);if(state_.focusIndex<state_.catalogScrollRow)state_.catalogScrollRow=state_.focusIndex;if(state_.focusIndex>=state_.catalogScrollRow+vis)state_.catalogScrollRow=state_.focusIndex-vis+1;state_.catalogScrollRow=std::max(0,std::min(state_.catalogScrollRow,maxs));}}

void FullCatalogScreen::clampDetailScroll(){const int i=selectedIndex();if(i<0){state_.detailScroll=0;return;}const CatalogItem&it=items_[i];const int cw=std::max(1,SCREEN_W/2-36);const int maxChars=std::max(18,cw/7);std::vector<std::string>lines;auto add=[&](const char*t,const std::string&v){if(v.empty())return;lines.push_back(t);std::vector<std::string>q;wrapText(v,maxChars,q);for(auto&s:q)lines.push_back(s);lines.push_back("");};add("Description",it.description);add("Long Description",it.longDescription);add("Requirements",it.requirements);lines.push_back("Information");lines.push_back("Title ID: "+it.titleId);lines.push_back("Version: "+it.version);lines.push_back("Release date: "+it.versionDate);lines.push_back("Category: "+it.category);lines.push_back("Subcategory: "+it.subcategory);lines.push_back("Size: "+it.size);lines.push_back("Status: "+it.status);lines.push_back("");if(!it.linkDetails.empty()){lines.push_back("Downloads & Links");for(const auto&l:it.linkDetails){std::string s=l.type;if(!l.name.empty()){if(!s.empty())s+=": ";s+=l.name;}if(l.recommended)s+=" [Recommended]";lines.push_back("- "+s);}lines.push_back("");}add("Changelog",it.changelog);const int screenshotCount=std::min(5,(int)it.screenshots.size());const int screenshotRows=(screenshotCount+2)/3;const int screenshotHeight=screenshotRows*80;const int textGap=screenshotCount?12:6;const int totalContent=screenshotHeight+textGap+(int)lines.size()*LINE_H;const int visibleHeight=std::max(1,SCREEN_H-HEADER_H-TABS_H-FOOTER_H-DETAIL_HEADER_H-18);state_.detailScroll=std::max(0,std::min(state_.detailScroll,std::max(0,totalContent-visibleHeight)));}

void FullCatalogScreen::moveCatalogFocus(int d){if(items_.empty())return;if(state_.mode==UiMode::FULL_CATALOG){if(d<0&&state_.focusIndex>=3)state_.focusIndex-=3;if(d>0&&state_.focusIndex+3<(int)items_.size())state_.focusIndex+=3;}else{if(d<0&&state_.focusIndex>0)--state_.focusIndex;if(d>0&&state_.focusIndex+1<(int)items_.size())++state_.focusIndex;}clampCatalogFocus();clampCatalogScroll();state_.detailScroll=0;}
void FullCatalogScreen::moveDetailScroll(int d){state_.detailScroll+=d<0?-24:24;clampDetailScroll();}
void FullCatalogScreen::changeCatalog(int d){int v=(int)state_.catalog+d,c=(int)CatalogType::Count;if(v<0)v=c-1;if(v>=c)v=0;CatalogType next=(CatalogType)v;if(catalogChange_){if(catalogChange_(next)){catalogLoading_=true;catalogLoadingLabel_=catalogName(next);catalogLoadingCurrent_=0;catalogLoadingTotal_=0;catalogLoadingMessage_="Checking catalog cache...";catalogError_.clear();}return;}setActiveCatalog(next);}
bool FullCatalogScreen::isTransitioning()const{return state_.mode==UiMode::OPENING_DETAIL||state_.mode==UiMode::CLOSING_DETAIL;}
void FullCatalogScreen::startOpeningDetail(){if(state_.mode!=UiMode::FULL_CATALOG||catalogLoading_||installProgressActive_||selectedIndex()<0)return;state_.detailScroll=0;state_.transitionStart=sceKernelGetProcessTimeWide();state_.mode=UiMode::OPENING_DETAIL;diagnostics::log("[UI] opening detail");}
void FullCatalogScreen::startClosingDetail(){if(state_.mode!=UiMode::SPLIT_DETAIL)return;state_.transitionStart=sceKernelGetProcessTimeWide();state_.mode=UiMode::CLOSING_DETAIL;diagnostics::log("[UI] closing detail");}
float FullCatalogScreen::transitionProgress()const{if(!isTransitioning())return 1.0f;uint64_t e=sceKernelGetProcessTimeWide()-state_.transitionStart;return std::max(0.0f,std::min(1.0f,(float)e/(float)(TRANSITION_MS*1000)));}
void FullCatalogScreen::updateTransition(){if(!isTransitioning()||transitionProgress()<1.0f)return;state_.mode=state_.mode==UiMode::OPENING_DETAIL?UiMode::SPLIT_DETAIL:UiMode::FULL_CATALOG;state_.activePanel=UiPanel::Catalog;clampCatalogScroll();}

void FullCatalogScreen::handleInput(){if(isTransitioning())return;SceCtrlData p{};sceCtrlPeekBufferPositive(0,&p,1);static uint32_t prev=0;uint32_t pressed=p.buttons&~prev;prev=p.buttons;if(pressed&SCE_CTRL_START){state_.requestExit=true;diagnostics::log("[UI] exit requested");return;}if(pressed&SCE_CTRL_LTRIGGER){changeCatalog(-1);return;}if(pressed&SCE_CTRL_RTRIGGER){changeCatalog(1);return;}if(catalogLoading_||installProgressActive_)return;if(state_.mode==UiMode::FULL_CATALOG){if(pressed&SCE_CTRL_LEFT){if(state_.focusIndex%3>0)--state_.focusIndex;}if(pressed&SCE_CTRL_RIGHT){if(state_.focusIndex%3<2&&state_.focusIndex+1<(int)items_.size())++state_.focusIndex;}if(pressed&SCE_CTRL_UP)moveCatalogFocus(-1);if(pressed&SCE_CTRL_DOWN)moveCatalogFocus(1);clampCatalogScroll();if(pressed&SCE_CTRL_CROSS)startOpeningDetail();return;}if(state_.mode!=UiMode::SPLIT_DETAIL)return;if(pressed&SCE_CTRL_CIRCLE){startClosingDetail();return;}if(pressed&SCE_CTRL_LEFT)state_.activePanel=UiPanel::Catalog;if(pressed&SCE_CTRL_RIGHT)state_.activePanel=UiPanel::Detail;if(state_.activePanel==UiPanel::Catalog){if(pressed&SCE_CTRL_UP)moveCatalogFocus(-1);if(pressed&SCE_CTRL_DOWN)moveCatalogFocus(1);if(pressed&SCE_CTRL_CROSS)state_.detailScroll=0;}else{if(pressed&SCE_CTRL_UP)moveDetailScroll(-1);if(pressed&SCE_CTRL_DOWN)moveDetailScroll(1);if((pressed&SCE_CTRL_TRIANGLE)&&selectedIndex()>=0&&installRequest_)installRequest_(items_[selectedIndex()]);}}

unsigned FullCatalogScreen::colorForStatus(const std::string&s)const{if(s=="Verified")return ACCENT;if(s=="Legacy")return TEXT;if(s=="Archive")return DIM;return TEXT;}
void FullCatalogScreen::drawHeader(int w){vita2d_draw_rectangle(0,0,w,HEADER_H,SURFACE2);vita2d_pgf_draw_text(font_,16,32,ACCENT,1.12f,"PSVitaAlive Store");vita2d_pgf_draw_text(font_,w-132,32,DIM,.76f,"START: Exit");}
void FullCatalogScreen::drawTabs(int w){vita2d_draw_rectangle(0,HEADER_H,w,TABS_H,SURFACE);float tw=(float)w/(int)CatalogType::Count;for(int i=0;i<(int)CatalogType::Count;++i){int x=(int)(i*tw);bool a=(int)state_.catalog==i;if(a)vita2d_draw_rectangle(x,HEADER_H+TABS_H-3,(int)tw,3,ACCENT);vita2d_pgf_draw_text(font_,x+12,HEADER_H+24,a?ACCENT:TEXT,.80f,catalogName((CatalogType)i));}}

void FullCatalogScreen::drawImage(const std::string&url,const std::string&ns,int x,int y,int w,int h){vita2d_draw_rectangle(x,y,w,h,SURFACE2);if(!imageCache_||url.empty())return;std::string path=imageCache_->request(url,ns);if(imageCache_->isFailed(path)||!imageCache_->isReady(path))return;auto it=textures_.find(path);if(it==textures_.end()){evictTextureIfNeeded(ns);vita2d_texture*t=nullptr;const char*e=extOf(path);if(std::strcmp(e,".jpg")==0||std::strcmp(e,".jpeg")==0)t=vita2d_load_JPEG_file(path.c_str());else t=vita2d_load_PNG_file(path.c_str());if(!t){SceIoStat st={};long long size=-1;if(sceIoGetstat(path.c_str(),&st)>=0)size=(long long)st.st_size;char m[700];sceClibSnprintf(m,sizeof(m),"[UI] texture load failed ns=%s path=%s size=%lld",ns.c_str(),path.c_str(),size);diagnostics::log(m);return;}textures_[path]=t;textureOrder_.push_back(path);}else touchTexture(path);vita2d_texture*t=textures_[path];float tw=(float)vita2d_texture_get_width(t),th=(float)vita2d_texture_get_height(t);if(tw<=0||th<=0){diagnostics::log(std::string("[UI] invalid texture dimensions: ")+path);return;}float s=std::min((float)w/tw,(float)h/th),dw=tw*s,dh=th*s;vita2d_draw_texture_scale(t,x+(w-dw)/2.0f,y+(h-dh)/2.0f,s,s);}

void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){vita2d_draw_rectangle(x,y,w,h,SURFACE);if(focus){vita2d_draw_rectangle(x,y,w,3,ACCENT);vita2d_draw_rectangle(x,y+h-3,w,3,ACCENT);vita2d_draw_rectangle(x,y,3,h,ACCENT);vita2d_draw_rectangle(x+w-3,y,3,h,ACCENT);}else vita2d_draw_rectangle(x,y,w,1,BORDER);int is=h>=100?76:54;drawImage(!it.icon.empty()?it.icon:it.cover,"app",x+10,y+9,is,is);int tx=x+is+20;vita2d_pgf_draw_text(font_,tx,y+26,WHITE,.80f,it.name.c_str());vita2d_pgf_draw_text(font_,tx,y+46,TEXT,.66f,it.author.empty()?"Unknown author":it.author.c_str());vita2d_pgf_draw_text(font_,tx,y+66,colorForStatus(it.status),.64f,it.status.c_str());std::string meta=(it.version.empty()?"":"v"+it.version)+(it.size.empty()?"":"  "+it.size);if(!meta.empty())vita2d_pgf_draw_text(font_,x+10,y+h-11,DIM,.60f,meta.c_str());(void)idx;}

void FullCatalogScreen::drawCatalogPanel(int x,int y,int w,int h,bool split){vita2d_draw_rectangle(x,y,w,h,PANEL);if(!split){const int vis=visibleRowsFull();const int rows=totalRows();const int cw=(w-GRID_PAD*2-CARD_GAP*2)/3;for(int r=0;r<vis;++r)for(int c=0;c<3;++c){int i=(state_.catalogScrollRow+r)*3+c;if(i<(int)items_.size())drawCatalogCard(items_[i],i,x+GRID_PAD+c*(cw+CARD_GAP),y+GRID_PAD+r*(FULL_CARD_H+CARD_GAP),cw,FULL_CARD_H,i==state_.focusIndex);}if(rows>vis){const int tx=x+w-7,ty=y+10,th=h-20;vita2d_draw_rectangle(tx,ty,3,th,BORDER);const int thumb=std::max(20,th*vis/rows);const int maxRow=std::max(1,rows-vis);const int yy=ty+(th-thumb)*state_.catalogScrollRow/maxRow;vita2d_draw_rectangle(tx,yy,3,thumb,ACCENT);}}else for(int r=0;r<visibleRowsSplit();++r){int i=state_.catalogScrollRow+r;if(i<(int)items_.size())drawCatalogCard(items_[i],i,x+GRID_PAD,y+GRID_PAD+r*(SPLIT_CARD_H+CARD_GAP),w-GRID_PAD*2,SPLIT_CARD_H,i==state_.focusIndex);}if(state_.mode==UiMode::SPLIT_DETAIL&&state_.activePanel==UiPanel::Catalog)vita2d_draw_rectangle(x,y,3,h,ACCENT);}

void FullCatalogScreen::wrapText(const std::string&text,int max,std::vector<std::string>&out)const{out.clear();std::string cur;for(char c:text){if(c=='\n'){out.push_back(cur);cur.clear();continue;}if((int)cur.size()>=max&&c==' '){out.push_back(cur);cur.clear();continue;}cur.push_back(c);if((int)cur.size()>=max){out.push_back(cur);cur.clear();}}if(!cur.empty())out.push_back(cur);}
void FullCatalogScreen::drawTextLines(const std::vector<std::string>&l,int x,int y,int lh,unsigned col,float sc,int start,int max,int clipTop,int clipBottom){int first=std::max(0,start),last=std::min((int)l.size(),first+max),dy=y+first*lh;for(int i=first;i<last;++i){if(dy>=clipTop&&dy<=clipBottom)vita2d_pgf_draw_text(font_,x,dy,col,sc,l[i].c_str());dy+=lh;}}

void FullCatalogScreen::drawDetailContent(const CatalogItem&it,int x,int y,int w,int h){
    const int cx=x+18,cw=w-36,max=std::max(18,cw/7);
    const int screenshotCount=std::min(5,(int)it.screenshots.size());
    const int screenshotRows=(screenshotCount+2)/3;
    const int sw=std::max(70,(cw-32)/3);
    std::vector<std::string>lines;
    auto add=[&](const char*t,const std::string&v){if(v.empty())return;lines.push_back(t);std::vector<std::string>q;wrapText(v,max,q);for(auto&s:q)lines.push_back(s);lines.push_back("");};
    add("Description",it.description);add("Long Description",it.longDescription);add("Requirements",it.requirements);
    lines.push_back("Information");lines.push_back("Title ID: "+it.titleId);lines.push_back("Version: "+it.version);lines.push_back("Release date: "+it.versionDate);lines.push_back("Category: "+it.category);lines.push_back("Subcategory: "+it.subcategory);lines.push_back("Size: "+it.size);lines.push_back("Status: "+it.status);lines.push_back("");
    if(!it.linkDetails.empty()){lines.push_back("Downloads & Links");for(const auto&l:it.linkDetails){std::string s=l.type;if(!l.name.empty()){if(!s.empty())s+=": ";s+=l.name;}if(l.recommended)s+=" [Recommended]";lines.push_back("- "+s);}lines.push_back("");}
    add("Changelog",it.changelog);

    const int contentTop=y+DETAIL_HEADER_H+10;
    const int contentBottom=y+h-10;
    const int screenshotHeight=screenshotRows*80;
    const int textGap=screenshotCount?12:6;
    const int visibleHeight=std::max(1,h-DETAIL_HEADER_H-18);
    const int totalContent=screenshotHeight+textGap+(int)lines.size()*LINE_H;
    const int maxScroll=std::max(0,totalContent-visibleHeight);
    const int scroll=std::min(std::max(0,state_.detailScroll),maxScroll);
    const int textTop=contentTop+screenshotHeight+textGap;

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x+2,contentTop,x+w-18,contentBottom);

    for(int i=0;i<screenshotCount;++i){
        int row=i/3,col=i%3;
        int sy=contentTop+row*80-scroll;
        drawImage(it.screenshots[i],"shot",cx+col*(sw+8),sy,sw,72);
    }

    const int visibleLines=std::max(1,visibleHeight/LINE_H+1);
    const int firstLine=std::min((int)lines.size(),scroll>screenshotHeight+textGap?std::max(0,(scroll-screenshotHeight-textGap)/LINE_H):0);
    drawTextLines(lines,cx,textTop-scroll,LINE_H,TEXT,.68f,firstLine,visibleLines,contentTop,contentBottom);
    vita2d_disable_clipping();

    if(maxScroll>0){
        int tx=x+w-8,ty=y+DETAIL_HEADER_H+8,th=h-DETAIL_HEADER_H-18;
        vita2d_draw_rectangle(tx,ty,3,th,BORDER);
        int thumb=std::max(20,th*visibleHeight/std::max(1,totalContent));
        int yy=ty+(th-thumb)*scroll/maxScroll;
        vita2d_draw_rectangle(tx,yy,3,thumb,ACCENT);
    }
}

void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){vita2d_draw_rectangle(x,y,w,h,PANEL);int i=selectedIndex();if(i<0)return;const CatalogItem&it=items_[i];if(state_.activePanel==UiPanel::Detail)vita2d_draw_rectangle(x+w-3,y,3,h,ACCENT);vita2d_draw_rectangle(x,y,w,DETAIL_HEADER_H,SURFACE);drawImage(!it.icon.empty()?it.icon:it.cover,"app",x+12,y+12,68,68);vita2d_pgf_draw_text(font_,x+92,y+29,WHITE,.90f,it.name.c_str());vita2d_pgf_draw_text(font_,x+92,y+50,TEXT,.68f,it.author.empty()?"Unknown author":it.author.c_str());vita2d_pgf_draw_text(font_,x+92,y+70,colorForStatus(it.status),.66f,it.status.c_str());if(state_.activePanel==UiPanel::Detail&&!it.downloadUrl.empty())vita2d_pgf_draw_text(font_,x+w-105,y+26,ACCENT,.60f,"△ Install");drawDetailContent(it,x,y,w,h);}

void FullCatalogScreen::drawLoadingOverlay(){const bool install=installProgressActive_;int w=620,h=250,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,185));vita2d_draw_rectangle(x,y,w,h,SURFACE2);vita2d_draw_rectangle(x,y,w,2,ACCENT);const std::string title=install?"Installing / Downloading":"Loading "+catalogLoadingLabel_+"...";vita2d_pgf_draw_text(font_,x+28,y+32,ACCENT,.68f,"PSVitaAlive");vita2d_pgf_draw_text(font_,x+28,y+68,WHITE,1.02f,title.c_str());if(install){const std::string stage=installProgressStage_.empty()?"Preparing":installProgressStage_;const std::string file=installProgressFile_.empty()?"File":installProgressFile_;vita2d_pgf_draw_text(font_,x+28,y+94,ACCENT,.72f,stage.c_str());vita2d_pgf_draw_text(font_,x+28,y+118,TEXT,.62f,file.c_str());vita2d_pgf_draw_text(font_,x+28,y+142,TEXT,.64f,installProgressMessage_.empty()?"Working...":installProgressMessage_.c_str());int bx=x+28,by=y+160,bw=w-56,bh=12;vita2d_draw_rectangle(bx,by,bw,bh,BORDER);uint32_t pct=installProgressTotal_?(uint32_t)std::min<uint64_t>(100,(installProgressCurrent_*100)/installProgressTotal_):0;vita2d_draw_rectangle(bx,by,bw*pct/100,bh,ACCENT);char p[64];sceClibSnprintf(p,sizeof(p),"%u%%   %s / %s",pct,formatBytes(installProgressCurrent_).c_str(),installProgressTotal_?formatBytes(installProgressTotal_).c_str():"?");vita2d_pgf_draw_text(font_,x+28,y+194,TEXT,.66f,p);char s[64];sceClibSnprintf(s,sizeof(s),"Speed: %s/s",formatBytes(installProgressSpeed_).c_str());vita2d_pgf_draw_text(font_,x+w-190,y+194,DIM,.60f,s);}else{vita2d_pgf_draw_text(font_,x+28,y+100,TEXT,.68f,catalogLoadingMessage_.c_str());int bx=x+28,by=y+132,bw=w-56,bh=12;vita2d_draw_rectangle(bx,by,bw,bh,BORDER);uint32_t pct=0;if(catalogLoadingTotal_)pct=(uint32_t)std::min<uint64_t>(100,(catalogLoadingCurrent_*100)/catalogLoadingTotal_);else pct=(uint32_t)std::max<uint64_t>(10,(sceKernelGetProcessTimeWide()/30000)%100);vita2d_draw_rectangle(bx,by,bw*pct/100,bh,ACCENT);char p[24];sceClibSnprintf(p,sizeof(p),"%u%%",pct);vita2d_pgf_draw_text(font_,x+28,y+176,TEXT,.74f,p);vita2d_pgf_draw_text(font_,x+w-190,y+176,DIM,.60f,"Checking / downloading");}}

void FullCatalogScreen::drawFullCatalog(){vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);drawCatalogPanel(0,HEADER_H+TABS_H,SCREEN_W,SCREEN_H-HEADER_H-TABS_H-FOOTER_H,false);vita2d_draw_rectangle(0,SCREEN_H-FOOTER_H,SCREEN_W,FOOTER_H,SURFACE2);vita2d_pgf_draw_text(font_,12,SCREEN_H-14,TEXT,.62f,"D-Pad: Navigate   X: Detail   L/R: Catalog   START: Exit");if(catalogLoading_||installProgressActive_)drawLoadingOverlay();if(!catalogError_.empty())vita2d_pgf_draw_text(font_,18,HEADER_H+TABS_H+26,ACCENT,.68f,catalogError_.c_str());vita2d_end_drawing();vita2d_swap_buffers();}
void FullCatalogScreen::drawSplitDetail(){vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H,lw=SCREEN_W/2;drawCatalogPanel(0,top,lw,hh,true);drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_draw_rectangle(lw-1,top,2,hh,BORDER);vita2d_draw_rectangle(0,SCREEN_H-FOOTER_H,SCREEN_W,FOOTER_H,SURFACE2);vita2d_pgf_draw_text(font_,12,SCREEN_H-14,TEXT,.58f,"D-Pad: Navigate/Scroll   LEFT/RIGHT: Panel   O: Back   △: Install   L/R: Catalog");if(catalogLoading_||installProgressActive_)drawLoadingOverlay();vita2d_end_drawing();vita2d_swap_buffers();}
void FullCatalogScreen::drawOpeningDetail(){float p=transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,rw,hh);vita2d_end_drawing();vita2d_swap_buffers();}
void FullCatalogScreen::drawClosingDetail(){float p=1.0f-transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,rw,hh);vita2d_end_drawing();vita2d_swap_buffers();}
void FullCatalogScreen::draw(){switch(state_.mode){case UiMode::FULL_CATALOG:drawFullCatalog();break;case UiMode::OPENING_DETAIL:drawOpeningDetail();break;case UiMode::SPLIT_DETAIL:drawSplitDetail();break;case UiMode::CLOSING_DETAIL:drawClosingDetail();break;}}
bool FullCatalogScreen::updateAndDraw(){if(!ready_)return false;handleInput();updateTransition();draw();return !state_.requestExit;}

} // namespace psvitaalive::ui
