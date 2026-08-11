from pathlib import Path
import re

ROOT=Path('Client PSVitaAlive')

def replace_fn(text, sig, repl):
    a=text.find(sig)
    if a<0: raise RuntimeError('missing '+sig)
    b=text.find('{',a)
    depth=0;i=b;quote=None;esc=False
    while i<len(text):
        c=text[i]
        if quote:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c==quote: quote=None
        else:
            if c in ('"',"'"): quote=c
            elif c=='{': depth+=1
            elif c=='}':
                depth-=1
                if depth==0:return text[:a]+repl+text[i+1:]
        i+=1
    raise RuntimeError('unclosed '+sig)

# DownloadManager cancellation cleanup.
p=ROOT/'source/network/download_manager.cpp';s=p.read_text()
if 'void DownloadManager::cancel(const std::string& jobId){if(auto*j=findJob(jobId)){j->cancelRequested=true;StorageManager st;' not in s:
    s=replace_fn(s,'void DownloadManager::cancel(const std::string& jobId)', 'void DownloadManager::cancel(const std::string& jobId){if(auto*j=findJob(jobId)){j->cancelRequested=true;StorageManager st;if(j->state==DownloadState::Queued){st.removeFile(j->temporaryPath);j->state=DownloadState::Cancelled;j->lastError="cancelled";saveMetadata(*j);}}}')
s,n=re.subn(r'if\s*\(hr == HttpResult::Cancelled \|\| job\.cancelRequested\)\s*\{.*?return false;\s*\}', 'if(hr==HttpResult::Cancelled||job.cancelRequested){st.removeFile(job.temporaryPath);job.downloadedSize=0;job.state=DownloadState::Cancelled;job.lastError="cancelled";saveMetadata(job);return false;}', s, count=1, flags=re.S)
if n!=1: raise RuntimeError('download cancellation branch missing')
p.write_text(s)

# InstallController cancellation.
p=ROOT/'source/installer/install_controller.cpp';s=p.read_text()
if 'void InstallController::cancel()' not in s:
    s=s.replace('bool InstallController::busy() const {','void InstallController::cancel(){const auto currentState=static_cast<InstallStatus::State>(state_.load());if(currentState!=InstallStatus::State::Downloading||activeJobId_.empty())return;downloads_.cancel(activeJobId_);setStage("Cancelling");setState(InstallStatus::State::Downloading,"Cancelling download...");diagnostics::log(std::string("[Installer] cancel requested job=")+activeJobId_);}\n\nbool InstallController::busy() const {',1)
s,n=re.subn(r'if\s*\(!downloaded \|\| !job \|\| job->state != DownloadState::Completed\)\s*\{.*?return 0;\s*}', 'if(!downloaded||!job||job->state!=DownloadState::Completed){const bool cancelled=job&&job->state==DownloadState::Cancelled;const std::string error=cancelled?"Download cancelled":(job&&!job->lastError.empty()?job->lastError:"Download failed");setStage(cancelled?"Cancelled":"Error");setState(InstallStatus::State::Failed,error.c_str());diagnostics::log(std::string("[Installer] ")+(cancelled?"download cancelled":"download failed")+": "+error);if(!activeJobId_.empty())downloads_.cleanupCompletedJob(activeJobId_);activeJobId_.clear();sceKernelDelayThread(cancelled?700*1000:2500*1000);setState(InstallStatus::State::Idle,"Ready");workerDone_.store(true);return 0;}', s, count=1, flags=re.S)
if n!=1: raise RuntimeError('install cancellation branch missing')
p.write_text(s)

# FullCatalogScreen download overlay/cancel wiring.
p=ROOT/'source/ui/full_catalog_screen.cpp';s=p.read_text()
if 'std::string formatEta(uint64_t seconds)' not in s:
    s=s.replace('FullCatalogScreen::FullCatalogScreen()=default;','std::string formatEta(uint64_t seconds){if(seconds==0)return "--";uint64_t h=seconds/3600,m=(seconds%3600)/60,sec=seconds%60;char o[64];if(h)sceClibSnprintf(o,sizeof(o),"%llu:%02llu:%02llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)sec);else sceClibSnprintf(o,sizeof(o),"%02llu:%02llu",(unsigned long long)m,(unsigned long long)sec);return o;}\nFullCatalogScreen::FullCatalogScreen()=default;',1)
if 'void FullCatalogScreen::setInstallCancelCallback' not in s:
    s=s.replace('void FullCatalogScreen::setCatalogChangeCallback','void FullCatalogScreen::setInstallCancelCallback(InstallCancelFn c){installCancel_=std::move(c);}void FullCatalogScreen::setCatalogChangeCallback',1)
overlay='''void FullCatalogScreen::drawLoadingOverlay(){const int w=620,h=350,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,110));vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);vita2d_draw_rectangle(x,y+2,2,h-4,ACCENT);vita2d_draw_rectangle(x+w-2,y+2,2,h-4,BORDER);vita2d_draw_rectangle(x,y+h-2,w,2,BORDER);if(catalogLoading_){vita2d_pgf_draw_text(font_,x+28,y+36,ACCENT,.68f,"PSVitaAlive");vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,catalogLoadingLabel_.empty()?"Loading catalog":catalogLoadingLabel_.c_str());vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.60f,catalogLoadingMessage_.empty()?"Preparing...":catalogLoadingMessage_.c_str());uint64_t pct=catalogLoadingTotal_?std::min<uint64_t>(100,(catalogLoadingCurrent_*100)/catalogLoadingTotal_):0;int bx=x+28,by=y+140,bw=w-56,bh=12;vita2d_draw_rectangle(bx,by,bw,bh,BORDER);vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);char st[160];sceClibSnprintf(st,sizeof(st),"%llu%%  %llu / %llu",(unsigned long long)pct,(unsigned long long)catalogLoadingCurrent_,(unsigned long long)catalogLoadingTotal_);vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,st);return;}vita2d_pgf_draw_text(font_,x+28,y+36,ACCENT,.68f,"PSVitaAlive");const bool downloading=installProgressStage_=="Downloading"||installProgressStage_=="Cancelling"||installProgressStage_.empty();vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,downloading?"Downloading":"Installing");std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,72);vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.62f,file.c_str());const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;int bx=x+28,by=y+140,bw=w-56,bh=12;vita2d_draw_rectangle(bx,by,bw,bh,BORDER);vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);char stats[220];sceClibSnprintf(stats,sizeof(stats),"%llu%%  %s / %s  •  %s/s",(unsigned long long)pct,formatBytes(current).c_str(),total?formatBytes(total).c_str():"?",formatBytes(installProgressSpeed_).c_str());vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,stats);uint64_t eta=0;if(installProgressSpeed_>0&&total>current)eta=(total-current)/installProgressSpeed_;char info[180];sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: %s",formatEta(eta).c_str());vita2d_pgf_draw_text(font_,x+28,y+194,ACCENT,.62f,info);if(!installProgressMessage_.empty())vita2d_pgf_draw_text(font_,x+28,y+222,DIM,.54f,ellipsize(installProgressMessage_,82).c_str());const int by2=y+268,bw2=330,bh2=40;vita2d_draw_rectangle(x+28,by2,bw2,bh2,SURFACE2);vita2d_draw_rectangle(x+28,by2,bw2,1,BORDER);vita2d_pgf_draw_text(font_,x+92,by2+26,WHITE,.62f,"CIRCLE  CANCEL DOWNLOAD");vita2d_pgf_draw_text(font_,x+28,y+h-14,DIM,.50f,"Circle: Cancel download and remove incomplete file");}'''
s=replace_fn(s,'void FullCatalogScreen::drawLoadingOverlay()',overlay)
if 'installCancel_)installCancel_()' not in s:
    s=s.replace('if(catalogLoading_||installProgressActive_)return;','if(installProgressActive_&&(pressed&SCE_CTRL_CIRCLE)){if(installCancel_)installCancel_();return;}if(catalogLoading_||installProgressActive_)return;',1)
p.write_text(s)

# Main: visual consistency, non-blocking preparation/cancellation, overall file progress and ETA.
p=ROOT/'source/main.cpp';s=p.read_text()
if 'std::string formatEta(uint64_t seconds)' not in s:
    marker='bool promptDownloadAllImages(size_t totalImages)'
    helper='std::string formatEta(uint64_t seconds){if(seconds==0)return "--";uint64_t h=seconds/3600,m=(seconds%3600)/60,sec=seconds%60;char o[64];if(h)sceClibSnprintf(o,sizeof(o),"%llu:%02llu:%02llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)sec);else sceClibSnprintf(o,sizeof(o),"%02llu:%02llu",(unsigned long long)m,(unsigned long long)sec);return o;}\n'
    s=s.replace(marker,helper+marker,1)
# Replace prompt entirely for spacing and dark panel aesthetic.
prompt='''bool promptDownloadAllImages(size_t totalImages){vita2d_wait_rendering_done();vita2d_pgf*font=vita2d_load_default_pgf();if(!font)return false;bool yes=false,done=false;int selected=0;uint32_t prev=0;while(!done){SceCtrlData pad={};sceCtrlPeekBufferPositive(0,&pad,1);uint32_t pressed=pad.buttons&~prev;prev=pad.buttons;if(pressed&SCE_CTRL_LEFT)selected=0;if(pressed&SCE_CTRL_RIGHT)selected=1;if(pressed&SCE_CTRL_CROSS){yes=selected==0;done=true;}if(pressed&SCE_CTRL_CIRCLE){yes=false;done=true;}vita2d_start_drawing();vita2d_clear_screen();const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);const int w=620,h=360,x=(960-w)/2,y=(544-h)/2;vita2d_draw_rectangle(0,0,960,544,RGBA8(0x18,0x18,0x18,255));vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,70));vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);vita2d_draw_rectangle(x,y+2,2,h-4,ACCENT);vita2d_draw_rectangle(x+w-2,y+2,2,h-4,BORDER);vita2d_draw_rectangle(x,y+h-2,w,2,BORDER);vita2d_pgf_draw_text(font,x+28,y+36,ACCENT,.68f,"PSVitaAlive");vita2d_pgf_draw_text(font,x+28,y+78,WHITE,1.02f,"Download catalog images?");vita2d_pgf_draw_text(font,x+28,y+108,TEXT,.58f,"Images are downloaded once and kept in the local cache.");vita2d_pgf_draw_text(font,x+28,y+130,DIM,.54f,"This can take a very long time and use network data.");char count[96];sceClibSnprintf(count,sizeof(count),"Pending images: %u",(unsigned)totalImages);vita2d_pgf_draw_text(font,x+28,y+160,ACCENT,.68f,count);vita2d_pgf_draw_text(font,x+28,y+190,TEXT,.54f,"Already cached images are excluded from this count.");vita2d_pgf_draw_text(font,x+28,y+212,TEXT,.54f,"Total size is calculated while downloading.");vita2d_pgf_draw_text(font,x+28,y+234,TEXT,.54f,"Speed, ETA and overall progress appear in the next panel.");int by=y+266,bw=220,bh=38;vita2d_draw_rectangle(x+58,by,bw,bh,selected==0?ACCENT:SURFACE);vita2d_draw_rectangle(x+342,by,bw,bh,selected==1?ACCENT:SURFACE);vita2d_pgf_draw_text(font,x+125,by+25,selected==0?BLACK:WHITE,.62f,"DOWNLOAD ALL");vita2d_pgf_draw_text(font,x+425,by+25,selected==1?BLACK:WHITE,.62f,"LATER");vita2d_pgf_draw_text(font,x+28,y+h-14,DIM,.52f,"Left/Right: Select    Cross: Confirm    Circle: Later");vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);}vita2d_wait_rendering_done();vita2d_free_pgf(font);return yes;}'''
s=replace_fn(s,'bool promptDownloadAllImages(size_t totalImages)',prompt)
# Add a preparation panel immediately before the queue is built.
if 'Preparing image downloads' not in s:
    marker='images.resetProgress();'
    prep='''images.resetProgress();vita2d_pgf*prepFont=vita2d_load_default_pgf();if(prepFont){vita2d_start_drawing();vita2d_clear_screen();vita2d_draw_rectangle(0,0,960,544,RGBA8(0x18,0x18,0x18,255));vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,70));const int pw=620,ph=280,px=(960-pw)/2,py=(544-ph)/2;vita2d_draw_rectangle(px,py,pw,ph,RGBA8(0x20,0x20,0x20,255));vita2d_draw_rectangle(px,py,pw,2,RGBA8(0x3B,0xFF,0,255));vita2d_pgf_draw_text(prepFont,px+28,py+44,RGBA8(0x3B,0xFF,0,255),.68f,"PSVitaAlive");vita2d_pgf_draw_text(prepFont,px+28,py+86,RGBA8(255,255,255,255),1.0f,"Preparing image downloads");vita2d_pgf_draw_text(prepFont,px+28,py+122,RGBA8(0xAA,0xAA,0xAA,255),.58f,"Building the download queue...");vita2d_pgf_draw_text(prepFont,px+28,py+160,RGBA8(0x3B,0xFF,0,255),.62f,"Please wait");vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);vita2d_free_pgf(prepFont);}'''
    s=s.replace(marker,prep,1)
# Replace the image progress overlay function with the new overall-progress version.
# Locate the existing block by the distinctive progress calculation.
s,n=re.subn(r'const uint64_t current=p\.downloaded,total=p\.total;.*?vita2d_pgf_draw_text\(font,x\+28,y\+168,TEXT,.58f,stats\);', 'const uint64_t current=p.downloaded,total=p.total;const uint64_t filePct=total?std::min<uint64_t>(100,(current*100)/total):0;const uint64_t overallPct=jobs.empty()?100:std::min<uint64_t>(100,((completed*10000)+(filePct*100))/jobs.size()/100);int bx=x+28,by=y+140,bw=w-56,bh=12;vita2d_draw_rectangle(bx,by,bw,bh,BORDER);vita2d_draw_rectangle(bx,by,bw*(int)overallPct/100,bh,ACCENT);char stats[220]={};sceClibSnprintf(stats,sizeof(stats),"%llu%%  Files: %llu / %u  •  %s/s",(unsigned long long)overallPct,(unsigned long long)completed,(unsigned)jobs.size(),formatBytes(p.speed).c_str());vita2d_pgf_draw_text(font,x+28,y+168,TEXT,.58f,stats);', s, count=1, flags=re.S)
if n!=1: print('warning: image progress block not found')
# Add ETA near the image overlay statistics.
if 'futureFiles' not in s:
    marker='vita2d_pgf_draw_text(font,x+28,y+168,TEXT,.58f,stats);'
    eta='uint64_t eta=0;if(p.speed>0){uint64_t avg=completed?p.completedBytes/completed:(p.total?p.total:0);uint64_t futureFiles=jobs.size()>completed+(p.active?1:0)?jobs.size()-completed-(p.active?1:0):0;uint64_t remaining=(p.total>p.downloaded?p.total-p.downloaded:0)+avg*futureFiles;eta=remaining/p.speed;}char etaText[96];sceClibSnprintf(etaText,sizeof(etaText),"ETA: %s",formatEta(eta).c_str());vita2d_pgf_draw_text(font,x+28,y+194,ACCENT,.62f,etaText);'
    s=s.replace(marker,marker+eta,1)
# Make Circle cancellation show a cancellation panel and wait for the worker to stop instead of falling through.
if 'Cancelling image downloads' not in s:
    s=s.replace('if(pad.buttons&SCE_CTRL_CIRCLE){images.cancelAll();cancelled=true;}','if(pad.buttons&SCE_CTRL_CIRCLE&&!cancelled){images.cancelAll();cancelled=true;}if(cancelled){vita2d_start_drawing();vita2d_clear_screen();vita2d_draw_rectangle(0,0,960,544,RGBA8(0x18,0x18,0x18,255));vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,70));vita2d_draw_rectangle(170,142,620,260,RGBA8(0x20,0x20,0x20,255));vita2d_draw_rectangle(170,142,620,2,RGBA8(0x3B,0xFF,0,255));vita2d_pgf_draw_text(font,198,184,RGBA8(0x3B,0xFF,0,255),.68f,"PSVitaAlive");vita2d_pgf_draw_text(font,198,226,RGBA8(255,255,255,255),1.0f,"Cancelling image downloads");vita2d_pgf_draw_text(font,198,262,RGBA8(0xAA,0xAA,0xAA,255),.58f,"Stopping transfer and removing incomplete file...");vita2d_end_drawing();vita2d_swap_buffers();if(!images.progress().active)break;sceKernelDelayThread(16*1000);continue;}',1)
# Wire cancel callback.
if 'setInstallCancelCallback' not in s:
    m=re.search(r'(\b[A-Za-z_]\w*)\.setInstallCallbacks\((.*?)\);',s,re.S)
    ctrls=re.findall(r'InstallController\s+([A-Za-z_]\w*)',s)
    if not m or not ctrls: raise RuntimeError('cannot wire install cancellation')
    s=s[:m.end()]+f'\n    {m.group(1)}.setInstallCancelCallback([&{ctrls[0]}](){{ {ctrls[0]}.cancel(); }});'+s[m.end():]
p.write_text(s)

print('patch_download_ui.py: patch complete')
