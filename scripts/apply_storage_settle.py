#!/usr/bin/env python3
from pathlib import Path
CPP = Path("Client PSVitaAlive/source/installer/install_controller.cpp")
cpp = CPP.read_text(encoding="utf-8")

old = """    current_.store(job->downloadedSize);
    total_.store(job->expectedSize ? job->expectedSize : job->downloadedSize);
    speed_.store(0);
    setStage("Installing");
    setState(InstallStatus::State::Installing, "Preparing installation...");
    diagnostics::log(std::string("[Installer] installing job=") + activeJobId_ + " file=" + job->finalPath);
"""

new = """    current_.store(job->downloadedSize);
    total_.store(job->expectedSize ? job->expectedSize : job->downloadedSize);
    speed_.store(0);
    // Slower SD2Vita / USB media may still be flushing the last blocks after rename.
    // ZIP EOCD / ZIP64 locators live at EOF — reading too early can look "incomplete".
    setStage("Installing");
    setState(InstallStatus::State::Installing, "Finalizing file on storage...");
    diagnostics::log(std::string("[Installer] post-download storage settle before extract path=") + job->finalPath);
    sceKernelDelayThread(1500 * 1000); // 1.5s settle
    {
        // Touch the file end so the FS materializes size/metadata before zip_open.
        const SceUID fd = sceIoOpen(job->finalPath.c_str(), SCE_O_RDONLY, 0);
        if (fd >= 0) {
            const SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END);
            if (sz > 0) current_.store(static_cast<uint64_t>(sz));
            sceIoClose(fd);
            diagnostics::log(std::string("[Installer] storage settle size=") + std::to_string(static_cast<long long>(sz > 0 ? sz : -1)));
        } else {
            diagnostics::log("[Installer] storage settle: open failed (continuing)");
        }
    }
    setState(InstallStatus::State::Installing, "Preparing installation...");
    diagnostics::log(std::string("[Installer] installing job=") + activeJobId_ + " file=" + job->finalPath);
"""

if old not in cpp:
    raise SystemExit("post-download block not found")
cpp = cpp.replace(old, new, 1)

if "psp2/io/fcntl.h" not in cpp:
    if "#include <psp2/kernel/threadmgr.h>" in cpp:
        cpp = cpp.replace(
            "#include <psp2/kernel/threadmgr.h>",
            "#include <psp2/kernel/threadmgr.h>\n#include <psp2/io/fcntl.h>",
            1,
        )

CPP.write_text(cpp, encoding="utf-8")
print("OK storage settle applied")
