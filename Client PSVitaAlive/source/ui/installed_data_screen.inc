// Installed Homebrew Game/Data Files inspector.
// Included at the end of full_catalog_screen.cpp, inside psvitaalive::ui.
// All filesystem traversal and package removals are performed by its worker.

void FullCatalogScreen::rebuildInstalledDataCandidates() {
    using ::psvitaalive::storage::InstalledDataApp;
    using ::psvitaalive::storage::safeExternalDataPath;
    installedDataCandidates_.clear();
    installedDataCandidates_.reserve(180);
    for (const CatalogItem& item : allItems_) {
        if (!::psvitaalive::storage::validInstalledTitleId(item.titleId) ||
            item.titleId == "PSVAS1178") continue;
        InstalledDataApp app;
        app.appId = item.id;
        app.titleId = item.titleId;
        app.name = item.name;
        bool hasDataLink = false;
        bool hasGameRoots = false;
        bool missingDataRoot = false;
        std::set<std::string> gameVariants;
        for (const CatalogLink& link : item.linkDetails) {
            const bool game = isGameFilesTypeLink(link);
            const bool data = isDataFilesTypeLink(link);
            if (!game && !data) continue;
            hasDataLink = true;
            const std::string& extract = link.extractPath;
            if (extract == "ux0:app/" ||
                extract == ("ux0:app/" + item.titleId + "/")) {
                // Managed by Vita package uninstall, not recursive file removal.
                continue;
            }
            if (extract.compare(0, 9, "ux0:data/") != 0) {
                app.unresolved = true;
                continue;
            }
            if (link.installedPaths.empty()) {
                if (game) app.unresolved = true;
                else missingDataRoot = true; // may inherit known Game Files
                continue;
            }
            std::string signature;
            for (const auto& relative : link.installedPaths) {
                const std::string full = "ux0:data/" + relative;
                if (!safeExternalDataPath(full) ||
                    (extract.size() > 9 &&
                     !(extract.compare(0, full.size(), full) == 0 &&
                       extract.size() > full.size() &&
                       extract[full.size()] == '/'))) {
                    app.unresolved = true;
                    continue;
                }
                app.roots.push_back(full);
                if (game) {
                    hasGameRoots = true;
                    signature += full + "\n";
                }
            }
            if (game && !signature.empty()) gameVariants.insert(signature);
        }
        if (missingDataRoot && !hasGameRoots) app.unresolved = true;
        if (gameVariants.size() > 1) app.unresolved = true;
        if (hasDataLink) installedDataCandidates_.push_back(std::move(app));
    }
}
void FullCatalogScreen::rebuildInstalledDataVisibleRows() {
    installedDataVisibleRows_.clear();
    for (size_t i = 0; i < installedDataSnapshot_.apps.size(); ++i) {
        const auto& app = installedDataSnapshot_.apps[i];
        bool present = app.packagePresent || app.titleId == installedDataPreferredId_;
        for (const auto& p : app.paths) present = present || p.exists;
        if (present) installedDataVisibleRows_.push_back(static_cast<int>(i));
    }
    installedDataListFocus_ = 0;
    for (size_t i = 0; i < installedDataVisibleRows_.size(); ++i)
        if (installedDataSnapshot_.apps[installedDataVisibleRows_[i]].titleId ==
            installedDataPreferredId_) { installedDataListFocus_ = static_cast<int>(i); break; }
    installedDataScroll_ = std::max(0, installedDataListFocus_ - 3);
}
void FullCatalogScreen::openInstalledDataMenu(bool fromStorage,
                                               const std::string& titleId) {
    if (installedDataVisible_ || installedDataWorker_ >= 0 ||
        storageWorker_ >= 0 || installProgressActive_ || catalogLoading_ ||
        (storageInstallerBusy_ && storageInstallerBusy_())) {
        showToast(::psvitaalive::L("STORAGE_BUSY"), 2800);
        return;
    }
    installedDataVisible_ = true;
    installedDataFromStorage_ = fromStorage;
    installedDataPreferredId_ = titleId;
    installedDataScanFocusId_.clear();
    installedDataTargetId_ = titleId;
    installedDataDetail_ = !titleId.empty();
    installedDataConfirm_ = false;
    installedDataActionFocus_ = 0;
    installedDataListFocus_ = 0;
    installedDataScroll_ = installedDataPathScroll_ = 0;
    // Homebrew without data links can still be removed via its detail button.
    if (!titleId.empty()) {
        bool found = false;
        for (const auto& c : installedDataCandidates_)
            if (c.titleId == titleId) { found = true; break; }
        if (!found && state_.catalog == CatalogType::Homebrew) {
            const int i = selectedIndex();
            if (i >= 0 && catalogView()[i].titleId == titleId) {
                ::psvitaalive::storage::InstalledDataApp extra;
                extra.appId = catalogView()[i].id;
                extra.titleId = titleId;
                extra.name = catalogView()[i].name;
                installedDataCandidates_.push_back(std::move(extra));
            }
        }
    }
    touchDown_ = touchMoved_ = false;
    startInstalledDataOperation(-1);
}
void FullCatalogScreen::closeInstalledDataMenu() {
    if (installedDataWorker_ >= 0) return;
    installedDataVisible_ = false;
    installedDataConfirm_ = false;
    installedDataDetail_ = false;
    installedDataTargetId_.clear();
    installedDataPreferredId_.clear();
    installedDataScanFocusId_.clear();
    installedDataSnapshot_.apps.clear();
    installedDataVisibleRows_.clear();
}
void FullCatalogScreen::startInstalledDataOperation(int action) {
    if (installedDataWorker_ >= 0) {
        if (!installedDataWorkerDone_.load(std::memory_order_acquire)) return;
        sceKernelWaitThreadEnd(installedDataWorker_, nullptr, nullptr);
        sceKernelDeleteThread(installedDataWorker_);
        installedDataWorker_ = -1;
    }
    if (storageWorker_ >= 0 || (storageInstallerBusy_ && storageInstallerBusy_())) {
        showToast(::psvitaalive::L("STORAGE_BUSY"), 2600);
        return;
    }
    installedDataAction_ = action;
    installedDataWorkerDone_.store(false, std::memory_order_release);
    installedDataWorkerOk_.store(false, std::memory_order_release);
    installedDataScanProgress_.reset();
    installedDataWorker_ = sceKernelCreateThread("PSVAInstalledData",
        &FullCatalogScreen::installedDataWorkerEntry,
        0x10000110, 96 * 1024, 0, 0, nullptr);
    if (installedDataWorker_ < 0) {
        installedDataWorkerDone_.store(true, std::memory_order_release);
        return;
    }
    FullCatalogScreen* self = this;
    if (sceKernelStartThread(installedDataWorker_, sizeof(self), &self) < 0) {
        sceKernelDeleteThread(installedDataWorker_);
        installedDataWorker_ = -1;
        installedDataWorkerDone_.store(true, std::memory_order_release);
    }
}
int FullCatalogScreen::installedDataWorkerEntry(SceSize, void* argp) {
    FullCatalogScreen* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    if (!self) return -1;
    using ::psvitaalive::storage::InstalledDataManager;
    const int action = self->installedDataAction_;
    const std::string titleId = self->installedDataTargetId_;
    bool ok = true;
    if (action >= 0) {
        if (action == 0 || action == 2) {
            ok = InstalledDataManager::uninstallPackage(titleId);
            if (ok) ::psvitaalive::update::InstalledReleaseTracker::removeReceipt(titleId);
        }
        if (ok && (action == 1 || action == 2)) {
            ok = InstalledDataManager::removeExternal(self->installedDataSnapshot_, titleId);
            if (ok) {
                ::psvitaalive::update::InstallReceipt receipt;
                if (::psvitaalive::update::InstalledReleaseTracker::readReceipt(titleId, receipt)) {
                    receipt.dataRevision = 0;
                    ::psvitaalive::update::InstalledReleaseTracker::writeReceipt(receipt);
                }
            }
        }
    }
    // Removing only the VPK cannot change data-folder sizes. Do not make a
    // person wait for another filesystem scan after package uninstallation.
    if (action == 0) {
        if (ok) {
            for (auto& app : self->installedDataSnapshot_.apps)
                if (app.titleId == titleId) app.packagePresent = false;
        }
        self->installedDataWorkerOk_.store(ok, std::memory_order_release);
        self->installedDataWorkerDone_.store(true, std::memory_order_release);
        return 0;
    }
    // Initial storage inspection: enumerate all declared Game/Data roots.
    // Initial detail inspection: touch ONLY the selected title's roots;
    // all other catalog entries remain metadata-only collision claims.
    const std::string scope = action >= 0 ? titleId :
        !self->installedDataScanFocusId_.empty() ? self->installedDataScanFocusId_ :
        (self->installedDataFromStorage_ ? "" : self->installedDataPreferredId_);
    const uint64_t budget = scope.empty() ? 22000000ULL : 7000000ULL;
    auto refreshed = InstalledDataManager::scan(
        self->installedDataCandidates_, scope, &self->installedDataScanProgress_,
        budget);
    if (action < 0 && self->installedDataScanFocusId_.empty()) {
        self->installedDataSnapshot_ = std::move(refreshed);
    } else {
        // A completed action or one focused row must not start another
        // full-catalog scan. Merge the inspected app into the global list;
        // all other catalog path claims remain available for collision checks.
        auto& previous = self->installedDataSnapshot_;
        for (auto& app : previous.apps) {
            if (app.titleId != titleId) continue;
            for (auto& fresh : refreshed.apps) {
                if (fresh.titleId != titleId) continue;
                app = std::move(fresh);
                break;
            }
            break;
        }
        previous.totalExternalBytes = InstalledDataManager::knownExternalBytes(previous);
        if (!refreshed.complete) previous.complete = false;
    }
    self->installedDataWorkerOk_.store(ok, std::memory_order_release);
    self->installedDataWorkerDone_.store(true, std::memory_order_release);
    return 0;
}
void FullCatalogScreen::pollInstalledDataOperation() {
    if (installedDataWorker_ < 0 ||
        !installedDataWorkerDone_.load(std::memory_order_acquire)) return;
    sceKernelWaitThreadEnd(installedDataWorker_, nullptr, nullptr);
    sceKernelDeleteThread(installedDataWorker_);
    installedDataWorker_ = -1;
    if (installedDataAction_ < 0 &&
        installedDataScanProgress_.cancelRequested.load(std::memory_order_acquire)) {
        showToast(::psvitaalive::L("ID_CANCELLED"), 3400);
    }
    if (installedDataAction_ >= 0) {
        const bool ok = installedDataWorkerOk_.load(std::memory_order_acquire);
        showToast(::psvitaalive::L(ok ? "ID_DONE" : "ID_FAILED"), 3700);
        invalidateInstallStatus(installedDataTargetId_);
        if (isInstallStateFilter()) rebuildFilteredItems();
        requestUx0SpaceRefresh();
    }
    const std::string focusId = installedDataScanFocusId_;
    rebuildInstalledDataVisibleRows();
    if (!focusId.empty()) {
        for (size_t i=0;i<installedDataVisibleRows_.size();++i) {
            if (installedDataSnapshot_.apps[installedDataVisibleRows_[i]].titleId == focusId) {
                installedDataListFocus_ = static_cast<int>(i);
                break;
            }
        }
        installedDataScanFocusId_.clear();
    }
    installedDataConfirm_ = false;
}
void FullCatalogScreen::activateInstalledDataSelection() {
    if (installedDataWorker_ >= 0) return;
    if (!installedDataDetail_) {
        if (installedDataListFocus_ < 0 ||
            installedDataListFocus_ >= static_cast<int>(installedDataVisibleRows_.size())) return;
        const auto& app = installedDataSnapshot_.apps[
            installedDataVisibleRows_[installedDataListFocus_]];
        installedDataTargetId_ = app.titleId;
        installedDataDetail_ = true;
        installedDataActionFocus_ = 0;
        installedDataPathScroll_ = 0;
        // A global scan can expire while counting a large game. Inspect
        // that single game's paths on demand, without redoing 150+ apps.
        bool needsDetails = false;
        for (const auto& path : app.paths)
            if (path.exists && !path.complete) needsDetails = true;
        if (needsDetails) {
            installedDataScanFocusId_ = app.titleId;
            startInstalledDataOperation(-1);
        }
        return;
    }
    if (installedDataActionFocus_ == 3) {
        if (installedDataPreferredId_.empty()) installedDataDetail_ = false;
        else closeInstalledDataMenu();
        return;
    }
    const ::psvitaalive::storage::InstalledDataApp* app = nullptr;
    for (const auto& candidate : installedDataSnapshot_.apps)
        if (candidate.titleId == installedDataTargetId_) { app = &candidate; break; }
    if (!app) return;
    const bool canPackage = app->packagePresent && app->titleId != "PSVAS1178";
    bool hasData = false;
    for (const auto& path : app->paths) hasData = hasData || path.exists;
    bool targetComplete = true;
    for (const auto& path : app->paths)
        if (path.exists && !path.complete) targetComplete = false;
    const bool canData = hasData && !app->blocked && !app->unresolved &&
        targetComplete;
    if ((installedDataActionFocus_ == 0 && !canPackage) ||
        (installedDataActionFocus_ == 1 && !canData) ||
        (installedDataActionFocus_ == 2 && (!canPackage || !canData))) {
        showToast(::psvitaalive::L("ID_UNSAFE"), 3400);
        return;
    }
    if (!installedDataConfirm_) {
        installedDataConfirm_ = true;
        return;
    }
    installedDataConfirm_ = false;
    startInstalledDataOperation(installedDataActionFocus_);
}
void FullCatalogScreen::handleInstalledDataInput(uint32_t pressed, uint32_t nav) {
    if (installedDataWorker_ >= 0) {
        // Cooperative cancellation only for read-only scans, never during
        // PromoterUtility or a filesystem deletion.
        if ((pressed & SCE_CTRL_CIRCLE) && installedDataAction_ == -1)
            installedDataScanProgress_.cancelRequested.store(
                true, std::memory_order_release);
        return;
    }
    if (installedDataConfirm_) {
        if (pressed & SCE_CTRL_CIRCLE) { installedDataConfirm_ = false; return; }
        if (pressed & SCE_CTRL_CROSS) activateInstalledDataSelection();
        return;
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        if (installedDataDetail_ && installedDataPreferredId_.empty())
            installedDataDetail_ = false;
        else closeInstalledDataMenu();
        return;
    }
    if (!installedDataDetail_) {
        const int rows = static_cast<int>(installedDataVisibleRows_.size());
        if (nav & (SCE_CTRL_UP|SCE_CTRL_LEFT))
            installedDataListFocus_ = std::max(0, installedDataListFocus_ -
                ((nav & SCE_CTRL_LEFT) ? 5 : 1));
        if (nav & (SCE_CTRL_DOWN|SCE_CTRL_RIGHT))
            installedDataListFocus_ = std::min(std::max(0, rows - 1),
                installedDataListFocus_ + ((nav & SCE_CTRL_RIGHT) ? 5 : 1));
        if (installedDataListFocus_ < installedDataScroll_)
            installedDataScroll_ = installedDataListFocus_;
        if (installedDataListFocus_ >= installedDataScroll_ + 5)
            installedDataScroll_ = installedDataListFocus_ - 4;
    } else {
        if (nav & (SCE_CTRL_LEFT|SCE_CTRL_UP))
            installedDataActionFocus_ = (installedDataActionFocus_ + 3) % 4;
        if (nav & (SCE_CTRL_RIGHT|SCE_CTRL_DOWN))
            installedDataActionFocus_ = (installedDataActionFocus_ + 1) % 4;
    }
    if (pressed & SCE_CTRL_CROSS) activateInstalledDataSelection();
}

void FullCatalogScreen::drawInstalledDataOverlay() {
    if (!installedDataVisible_) return;
    constexpr int W = 864, H = 500, VISIBLE = 5, ROW = 54;
    const int x = (SCREEN_W-W)/2, y=(SCREEN_H-H)/2;
    vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,190));
    vita2d_draw_rectangle(x,y,W,H,PANEL);
    vita2d_draw_rectangle(x,y,W,3,ACCENT);
    ::psvitaalive::ui::uiDrawText(&font_,x+25,y+40,WHITE,1.02f,
        ::psvitaalive::L("ID_TITLE"));
    if (installedDataWorker_ >= 0) {
        const auto& prog = installedDataScanProgress_;
        const uint32_t total = prog.appsTotal.load(std::memory_order_acquire);
        const uint32_t done = prog.appsCompleted.load(std::memory_order_acquire);
        const uint32_t phase = prog.phase.load(std::memory_order_acquire);
        const uint32_t pct = phase < 2 ? 0 :
            ::psvitaalive::storage::installedDataProgressPercent(done, total);
        const bool cancelled = prog.cancelRequested.load(std::memory_order_acquire);
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+128,ACCENT,1.0f,
            ::psvitaalive::L(cancelled ? "ID_CANCELLING" :
                installedDataAction_ < 0 ? "ID_SCANNING" : "ID_WORKING"));
        char textLine[160];
        sceClibSnprintf(textLine,sizeof(textLine),"%s %u / %u   (%u%%)",
            ::psvitaalive::L("ID_APPS_PROGRESS"),done,total,pct);
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+180,WHITE,.85f,textLine);
        // Percentage reflects *completed applications*, not guessed bytes.
        // The animated stripe proves that a huge individual folder is still
        // being traversed even while that percentage does not move.
        const int barX=x+28,barY=y+205,barW=W-56,barH=26;
        vita2d_draw_rectangle(barX,barY,barW,barH,SURFACE2);
        const int fill=static_cast<int>((static_cast<uint64_t>(barW)*pct)/100);
        if(fill>0) vita2d_draw_rectangle(barX,barY,fill,barH,ACCENT);
        const int scanX=barX+static_cast<int>(
            (sceKernelGetProcessTimeWide()/18000ULL) % (barW-44));
        vita2d_draw_rectangle(scanX,barY+barH-6,44,4,ACCENT);
        sceClibSnprintf(textLine,sizeof(textLine),"%s %u",
            ::psvitaalive::L("ID_FOLDERS_FOUND"),
            prog.rootsFound.load(std::memory_order_relaxed));
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+286,TEXT,.81f,textLine);
        sceClibSnprintf(textLine,sizeof(textLine),"%s %u  /  %s %u",
            ::psvitaalive::L("ID_FOLDERS_READ"),
            prog.dirsVisited.load(std::memory_order_relaxed),
            ::psvitaalive::L("ID_FILES_READ"),
            prog.filesVisited.load(std::memory_order_relaxed));
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+328,TEXT,.80f,textLine);
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+390,DIM,.74f,
            ::psvitaalive::L(phase==1 ? "ID_DISCOVERING" :
                "ID_SCAN_LIMIT_HINT"));
        if(installedDataAction_ == -1)
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+H-28,DIM,.75f,
                ::psvitaalive::L("ID_CANCEL_SCAN"));
        return;
    }
    if (installedDataConfirm_) {
        const ::psvitaalive::storage::InstalledDataApp* app = nullptr;
        for (const auto& candidate : installedDataSnapshot_.apps)
            if (candidate.titleId == installedDataTargetId_) {app=&candidate;break;}
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+88,ACCENT,1.06f,
            ::psvitaalive::L("ID_CONFIRM_TITLE"));
        if (app) ::psvitaalive::ui::uiDrawText(&font_,x+28,y+125,WHITE,.88f,
            ellipsize(app->name,54).c_str());
        // Never confirm a destructive external action without naming its
        // actual validated targets. Do not imply individual Data Files can be
        // deleted independently when Game/Data share a parent directory.
        if (app && installedDataActionFocus_ != 0) {
            for (size_t i=0;i<app->paths.size() && i<3;++i) {
                ::psvitaalive::ui::uiDrawText(&font_,x+32,
                    y+158+static_cast<int>(i)*22,TEXT,.66f,
                    ellipsize(app->paths[i].path,75).c_str());
            }
            if (app->paths.size()>3) {
                const std::string extra="+"+std::to_string(app->paths.size()-3);
                ::psvitaalive::ui::uiDrawText(&font_,x+W-87,y+215,DIM,.69f,
                    extra.c_str());
            }
        }
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+257,WHITE,.74f,
            ::psvitaalive::L("ID_WARNING1"));
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+286,TEXT,.73f,
            ::psvitaalive::L("ID_WARNING2"));
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+317,ACCENT,.74f,
            ::psvitaalive::L("ID_WARNING3"));
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+358,ACCENT,.74f,
            ::psvitaalive::L(installedDataActionFocus_==0 ? "ID_ONLY_APP" :
                installedDataActionFocus_==1 ? "ID_ONLY_DATA" : "ID_APP_DATA"));
        vita2d_draw_rectangle(x+30,y+383,W-60,57,SURFACE2);
        ::psvitaalive::ui::uiDrawText(&font_,x+55,y+419,WHITE,.82f,
            ::psvitaalive::L("ID_CONFIRM_CONTROLS"));
        return;
    }
    const std::string total = std::string(::psvitaalive::L("ID_TOTAL")) + "  " +
        formatBytesShort(installedDataSnapshot_.totalExternalBytes);
    ::psvitaalive::ui::uiDrawText(&font_,x+28,y+82,ACCENT,.82f,total.c_str());
    if (!installedDataSnapshot_.complete)
        ::psvitaalive::ui::uiDrawText(&font_,x+450,y+82,ACCENT,.70f,
            ::psvitaalive::L("ID_INCOMPLETE"));
    vita2d_draw_rectangle(x+20,y+96,W-40,1,BORDER);
    if (!installedDataDetail_) {
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+122,TEXT,.74f,
            ::psvitaalive::L("ID_LIST_HINT"));
        for(int n=0;n<VISIBLE;++n) {
            const int i=installedDataScroll_+n;
            if(i<0 || i>=static_cast<int>(installedDataVisibleRows_.size())) break;
            const auto& app=installedDataSnapshot_.apps[installedDataVisibleRows_[i]];
            const int ry=y+140+n*ROW;
            const bool focused=i==installedDataListFocus_;
            vita2d_draw_rectangle(x+23,ry,W-46,ROW-5,focused?SURFACE2:SURFACE);
            if (focused) vita2d_draw_rectangle(x+23,ry,4,ROW-5,ACCENT);
            ::psvitaalive::ui::uiDrawText(&font_,x+36,ry+21,
                focused?WHITE:TEXT,.76f,ellipsize(app.name,47).c_str());
            const std::string state = std::string(app.blocked ?
                ::psvitaalive::L("ID_PROTECTED") : ::psvitaalive::L("ID_DETECTED"));
            ::psvitaalive::ui::uiDrawText(&font_,x+36,ry+40,DIM,.61f,state.c_str());
            bool partial = false;
            for (const auto& path : app.paths)
                if (path.exists && !path.complete) partial = true;
            const std::string spaceLabel = partial ?
                ::psvitaalive::L("ID_UNKNOWN_SIZE") : formatBytesShort(app.bytes);
            ::psvitaalive::ui::uiDrawText(&font_,x+W-130,ry+28,
                focused?ACCENT:WHITE,.77f,spaceLabel.c_str());
        }
        if(installedDataVisibleRows_.empty())
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+210,DIM,.85f,
                ::psvitaalive::L("ID_NONE"));
    } else {
        const ::psvitaalive::storage::InstalledDataApp* app=nullptr;
        for (const auto& candidate : installedDataSnapshot_.apps)
            if (candidate.titleId == installedDataTargetId_) { app=&candidate; break; }
        if (app) {
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+124,WHITE,.89f,
                ellipsize(app->name,48).c_str());
            bool appPartial = false;
            for (const auto& path : app->paths)
                if (path.exists && !path.complete) appPartial = true;
            const std::string details = app->titleId + "  |  " +
                (app->packagePresent ? ::psvitaalive::L("ID_APP_PRESENT") :
                    ::psvitaalive::L("ID_APP_MISSING")) + "  |  " +
                (appPartial ? ::psvitaalive::L("ID_UNKNOWN_SIZE") :
                    formatBytesShort(app->bytes));
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+151,DIM,.73f,details.c_str());
            for (size_t i=0;i<app->paths.size() && i<5;i++) {
                const auto& path=app->paths[i];
                const int yy=y+180+static_cast<int>(i)*30;
                std::string status=!path.exists ?
                    ::psvitaalive::L("ID_NOT_FOUND") :
                    !path.complete ? ::psvitaalive::L("ID_UNKNOWN_SIZE") :
                    formatBytesShort(path.bytes);
                if(path.shared) status += "  (" + std::string(
                    ::psvitaalive::L("ID_SHARED")) + ")";
                ::psvitaalive::ui::uiDrawText(&font_,x+28,yy,TEXT,.67f,
                    ellipsize(path.path,65).c_str());
                ::psvitaalive::ui::uiDrawText(&font_,x+W-156,yy,ACCENT,.67f,
                    ellipsize(status,20).c_str());
            }
            if (app->paths.empty())
                ::psvitaalive::ui::uiDrawText(&font_,x+28,y+192,DIM,.75f,
                    ::psvitaalive::L("ID_NO_EXTERNAL"));
            if (app->blocked)
                ::psvitaalive::ui::uiDrawText(&font_,x+28,y+337,ACCENT,.72f,
                    ::psvitaalive::L("ID_PROTECTED_DESC"));
        }
        const char* labels[] = {
            ::psvitaalive::L("ID_ONLY_APP"), ::psvitaalive::L("ID_ONLY_DATA"),
            ::psvitaalive::L("ID_APP_DATA"), ::psvitaalive::L("ID_BACK")
        };
        for (int i=0; i<4; ++i) {
            const int bx=x+24+i*204, by=y+378;
            const bool on=installedDataActionFocus_==i;
            vita2d_draw_rectangle(bx,by,195,52,on?ACCENT:SURFACE2);
            const int textW=::psvitaalive::ui::uiTextWidth(&font_,.68f,labels[i]);
            ::psvitaalive::ui::uiDrawText(&font_,bx+std::max(8,(195-textW)/2),
                by+33,on?BG:WHITE,.68f,labels[i]);
        }
    }
    ::psvitaalive::ui::uiDrawText(&font_,x+29,y+H-27,DIM,.71f,
        ::psvitaalive::L("ID_CONTROLS"));
}
