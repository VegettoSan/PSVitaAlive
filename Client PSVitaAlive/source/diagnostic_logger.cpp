#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>

#include <cstring>

namespace psvitaalive::diagnostics {
namespace {
constexpr const char* LOG_DIR = "ux0:data/psvitaalive/logs";
constexpr const char* LOG_FILE = "ux0:data/psvitaalive/logs/session.log";
SceUID g_mutex = -1;
bool g_initialized = false;

void ensureDirectories() {
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir(LOG_DIR, 0777);
}
}

void init() {
    if (g_initialized) return;
    ensureDirectories();
    g_mutex = sceKernelCreateMutex("PSVitaAliveDiag", 0, 0, nullptr);
    g_initialized = true;
    log("[System] shared diagnostic logger initialized");
}

void log(const std::string& message) {
    if (!g_initialized) ensureDirectories();
    if (g_mutex >= 0) sceKernelLockMutex(g_mutex, 1, nullptr);

    SceUID fd = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        char line[1600];
        const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
        sceClibSnprintf(line, sizeof(line), "[%llu ms] %s\n", (unsigned long long)ms, message.c_str());
        sceIoWrite(fd, line, std::strlen(line));
        sceIoClose(fd);
    }

    if (g_mutex >= 0) sceKernelUnlockMutex(g_mutex, 1);
}

void shutdown() {
    if (!g_initialized) return;
    log("[System] shared diagnostic logger shutdown");
    if (g_mutex >= 0) {
        sceKernelDeleteMutex(g_mutex);
        g_mutex = -1;
    }
    g_initialized = false;
}

} // namespace psvitaalive::diagnostics
