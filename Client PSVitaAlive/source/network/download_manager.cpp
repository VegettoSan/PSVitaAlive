#include "network/download_manager.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstdio>
#include <cstring>
#include <sstream>

namespace psvitaalive {

namespace {
uint32_t g_jobCounter = 1;
}

const char* toString(DownloadState s) {
    switch (s) {
        case DownloadState::Queued: return "Queued";
        case DownloadState::Preparing: return "Preparing";
        case DownloadState::Downloading: return "Downloading";
        case DownloadState::Paused: return "Paused";
        case DownloadState::Verifying: return "Verifying";
        case DownloadState::Ready: return "Ready";
        case DownloadState::Failed: return "Failed";
        case DownloadState::Cancelled: return "Cancelled";
        case DownloadState::Completed: return "Completed";
        default: return "Unknown";
    }
}

DownloadManager::DownloadManager(HttpClient& http) : http_(http) {}

void DownloadManager::setProgressCallback(DownloadProgressFn fn) {
    onProgress_ = std::move(fn);
}

std::string DownloadManager::jobsRoot() {
    return std::string(StorageManager::JOBS_DIR);
}

std::string DownloadManager::makeJobId() {
    char buf[64];
    sceClibSnprintf(buf, sizeof(buf), "job_%u_%u", g_jobCounter++, (unsigned)sceKernelGetProcessTimeLow());
    return std::string(buf);
}

DownloadJob* DownloadManager::findJob(const std::string& id) {
    for (auto& j : jobs_) {
        if (j.id == id) return &j;
    }
    return nullptr;
}

bool DownloadManager::ensureJobDirs(DownloadJob& job) {
    StorageManager st;
    std::string dir = jobsRoot() + "/" + job.id;
    if (!st.createDirectories(dir)) return false;
    job.temporaryPath = dir + "/payload.part";
    job.metadataPath = dir + "/metadata.json";
    job.finalPath = dir + "/payload.bin";
    return true;
}

bool DownloadManager::saveMetadata(const DownloadJob& job) const {
    char body[1400];
    sceClibSnprintf(
        body, sizeof(body),
        "{\n"
        "  \"id\": \"%s\",\n"
        "  \"url\": \"%s\",\n"
        "  \"file_name\": \"%s\",\n"
        "  \"expected_size\": %llu,\n"
        "  \"downloaded_size\": %llu,\n"
        "  \"bytes_per_second\": %llu,\n"
        "  \"state\": \"%s\",\n"
        "  \"last_http_status\": %d\n"
        "}\n",
        job.id.c_str(),
        job.url.c_str(),
        job.fileName.c_str(),
        (unsigned long long)job.expectedSize,
        (unsigned long long)job.downloadedSize,
        (unsigned long long)job.bytesPerSecond,
        toString(job.state),
        job.lastHttpStatus
    );

    StorageManager st;
    return st.writeTextFile(job.metadataPath, body);
}

bool DownloadManager::loadMetadata(DownloadJob& job) const {
    StorageManager st;
    std::string text;
    if (!st.readTextFile(job.metadataPath, text)) return false;

    auto findNum = [&](const char* key) -> uint64_t {
        std::string k = std::string("\"") + key + "\"";
        auto p = text.find(k);
        if (p == std::string::npos) return 0;
        p = text.find(':', p);
        if (p == std::string::npos) return 0;
        return strtoull(text.c_str() + p + 1, nullptr, 10);
    };
    auto findStr = [&](const char* key) -> std::string {
        std::string k = std::string("\"") + key + "\"";
        auto p = text.find(k);
        if (p == std::string::npos) return {};
        p = text.find('"', p + k.size());
        if (p == std::string::npos) return {};
        auto p2 = text.find('"', p + 1);
        if (p2 == std::string::npos) return {};
        return text.substr(p + 1, p2 - p - 1);
    };

    job.url = findStr("url");
    job.fileName = findStr("file_name");
    if (job.fileName.empty()) job.fileName = "payload";
    job.expectedSize = findNum("expected_size");
    job.downloadedSize = findNum("downloaded_size");
    job.bytesPerSecond = findNum("bytes_per_second");
    std::string stName = findStr("state");
    if (stName == "Completed") job.state = DownloadState::Completed;
    else if (stName == "Failed") job.state = DownloadState::Failed;
    else if (stName == "Cancelled") job.state = DownloadState::Cancelled;
    else if (stName == "Ready") job.state = DownloadState::Ready;
    else job.state = DownloadState::Queued;

    return !job.url.empty();
}

std::string DownloadManager::enqueue(const std::string& url, const std::string& finalFileName) {
    DownloadJob job;
    job.id = makeJobId();
    job.url = url;
    job.fileName = finalFileName.empty() ? "download" : finalFileName;
    job.state = DownloadState::Queued;

    if (!ensureJobDirs(job)) {
        sceClibPrintf("[DownloadManager] ensureJobDirs failed\n");
        return {};
    }

    if (!finalFileName.empty()) {
        job.finalPath = jobsRoot() + "/" + job.id + "/" + finalFileName;
    }

    saveMetadata(job);
    jobs_.push_back(job);
    sceClibPrintf("[DownloadManager] enqueued %s file=%s\n", job.id.c_str(), job.fileName.c_str());
    return job.id;
}

void DownloadManager::cancel(const std::string& jobId) {
    if (auto* j = findJob(jobId)) {
        j->cancelRequested = true;
        if (j->state == DownloadState::Queued) {
            j->state = DownloadState::Cancelled;
            saveMetadata(*j);
        }
    }
}

bool DownloadManager::runJob(DownloadJob& job) {
    job.state = DownloadState::Preparing;
    job.cancelRequested = false;
    saveMetadata(job);

    StorageManager st;
    uint64_t offset = 0;
    if (st.exists(job.temporaryPath)) {
        int64_t sz = st.fileSize(job.temporaryPath);
        if (sz > 0) offset = static_cast<uint64_t>(sz);
    }
    job.downloadedSize = offset;
    job.state = DownloadState::Downloading;
    saveMetadata(job);

    activeJobId_ = job.id;

    auto progress = [&](const HttpProgress& p) {
        job.downloadedSize = p.absoluteDownloaded;
        job.bytesPerSecond = p.bytesPerSecond;
        if (p.total > 0) job.expectedSize = p.total;
        if (onProgress_) {
            DownloadProgressEvent ev;
            ev.jobId = job.id;
            ev.fileName = job.fileName;
            ev.downloaded = job.downloadedSize;
            ev.total = job.expectedSize;
            ev.bytesPerSecond = job.bytesPerSecond;
            ev.state = DownloadState::Downloading;
            onProgress_(ev);
        }
        static uint64_t lastSaved = 0;
        if (job.downloadedSize - lastSaved >= 256 * 1024 || job.downloadedSize < lastSaved) {
            saveMetadata(job);
            lastSaved = job.downloadedSize;
        }
    };

    auto cancelFn = [&]() -> bool {
        return job.cancelRequested;
    };

    HttpResult hr = http_.downloadToFile(
        job.url,
        job.temporaryPath,
        offset,
        progress,
        cancelFn
    );

    job.lastHttpStatus = http_.lastStatusCode();
    activeJobId_.clear();

    if (hr == HttpResult::Cancelled || job.cancelRequested) {
        job.state = DownloadState::Cancelled;
        job.lastError = "cancelled";
        saveMetadata(job);
        return false;
    }

    if (hr != HttpResult::Ok) {
        job.state = DownloadState::Failed;
        job.lastError = http_.lastError();
        saveMetadata(job);
        return false;
    }

    st.removeFile(job.finalPath);
    if (!st.rename(job.temporaryPath, job.finalPath)) {
        job.state = DownloadState::Failed;
        job.lastError = "rename part->final failed";
        saveMetadata(job);
        return false;
    }

    job.state = DownloadState::Completed;
    int64_t fs = st.fileSize(job.finalPath);
    job.downloadedSize = static_cast<uint64_t>(fs > 0 ? fs : 0);
    saveMetadata(job);

    if (onProgress_) {
        DownloadProgressEvent ev;
        ev.jobId = job.id;
        ev.fileName = job.fileName;
        ev.downloaded = job.downloadedSize;
        ev.total = job.expectedSize ? job.expectedSize : job.downloadedSize;
        ev.bytesPerSecond = job.bytesPerSecond;
        ev.state = DownloadState::Completed;
        onProgress_(ev);
    }

    sceClibPrintf("[DownloadManager] completed %s (%llu bytes)\n",
                  job.id.c_str(),
                  (unsigned long long)job.downloadedSize);
    return true;
}

bool DownloadManager::processQueue() {
    bool any = false;
    for (auto& job : jobs_) {
        if (job.state == DownloadState::Queued || job.state == DownloadState::Failed) {
            if (job.state != DownloadState::Queued) continue;
            any = true;
            runJob(job);
        }
    }
    return any;
}

bool DownloadManager::cleanupCompletedJob(const std::string& jobId) {
    DownloadJob* job = findJob(jobId);
    if (!job) return false;

    StorageManager st;
    bool ok = true;
    if (st.exists(job->temporaryPath)) ok = st.removeFile(job->temporaryPath) && ok;
    if (st.exists(job->finalPath)) ok = st.removeFile(job->finalPath) && ok;
    if (st.exists(job->metadataPath)) ok = st.removeFile(job->metadataPath) && ok;

    const std::string jobDir = jobsRoot() + "/" + job->id;
    if (st.exists(jobDir) && st.isDirectory(jobDir)) {
        ok = st.removeDirectory(jobDir) && ok;
    }

    if (ok) {
        jobs_.erase(
            std::remove_if(jobs_.begin(), jobs_.end(), [&](const DownloadJob& item) {
                return item.id == jobId;
            }),
            jobs_.end()
        );
    }

    sceClibPrintf("[DownloadManager] cleanup job=%s ok=%d\n", jobId.c_str(), ok ? 1 : 0);
    return ok;
}

int DownloadManager::recoverJobs() {
    StorageManager st;
    st.createDirectories(jobsRoot());

    SceUID uid = sceIoDopen(jobsRoot().c_str());
    if (uid < 0) return 0;

    int recovered = 0;
    SceIoDirent ent;
    while (sceIoDread(uid, &ent) > 0) {
        if (ent.d_name[0] == '.') continue;
        if ((ent.d_stat.st_mode & SCE_S_IFDIR) == 0) continue;

        DownloadJob job;
        job.id = ent.d_name;
        if (!ensureJobDirs(job)) continue;
        if (!loadMetadata(job)) continue;
        if (job.state == DownloadState::Completed) continue;

        if (job.state == DownloadState::Downloading || job.state == DownloadState::Preparing) {
            job.state = DownloadState::Queued;
        }
        jobs_.push_back(job);
        recovered++;
        sceClibPrintf("[DownloadManager] recovered %s state=%s dl=%llu\n",
                      job.id.c_str(), toString(job.state),
                      (unsigned long long)job.downloadedSize);
    }
    sceIoDclose(uid);
    return recovered;
}

} // namespace psvitaalive
