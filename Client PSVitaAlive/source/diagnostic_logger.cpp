#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstdint>
#include <cstring>

namespace psvitaalive::diagnostics {
namespace {
constexpr const char* LOG_DIR = "ux0:data/psvitaalive/logs";
constexpr const char* LOG_FILE = "ux0:data/psvitaalive/logs/session.log";
constexpr const char* INSTALL_LOG = "ux0:data/psvitaalive/logs/install.log";
constexpr const char* ARCHIVE_NODE_LOG = "ux0:data/psvitaalive/logs/archive_nodes.log";
SceUID g_mutex = -1;
bool g_initialized = false;

void ensureDirectories() {
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir(LOG_DIR, 0777);
}

void resetLogFile(const char* path) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) sceIoClose(fd);
}

void appendLogFile(const char* path, const char* tag, const std::string& message) {
    if (!path) return;
    if (!g_initialized) ensureDirectories();
    if (g_mutex >= 0) sceKernelLockMutex(g_mutex, 1, nullptr);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        char line[1800];
        const uint64_t ms = sceKernelGetSystemTimeWide() / 1000ULL;
        if (tag && *tag) {
            sceClibSnprintf(line, sizeof(line), "[%llu ms] [%s] %s\n",
                (unsigned long long)ms, tag, message.c_str());
        } else {
            sceClibSnprintf(line, sizeof(line), "[%llu ms] %s\n",
                (unsigned long long)ms, message.c_str());
        }
        sceIoWrite(fd, line, std::strlen(line));
        sceIoClose(fd);
    }
    if (g_mutex >= 0) sceKernelUnlockMutex(g_mutex, 1);
}
} // namespace

void init() {
    if (g_initialized) return;
    ensureDirectories();
    resetLogFile(LOG_FILE);
    resetLogFile(INSTALL_LOG);
    resetLogFile(ARCHIVE_NODE_LOG);
    g_mutex = sceKernelCreateMutex("PSVitaAliveDiag", 0, 0, nullptr);
    g_initialized = true;
    log("[System] shared diagnostic logger initialized (session.log reset)");
    archiveNodeLog("============================================================");
    archiveNodeLog("PSVitaAlive Internet Archive node diagnostics - session start");
    archiveNodeLog("Metadata candidates, probes, ranking, transfer attempts and failovers are recorded here.");
    archiveNodeLog("Catalog size is only a selector threshold hint; remote HTTP totals remain authoritative.");
    archiveNodeLog("============================================================");
}

void log(const std::string& message) {
    appendLogFile(LOG_FILE, nullptr, message);
}

void archiveNodeLog(const std::string& message) {
    appendLogFile(ARCHIVE_NODE_LOG, "Archive", message);
}

void shutdown() {
    if (!g_initialized) return;
    archiveNodeLog("PSVitaAlive Internet Archive node diagnostics - session end");
    log("[System] shared diagnostic logger shutdown");
    if (g_mutex >= 0) {
        sceKernelDeleteMutex(g_mutex);
        g_mutex = -1;
    }
    g_initialized = false;
}

} // namespace psvitaalive::diagnostics
