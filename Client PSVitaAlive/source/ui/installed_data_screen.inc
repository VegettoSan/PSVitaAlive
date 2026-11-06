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
        app.icon = !item.icon.empty() ? item.icon : item.cover;
        bool hasGameRoots = false;
        bool missingDataRoot = false;
        std::set<std::string> gameVariants;
        for (const CatalogLink& link : item.linkDetails) {
            const bool game = isGameFilesTypeLink(link);
            const bool data = isDataFilesTypeLink(link);
            if (!game && !data) continue;
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
        // Also include installed Homebrew without Game/Data links. Their
        // package presence is detected, but there is no external size to
        // calculate: show 'App only', never the misleading '0 B'.
        installedDataCandidates_.push_back(std::move(app));
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
    installedDataResultVisible_ = false;
    installedDataAutoEnabled_ = fromStorage;
    installedDataAutoClosePending_ = false;
    installedDataAutoAttempted_.clear();
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
                extra.icon = !catalogView()[i].icon.empty() ?
                    catalogView()[i].icon : catalogView()[i].cover;
                installedDataCandidates_.push_back(std::move(extra));
            }
        }
    }
    touchDown_ = touchMoved_ = false;
    startInstalledDataOperation(-1);
}
void FullCatalogScreen::closeInstalledDataMenu() {
    if (installedDataWorker_ >= 0) {
        if (installedDataAction_ == -3) {
            installedDataAutoClosePending_ = true;
            installedDataScanProgress_.cancelRequested.store(
                true, std::memory_order_release);
        }
        return;
    }
    installedDataVisible_ = false;
    installedDataConfirm_ = false;
    installedDataDetail_ = false;
    installedDataTargetId_.clear();
    installedDataPreferredId_.clear();
    installedDataScanFocusId_.clear();
    installedDataAutoEnabled_ = false;
    installedDataAutoClosePending_ = false;
    installedDataAutoAttempted_.clear();
    installedDataResultVisible_ = false;
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
    installedDataVpkOk_.store(false, std::memory_order_release);
    installedDataFilesOk_.store(false, std::memory_order_release);
    installedDataPreflightFailed_.store(false, std::memory_order_release);
    installedDataScanProgress_.reset();
    installedDataOpProgress_.reset();
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
    const std::string titleId = action == -3 ?
        self->installedDataScanFocusId_ : self->installedDataTargetId_;
    bool ok = true;
    if (action >= 0) {
        if (action == 1 || action == 2) {
            // Same complete pre-count used by VitaShell, but with PSVitaAlive's
            // root/ownership guards. For "both", this must run BEFORE deleting
            // the package, so a known unsafe Game Files target leaves APP intact.
            ok = InstalledDataManager::prepareExternalRemoval(
                self->installedDataSnapshot_, titleId,
                &self->installedDataOpProgress_);
            if (!ok)
                self->installedDataPreflightFailed_.store(true,
                    std::memory_order_release);
        }
        if (ok && (action == 0 || action == 2)) {
            const bool vpkOk = InstalledDataManager::uninstallPackage(
                titleId, &self->installedDataOpProgress_);
            self->installedDataVpkOk_.store(vpkOk, std::memory_order_release);
            ok = vpkOk;
            if (ok) ::psvitaalive::update::InstalledReleaseTracker::removeReceipt(titleId);
        }
        if (ok && (action == 1 || action == 2)) {
            const bool dataOk = InstalledDataManager::removeExternal(
                self->installedDataSnapshot_, titleId,
                &self->installedDataOpProgress_);
            self->installedDataFilesOk_.store(dataOk, std::memory_order_release);
            ok = dataOk;
            if (ok) {
                ::psvitaalive::update::InstallReceipt receipt;
                if (::psvitaalive::update::InstalledReleaseTracker::readReceipt(titleId, receipt)) {
                    receipt.dataRevision = 0;
                    ::psvitaalive::update::InstalledReleaseTracker::writeReceipt(receipt);
                }
            }
        }
        self->installedDataOpProgress_.phase.store(3, std::memory_order_release);
    }
    // Removing only the VPK cannot change data-folder sizes. Do not make a
    // person wait for another filesystem scan after package uninstallation.
    if (action == 0) {
        if (ok) {
            for (auto& app : self->installedDataSnapshot_.apps)
                if (app.titleId == titleId) app.packagePresent = false;
        }
        self->installedDataOpProgress_.phase.store(4, std::memory_order_release);
        self->installedDataWorkerOk_.store(ok, std::memory_order_release);
        self->installedDataWorkerDone_.store(true, std::memory_order_release);
        return 0;
    }
    // Initial storage inspection: enumerate all declared Game/Data roots.
    // Initial detail inspection: touch ONLY the selected title's roots;
    // all other catalog entries remain metadata-only collision claims.
    const std::string scope = action == -3 || action == -2 || action >= 0 ? titleId :
        !self->installedDataScanFocusId_.empty() ? self->installedDataScanFocusId_ :
        (self->installedDataFromStorage_ ? "" : self->installedDataPreferredId_);
    const bool deepMeasure = action == -2 || action == -3;
    // Read-only background work yields to navigation, but give large,
    // legitimate Game Files enough time to complete once per session.
    // A scan timeout must not make the app "protected".
    const uint64_t budget = action == -3 ? 25000000ULL :
        deepMeasure ? 35000000ULL : 8000000ULL;
    auto refreshed = InstalledDataManager::scan(
        self->installedDataCandidates_, scope, &self->installedDataScanProgress_,
        budget, deepMeasure);
    if (action == -3) {
        // The UI is still navigable during background measurement, so
        // NEVER write its active snapshot from this worker thread.
        for (auto& found : refreshed.apps)
            if (found.titleId == titleId) {
                self->installedDataAutoResult_ = std::move(found);
                break;
            }
        self->installedDataWorkerOk_.store(ok, std::memory_order_release);
        self->installedDataWorkerDone_.store(true, std::memory_order_release);
        return 0;
    }
    if (action == -1 && self->installedDataScanFocusId_.empty()) {
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
        previous.totalSizeKnown = true;
        for (const auto& item : previous.apps)
            for (const auto& p : item.paths)
                if (p.exists && !p.sizeKnown) previous.totalSizeKnown = false;
        if (!refreshed.complete) previous.complete = false;
    }
    if (action >= 0)
        self->installedDataOpProgress_.phase.store(4, std::memory_order_release);
    self->installedDataWorkerOk_.store(ok, std::memory_order_release);
    self->installedDataWorkerDone_.store(true, std::memory_order_release);
    return 0;
}
void FullCatalogScreen::startNextInstalledDataAutoMeasure() {
    if (!installedDataAutoEnabled_ || !installedDataVisible_ ||
        installedDataDetail_ || installedDataConfirm_ ||
        installedDataResultVisible_ || installedDataAutoClosePending_ ||
        installedDataWorker_ >= 0) return;
    const int count = static_cast<int>(installedDataVisibleRows_.size());
    if (count <= 0) return;
    // Prioritize the selected game; then continue with the other rows one
    // by one. Each attempt gets its own time budget and is never retried in
    // the same session if it was incomplete.
    const int focus = std::max(0, std::min(installedDataListFocus_, count-1));
    for (int offset = 0; offset < count; ++offset) {
        const auto& app = installedDataSnapshot_.apps[
            installedDataVisibleRows_[(focus + offset) % count]];
        if (installedDataAutoAttempted_.count(app.titleId)) continue;
        bool needsMeasure = false;
        for (const auto& path : app.paths)
            if (path.exists && !path.sizeKnown) needsMeasure = true;
        if (!needsMeasure) continue;
        installedDataAutoAttempted_.insert(app.titleId);
        installedDataScanFocusId_ = app.titleId;
        startInstalledDataOperation(-3);
        return;
    }
}
void FullCatalogScreen::finishInstalledDataResult() {
    installedDataResultVisible_ = false;
    installedDataConfirm_ = false;
    if (!installedDataPreferredId_.empty()) {
        // Returning to the catalog detail avoids showing outdated app state
        // after deleting the VPK. invalidateInstallStatus() already ran.
        closeInstalledDataMenu();
        return;
    }
    installedDataDetail_ = false;
    installedDataTargetId_.clear();
    rebuildInstalledDataVisibleRows();
    startNextInstalledDataAutoMeasure();
}
void FullCatalogScreen::pollInstalledDataOperation() {
    if (installedDataWorker_ < 0 ||
        !installedDataWorkerDone_.load(std::memory_order_acquire)) return;
    const int finishedAction = installedDataAction_;
    sceKernelWaitThreadEnd(installedDataWorker_, nullptr, nullptr);
    sceKernelDeleteThread(installedDataWorker_);
    installedDataWorker_ = -1;

    if (finishedAction == -3) {
        const std::string measured = installedDataScanFocusId_;
        if (installedDataAutoResult_.titleId == measured) {
            for (auto& app : installedDataSnapshot_.apps)
                if (app.titleId == measured) {
                    app = std::move(installedDataAutoResult_);
                    break;
                }
            installedDataSnapshot_.totalExternalBytes =
                ::psvitaalive::storage::InstalledDataManager::knownExternalBytes(
                    installedDataSnapshot_);
            installedDataSnapshot_.totalSizeKnown = true;
            for (const auto& app : installedDataSnapshot_.apps)
                for (const auto& path : app.paths)
                    if (path.exists && !path.sizeKnown)
                        installedDataSnapshot_.totalSizeKnown = false;
        }
        installedDataAutoResult_ = {};
        installedDataScanFocusId_.clear();
        if (installedDataAutoClosePending_) {
            closeInstalledDataMenu();
            return;
        }
        startNextInstalledDataAutoMeasure();
        return;
    }
    if (finishedAction < 0 &&
        installedDataScanProgress_.cancelRequested.load(std::memory_order_acquire)) {
        showToast(::psvitaalive::L("ID_CANCELLED"), 3300);
    }
    if (finishedAction >= 0) {
        installedDataResultAction_ = finishedAction;
        installedDataResultVisible_ = true; // persists until X or Circle
        invalidateInstallStatus(installedDataTargetId_);
        if (isInstallStateFilter()) rebuildFilteredItems();
        requestUx0SpaceRefresh();
    } else if (finishedAction == -2) {
        bool allKnown = true;
        for (const auto& app : installedDataSnapshot_.apps)
            if (app.titleId == installedDataTargetId_)
                for (const auto& path : app.paths)
                    if (path.exists && !path.sizeKnown) allKnown = false;
        showToast(::psvitaalive::L(allKnown ?
            "ID_MEASURE_COMPLETE" : "ID_MEASURE_PARTIAL"), 3300);
    }

    const std::string focusId = !installedDataScanFocusId_.empty() ?
        installedDataScanFocusId_ : installedDataTargetId_;
    rebuildInstalledDataVisibleRows();
    if (!focusId.empty()) {
        for (size_t i = 0; i < installedDataVisibleRows_.size(); ++i)
            if (installedDataSnapshot_.apps[installedDataVisibleRows_[i]].titleId ==
                focusId) {
                installedDataListFocus_ = static_cast<int>(i);
                installedDataScroll_ = std::max(0, installedDataListFocus_ - 3);
                break;
            }
    }
    installedDataScanFocusId_.clear();
    installedDataConfirm_ = false;
    if (installedDataDetail_ && finishedAction == -1) {
        for (const auto& app : installedDataSnapshot_.apps) {
            if (app.titleId != installedDataTargetId_) continue;
            bool hasData = false;
            for (const auto& p : app.paths) hasData = hasData || p.exists;
            const bool canApp = app.packagePresent && app.titleId != "PSVAS1178";
            const bool canData = hasData && !app.blocked && !app.unresolved;
            installedDataActionFocus_ =
                ::psvitaalive::storage::installedDataFirstAction(canApp, canData);
            break;
        }
    }
    if (finishedAction == -1) startNextInstalledDataAutoMeasure();
}
void FullCatalogScreen::activateInstalledDataSelection() {
    if (installedDataResultVisible_) { finishInstalledDataResult(); return; }
    if (installedDataWorker_ >= 0 && installedDataAction_ != -3) return;
    if (!installedDataDetail_) {
        if (installedDataListFocus_ < 0 ||
            installedDataListFocus_ >= static_cast<int>(
                installedDataVisibleRows_.size())) return;
        const auto& app = installedDataSnapshot_.apps[
            installedDataVisibleRows_[installedDataListFocus_]];
        installedDataTargetId_ = app.titleId;
        installedDataDetail_ = true;
        bool hasData = false;
        for (const auto& p : app.paths) hasData = hasData || p.exists;
        const bool canApp = app.packagePresent && app.titleId != "PSVAS1178";
        const bool canData = hasData && !app.blocked && !app.unresolved;
        installedDataActionFocus_ =
            ::psvitaalive::storage::installedDataFirstAction(canApp, canData);
        installedDataPathScroll_ = 0;
        return;
    }
    if (installedDataWorker_ >= 0) {
        // Never start a destructive job concurrently with a read-only worker.
        showToast(::psvitaalive::L("ID_SCAN_FINISHING"), 2900);
        return;
    }
    if (installedDataActionFocus_ < 0 || installedDataActionFocus_ > 2) return;
    const ::psvitaalive::storage::InstalledDataApp* app = nullptr;
    for (const auto& candidate : installedDataSnapshot_.apps)
        if (candidate.titleId == installedDataTargetId_) {app = &candidate; break;}
    if (!app) return;
    const bool canPackage = app->packagePresent && app->titleId != "PSVAS1178";
    bool hasData = false;
    for (const auto& path : app->paths)
        hasData = hasData || path.exists;
    // Directory measurement is optional. A timeout, huge archive or unreadable
    // size must not disable deleting a path with a valid declared root.
    const bool canData = hasData && !app->blocked && !app->unresolved;
    if (!::psvitaalive::storage::installedDataActionAvailable(
            installedDataActionFocus_, canPackage, canData)) {
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
    if (installedDataWorker_ >= 0 && installedDataAction_ != -3) {
        // Only read-only scans are cancelable. Never interrupt promoter
        // uninstall or recursive deletion halfway through.
        if ((pressed & SCE_CTRL_CIRCLE) && installedDataAction_ < 0)
            installedDataScanProgress_.cancelRequested.store(
                true, std::memory_order_release);
        return;
    }
    if (installedDataResultVisible_) {
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE))
            finishInstalledDataResult();
        return;
    }
    if (installedDataConfirm_) {
        if (pressed & SCE_CTRL_CIRCLE) {installedDataConfirm_ = false; return;}
        if (pressed & SCE_CTRL_CROSS) activateInstalledDataSelection();
        return;
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        if (installedDataDetail_ && installedDataPreferredId_.empty()) {
            installedDataDetail_ = false;
            startNextInstalledDataAutoMeasure();
        } else closeInstalledDataMenu();
        return;
    }
    if (installedDataDetail_) {
        // Separate, non-destructive size refresh. The three large buttons
        // remain exclusively for uninstall actions.
        if (pressed & SCE_CTRL_SQUARE) {
            if (installedDataWorker_ >= 0) {
                showToast(::psvitaalive::L("ID_SCAN_FINISHING"), 2800);
                return;
            }
            startInstalledDataOperation(-2);
            return;
        }
        const ::psvitaalive::storage::InstalledDataApp* app = nullptr;
        for (const auto& candidate : installedDataSnapshot_.apps)
            if (candidate.titleId == installedDataTargetId_) {
                app = &candidate;
                break;
            }
        if (app) {
            bool hasData = false;
            for (const auto& p : app->paths) hasData = hasData || p.exists;
            const bool canApp = app->packagePresent && app->titleId != "PSVAS1178";
            const bool canData = hasData && !app->blocked && !app->unresolved;
            if (nav & (SCE_CTRL_LEFT | SCE_CTRL_UP))
                installedDataActionFocus_ =
                    ::psvitaalive::storage::installedDataMoveAction(
                        installedDataActionFocus_, -1, canApp, canData);
            if (nav & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN))
                installedDataActionFocus_ =
                    ::psvitaalive::storage::installedDataMoveAction(
                        installedDataActionFocus_, +1, canApp, canData);
        }
    } else {
        const int rows = static_cast<int>(installedDataVisibleRows_.size());
        if (nav & (SCE_CTRL_UP | SCE_CTRL_LEFT))
            installedDataListFocus_ = std::max(0, installedDataListFocus_ -
                ((nav & SCE_CTRL_LEFT) ? 5 : 1));
        if (nav & (SCE_CTRL_DOWN | SCE_CTRL_RIGHT))
            installedDataListFocus_ = std::min(std::max(0, rows - 1),
                installedDataListFocus_ + ((nav & SCE_CTRL_RIGHT) ? 5 : 1));
        if (installedDataListFocus_ < installedDataScroll_)
            installedDataScroll_ = installedDataListFocus_;
        if (installedDataListFocus_ >= installedDataScroll_ + 5)
            installedDataScroll_ = installedDataListFocus_ - 4;
    }
    if (pressed & SCE_CTRL_CROSS) activateInstalledDataSelection();
}

void FullCatalogScreen::drawInstalledDataOverlay() {
    if (!installedDataVisible_) return;
    constexpr int W = 864, H = 500, VISIBLE = 3, ROW = 90;
    const int x = (SCREEN_W-W)/2, y=(SCREEN_H-H)/2;
    vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,190));
    vita2d_draw_rectangle(x,y,W,H,PANEL);
    vita2d_draw_rectangle(x,y,W,3,ACCENT);
    ::psvitaalive::ui::uiDrawText(&font_,x+25,y+40,WHITE,1.02f,
        ::psvitaalive::L("ID_TITLE"));
    if (installedDataWorker_ >= 0 && installedDataAction_ >= 0) {
        const auto& prog = installedDataOpProgress_;
        const uint32_t phase = prog.phase.load(std::memory_order_acquire);
        const bool removingFiles = phase == 2;
        const bool preparingFiles = phase == 5;
        const bool verifying = phase == 3;
        const char* stageText = preparingFiles ? "ID_OP_PREPARE_STAGE" :
            removingFiles ? "ID_OP_DATA_STAGE" :
            verifying ? "ID_OP_VERIFY_STAGE" : "ID_OP_APP_STAGE";
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+106,ACCENT,.98f,
            ::psvitaalive::L(stageText));
        const std::string actionName = installedDataAction_ == 0 ?
            ::psvitaalive::L("ID_DELETE_APP") : installedDataAction_ == 1 ?
            ::psvitaalive::L("ID_DELETE_FILES") :
            ::psvitaalive::L("ID_DELETE_ALL");
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+153,WHITE,.93f,
            actionName.c_str());
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+196,TEXT,.73f,
            ::psvitaalive::L(preparingFiles ? "ID_OP_PREPARE_EXPLAIN" :
                removingFiles ? "ID_OP_FILES_EXPLAIN" : "ID_OP_APP_EXPLAIN"));
        const int barX=x+30,barY=y+236,barW=W-60;
        vita2d_draw_rectangle(barX,barY,barW,23,SURFACE2);
        char counter[32];
        if (removingFiles) {
            const uint64_t total = prog.entriesTotal.load(std::memory_order_acquire);
            const uint64_t done =
                static_cast<uint64_t>(prog.filesRemoved.load(std::memory_order_relaxed)) +
                prog.foldersRemoved.load(std::memory_order_relaxed);
            const uint32_t pct = ::psvitaalive::storage::installedDataDeletionPercent(
                done, total);
            if (pct > 0)
                vita2d_draw_rectangle(barX,barY,
                    static_cast<int>(static_cast<uint64_t>(barW)*pct/100u),
                    23,ACCENT);
            sceClibSnprintf(counter,sizeof(counter),"%u%%",pct);
            const int textWidth = ::psvitaalive::ui::uiTextWidth(&font_,1.25f,counter);
            ::psvitaalive::ui::uiDrawText(&font_,x+(W-textWidth)/2,y+316,
                ACCENT,1.25f,counter);
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+354,TEXT,.78f,
                ::psvitaalive::L("ID_OP_FILES_EXPLAIN"));
        } else {
            // PromoterUtility has no file-level percentage. During the safe
            // pre-count the total is not yet known, so both remain indeterminate.
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+308,WHITE,.81f,
                ::psvitaalive::L(preparingFiles ? "ID_OP_PREPARE_EXPLAIN" :
                    verifying ? "ID_OP_VERIFY_STAGE" : "ID_OP_INDETERMINATE"));
            const int marker=barX+static_cast<int>(
                (sceKernelGetProcessTimeWide()/19000ULL)%(barW-40));
            vita2d_draw_rectangle(marker,barY+18,40,4,ACCENT);
        }
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+420,DIM,.74f,
            ::psvitaalive::L("ID_OP_DO_NOT_CLOSE"));
        return;
    }
    if (installedDataWorker_ >= 0 && installedDataAction_ != -3) {
        const auto& prog = installedDataScanProgress_;
        const uint32_t total = prog.appsTotal.load(std::memory_order_acquire);
        const uint32_t done = prog.appsCompleted.load(std::memory_order_acquire);
        const uint32_t phase = prog.phase.load(std::memory_order_acquire);
        const uint32_t pct = phase < 2 ? 0 :
            ::psvitaalive::storage::installedDataProgressPercent(done, total);
        const bool cancelled = prog.cancelRequested.load(std::memory_order_acquire);
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+128,ACCENT,1.0f,
            ::psvitaalive::L(cancelled ? "ID_CANCELLING" :
                installedDataAction_ == -2 ? "ID_MEASURING" :
                installedDataAction_ < 0 ? "ID_DISCOVERING" : "ID_WORKING"));
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
        if(installedDataAction_ < 0)
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+H-28,DIM,.75f,
                ::psvitaalive::L("ID_CANCEL_SCAN"));
        return;
    }
    if (installedDataResultVisible_) {
        const int action = installedDataResultAction_;
        const bool appOk = installedDataVpkOk_.load(std::memory_order_acquire);
        const bool dataOk = installedDataFilesOk_.load(std::memory_order_acquire);
        const bool success = action == 0 ? appOk :
            action == 1 ? dataOk : appOk && dataOk;
        const bool partial = action == 2 && appOk && !dataOk;
        const char* headerKey = success ? "ID_RESULT_SUCCESS" :
            partial ? "ID_RESULT_PARTIAL" : "ID_RESULT_FAILURE";
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+105,
            success?ACCENT:WHITE,1.03f,::psvitaalive::L(headerKey));
        const ::psvitaalive::storage::InstalledDataApp* app = nullptr;
        for (const auto& candidate : installedDataSnapshot_.apps)
            if (candidate.titleId == installedDataTargetId_) {
                app=&candidate; break;
            }
        if (app) ::psvitaalive::ui::uiDrawText(&font_,x+30,y+153,WHITE,.89f,
            ellipsize(app->name,50).c_str());
        const bool preflightFailed =
            installedDataPreflightFailed_.load(std::memory_order_acquire);
        if (preflightFailed) {
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+224,WHITE,.83f,
                ::psvitaalive::L("ID_UNSAFE"));
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+271,TEXT,.83f,
                ::psvitaalive::L("ID_RESULT_FILES_NOT_STARTED"));
        }
        if (!preflightFailed && (action == 0 || action == 2)) {
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+224,
                appOk?ACCENT:WHITE,.83f,
                ::psvitaalive::L(appOk ? "ID_RESULT_APP_REMOVED" :
                    "ID_RESULT_APP_FAILED"));
        }
        if (!preflightFailed && (action == 1 || action == 2)) {
            const char* dataText = dataOk ? "ID_RESULT_FILES_REMOVED" :
                action == 2 && !appOk ? "ID_RESULT_FILES_NOT_STARTED" :
                    "ID_RESULT_FILES_FAILED";
            ::psvitaalive::ui::uiDrawText(&font_,x+30,
                y+(action==1?224:271),dataOk?ACCENT:WHITE,.83f,
                ::psvitaalive::L(dataText));
        }
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+345,DIM,.73f,
            ::psvitaalive::L("ID_RESULT_RECHECK"));
        vita2d_draw_rectangle(x+30,y+381,W-60,62,ACCENT);
        const char* doneText=::psvitaalive::L("ID_RESULT_CONTINUE");
        const int textW=::psvitaalive::ui::uiTextWidth(&font_,.87f,doneText);
        ::psvitaalive::ui::uiDrawText(&font_,x+(W-textW)/2,y+420,
            BG,.87f,doneText);
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
            ::psvitaalive::L(installedDataActionFocus_==0 ? "ID_DELETE_APP" :
                installedDataActionFocus_==1 ? "ID_DELETE_FILES" : "ID_DELETE_ALL"));
        vita2d_draw_rectangle(x+30,y+383,W-60,57,SURFACE2);
        ::psvitaalive::ui::uiDrawText(&font_,x+55,y+419,WHITE,.82f,
            ::psvitaalive::L("ID_CONFIRM_CONTROLS"));
        return;
    }
    bool containsCachedSize = false;
    for (const auto& app : installedDataSnapshot_.apps)
        for (const auto& p : app.paths)
            containsCachedSize = containsCachedSize || (p.exists && p.sizeCached);
    const std::string total = std::string(::psvitaalive::L("ID_TOTAL")) + "  " +
        (!installedDataSnapshot_.totalSizeKnown
            ? installedDataSnapshot_.totalExternalBytes > 0
                ? formatBytesShort(installedDataSnapshot_.totalExternalBytes) +
                    " (" + std::string(::psvitaalive::L("ID_TOTAL_PARTIAL")) + ")"
                : std::string(::psvitaalive::L("ID_SIZE_PENDING"))
            : formatBytesShort(installedDataSnapshot_.totalExternalBytes) +
                (containsCachedSize ? " *" : ""));
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
            vita2d_draw_rectangle(x+23,ry,W-69,ROW-5,focused?SURFACE2:SURFACE);
            if (focused) vita2d_draw_rectangle(x+23,ry,4,ROW-5,ACCENT);
            drawImage(app.icon,"app",x+39,ry+11,64,64);
            ::psvitaalive::ui::uiDrawText(&font_,x+118,ry+31,
                focused?WHITE:TEXT,.85f,ellipsize(app.name,43).c_str());
            bool cachedSize = false;
            bool hasExternal = false;
            bool partial = false;
            for (const auto& path : app.paths) {
                cachedSize = cachedSize || (path.exists && path.sizeCached);
                hasExternal = hasExternal || path.exists;
                if (path.exists && !path.sizeKnown) partial = true;
            }
            std::string state = !hasExternal ?
                ::psvitaalive::L("ID_ONLY_APP_LABEL") :
                !app.packagePresent ? ::psvitaalive::L("ID_ONLY_FILES_LABEL") :
                ::psvitaalive::L("ID_APP_AND_FILES_LABEL");
            if (app.blocked) state += " | " + std::string(
                ::psvitaalive::L("ID_PROTECTED"));
            if (cachedSize) state += " | " + std::string(
                ::psvitaalive::L("ID_LAST_MEASURED"));
            ::psvitaalive::ui::uiDrawText(&font_,x+118,ry+60,DIM,.67f,
                ellipsize(state,65).c_str());
            const std::string spaceLabel = !hasExternal ?
                std::string(::psvitaalive::L("ID_ONLY_APP_LABEL")) :
                partial ? ::psvitaalive::L("ID_SIZE_PENDING") :
                formatBytesShort(app.bytes) + (cachedSize ? "*" : "");
            ::psvitaalive::ui::uiDrawText(&font_,x+W-172,ry+36,
                focused?ACCENT:WHITE,.76f,spaceLabel.c_str());
        }
        if(installedDataVisibleRows_.empty())
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+210,DIM,.85f,
                ::psvitaalive::L("ID_NONE"));
        const int rows = static_cast<int>(installedDataVisibleRows_.size());
        if (rows > VISIBLE) {
            const int trackX = x+W-27, trackY = y+140, trackH = VISIBLE*ROW-5;
            const int thumbH = std::max(28,trackH*VISIBLE/rows);
            const int maxScroll = rows-VISIBLE;
            const int scroll = std::max(0,std::min(installedDataScroll_,maxScroll));
            const int thumbY = trackY+(scroll*(trackH-thumbH))/maxScroll;
            vita2d_draw_rectangle(trackX,trackY,4,trackH,BORDER);
            vita2d_draw_rectangle(trackX,thumbY,4,thumbH,ACCENT);
        }
        if (installedDataWorker_ >= 0 && installedDataAction_ == -3) {
            char scanInfo[160];
            sceClibSnprintf(scanInfo,sizeof(scanInfo),"%s %u  |  %s %u",
                ::psvitaalive::L("ID_AUTO_MEASURING"),
                static_cast<unsigned>(installedDataAutoAttempted_.size()),
                ::psvitaalive::L("ID_FILES_READ"),
                installedDataScanProgress_.filesVisited.load(
                    std::memory_order_relaxed));
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+H-55,ACCENT,.69f,
                scanInfo);
        } else {
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+H-55,DIM,.66f,
                ::psvitaalive::L("ID_CACHE_HINT"));
        }
    } else {
        const ::psvitaalive::storage::InstalledDataApp* app=nullptr;
        for (const auto& candidate : installedDataSnapshot_.apps)
            if (candidate.titleId == installedDataTargetId_) { app=&candidate; break; }
        if (app) {
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+124,WHITE,.89f,
                ellipsize(app->name,48).c_str());
            bool appPartial = false;
            for (const auto& path : app->paths)
                if (path.exists && !path.sizeKnown) appPartial = true;
            bool hasExternal = false;
            for (const auto& path : app->paths)
                hasExternal = hasExternal || path.exists;
            const std::string contentType = !hasExternal ?
                ::psvitaalive::L("ID_ONLY_APP_LABEL") :
                !app->packagePresent ? ::psvitaalive::L("ID_ONLY_FILES_LABEL") :
                ::psvitaalive::L("ID_APP_AND_FILES_LABEL");
            const std::string details = app->titleId + "  |  " +
                contentType + "  |  " + (!hasExternal ?
                    std::string(::psvitaalive::L("ID_NO_EXTERNAL")) :
                    appPartial ? std::string(::psvitaalive::L("ID_SIZE_PENDING")) :
                        formatBytesShort(app->bytes));
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+151,DIM,.73f,details.c_str());
            for (size_t i=0;i<app->paths.size() && i<5;i++) {
                const auto& path=app->paths[i];
                const int yy=y+180+static_cast<int>(i)*30;
                std::string status=!path.exists ?
                    ::psvitaalive::L("ID_NOT_FOUND") :
                    !path.sizeKnown ? ::psvitaalive::L("ID_SIZE_PENDING") :
                    formatBytesShort(path.bytes) + (path.sizeCached ? "*" : "");
                // Same-path catalog declarations no longer create a
                // misleading "shared" badge; ownership is checked only
                // immediately before a destructive operation.
                ::psvitaalive::ui::uiDrawText(&font_,x+28,yy,TEXT,.67f,
                    ellipsize(path.path,65).c_str());
                ::psvitaalive::ui::uiDrawText(&font_,x+W-156,yy,ACCENT,.67f,
                    ellipsize(status,20).c_str());
            }
            if (app->paths.empty())
                ::psvitaalive::ui::uiDrawText(&font_,x+28,y+192,DIM,.75f,
                    ::psvitaalive::L("ID_NO_EXTERNAL"));
            if (app->blocked)
                ::psvitaalive::ui::uiDrawText(&font_,x+28,y+327,ACCENT,.65f,
                    ::psvitaalive::L("ID_PROTECTED_DESC"));
        }
        // Only render actions that are actually safe and meaningful.
        vita2d_draw_rectangle(x+24,y+341,W-48,28,SURFACE2);
        const char* measureHint=::psvitaalive::L("ID_REFRESH_SIZES_HINT");
        ::psvitaalive::ui::uiDrawText(&font_,x+40,y+361,TEXT,.66f,measureHint);
        bool hasData = false;
        if (app) for (const auto& p : app->paths) hasData = hasData || p.exists;
        const bool canApp = app && app->packagePresent && app->titleId!="PSVAS1178";
        const bool canData = app && hasData && !app->blocked && !app->unresolved;
        const int count = (canApp?1:0) + (canData?1:0) + ((canApp&&canData)?1:0);
        const int buttonW = count==1 ? 420 : 260;
        const int firstX = x+(W-count*buttonW-(count-1)*12)/2;
        const char* labels[] = {
            ::psvitaalive::L("ID_DELETE_APP"),
            ::psvitaalive::L("ID_DELETE_FILES"),
            ::psvitaalive::L("ID_DELETE_ALL")
        };
        const char* notes[] = {
            ::psvitaalive::L("ID_DELETE_APP_NOTE"),
            ::psvitaalive::L("ID_DELETE_FILES_NOTE"),
            ::psvitaalive::L("ID_DELETE_ALL_NOTE")
        };
        for (int i=0; i<3; ++i) {
            const int slot = ::psvitaalive::storage::installedDataActionSlot(
                i,canApp,canData);
            if (slot < 0) continue;
            const int bx=firstX+slot*(buttonW+12), by=y+382;
            const bool on=installedDataActionFocus_==i;
            vita2d_draw_rectangle(bx,by,buttonW,61,on?ACCENT:SURFACE2);
            if(on) vita2d_draw_rectangle(bx,by,buttonW,3,WHITE);
            const int titleW=::psvitaalive::ui::uiTextWidth(&font_,.70f,labels[i]);
            ::psvitaalive::ui::uiDrawText(&font_,bx+std::max(6,(buttonW-titleW)/2),
                by+26,on?BG:WHITE,.70f,labels[i]);
            const int noteW=::psvitaalive::ui::uiTextWidth(&font_,.58f,notes[i]);
            ::psvitaalive::ui::uiDrawText(&font_,bx+std::max(6,(buttonW-noteW)/2),
                by+47,on?BG:DIM,.58f,notes[i]);
        }
        if (count == 0)
            ::psvitaalive::ui::uiDrawText(&font_,x+32,y+416,DIM,.78f,
                ::psvitaalive::L("ID_UNSAFE"));
    }
    ::psvitaalive::ui::uiDrawText(&font_,x+29,y+H-27,DIM,.71f,
        ::psvitaalive::L("ID_CONTROLS"));
}
