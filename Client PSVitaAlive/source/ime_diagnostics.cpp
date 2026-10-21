#include "ime_diagnostics.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdint>

namespace psvitaalive::ime_diagnostics {
namespace {
constexpr const char* kLog = "ux0:data/psvitaalive/logs/ime_trace.log";
constexpr const char* kPrevious = "ux0:data/psvitaalive/logs/ime_trace_previous.log";
constexpr uint32_t kMaxBytes = 1024 * 1024;
bool gReady = false;
unsigned gSession = 0;
uint32_t gSequence = 0;
uint32_t gBytes = 0;

// Deliberately synchronous on UI transitions. The normal session.log uses an
// async queue whose last lines can be lost when Sony's PAF thread crashes.
void append(const char* data, unsigned count) {
    if (!gReady || !count || gBytes >= kMaxBytes) return;
    const SceUID fd = sceIoOpen(kLog, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0) return;
    unsigned done = 0;
    while (done < count) {
        const int n = sceIoWrite(fd, data + done, count - done);
        if (n <= 0) break;
        done += (unsigned)n;
    }
    sceIoClose(fd);
    gBytes += done;
}
} // namespace

void init() {
    if (gReady) return;
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir("ux0:data/psvitaalive/logs", 0777);
    SceIoStat previous{};
    if (sceIoGetstat(kLog, &previous) >= 0) {
        sceIoRemove(kPrevious);
        sceIoRename(kLog, kPrevious);
    }
    const SceUID fd = sceIoOpen(kLog, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) sceIoClose(fd);
    gReady = true;
    gSession = gSequence = gBytes = 0;
    record("trace_start_diagnostic_build", 0, 1, 0, true);
}
void setSession(unsigned session) { gSession = session; }
unsigned activeSession() { return gSession; }

void record(const char* phase, unsigned session, int value, int extra, bool sampleMemory) {
    if (!gReady || !phase) return;
    int rc = -1, user = -1, cdram = -1, phy = -1;
    if (sampleMemory) {
        SceKernelFreeMemorySizeInfo memory{};
        memory.size = sizeof(memory);
        rc = sceKernelGetFreeMemorySize(&memory);
        if (rc >= 0) {
            user = memory.size_user;
            cdram = memory.size_cdram;
            phy = memory.size_phycont;
        }
    }
    char line[320];
    const int n = sceClibSnprintf(line, sizeof(line),
        "[IME-DIAG] seq=%u time_us=%llu sid=%u phase=%s a=%d b=%d mem_rc=%d user_free=%d cdram_free=%d phy_free=%d\n",
        ++gSequence, (unsigned long long)sceKernelGetSystemTimeWide(),
        session, phase, value, extra, rc, user, cdram, phy);
    if (n > 0) append(line, (unsigned)n < sizeof(line) ? (unsigned)n : sizeof(line) - 1);
}
void shutdown() {
    if (!gReady) return;
    record("clean_shutdown", gSession, 0, 0, true);
    gReady = false;
}
} // namespace psvitaalive::ime_diagnostics
