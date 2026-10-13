#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace psvitaalive::diagnostics {
namespace {
constexpr const char* LOG_DIR = "ux0:data/psvitaalive/logs";
constexpr const char* LOG_FILE = "ux0:data/psvitaalive/logs/session.log";
constexpr const char* INSTALL_LOG = "ux0:data/psvitaalive/logs/install.log";

// A fixed 64 KiB queue: no allocations in the logger's hot path and no disk I/O
// on the UI, HTTP, image-cache, or installer caller threads.
constexpr size_t QUEUE_CAPACITY = 64;
constexpr size_t LINE_CAPACITY = 1024;
constexpr size_t WRITE_BATCH_BYTES = 4096;
struct LogLine {
    char data[LINE_CAPACITY];
    uint16_t bytes = 0;
    uint64_t sequence = 0;
};
LogLine g_queue[QUEUE_CAPACITY]{};
size_t g_head = 0;
size_t g_tail = 0;
size_t g_count = 0;
uint64_t g_lastEnqueued = 0;
uint64_t g_lastWritten = 0;
uint32_t g_dropped = 0;

SceUID g_mutex = -1;
SceUID g_sema = -1;
SceUID g_thread = -1;
bool g_initialized = false;
bool g_async = false;
bool g_stopping = false;

void ensureDirectories() {
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir(LOG_DIR, 0777);
}

void resetLogFile(const char* path) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) sceIoClose(fd);
}

void writeAll(SceUID fd, const char* data, size_t n) {
    if (fd < 0 || !data) return;
    while (n > 0) {
        const int written = sceIoWrite(fd, data, static_cast<SceSize>(n));
        if (written <= 0) break;
        data += written;
        n -= static_cast<size_t>(written);
    }
}

// Used only if the PS Vita cannot create the diagnostic worker.
void fallbackWrite(const char* text, size_t bytes) {
    if (g_mutex >= 0) sceKernelLockMutex(g_mutex, 1, nullptr);
    SceUID fd = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        writeAll(fd, text, bytes);
        sceIoClose(fd);
    }
    if (g_mutex >= 0) sceKernelUnlockMutex(g_mutex, 1);
}

int logWorker(SceSize, void*) {
    // One open handle for the session; batch short messages into larger writes.
    SceUID fd = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    char batch[WRITE_BATCH_BYTES];
    for (;;) {
        if (sceKernelWaitSema(g_sema, 1, nullptr) < 0) break;
        size_t used = 0;
        uint64_t batchLastSequence = 0;
        for (;;) {
            LogLine line{};
            bool got = false;
            sceKernelLockMutex(g_mutex, 1, nullptr);
            if (g_count > 0) {
                line = g_queue[g_head];
                g_head = (g_head + 1) % QUEUE_CAPACITY;
                --g_count;
                got = true;
            }
            sceKernelUnlockMutex(g_mutex, 1);
            if (!got) break;
            if (used && used + line.bytes > sizeof(batch)) {
                if (fd < 0) fd = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
                writeAll(fd, batch, used);
                sceKernelLockMutex(g_mutex, 1, nullptr);
                g_lastWritten = batchLastSequence;
                sceKernelUnlockMutex(g_mutex, 1);
                used = 0;
            }
            std::memcpy(batch + used, line.data, line.bytes);
            used += line.bytes;
            batchLastSequence = line.sequence;
        }
        if (used) {
            if (fd < 0) fd = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
            writeAll(fd, batch, used);
            sceKernelLockMutex(g_mutex, 1, nullptr);
            g_lastWritten = batchLastSequence;
            sceKernelUnlockMutex(g_mutex, 1);
        }
        sceKernelLockMutex(g_mutex, 1, nullptr);
        const bool finished = g_stopping && g_count == 0;
        sceKernelUnlockMutex(g_mutex, 1);
        if (finished) break;
    }
    if (g_dropped && fd >= 0) {
        char notice[160];
        const int n = sceClibSnprintf(notice, sizeof(notice),
            "[Logger] dropped=%u old messages due to bounded queue; newest diagnostics preserved\n",
            (unsigned)g_dropped);
        if (n > 0) writeAll(fd, notice, static_cast<size_t>(n));
    }
    if (fd >= 0) sceIoClose(fd);
    return 0;
}
} // namespace

void init() {
    if (g_initialized) return;
    ensureDirectories();
    resetLogFile(LOG_FILE);
    resetLogFile(INSTALL_LOG);
    // No new verbose Archive node trace file; remove the old one once.
    sceIoRemove("ux0:data/psvitaalive/logs/archive_nodes.log");
    g_head = g_tail = g_count = 0;
    g_lastEnqueued = g_lastWritten = 0;
    g_dropped = 0;
    g_stopping = false;
    g_mutex = sceKernelCreateMutex("PSVitaAliveDiag", 0, 0, nullptr);
    if (g_mutex >= 0) {
        g_sema = sceKernelCreateSema("PSVitaAliveDiagWake", 0, 0, 1, nullptr);
        if (g_sema >= 0) {
            g_thread = sceKernelCreateThread("PSVitaAliveDiagIO", logWorker,
                                            0x10000120, 16 * 1024, 0, 0, nullptr);
            if (g_thread >= 0) {
                if (sceKernelStartThread(g_thread, 0, nullptr) >= 0) {
                    g_async = true;
                } else {
                    sceKernelDeleteThread(g_thread);
                    g_thread = -1;
                }
            }
            if (!g_async) {
                sceKernelDeleteSema(g_sema);
                g_sema = -1;
            }
        }
    }
    g_initialized = true;
    log(g_async
        ? "[System] diagnostic logger initialized (async, bounded, session.log reset)"
        : "[System] diagnostic worker unavailable; synchronous fallback (session.log reset)");
}

void log(const std::string& message) {
    if (!g_initialized) return;
    char line[LINE_CAPACITY];
    const uint64_t ms = sceKernelGetSystemTimeWide() / 1000ULL;
    const int printed = sceClibSnprintf(line, sizeof(line), "[%llu ms] %s\n",
        (unsigned long long)ms, message.c_str());
    if (printed <= 0) return;
    // snprintf returns the *desired* length, which may exceed the buffer.
    const size_t bytes = static_cast<size_t>(printed) < sizeof(line)
        ? static_cast<size_t>(printed) : sizeof(line) - 1;
    if (!g_async) {
        fallbackWrite(line, bytes);
        return;
    }
    sceKernelLockMutex(g_mutex, 1, nullptr);
    if (!g_stopping) {
        if (g_count == QUEUE_CAPACITY) {
            // Prefer recent failure context over old UI chatter when storage stalls.
            g_head = (g_head + 1) % QUEUE_CAPACITY;
            --g_count;
            ++g_dropped;
        }
        LogLine& dst = g_queue[g_tail];
        std::memcpy(dst.data, line, bytes);
        dst.bytes = static_cast<uint16_t>(bytes);
        dst.sequence = ++g_lastEnqueued;
        g_tail = (g_tail + 1) % QUEUE_CAPACITY;
        ++g_count;
    }
    sceKernelUnlockMutex(g_mutex, 1);
    // Saturating one-permit semaphore: a single wake drains all queued messages.
    sceKernelSignalSema(g_sema, 1);
}

void flush() {
    // Call from report-generation workers only, never during normal navigation.
    if (!g_async) return;
    sceKernelLockMutex(g_mutex, 1, nullptr);
    const uint64_t target = g_lastEnqueued;
    sceKernelUnlockMutex(g_mutex, 1);
    for (unsigned attempt = 0; attempt < 500; ++attempt) {
        sceKernelLockMutex(g_mutex, 1, nullptr);
        const bool done = g_lastWritten >= target;
        sceKernelUnlockMutex(g_mutex, 1);
        if (done) break;
        sceKernelDelayThread(2000); // up to 1 s for a stalled SD2Vita
    }
}

void shutdown() {
    if (!g_initialized) return;
    log("[System] shared diagnostic logger shutdown");
    if (g_async && g_thread >= 0) {
        sceKernelLockMutex(g_mutex, 1, nullptr);
        g_stopping = true;
        sceKernelUnlockMutex(g_mutex, 1);
        sceKernelSignalSema(g_sema, 1);
        sceKernelWaitThreadEnd(g_thread, nullptr, nullptr);
        sceKernelDeleteThread(g_thread);
        g_thread = -1;
        g_async = false;
    }
    if (g_sema >= 0) {
        sceKernelDeleteSema(g_sema);
        g_sema = -1;
    }
    g_initialized = false;
    if (g_mutex >= 0) {
        sceKernelDeleteMutex(g_mutex);
        g_mutex = -1;
    }
}
} // namespace psvitaalive::diagnostics
