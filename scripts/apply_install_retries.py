#!/usr/bin/env python3
from pathlib import Path
CPP = Path("Client PSVitaAlive/source/installer/install_controller.cpp")
cpp = CPP.read_text(encoding="utf-8")
if "kMaxInstallAttempts" in cpp:
    print("already applied")
    raise SystemExit(0)

old = """    const InstallDispatchResult result = dispatcher_.installFile(
        job->finalPath,
        [&](const InstallDispatchProgress& progress) {
            current_.store(progress.current);
            total_.store(progress.total);
            setStage(progress.message.empty() ? "Installing" : progress.message.c_str());
            if (!progress.message.empty()) setMessage(progress.message.c_str());
        },
        nullptr,
        activeZipDestination_,
        directRifPath
    );

    if (result != InstallDispatchResult::Ok) {
        const bool cancelled = (result == InstallDispatchResult::Cancelled);
        const std::string error = cancelled
            ? (dispatcher_.lastError().empty() ? "Installation cancelled" : dispatcher_.lastError())
            : (dispatcher_.lastError().empty() ? "Installation failed" : dispatcher_.lastError());
        setStage(cancelled ? "Cancelled" : "Error");
        setState(cancelled ? InstallStatus::State::Cancelled : InstallStatus::State::Failed, error.c_str());
        liveAreaOk_.store(false);
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        diagnostics::log(std::string("[Installer] ") + (cancelled ? "installation cancelled: " : "installation failed: ") + error);
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    } else {"""

new = """    // ZIP extract / VPK promote can fail transiently (I/O, promoter glitch). Retry a few
    // times with clear UI + log lines so Discord reports show the full attempt trail.
    // Incomplete downloads (missing EOCD) are treated as permanent — retrying extract will not help.
    static const int kMaxInstallAttempts = 5;
    InstallDispatchResult result = InstallDispatchResult::InstallFailed;
    std::string lastInstallError;
    int attemptsUsed = 0;
    for (int attempt = 1; attempt <= kMaxInstallAttempts; ++attempt) {
        attemptsUsed = attempt;
        if (attempt > 1) {
            char retryMsg[220];
            sceClibSnprintf(
                retryMsg, sizeof(retryMsg),
                "Something went wrong — retrying extract/install (%d/%d)...",
                attempt, kMaxInstallAttempts);
            setStage("Retrying");
            setState(InstallStatus::State::Installing, retryMsg);
            diagnostics::log(
                std::string("[Installer] extract/install retry ") +
                std::to_string(attempt) + "/" + std::to_string(kMaxInstallAttempts) +
                " after: " + lastInstallError);
            sceKernelDelayThread(2 * 1000 * 1000); // 2s between attempts
        }

        result = dispatcher_.installFile(
            job->finalPath,
            [&](const InstallDispatchProgress& progress) {
                current_.store(progress.current);
                total_.store(progress.total);
                setStage(progress.message.empty() ? "Installing" : progress.message.c_str());
                if (!progress.message.empty()) setMessage(progress.message.c_str());
            },
            nullptr,
            activeZipDestination_,
            directRifPath
        );

        if (result == InstallDispatchResult::Ok ||
            result == InstallDispatchResult::Cancelled) {
            break;
        }

        lastInstallError = dispatcher_.lastError().empty()
            ? "Installation failed"
            : dispatcher_.lastError();
        diagnostics::log(
            std::string("[Installer] extract/install attempt ") +
            std::to_string(attempt) + "/" + std::to_string(kMaxInstallAttempts) +
            " failed: " + lastInstallError);

        // Permanent failures: do not burn remaining retries.
        const bool permanent =
            result == InstallDispatchResult::InvalidArgument ||
            result == InstallDispatchResult::UnsupportedFormat ||
            result == InstallDispatchResult::DetectFailed ||
            lastInstallError.find("incomplete") != std::string::npos ||
            lastInstallError.find("EOCD") != std::string::npos ||
            lastInstallError.find("ZIP64") != std::string::npos ||
            lastInstallError.find("cancelled") != std::string::npos ||
            lastInstallError.find("not found") != std::string::npos ||
            lastInstallError.find("empty installation") != std::string::npos;
        if (permanent) {
            diagnostics::log(
                std::string("[Installer] permanent install error — stop retries: ") +
                lastInstallError);
            break;
        }
    }

    if (result != InstallDispatchResult::Ok) {
        const bool cancelled = (result == InstallDispatchResult::Cancelled);
        std::string error = cancelled
            ? (dispatcher_.lastError().empty() ? "Installation cancelled" : dispatcher_.lastError())
            : (lastInstallError.empty()
                   ? (dispatcher_.lastError().empty() ? "Installation failed" : dispatcher_.lastError())
                   : lastInstallError);
        if (!cancelled && attemptsUsed > 1) {
            error = "Failed after " + std::to_string(attemptsUsed) + " attempts: " + error;
        }
        setStage(cancelled ? "Cancelled" : "Error");
        setState(cancelled ? InstallStatus::State::Cancelled : InstallStatus::State::Failed, error.c_str());
        liveAreaOk_.store(false);
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        diagnostics::log(std::string("[Installer] ") + (cancelled ? "installation cancelled: " : "installation failed: ") + error +
            " attempts=" + std::to_string(attemptsUsed));
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    } else {"""

if old not in cpp:
    raise SystemExit("install block not found")
cpp = cpp.replace(old, new, 1)

old_ok = """        diagnostics::log(std::string("[Installer] installation completed path=") +
            dispatcher_.lastInstallPath() + " titleId=" + dispatcher_.lastTitleId() +
            " liveArea=" + (dispatcher_.lastLiveAreaOk() ? "yes" : "no") +
            " verify=" + verifyMsg);"""
new_ok = """        diagnostics::log(std::string("[Installer] installation completed path=") +
            dispatcher_.lastInstallPath() + " titleId=" + dispatcher_.lastTitleId() +
            " liveArea=" + (dispatcher_.lastLiveAreaOk() ? "yes" : "no") +
            " verify=" + verifyMsg +
            " attempts=" + std::to_string(attemptsUsed));"""
if old_ok not in cpp:
    raise SystemExit("ok log not found")
cpp = cpp.replace(old_ok, new_ok, 1)
CPP.write_text(cpp, encoding="utf-8")
print("OK install retry patch applied")
