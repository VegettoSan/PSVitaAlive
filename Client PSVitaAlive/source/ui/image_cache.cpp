#include "ui/image_cache.hpp"

#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace psvitaalive {
namespace ui {

namespace {
constexpr const char* IMAGE_ROOT = "ux0:data/psvitaalive/cache/images";
constexpr int WORKER_PRIORITY = 0x10000100;
constexpr int WORKER_STACK = 48 * 1024;
constexpr uint32_t MUTEX_TIMEOUT_US = 0xFFFFFFFF;

uint32_t fnv1a(const std::string& value) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

std::string hex32(uint32_t value) {
    char buffer[16];
    sceClibSnprintf(buffer, sizeof(buffer), "%08X", value);
    return buffer;
}

std::string extensionOf(const std::string& url) {
    std::string clean = url;
    const std::size_t query = clean.find('?');
    if (query != std::string::npos) clean.erase(query);
    const std::size_t fragment = clean.find('#');
    if (fragment != std::string::npos) clean.erase(fragment);

    const std::size_t dot = clean.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= clean.size()) return ".img";

    std::string ext = clean.substr(dot);
    if (ext.size() > 5) return ".img";
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") return ".img";
    if (ext == ".jpeg") return ".jpg";
    return ext;
}
}

ImageCache::ImageCache() = default;
ImageCache::~ImageCache() { shutdown(); }

bool ImageCache::ensureDirectory(const std::string& path) const {
    StorageManager storage;
    return storage.createDirectories(path);
}

bool ImageCache::init() {
    if (workerThread_ >= 0) return true;

    if (!ensureDirectory(IMAGE_ROOT)) {
        sceClibPrintf("[ImageCache] cannot create image cache directory\n");
        return false;
    }

    mutex_ = sceKernelCreateMutex("PSVitaAliveImageCache", 0, 0, nullptr);
    if (mutex_ < 0) {
        sceClibPrintf("[ImageCache] mutex creation failed: 0x%08X\n", mutex_);
        return false;
    }

    stopping_ = false;
    workerThread_ = sceKernelCreateThread(
        "PSVitaAliveImageWorker",
        &ImageCache::workerEntry,
        WORKER_PRIORITY,
        WORKER_STACK,
        0,
        0,
        nullptr
    );

    if (workerThread_ < 0) {
        sceKernelDeleteMutex(mutex_);
        mutex_ = -1;
        sceClibPrintf("[ImageCache] worker creation failed: 0x%08X\n", workerThread_);
        return false;
    }

    ImageCache* self = this;
    const int result = sceKernelStartThread(workerThread_, sizeof(self), &self);
    if (result < 0) {
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
        sceKernelDeleteMutex(mutex_);
        mutex_ = -1;
        sceClibPrintf("[ImageCache] worker start failed: 0x%08X\n", result);
        return false;
    }

    return true;
}

void ImageCache::shutdown() {
    stopping_ = true;

    if (workerThread_ >= 0) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }

    if (mutex_ >= 0) {
        sceKernelDeleteMutex(mutex_);
        mutex_ = -1;
    }

    queue_.clear();
}

bool ImageCache::contains(const std::vector<std::string>& values, const std::string& value) const {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string ImageCache::makePath(const std::string& url, const std::string& namespaceName) const {
    const std::string safeNamespace = namespaceName.empty() ? "misc" : namespaceName;
    const std::string ext = extensionOf(url);
    return std::string(IMAGE_ROOT) + "/" + safeNamespace + "_" + hex32(fnv1a(url)) + ext;
}

std::string ImageCache::request(const std::string& url, const std::string& namespaceName) {
    if (url.empty() || mutex_ < 0) return {};

    const std::string path = makePath(url, namespaceName);
    SceIoStat stat = {};
    if (sceIoGetstat(path.c_str(), &stat) >= 0 && stat.st_size > 0) {
        if (!contains(ready_, path)) ready_.push_back(path);
        return path;
    }

    sceKernelLockMutex(mutex_, 1, nullptr);
    const bool queued = std::any_of(queue_.begin(), queue_.end(), [&](const Job& job) {
        return job.path == path;
    });
    const bool done = contains(ready_, path) || contains(failed_, path);
    if (!queued && !done) queue_.push_back({url, path});
    sceKernelUnlockMutex(mutex_, 1);
    return path;
}

bool ImageCache::isReady(const std::string& localPath) const {
    if (localPath.empty()) return false;
    SceIoStat stat = {};
    return sceIoGetstat(localPath.c_str(), &stat) >= 0 && stat.st_size > 0;
}

bool ImageCache::isFailed(const std::string& localPath) const {
    if (mutex_ < 0 || localPath.empty()) return false;
    // This is read-only UI state; a short mutex section keeps it race-safe.
    sceKernelLockMutex(mutex_, 1, nullptr);
    const bool result = contains(failed_, localPath);
    sceKernelUnlockMutex(mutex_, 1);
    return result;
}

void ImageCache::markReady(const std::string& path) {
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (!contains(ready_, path)) ready_.push_back(path);
    failed_.erase(std::remove(failed_.begin(), failed_.end(), path), failed_.end());
    sceKernelUnlockMutex(mutex_, 1);
}

void ImageCache::markFailed(const std::string& path) {
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (!contains(failed_, path)) failed_.push_back(path);
    sceKernelUnlockMutex(mutex_, 1);
}

int ImageCache::workerEntry(SceSize args, void* argp) {
    (void)args;
    ImageCache* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    return self ? self->workerMain() : -1;
}

int ImageCache::workerMain() {
    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        sceClibPrintf("[ImageCache] HttpClient init failed\n");
        return -1;
    }

    while (!stopping_) {
        Job job;
        bool haveJob = false;

        sceKernelLockMutex(mutex_, 1, nullptr);
        if (!queue_.empty()) {
            job = queue_.front();
            queue_.erase(queue_.begin());
            haveJob = true;
        }
        sceKernelUnlockMutex(mutex_, 1);

        if (!haveJob) {
            sceKernelDelayThread(50 * 1000);
            continue;
        }

        const HttpResult result = http.downloadToFile(job.url, job.path);
        if (result == HttpResult::Ok && isReady(job.path)) {
            markReady(job.path);
            sceClibPrintf("[ImageCache] ready %s\n", job.path.c_str());
        } else {
            markFailed(job.path);
            sceClibPrintf("[ImageCache] failed url=%s error=%s\n", job.url.c_str(), http.lastError().c_str());
            sceIoRemove(job.path.c_str());
        }
    }

    http.shutdown();
    return 0;
}

} // namespace ui
} // namespace psvitaalive
