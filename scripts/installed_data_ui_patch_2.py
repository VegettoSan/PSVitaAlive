from pathlib import Path
p=Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
s=p.read_text(encoding="utf-8")
def change(a,b):
 global s
 assert s.count(a)==1, (s.count(a),a[:90])
 s=s.replace(a,b,1)

# Modal owns gamepad before UX0 storage or catalog buttons.
change('void FullCatalogScreen::handleInput(){\n    if (storageVisible_) {',
'''void FullCatalogScreen::handleInput(){
    if (installedDataVisible_) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0,&pad,1);
        static uint32_t prevIdButtons=0,prevIdAnalog=0;
        static uint64_t idRepeatAt=0;
        const uint32_t pressed=pad.buttons & ~prevIdButtons;
        const uint32_t analog=leftStickNavMask(pad);
        const uint32_t direct=(pressed & NAV_DIRECTION_MASK)|
                              (analog & ~prevIdAnalog);
        const uint32_t held=(pad.buttons & NAV_DIRECTION_MASK)|analog;
        const uint64_t now=sceKernelGetProcessTimeWide();
        uint32_t repeat=0;
        if(!held) idRepeatAt=0;
        else if(direct) idRepeatAt=now+330000ULL;
        else if(idRepeatAt&&now>=idRepeatAt){
            repeat=held;idRepeatAt=now+130000ULL;
        }
        prevIdButtons=pad.buttons;
        prevIdAnalog=analog;
        handleInstalledDataInput(pressed,direct|repeat);
        return;
    }
    if (storageVisible_) {''')

# Dedicated touchscreen list/detail/confirmation, before storage touch handler.
touch='''    if (installedDataVisible_) {
        constexpr int w=864,h=500,ROW=54;
        const int ox=(SCREEN_W-w)/2,oy=(SCREEN_H-h)/2;
        if(td.reportNum>0) {
            const int px=mapX(td.report[0].x),py=mapY(td.report[0].y);
            if(!touchDown_) {
                touchDown_=true;touchStartX_=px;touchStartY_=py;
                touchLastY_=py;touchMoved_=false;
            }else{
                if(std::abs(px-touchStartX_)>14||std::abs(py-touchStartY_)>14)
                    touchMoved_=true;
                if(touchMoved_&&installedDataWorker_<0&&!installedDataDetail_&&
                   !installedDataConfirm_&&std::abs(touchLastY_-py)>=23){
                    const int step=touchLastY_>py?1:-1;
                    installedDataScroll_=std::max(0,std::min(
                        std::max(0,(int)installedDataVisibleRows_.size()-5),
                        installedDataScroll_+step));
                    touchLastY_=py;
                }
            }
        }else if(touchDown_){
            const int px=touchStartX_,py=touchStartY_;
            const bool moved=touchMoved_;
            touchDown_=touchMoved_=false;
            if(moved||installedDataWorker_>=0)return;
            if(installedDataConfirm_){
                if(hit(px,py,ox+30,oy+383,w-60,57))
                    activateInstalledDataSelection();
                else installedDataConfirm_=false;
                return;
            }
            if(!installedDataDetail_){
                if(hit(px,py,ox+23,oy+140,w-46,5*ROW)){
                    const int row=(py-(oy+140))/ROW+installedDataScroll_;
                    if(row>=0&&row<(int)installedDataVisibleRows_.size()){
                        installedDataListFocus_=row;
                        activateInstalledDataSelection();
                    }
                }else if(py>oy+h-55)closeInstalledDataMenu();
            }else{
                for(int i=0;i<4;i++){
                    if(hit(px,py,ox+24+i*204,oy+378,195,52)){
                        installedDataActionFocus_=i;
                        activateInstalledDataSelection();
                        break;
                    }
                }
                if(py>oy+h-55){
                    if(installedDataPreferredId_.empty())installedDataDetail_=false;
                    else closeInstalledDataMenu();
                }
            }
        }
        return;
    }

'''
change('    if (storageVisible_) {\n        constexpr int w=856,h=500,viewportH=286;',
      touch+'    if (storageVisible_) {\n        constexpr int w=856,h=500,viewportH=286;')

# Keep original Request Data affordance for noninstalled Homebrew. For
# installed Homebrew the header's second button is explicitly Uninstall.
change('        if (!state_.linkNavigation && itemEligibleForDataRequest(it)) {',
'''        const LocalInstallInfo localUninstall=queryLocalInstall(it);
        const bool canUninstall=state_.catalog==CatalogType::Homebrew &&
            it.titleId!="PSVAS1178" &&
            (localUninstall.state==LocalInstallState::Installed ||
             localUninstall.state==LocalInstallState::UpdateAvailable ||
             localUninstall.state==LocalInstallState::InstalledUnknown);
        if(!state_.linkNavigation && canUninstall){
            const int ubY=by+bh+6;
            vita2d_draw_rectangle(bx,ubY,bw,32,SURFACE2);
            vita2d_draw_rectangle(bx,ubY,bw,2,ACCENT);
            drawMarqueeText(&font_,bx+8,ubY+22,bw-16,ACCENT,.65f,
                ::psvitaalive::L("ID_UNINSTALL_BUTTON"),true,
                bx+4,ubY,bx+bw-4,ubY+32);
        }else if(!state_.linkNavigation && itemEligibleForDataRequest(it)){''')
change('            if (!state_.linkNavigation && itemEligibleForDataRequest(tapItem)) {',
'''            const LocalInstallInfo liUninstall=queryLocalInstall(tapItem);
            const bool uninstallButton=state_.catalog==CatalogType::Homebrew &&
                tapItem.titleId!="PSVAS1178" &&
                (liUninstall.state==LocalInstallState::Installed ||
                 liUninstall.state==LocalInstallState::UpdateAvailable ||
                 liUninstall.state==LocalInstallState::InstalledUnknown);
            if(!state_.linkNavigation && uninstallButton &&
               hit(x,y,bx,by+bh+6,bw,32)){
                openInstalledDataMenu(false,tapItem.titleId);
                return;
            }
            if(!state_.linkNavigation && !uninstallButton &&
               itemEligibleForDataRequest(tapItem)){''')

# PS TV can invoke the visible header action via Triangle when detail focused.
change('if(catalogLoading_)return;if(pressed&SCE_CTRL_SQUARE){',
'''if(catalogLoading_)return;
if((pressed&SCE_CTRL_TRIANGLE)&&state_.mode==UiMode::SPLIT_DETAIL&&
   state_.activePanel==UiPanel::Detail&&!state_.linkNavigation&&
   state_.catalog==CatalogType::Homebrew){
    const int i=selectedIndex();
    if(i>=0){
        const auto& item=catalogView()[i];
        const auto local=queryLocalInstall(item);
        if(item.titleId!="PSVAS1178" &&
           (local.state==LocalInstallState::Installed ||
            local.state==LocalInstallState::UpdateAvailable ||
            local.state==LocalInstallState::InstalledUnknown)){
            openInstalledDataMenu(false,item.titleId);
            return;
        }
    }
}
if(pressed&SCE_CTRL_SQUARE){''')
p.write_text(s,encoding="utf-8")
print("Applied installed data modal and uninstall button patch 2")
