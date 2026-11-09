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
        bool missingGameRoot = false;
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
                if (game) missingGameRoot = true;
                else missingDataRoot = true; // may inherit known Game Files
                continue;
            }
            std::vector<std::string> signatureParts;
            for (const auto& relative : link.installedPaths) {
                const std::string full = "ux0:data/" + relative;
                if (!safeExternalDataPath(full) ||
                    !::psvitaalive::storage::installedDataExtractWithinRoot(
                        extract, full)) {
                    app.unresolved = true;
                    continue;
                }
                app.roots.push_back(full);
                if (game) {
                    signatureParts.push_back(full);
                }
            }
            if (game && !signatureParts.empty()) {
                // Identical destinations in different order are one variant.
                std::sort(signatureParts.begin(), signatureParts.end());
                signatureParts.erase(std::unique(signatureParts.begin(),
                    signatureParts.end()), signatureParts.end());
                std::string signature;
                for (const auto& p : signatureParts) signature += p + "\n";
                gameVariants.insert(signature);
            }
        }
        // Missing metadata on one alternate link does not invalidate a
        // different, explicitly declared safe root. We delete only the
        // detected paths, never infer an undocumented directory.
        if ((missingGameRoot || missingDataRoot) && app.roots.empty())
            app.unresolved = true;
        app.alternateGameRoots = gameVariants.size() > 1;
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
    installedDataScroll_ = std::min(
        std::max(0, static_cast<int>(installedDataVisibleRows_.size()) - 3),
        std::max(0, installedDataListFocus_ - 1));
}
void FullCatalogScreen::openInstalledDataMenu(bool fromStorage,
                                               const std::string& titleId) {
    if (installedDataVisible_ || installedDataWorker_ >= 0 ||
        storageWorker_ >= 0 || installProgressActive_ || catalogLoading_ ||
        (storageInstallerBusy_ && storageInstallerBusy_())) {
        showToast(::psvitaalive::L("STORAGE_BUSY"), 2800);
        return;
    }
    // Scope only new GPU decodes. Textures shared with the catalog remain in
    // its existing app-icon LRU; no duplicate textures are created.
    installedDataIntroducedIconTextures_.clear();
    installedDataVisible_ = true;
    installedDataFromStorage_ = fromStorage;
    installedDataPreferredId_ = titleId;
    installedDataScanFocusId_.clear();
    installedDataTargetId_ = titleId;
    installedDataDetail_ = !titleId.empty();
    installedDataConfirm_ = false;
    installedDataShareApproved_ = false;
    installedDataShareLabels_.clear();
    installedDataShareScroll_ = 0;
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
void FullCatalogScreen::releaseInstalledDataExclusiveTextures() {
    // The catalog remains drawn behind this modal and is restored after exit.
    // Keep precisely its currently needed icon paths, including the selected
    // detail icon, regardless of which view originally decoded the texture.
    std::unordered_set<std::string> catalogKeep;
    if (imageCache_) {
        const auto& view = catalogView();
        auto keepItem = [&](int index) {
            if (index < 0 || index >= static_cast<int>(view.size())) return;
            const auto& item = view[static_cast<size_t>(index)];
            const std::string& url = !item.icon.empty() ? item.icon : item.cover;
            if (!url.empty()) {
                const std::string path = imageCache_->pathFor(url, "app");
                if (!path.empty()) catalogKeep.insert(path);
            }
        };
        if (state_.mode == UiMode::FULL_CATALOG) {
            const int first = std::max(0, state_.catalogScrollRow * 3);
            const int last = std::min(static_cast<int>(view.size()), first + 9);
            for (int i = first; i < last; ++i) keepItem(i);
        } else if (state_.mode == UiMode::OPENING_DETAIL ||
                   state_.mode == UiMode::SPLIT_DETAIL ||
                   state_.mode == UiMode::CLOSING_DETAIL) {
            const int first = std::max(0, state_.catalogScrollRow);
            const int last = std::min(static_cast<int>(view.size()),
                                      first + visibleRowsSplit());
            for (int i = first; i < last; ++i) keepItem(i);
            keepItem(selectedIndex());
        }
    }

    const auto obsolete = installedDataExclusiveTextureKeys(
        installedDataIntroducedIconTextures_, catalogKeep);
    unsigned freed = 0;
    for (const std::string& path : obsolete) {
        auto found = textures_.find(path);
        if (found == textures_.end()) continue; // LRU already evicted it
        scheduleTextureFree(found->second);    // GPU-safe next-frame disposal
        textures_.erase(found);
        textureOrder_.erase(std::remove(textureOrder_.begin(),
            textureOrder_.end(), path), textureOrder_.end());
        ++freed;
    }
    const unsigned preserved = static_cast<unsigned>(
        installedDataIntroducedIconTextures_.size() - obsolete.size());
    installedDataIntroducedIconTextures_.clear();

    // Stop any queued image downloads used solely by the closed dialog.
    // Keep the restored catalog's requests; the normal ImageCache disk cache
    // remains untouched, so subsequent opens can reuse downloaded icons.
    if (imageCache_) imageCache_->cancelQueuedExcept(catalogKeep);
    if (freed || preserved) {
        char msg[180];
        sceClibSnprintf(msg, sizeof(msg),
            "[UI] installed-data icons: released=%u reused_by_catalog=%u",
            freed, preserved);
        diagnostics::log(msg);
    }
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
    // Returning from the inspector updates the parent summary entirely in
    // RAM; no second recursive filesystem walk is needed.
    if (installedDataFromStorage_ && storageVisible_)
        applyInstalledStorageSummary(storageSnapshot_, installedDataSnapshot_);
    releaseInstalledDataExclusiveTextures();
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
    installedDataShareLabels_.clear();
    installedDataShareScroll_ = 0;
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
    const bool shareApproved = self->installedDataShareApproved_;
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
                &self->installedDataOpProgress_, shareApproved);
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
                &self->installedDataOpProgress_, shareApproved);
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
                if (app.titleId == titleId) {
                    app.packagePresent = false;
                    app.packageBytes = 0;
                    app.packageSizeKnown = false;
                    app.packageSizeCached = false;
                }
            for (auto& app : self->installedDataSnapshot_.apps)
                {
                    app.activeShared = ::psvitaalive::storage::installedDataHasActiveShare(
                        self->installedDataSnapshot_, app);
                    app.catalogShared = ::psvitaalive::storage::installedDataHasCatalogShare(
                        self->installedDataSnapshot_, app);
                }
        }
        self->installedDataOpProgress_.phase.store(4, std::memory_order_release);
        self->installedDataWorkerOk_.store(ok, std::memory_order_release);
        self->installedDataWorkerDone_.store(true, std::memory_order_release);
        return 0;
    }
    // Initial storage inspection: enumerate all declared Game/Data roots.
    // Initial detail inspection: touch ONLY the selected title's roots;
    // all other catalog entries remain metadata-only collision claims.
    const bool refreshAllAfterSharedDelete = action >= 0 && shareApproved &&
        self->installedDataFilesOk_.load(std::memory_order_acquire);
    const std::string scope = refreshAllAfterSharedDelete ? std::string() :
        action == -3 || action == -2 || action >= 0 ? titleId :
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
    if ((action == -1 && self->installedDataScanFocusId_.empty()) ||
        refreshAllAfterSharedDelete) {
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
                if (deepMeasure)
                    {
                        app.activeShared =
                            ::psvitaalive::storage::installedDataHasActiveShare(
                                previous, app);
                        app.catalogShared =
                            ::psvitaalive::storage::installedDataHasCatalogShare(
                                previous, app);
                    }
                break;
            }
            break;
        }
        previous.totalExternalBytes = InstalledDataManager::knownExternalBytes(previous);
        previous.totalPackageBytes = InstalledDataManager::knownPackageBytes(previous);
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
    // Prioritize the selected Homebrew. Measure exactly one Homebrew at
    // a time (external data + package), within the existing work budget.
    const int focus = std::max(0, std::min(installedDataListFocus_, count-1));
    for (int offset = 0; offset < count; ++offset) {
        const auto& app = installedDataSnapshot_.apps[
            installedDataVisibleRows_[(focus + offset) % count]];
        if (installedDataAutoAttempted_.count(app.titleId)) continue;
        bool needsMeasure = app.packagePresent && !app.packageSizeKnown;
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
                    // Deep measurement skips unrelated SFO checks; keep the
                    // already discovered active collisions in the main UI.
                    app.activeShared =
                        ::psvitaalive::storage::installedDataHasActiveShare(
                            installedDataSnapshot_, app);
                    app.catalogShared =
                        ::psvitaalive::storage::installedDataHasCatalogShare(
                            installedDataSnapshot_, app);
                    break;
                }
            installedDataSnapshot_.totalExternalBytes =
                ::psvitaalive::storage::InstalledDataManager::knownExternalBytes(
                    installedDataSnapshot_);
            installedDataSnapshot_.totalPackageBytes =
                ::psvitaalive::storage::InstalledDataManager::knownPackageBytes(
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
            if (app.titleId == installedDataTargetId_) {
                if (app.packagePresent && !app.packageSizeKnown) allKnown = false;
                for (const auto& path : app.paths)
                    if (path.exists && !path.sizeKnown) allKnown = false;
            }
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
                installedDataScroll_ = std::min(
        std::max(0, static_cast<int>(installedDataVisibleRows_.size()) - 3),
        std::max(0, installedDataListFocus_ - 1));
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
            const bool canData = hasData &&
                ::psvitaalive::storage::installedDataCanOfferExternalDelete(app);
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
        const bool canData = hasData &&
                ::psvitaalive::storage::installedDataCanOfferExternalDelete(app);
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
    const bool canData = hasData &&
                ::psvitaalive::storage::installedDataCanOfferExternalDelete(*app);
    if (!::psvitaalive::storage::installedDataActionAvailable(
            installedDataActionFocus_, canPackage, canData)) {
        showToast(::psvitaalive::L("ID_UNSAFE"), 3400);
        return;
    }
    if (!installedDataConfirm_) {
        installedDataShareLabels_.clear();
        installedDataShareScroll_ = 0;
        if (installedDataActionFocus_ != 0) {
            const auto shared = ::psvitaalive::storage::installedDataCatalogShares(
                installedDataSnapshot_, *app);
            installedDataShareLabels_.reserve(shared.size());
            for (const auto& other : shared) {
                const char* status = ::psvitaalive::L(other.installed ?
                    "ID_APP_PRESENT" : "ID_APP_MISSING");
                installedDataShareLabels_.push_back(
                    other.titleId + "  |  " + other.name + " - " + status);
            }
        }
        installedDataConfirm_ = true;
        return;
    }
    installedDataConfirm_ = false;
    // Only after the catalog matches have been shown and X was pressed.
    // No package-installation requirement; users can delete orphaned data.
    installedDataShareApproved_ = installedDataActionFocus_ != 0 &&
                                  !installedDataShareLabels_.empty();
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
        const int maxSharedScroll = std::max(0,
            static_cast<int>(installedDataShareLabels_.size()) - 3);
        if (nav & (SCE_CTRL_DOWN | SCE_CTRL_RIGHT))
            installedDataShareScroll_ = std::min(maxSharedScroll,
                installedDataShareScroll_ + 1);
        if (nav & (SCE_CTRL_UP | SCE_CTRL_LEFT))
            installedDataShareScroll_ = std::max(0,installedDataShareScroll_-1);
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
            const bool canData = hasData &&
                ::psvitaalive::storage::installedDataCanOfferExternalDelete(*app);
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
                ((nav & SCE_CTRL_LEFT) ? 3 : 1));
        if (nav & (SCE_CTRL_DOWN | SCE_CTRL_RIGHT))
            installedDataListFocus_ = std::min(std::max(0, rows - 1),
                installedDataListFocus_ + ((nav & SCE_CTRL_RIGHT) ? 3 : 1));
        if (installedDataListFocus_ < installedDataScroll_)
            installedDataScroll_ = installedDataListFocus_;
        if (installedDataListFocus_ >= installedDataScroll_ + 3)
            installedDataScroll_ = installedDataListFocus_ - 2;
    }
    if (pressed & SCE_CTRL_CROSS) activateInstalledDataSelection();
}

void FullCatalogScreen::drawInstalledDataOverlay() {
    if (!installedDataVisible_) return;
    if (installedDataConfirm_) {
        // Large, legible confirmation that names ALL catalog users of shared
        // roots; only three rows are rendered at once to avoid text overflow.
        constexpr int DW=824, DH=474, BW=244, BH=46, GAP=22;
        const int dx=(SCREEN_W-DW)/2, dy=(SCREEN_H-DH)/2;
        const int firstButtonX=dx+(DW-(2*BW+GAP))/2;
        const int buttonsY=dy+DH-59;
        const ::psvitaalive::storage::InstalledDataApp* app=nullptr;
        for (const auto& candidate:installedDataSnapshot_.apps)
            if (candidate.titleId==installedDataTargetId_){app=&candidate;break;}
        bool hasExternal=false;
        if (app) for (const auto& p:app->paths)
            hasExternal=hasExternal||p.exists;
        const int action=installedDataActionFocus_;
        const bool deleteExternal=action==1||action==2;
        const bool warnShared=deleteExternal && !installedDataShareLabels_.empty();
        const char* actionKey=action==0 ? "ID_DELETE_APP" :
                              action==1 ? "ID_DELETE_FILES" : "ID_DELETE_ALL";
        const char* impactKey=::psvitaalive::storage::installedDataConfirmImpactKey(
            action,hasExternal);
        vita2d_disable_clipping();
        vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,190));
        vita2d_draw_rectangle(dx,dy,DW,DH,PANEL);
        vita2d_draw_rectangle(dx,dy,DW,3,ACCENT);
        vita2d_draw_rectangle(dx,dy+3,3,DH-6,ACCENT);
        vita2d_draw_rectangle(dx+DW-3,dy+3,3,DH-6,BORDER);
        vita2d_draw_rectangle(dx,dy+DH-3,DW,3,BORDER);
        drawMarqueeText(&font_,dx+24,dy+38,DW-48,ACCENT,1.07f,
            ::psvitaalive::L("ID_CONFIRM_TITLE"),false);
        if(app)drawMarqueeText(&font_,dx+24,dy+75,DW-48,WHITE,.98f,
            app->name,false);
        vita2d_draw_rectangle(dx+22,dy+91,DW-44,1,BORDER);

        const int cardY=dy+101, cardH=deleteExternal?131:108;
        vita2d_draw_rectangle(dx+22,cardY,DW-44,cardH,SURFACE2);
        vita2d_draw_rectangle(dx+22,cardY,3,cardH,ACCENT);
        drawMarqueeText(&font_,dx+42,cardY+27,DW-84,ACCENT,.98f,
            ::psvitaalive::L(actionKey),false);
        const auto impactLines=wrapTextToWidth(&font_,.87f,
            ::psvitaalive::L(impactKey),DW-90);
        for(int i=0;i<(int)impactLines.size() && i<2;++i)
            ::psvitaalive::ui::uiDrawText(&font_,dx+42,cardY+55+i*22,
                TEXT,.87f,impactLines[i].c_str());
        if(deleteExternal && app) {
            int shown=0,remaining=0;
            for(const auto& p:app->paths) {
                if(!p.exists)continue;
                if(shown<2) {
                    drawMarqueeText(&font_,dx+42,cardY+95+shown*23,
                        DW-115,DIM,.83f,p.path,false);
                    ++shown;
                }else ++remaining;
            }
            if(remaining>0) {
                const std::string extra="+"+std::to_string(remaining);
                ::psvitaalive::ui::uiDrawText(&font_,dx+DW-66,cardY+117,
                    ACCENT,.83f,extra.c_str());
            }
        }

        if(warnShared) {
            // A metadata match is NOT proof that other apps are installed.
            // Every matching catalog entry remains visible via Up/Down.
            const int listY=dy+240, listH=127;
            vita2d_draw_rectangle(dx+22,listY,DW-44,listH,SURFACE);
            vita2d_draw_rectangle(dx+22,listY,4,listH,ACCENT);
            drawMarqueeText(&font_,dx+40,listY+25,DW-140,ACCENT,.91f,
                ::psvitaalive::L("ID_SHARED_CATALOG_TITLE"),false);
            const int total=static_cast<int>(installedDataShareLabels_.size());
            const int maxScroll=std::max(0,total-3);
            const int scroll=std::max(0,std::min(installedDataShareScroll_,maxScroll));
            const std::string counter=std::to_string(scroll+1)+"-"+
                std::to_string(std::min(total,scroll+3))+"/"+std::to_string(total);
            ::psvitaalive::ui::uiDrawText(&font_,dx+DW-108,listY+25,
                DIM,.83f,counter.c_str());
            vita2d_enable_clipping();
            vita2d_set_clip_rectangle(dx+38,listY+32,
                dx+DW-48,listY+listH-8);
            for(int n=0;n<3 && scroll+n<total;++n) {
                const int rowY=listY+55+n*23;
                drawMarqueeText(&font_,dx+43,rowY,DW-112,WHITE,.86f,
                    installedDataShareLabels_[scroll+n],false);
            }
            vita2d_disable_clipping();
            if(total>3) {
                const int trackY=listY+34,trackH=78,thumbH=25;
                vita2d_draw_rectangle(dx+DW-35,trackY,4,trackH,BORDER);
                vita2d_draw_rectangle(dx+DW-35,trackY+
                    (scroll*(trackH-thumbH))/maxScroll,4,thumbH,ACCENT);
            }
            drawMarqueeText(&font_,dx+25,dy+393,DW-50,WHITE,.87f,
                ::psvitaalive::L("ID_SHARED_DELETE_WARNING"),false);
            drawMarqueeText(&font_,dx+25,dy+411,DW-50,ACCENT,.81f,
                ::psvitaalive::L("ID_WARNING3"),false);
        } else {
            const int warningY=dy+(deleteExternal?278:252);
            const char* warningKeys[]={"ID_WARNING1","ID_WARNING2","ID_WARNING3"};
            const unsigned warningColors[]={WHITE,TEXT,ACCENT};
            for(int i=0;i<3;++i)
                drawMarqueeText(&font_,dx+24,warningY+i*31,DW-48,
                    warningColors[i],.88f,
                    ::psvitaalive::L(warningKeys[i]),false);
        }
        vita2d_draw_rectangle(firstButtonX,buttonsY,BW,BH,SURFACE2);
        vita2d_draw_rectangle(firstButtonX,buttonsY,BW,2,BORDER);
        vita2d_draw_rectangle(firstButtonX+BW+GAP,buttonsY,BW,BH,ACCENT);
        const char* cancelText=::psvitaalive::L("ID_CONFIRM_CANCEL_BUTTON");
        const char* deleteText=::psvitaalive::L("ID_CONFIRM_DELETE_BUTTON");
        drawMarqueeText(&font_,firstButtonX+12,buttonsY+31,BW-24,
            WHITE,.92f,cancelText,false);
        drawMarqueeText(&font_,firstButtonX+BW+GAP+12,buttonsY+31,
            BW-24,BG,.92f,deleteText,false);
        return;
    }
    constexpr int W = 864, H = 500, VISIBLE = 3, ROW = 90;
    const int x = (SCREEN_W-W)/2, y=(SCREEN_H-H)/2;
    vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(0,0,0,190));
    vita2d_draw_rectangle(x,y,W,H,PANEL);
    vita2d_draw_rectangle(x,y,W,3,ACCENT);
    vita2d_draw_rectangle(x,y+3,3,H-6,ACCENT);
    vita2d_draw_rectangle(x+W-3,y+3,3,H-6,BORDER);
    vita2d_draw_rectangle(x,y+H-3,W,3,BORDER);
    ::psvitaalive::ui::uiDrawText(&font_,x+25,y+40,WHITE,1.14f,
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
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+106,ACCENT,1.13f,
            ::psvitaalive::L(stageText));
        const std::string actionName = installedDataAction_ == 0 ?
            ::psvitaalive::L("ID_DELETE_APP") : installedDataAction_ == 1 ?
            ::psvitaalive::L("ID_DELETE_FILES") :
            ::psvitaalive::L("ID_DELETE_ALL");
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+153,WHITE,1.09f,
            actionName.c_str());
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+196,TEXT,.90f,
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
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+354,TEXT,.95f,
                ::psvitaalive::L("ID_OP_FILES_EXPLAIN"));
        } else {
            // PromoterUtility has no file-level percentage. During the safe
            // pre-count the total is not yet known, so both remain indeterminate.
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+308,WHITE,.98f,
                ::psvitaalive::L(preparingFiles ? "ID_OP_PREPARE_EXPLAIN" :
                    verifying ? "ID_OP_VERIFY_STAGE" : "ID_OP_INDETERMINATE"));
            const int marker=barX+static_cast<int>(
                (sceKernelGetProcessTimeWide()/19000ULL)%(barW-40));
            vita2d_draw_rectangle(marker,barY+18,40,4,ACCENT);
        }
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+420,DIM,.92f,
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
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+128,ACCENT,1.12f,
            ::psvitaalive::L(cancelled ? "ID_CANCELLING" :
                installedDataAction_ == -2 ? "ID_MEASURING" :
                installedDataAction_ < 0 ? "ID_DISCOVERING" : "ID_WORKING"));
        char textLine[160];
        sceClibSnprintf(textLine,sizeof(textLine),"%s %u / %u   (%u%%)",
            ::psvitaalive::L("ID_APPS_PROGRESS"),done,total,pct);
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+180,WHITE,1.02f,textLine);
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
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+286,TEXT,.98f,textLine);
        sceClibSnprintf(textLine,sizeof(textLine),"%s %u  /  %s %u",
            ::psvitaalive::L("ID_FOLDERS_READ"),
            prog.dirsVisited.load(std::memory_order_relaxed),
            ::psvitaalive::L("ID_FILES_READ"),
            prog.filesVisited.load(std::memory_order_relaxed));
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+328,TEXT,.96f,textLine);
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+390,DIM,.92f,
            ::psvitaalive::L(phase==1 ? "ID_DISCOVERING" :
                "ID_SCAN_LIMIT_HINT"));
        if(installedDataAction_ < 0)
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+H-28,DIM,.92f,
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
            success?ACCENT:WHITE,1.14f,::psvitaalive::L(headerKey));
        const ::psvitaalive::storage::InstalledDataApp* app = nullptr;
        for (const auto& candidate : installedDataSnapshot_.apps)
            if (candidate.titleId == installedDataTargetId_) {
                app=&candidate; break;
            }
        if (app) ::psvitaalive::ui::uiDrawText(&font_,x+30,y+153,WHITE,1.06f,
            ellipsize(app->name,50).c_str());
        const bool preflightFailed =
            installedDataPreflightFailed_.load(std::memory_order_acquire);
        if (preflightFailed) {
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+224,WHITE,1.00f,
                ::psvitaalive::L("ID_UNSAFE"));
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+271,TEXT,1.00f,
                ::psvitaalive::L("ID_RESULT_FILES_NOT_STARTED"));
        }
        if (!preflightFailed && (action == 0 || action == 2)) {
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+224,
                appOk?ACCENT:WHITE,1.00f,
                ::psvitaalive::L(appOk ? "ID_RESULT_APP_REMOVED" :
                    "ID_RESULT_APP_FAILED"));
        }
        if (!preflightFailed && (action == 1 || action == 2)) {
            const char* dataText = dataOk ? "ID_RESULT_FILES_REMOVED" :
                action == 2 && !appOk ? "ID_RESULT_FILES_NOT_STARTED" :
                    "ID_RESULT_FILES_FAILED";
            ::psvitaalive::ui::uiDrawText(&font_,x+30,
                y+(action==1?224:271),dataOk?ACCENT:WHITE,1.00f,
                ::psvitaalive::L(dataText));
        }
        ::psvitaalive::ui::uiDrawText(&font_,x+30,y+345,DIM,.90f,
            ::psvitaalive::L("ID_RESULT_RECHECK"));
        vita2d_draw_rectangle(x+30,y+381,W-60,62,ACCENT);
        const char* doneText=::psvitaalive::L("ID_RESULT_CONTINUE");
        const int textW=::psvitaalive::ui::uiTextWidth(&font_,1.04f,doneText);
        ::psvitaalive::ui::uiDrawText(&font_,x+(W-textW)/2,y+420,
            BG,1.04f,doneText);
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
    drawMarqueeText(&font_,x+28,y+82,W-290,ACCENT,.99f,total,false);
    if (!installedDataSnapshot_.complete)
        drawMarqueeText(&font_,x+W-240,y+82,205,ACCENT,.88f,
            ::psvitaalive::L("ID_INCOMPLETE"),false);
    vita2d_draw_rectangle(x+20,y+96,W-40,1,BORDER);
    if (!installedDataDetail_) {
        ::psvitaalive::ui::uiDrawText(&font_,x+28,y+122,TEXT,.92f,
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
            drawMarqueeText(&font_,x+118,ry+32,W-332,
                focused?WHITE:TEXT,1.02f,app.name,false);
            bool cachedSize = app.packagePresent && app.packageSizeCached;
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
            else if (app.catalogShared) state += " | " +
                std::string(::psvitaalive::L("ID_SHARED"));
            if (cachedSize) state += " | " + std::string(
                ::psvitaalive::L("ID_LAST_MEASURED"));
            drawMarqueeText(&font_,x+118,ry+62,W-300,DIM,.85f,
                state,false);
            const bool unknown = (app.packagePresent && !app.packageSizeKnown) ||
                (hasExternal && partial);
            const std::string spaceLabel = unknown ?
                std::string(::psvitaalive::L("ID_SIZE_PENDING")) :
                formatBytesShort(app.bytes + (app.packagePresent ?
                    app.packageBytes : 0)) + (cachedSize ? "*" : "");
            drawMarqueeText(&font_,x+W-179,ry+37,146,
                focused?ACCENT:WHITE,.93f,spaceLabel,false);
        }
        if(installedDataVisibleRows_.empty())
            ::psvitaalive::ui::uiDrawText(&font_,x+30,y+210,DIM,1.02f,
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
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+H-55,ACCENT,.86f,
                scanInfo);
        } else {
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+H-55,DIM,.84f,
                ::psvitaalive::L("ID_CACHE_HINT"));
        }
    } else {
        const ::psvitaalive::storage::InstalledDataApp* app=nullptr;
        for (const auto& candidate : installedDataSnapshot_.apps)
            if (candidate.titleId == installedDataTargetId_) { app=&candidate; break; }
        if (app) {
            drawMarqueeText(&font_,x+28,y+124,W-60,WHITE,1.06f,
                app->name,false);
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
            const std::string details = app->titleId + "  |  " + contentType;
            ::psvitaalive::ui::uiDrawText(&font_,x+28,y+151,DIM,.90f,details.c_str());
            const std::string packageSize = !app->packagePresent ? "-" :
                !app->packageSizeKnown ? ::psvitaalive::L("ID_SIZE_PENDING") :
                formatBytesShort(app->packageBytes) +
                    (app->packageSizeCached ? "*" : "");
            const std::string dataSize = !hasExternal ?
                std::string(::psvitaalive::L("ID_NO_EXTERNAL")) :
                appPartial ? std::string(::psvitaalive::L("ID_SIZE_PENDING")) :
                formatBytesShort(app->bytes);
            const std::string packageLabel =
                std::string(::psvitaalive::L("ID_APP_SIZE")) + ": " + packageSize;
            const std::string dataLabel =
                std::string(::psvitaalive::L("ID_DATA_SIZE")) + ": " + dataSize;
            drawMarqueeText(&font_,x+28,y+178,340,WHITE,.90f,
                packageLabel,false);
            drawMarqueeText(&font_,x+385,y+178,W-415,WHITE,.90f,
                dataLabel,false);
            for (size_t i=0;i<app->paths.size() && i<5;i++) {
                const auto& path=app->paths[i];
                const int yy=y+203+static_cast<int>(i)*24;
                std::string status=!path.exists ?
                    ::psvitaalive::L("ID_NOT_FOUND") :
                    !path.sizeKnown ? ::psvitaalive::L("ID_SIZE_PENDING") :
                    formatBytesShort(path.bytes) + (path.sizeCached ? "*" : "");
                // Same-path catalog declarations no longer create a
                // misleading "shared" badge; ownership is checked only
                // immediately before a destructive operation.
                drawMarqueeText(&font_,x+28,yy,W-214,TEXT,.85f,
                    path.path,false);
                drawMarqueeText(&font_,x+W-158,yy,130,ACCENT,.85f,
                    status,false);
            }
            if (app->paths.empty())
                ::psvitaalive::ui::uiDrawText(&font_,x+28,y+192,DIM,.92f,
                    ::psvitaalive::L("ID_NO_EXTERNAL"));
            if (app->blocked || app->catalogShared) {
                const char* key = app->blocked ? "ID_PROTECTED_DESC" :
                    "ID_SHARED_ACTIVE_HINT";
                drawMarqueeText(&font_,x+28,y+327,W-56,ACCENT,.83f,
                    ::psvitaalive::L(key),false);
            }
        }
        // Only render actions that are actually safe and meaningful.
        vita2d_draw_rectangle(x+24,y+341,W-48,28,SURFACE2);
        const char* measureHint=::psvitaalive::L("ID_REFRESH_SIZES_HINT");
        drawMarqueeText(&font_,x+40,y+361,W-80,TEXT,.84f,
            measureHint,false);
        bool hasData = false;
        if (app) for (const auto& p : app->paths) hasData = hasData || p.exists;
        const bool canApp = app && app->packagePresent && app->titleId!="PSVAS1178";
        const bool canData = app && hasData &&
            ::psvitaalive::storage::installedDataCanOfferExternalDelete(*app);
        const int count = (canApp?1:0) + (canData?1:0) + ((canApp&&canData)?1:0);
        const int buttonW = count==1 ? 420 : 260;
        const int firstX = x+(W-count*buttonW-(count-1)*12)/2;
        const char* labels[] = {
            ::psvitaalive::L("ID_DELETE_APP"),
            ::psvitaalive::L("ID_DELETE_FILES"),
            ::psvitaalive::L("ID_DELETE_ALL")
        };
        const char* notes[] = {
            ::psvitaalive::L(::psvitaalive::storage::installedDataActionNoteKey(0,hasData)),
            ::psvitaalive::L(::psvitaalive::storage::installedDataActionNoteKey(1,hasData)),
            ::psvitaalive::L(::psvitaalive::storage::installedDataActionNoteKey(2,hasData))
        };
        for (int i=0; i<3; ++i) {
            const int slot = ::psvitaalive::storage::installedDataActionSlot(
                i,canApp,canData);
            if (slot < 0) continue;
            const int bx=firstX+slot*(buttonW+12), by=y+382;
            const bool on=installedDataActionFocus_==i;
            vita2d_draw_rectangle(bx,by,buttonW,61,on?ACCENT:SURFACE2);
            if(on) vita2d_draw_rectangle(bx,by,buttonW,3,WHITE);
            const int titleW=::psvitaalive::ui::uiTextWidth(&font_,.88f,labels[i]);
            drawMarqueeText(&font_,bx+std::max(8,(buttonW-titleW)/2),
                by+26,buttonW-16,on?BG:WHITE,.88f,labels[i],false);
            const int noteW=::psvitaalive::ui::uiTextWidth(&font_,.81f,notes[i]);
            drawMarqueeText(&font_,bx+std::max(8,(buttonW-noteW)/2),
                by+47,buttonW-16,on?BG:DIM,.81f,notes[i],false);
        }
        if (count == 0)
            ::psvitaalive::ui::uiDrawText(&font_,x+32,y+416,DIM,.95f,
                ::psvitaalive::L("ID_UNSAFE"));
    }
    drawMarqueeText(&font_,x+29,y+H-27,W-58,DIM,.89f,
        ::psvitaalive::L("ID_CONTROLS"),false);
}
