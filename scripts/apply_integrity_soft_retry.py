#!/usr/bin/env python3
from pathlib import Path
CPP = Path("Client PSVitaAlive/source/installer/install_controller.cpp")
cpp = CPP.read_text(encoding="utf-8")

old = """    // ZIP extract / VPK promote can fail transiently (I/O, promoter glitch). Retry a few
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
"""

new = """    // ZIP extract / VPK promote can fail transiently (I/O, promoter glitch, FS lag).
    // Retry with clear UI + logs. EOCD/ZIP64/incomplete: allow a few slower retries
    // (storage may still be flushing the end of a large file); then stop.
    static const int kMaxInstallAttempts = 5;
    static const int kMaxIntegrityAttempts = 3; // EOCD / ZIP64 / incomplete
    InstallDispatchResult result = InstallDispatchResult::InstallFailed;
    std::string lastInstallError;
    int attemptsUsed = 0;
    for (int attempt = 1; attempt <= kMaxInstallAttempts; ++attempt) {
        attemptsUsed = attempt;
        if (attempt > 1) {
            const bool integrityHint =
                lastInstallError.find("incomplete") != std::string::npos ||
                lastInstallError.find("EOCD") != std::string::npos ||
                lastInstallError.find("ZIP64") != std::string::npos;
            char retryMsg[240];
            if (integrityHint) {
                sceClibSnprintf(
                    retryMsg, sizeof(retryMsg),
                    "Archive looked incomplete — waiting and retrying (%d/%d)...",
                    attempt, kMaxIntegrityAttempts);
            } else {
                sceClibSnprintf(
                    retryMsg, sizeof(retryMsg),
                    "Something went wrong — retrying extract/install (%d/%d)...",
                    attempt, kMaxInstallAttempts);
            }
            setStage("Retrying");
            setState(InstallStatus::State::Installing, retryMsg);
            diagnostics::log(
                std::string("[Installer] extract/install retry ") +
                std::to_string(attempt) + "/" +
                std::to_string(integrityHint ? kMaxIntegrityAttempts : kMaxInstallAttempts) +
                (integrityHint ? " (integrity/FS settle)" : "") +
                " after: " + lastInstallError);
            // Longer pause when EOCD may be a short FS lag after a big write.
            sceKernelDelayThread((integrityHint ? 4 : 2) * 1000 * 1000);
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

        const bool hardPermanent =
            result == InstallDispatchResult::InvalidArgument ||
            result == InstallDispatchResult::UnsupportedFormat ||
            result == InstallDispatchResult::DetectFailed ||
            lastInstallError.find("cancelled") != std::string::npos ||
            lastInstallError.find("not found") != std::string::npos ||
            lastInstallError.find("empty installation") != std::string::npos;
        if (hardPermanent) {
            diagnostics::log(
                std::string("[Installer] permanent install error — stop retries: ") +
                lastInstallError);
            break;
        }

        // Soft integrity errors: retry a few times with longer delay, then give up.
        const bool integrityError =
            lastInstallError.find("incomplete") != std::string::npos ||
            lastInstallError.find("EOCD") != std::string::npos ||
            lastInstallError.find("ZIP64") != std::string::npos;
        if (integrityError && attempt >= kMaxIntegrityAttempts) {
            diagnostics::log(
                std::string("[Installer] integrity error after ") +
                std::to_string(attempt) + " attempts — stop retries: " + lastInstallError);
            break;
        }
    }
"""

if old not in cpp:
    raise SystemExit("block not found")
cpp = cpp.replace(old, new, 1)
CPP.write_text(cpp, encoding="utf-8")
print("OK integrity soft-retry applied")
