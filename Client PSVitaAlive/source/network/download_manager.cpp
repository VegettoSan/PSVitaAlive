#include "localization/localization.hpp"
#include "network/download_manager.hpp"
#include "network/mediafire_resolver.hpp"
#include "storage/storage_manager.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

namespace psvitaalive {

namespace {
int hexValue(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
std::string urlDecodePath(std::string value){for(int pass=0;pass<2;++pass){std::string out;out.reserve(value.size());bool changed=false;for(size_t i=0;i<value.size();++i){if(value[i]=='%'&&i+2<value.size()){const int h=hexValue(value[i+1]);const int l=hexValue(value[i+2]);if(h>=0&&l>=0){out.push_back(static_cast<char>((h<<4)|l));i+=2;changed=true;continue;}}out.push_back(value[i]);}value.swap(out);if(!changed)break;}return value;}
std::string inferFileNameFromUrl(const std::string&url){std::string clean=url;const size_t q=clean.find('?');if(q!=std::string::npos)clean.erase(q);const size_t f=clean.find('#');if(f!=std::string::npos)clean.erase(f);std::string name;const size_t media=clean.find("mediafire.com");const size_t marker=clean.find("/file/",media==std::string::npos?0:media);if(media!=std::string::npos&&marker!=std::string::npos){const size_t idEnd=clean.find('/',marker+6);if(idEnd!=std::string::npos){const size_t nameEnd=clean.find('/',idEnd+1);name=clean.substr(idEnd+1,nameEnd==std::string::npos?std::string::npos:nameEnd-(idEnd+1));}}if(name.empty()){const size_t slash=clean.find_last_of('/');name=slash==std::string::npos?clean:clean.substr(slash+1);if(name=="file"&&slash!=std::string::npos){const size_t prev=clean.find_last_of('/',slash-1);if(prev!=std::string::npos)name=clean.substr(prev+1,slash-prev-1);}}return urlDecodePath(name);}
std::string sanitizePayloadFileName(const std::string&name,const std::string&url){std::string n=name;auto lower=[](std::string s){for(char&c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;};const std::string low=lower(n);const bool placeholder=n.empty()||low=="file"||low=="download"||low=="payload"||low=="payload.bin"||low.find("get_hb_url")!=std::string::npos;if(placeholder){const std::string inferred=inferFileNameFromUrl(url);if(!inferred.empty()&&inferred!="file")n=inferred;}if(!n.empty()&&lower(n)!="file"&&lower(n)!="download")return n;return "payload.bin";}
} // namespace

namespace { uint32_t g_jobCounter = 1; }

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
void DownloadManager::setProgressCallback(DownloadProgressFn fn) { onProgress_ = std::move(fn); }
std::string DownloadManager::jobsRoot() { return std::string(StorageManager::JOBS_DIR); }

std::string DownloadManager::makeJobId() {
    char buf[64];
    sceClibSnprintf(buf, sizeof(buf), "job_%u_%u", g_jobCounter++, (unsigned)sceKernelGetProcessTimeLow());
    return std::string(buf);
}

DownloadJob* DownloadManager::findJob(const std::string& id) {
    for (auto& j : jobs_) if (j.id == id) return &j;
    return nullptr;
}

bool DownloadManager::ensureJobDirs(DownloadJob& job) {
    StorageManager st;
    const std::string dir = jobsRoot() + "/" + job.id;
    if (!st.createDirectories(dir)) return false;
    job.temporaryPath = dir + "/payload.part";
    job.metadataPath = dir + "/metadata.json";

    std::string name = job.fileName.empty() ? "payload.bin" : job.fileName;
    for (char& c : name) {
        if (c == '/' || c == '\\') c = '_';
    }
    job.finalPath = dir + "/" + name;
    return true;
}

bool DownloadManager::saveMetadata(const DownloadJob& job) const {
    auto jsonEsc = [](const std::string& in) -> std::string {
        std::string out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in) {
            if (c == '"') { out += "\\\""; continue; }
            if (c == '\\') { out += "\\\\"; continue; }
            if (c == '\n') { out += "\\n"; continue; }
            if (c == '\r') { out += "\\r"; continue; }
            if (c == '\t') { out += "\\t"; continue; }
            if (c < 0x20) {
                char buf[8];
                sceClibSnprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
                continue;
            }
            out.push_back(static_cast<char>(c));
        }
        return out;
    };
    const std::string id = jsonEsc(job.id);
    const std::string url = jsonEsc(job.url);
    const std::string fileName = jsonEsc(job.fileName);
    const std::string etag = jsonEsc(job.etag);
    const std::string lastModified = jsonEsc(job.lastModified);
    const std::string validatorUrl = jsonEsc(job.validatorUrl);
    const std::string state = jsonEsc(toString(job.state));

    char body[2048];
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
        "  \"last_http_status\": %d,\n"
        "  \"etag\": \"%s\",\n"
        "  \"last_modified\": \"%s\",\n"
        "  \"validator_url\": \"%s\"\n"
        "}\n",
        id.c_str(),
        url.c_str(),
        fileName.c_str(),
        (unsigned long long)job.expectedSize,
        (unsigned long long)job.downloadedSize,
        (unsigned long long)job.bytesPerSecond,
        state.c_str(),
        job.lastHttpStatus,
        etag.c_str(),
        lastModified.c_str(),
        validatorUrl.c_str()
    );
    StorageManager st;
    return st.writeTextFile(job.metadataPath, body);
}

bool DownloadManager::loadMetadata(DownloadJob& job) const {
    StorageManager st;
    std::string text;
    if (!st.readTextFile(job.metadataPath, text)) return false;

    auto findNum = [&](const char* key) -> uint64_t {
        const std::string k = std::string("\"") + key + "\"";
        auto p = text.find(k);
        if (p == std::string::npos) return 0;
        p = text.find(':', p);
        if (p == std::string::npos) return 0;
        return strtoull(text.c_str() + p + 1, nullptr, 10);
    };
    auto findStr = [&](const char* key) -> std::string {
        const std::string k = std::string("\"") + key + "\"";
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
    job.etag = findStr("etag");
    job.lastModified = findStr("last_modified");
    job.validatorUrl = findStr("validator_url");
    const std::string state = findStr("state");
    if (state == "Completed") job.state = DownloadState::Completed;
    else if (state == "Failed") job.state = DownloadState::Failed;
    else if (state == "Cancelled") job.state = DownloadState::Cancelled;
    else if (state == "Ready") job.state = DownloadState::Ready;
    else job.state = DownloadState::Queued;
    return !job.url.empty();
}

std::string DownloadManager::enqueue(const std::string& url, const std::string& finalFileName, uint64_t expectedSizeHint) {
    DownloadJob job;
    job.id = makeJobId();
    job.url = url;
    job.fileName = sanitizePayloadFileName(finalFileName.empty() ? "download" : finalFileName, url);
    // Catalog/provider size is a hint. HttpClient still replaces it with an
    // authoritative remote total once Content-Length/Content-Range is observed.
    job.expectedSize = expectedSizeHint;
    job.state = DownloadState::Queued;
    if (!ensureJobDirs(job)) return {};
    saveMetadata(job);
    jobs_.push_back(job);
    return job.id;
}

void DownloadManager::cancel(const std::string& jobId) {
    if (auto* j = findJob(jobId)) {
        j->cancelRequested = true;
        StorageManager st;
        if (j->state == DownloadState::Queued) {
            st.removeFile(j->temporaryPath);
            j->state = DownloadState::Cancelled;
            j->lastError = "cancelled";
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
        const int64_t sz = st.fileSize(job.temporaryPath);
        if (sz > 0) offset = static_cast<uint64_t>(sz);
    }
    job.downloadedSize = offset;
    job.state = DownloadState::Downloading;
    saveMetadata(job);
    activeJobId_ = job.id;

    // Catalog and MediaFire page sizes are hints. Only enforce a hard overrun
    // after HttpClient has received an authoritative Content-Length/Content-Range.
    auto sizeHardLimit = [](uint64_t total) -> uint64_t {
        if (total == 0) return 0;
        const uint64_t slack = 8ULL * 1024ULL * 1024ULL;
        return total > (~0ULL - slack) ? ~0ULL : total + slack;
    };

    bool sizeLimitHit = false;
    bool diskSpaceHit = false;
    bool remoteTotalKnown = false;
    uint64_t lastSaved = offset;
    uint64_t lastSpaceCheck = 0;
    constexpr uint64_t kMetadataSaveStep = 8ULL * 1024ULL * 1024ULL;
    constexpr uint64_t kSpaceCheckStep = 32ULL * 1024ULL * 1024ULL;
    constexpr uint64_t kFreeSpaceReserve = 16ULL * 1024ULL * 1024ULL;
    auto progress = [&](const HttpProgress& p) {
        job.downloadedSize = p.absoluteDownloaded;
        job.bytesPerSecond = p.bytesPerSecond;
        // p.total is only non-zero when HttpClient observed a remote length.
        if (p.total > 0) {
            job.expectedSize = p.total;
            remoteTotalKnown = true;
        }
        const uint64_t limit = remoteTotalKnown ? sizeHardLimit(job.expectedSize) : 0;
        if (limit > 0 && job.downloadedSize > limit) {
            sizeLimitHit = true;
            job.cancelRequested = true;
            char m[160];
            sceClibSnprintf(m, sizeof(m),
                "[DownloadManager] size limit hit downloaded=%llu expected=%llu limit=%llu",
                (unsigned long long)job.downloadedSize,
                (unsigned long long)job.expectedSize,
                (unsigned long long)limit);
            diagnostics::log(m);
        }
        if (!diskSpaceHit &&
            (lastSpaceCheck == 0 || job.downloadedSize < lastSpaceCheck ||
             job.downloadedSize - lastSpaceCheck >= kSpaceCheckStep)) {
            uint64_t freeB = 0, totalB = 0;
            if (StorageManager::queryUx0Space(freeB, totalB) && freeB < kFreeSpaceReserve) {
                diskSpaceHit = true;
                job.cancelRequested = true;
                char m[180];
                sceClibSnprintf(m, sizeof(m),
                    "[DownloadManager] low-space guard free=%llu downloaded=%llu reserve=%llu",
                    (unsigned long long)freeB,
                    (unsigned long long)job.downloadedSize,
                    (unsigned long long)kFreeSpaceReserve);
                diagnostics::log(m);
            }
            lastSpaceCheck = job.downloadedSize;
        }
        if (onProgress_) {
            DownloadProgressEvent ev;
            ev.jobId = job.id;
            ev.fileName = job.fileName;
            ev.downloaded = job.downloadedSize;
            // UI %: use expected hint even when CDN omits Content-Length
            ev.total = job.expectedSize ? job.expectedSize : job.downloadedSize;
            ev.bytesPerSecond = job.bytesPerSecond;
            ev.state = DownloadState::Downloading;
            onProgress_(ev);
        }
        if ((job.downloadedSize >= lastSaved && job.downloadedSize - lastSaved >= kMetadataSaveStep) ||
            job.downloadedSize < lastSaved) {
            saveMetadata(job);
            lastSaved = job.downloadedSize;
        }
    };
    auto cancelFn = [&]() -> bool { return job.cancelRequested; };

    std::string effectiveUrl = job.url;
    const bool mediafire = isMediaFireUrl(job.url);
    if (mediafire) {
        diagnostics::log("[DownloadManager] MediaFire URL detected - resolving direct link");
        std::string direct;
        std::string mfErr;
        uint64_t mfSize = 0;
        if (!resolveMediaFireDirectUrl(http_, job.url, direct, mfErr, &mfSize) || direct.empty()) {
            job.state = DownloadState::Failed;
            job.lastError = mfErr.empty() ? "MediaFire resolve failed" : mfErr;
            saveMetadata(job);
            activeJobId_.clear();
            diagnostics::log(std::string("[DownloadManager] MediaFire resolve failed: ") + job.lastError);
            return false;
        }
        effectiveUrl = direct;
        // Prefer page size when we do not yet have a better expected size.
        if (mfSize > 0 && job.expectedSize == 0) {
            job.expectedSize = mfSize;
            saveMetadata(job);
        }
        diagnostics::log(std::string("[DownloadManager] MediaFire direct link OK expected=") +
                         std::to_string(job.expectedSize));
    }

    const bool isArchiveUrl =
        job.url.find("archive.org") != std::string::npos ||
        effectiveUrl.find("archive.org") != std::string::npos;
    // Outer attempts on top of HttpClient's internal retries.
    // archive.org is flaky under load — allow a couple of full restarts.
    const int outerAttempts = isArchiveUrl ? 4 : 2;
    HttpResult hr = HttpResult::NetworkError;
    for (int outer = 0; outer < outerAttempts; ++outer) {
        if (outer > 0) {
            const std::string prevErr = http_.lastError();
            char msg[160];
            sceClibSnprintf(msg, sizeof(msg),
                "[DownloadManager] attempt %d/%d failed: %s — retrying",
                outer, outerAttempts, prevErr.c_str());
            sceClibPrintf("%s\n", msg);
            diagnostics::log(msg);
            // Let the install UI show a clear retry line (e.g. "retrying download (2/3)...").
            if (onProgress_) {
                DownloadProgressEvent ev;
                ev.jobId = job.id;
                ev.fileName = job.fileName;
                ev.downloaded = job.downloadedSize;
                ev.total = job.expectedSize;
                ev.bytesPerSecond = 0;
                ev.state = DownloadState::Downloading;
                char uiMsg[48];
                sceClibSnprintf(uiMsg, sizeof(uiMsg), ::psvitaalive::L(::psvitaalive::TextId::InstMsgRetryDownload),
                    outer + 1, outerAttempts);
                ev.message = uiMsg;
                onProgress_(ev);
            }
            // Outer gap kept small — inner loop already retried with fail-fast.
            const int delayMs = isArchiveUrl ? (1000 * outer) : 500;
            sceKernelDelayThread(delayMs * 1000);
            if (mediafire) {
                // MediaFire direct URLs expire, but the bytes already downloaded do not.
                // Re-resolve the page and attempt a normal Range resume against the new
                // direct URL. HttpClient will safely truncate/restart if that CDN edge
                // ignores Range or serves a changed resource.
                const int64_t sz = st.fileSize(job.temporaryPath);
                offset = sz > 0 ? static_cast<uint64_t>(sz) : 0;
                job.downloadedSize = offset;
                std::string direct;
                std::string mfErr;
                uint64_t mfSize = 0;
                if (resolveMediaFireDirectUrl(http_, job.url, direct, mfErr, &mfSize) && !direct.empty()) {
                    effectiveUrl = direct;
                    if (mfSize > 0) job.expectedSize = mfSize;
                    job.validatorUrl.clear();
                    job.etag.clear();
                    job.lastModified.clear();
                    remoteTotalKnown = false;
                    char mfMsg[180];
                    sceClibSnprintf(mfMsg, sizeof(mfMsg),
                        "[DownloadManager] MediaFire re-resolved resume_offset=%llu expected_hint=%llu",
                        (unsigned long long)offset,
                        (unsigned long long)job.expectedSize);
                    diagnostics::log(mfMsg);
                } else {
                    diagnostics::log(std::string("[DownloadManager] MediaFire re-resolve failed: ") + mfErr);
                    if (outer + 1 < outerAttempts) continue;
                    break;
                }
            } else if (job.downloadedSize == 0) {
                st.removeFile(job.temporaryPath);
                offset = 0;
            } else {
                const int64_t sz = st.fileSize(job.temporaryPath);
                offset = sz > 0 ? static_cast<uint64_t>(sz) : 0;
                job.downloadedSize = offset;
            }
            sizeLimitHit = false;
            diskSpaceHit = false;
            remoteTotalKnown = false;
            lastSpaceCheck = 0;
            job.cancelRequested = false;
        }
        // Validators are only meaningful for the same resolved resource. MediaFire
        // direct links are short-lived, so do not reuse a validator across a new URL.
        std::string ifRangeValidator;
        if (offset > 0 && job.validatorUrl == effectiveUrl) {
            ifRangeValidator = !job.etag.empty() ? job.etag : job.lastModified;
        }
        hr = http_.downloadToFile(
            effectiveUrl,
            job.temporaryPath,
            offset,
            progress,
            cancelFn,
            0,
            ifRangeValidator,
            job.expectedSize
        );
        if (!http_.lastEtag().empty()) job.etag = http_.lastEtag();
        if (!http_.lastModified().empty()) job.lastModified = http_.lastModified();
        job.validatorUrl = effectiveUrl;
        saveMetadata(job);
        if (hr == HttpResult::Ok || hr == HttpResult::Cancelled || job.cancelRequested || sizeLimitHit)
            break;
    }
    job.lastHttpStatus = http_.lastStatusCode();
    activeJobId_.clear();

    if (diskSpaceHit) {
        st.removeFile(job.temporaryPath);
        job.downloadedSize = 0;
        job.state = DownloadState::Failed;
        job.lastError = "not enough free space while downloading";
        saveMetadata(job);
        st.removeFile(job.finalPath);
        diagnostics::log("[DownloadManager] aborted by runtime low-space guard");
        return false;
    }
    if (sizeLimitHit) {
        st.removeFile(job.temporaryPath);
        job.downloadedSize = 0;
        job.state = DownloadState::Failed;
        // Usually a bad mid-retry append or a wrong Content-Length; MediaFire is only one cause.
        job.lastError = mediafire
            ? "download exceeded expected size (possible MediaFire error page)"
            : "download exceeded expected size (interrupted transfer; please retry)";
        saveMetadata(job);
        st.removeFile(job.finalPath);
        diagnostics::log("[DownloadManager] aborted: exceeded expected size with margin");
        return false;
    }
    if (hr == HttpResult::Cancelled || job.cancelRequested) {
        st.removeFile(job.temporaryPath);
        job.downloadedSize = 0;
        job.state = DownloadState::Cancelled;
        job.lastError = "cancelled by user";
        saveMetadata(job);
        st.removeFile(job.finalPath);
        return false;
    }
    if (hr != HttpResult::Ok) {
        job.state = DownloadState::Failed;
        job.lastError = http_.lastError().empty() ? "download failed" : http_.lastError();
        saveMetadata(job);
        st.removeFile(job.temporaryPath);
        st.removeFile(job.finalPath);
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
    const int64_t fs = st.fileSize(job.finalPath);
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
    return true;
}

bool DownloadManager::processQueue() {
    for (auto& job : jobs_) {
        if (job.state == DownloadState::Queued) return runJob(job);
    }
    return false;
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
    if (st.exists(jobDir) && st.isDirectory(jobDir)) ok = st.removeDirectory(jobDir) && ok;
    if (ok) {
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const DownloadJob& item) {
            return item.id == jobId;
        }), jobs_.end());
    }
    return ok;
}

int DownloadManager::purgeIncompleteJobs() {
    StorageManager st;
    st.createDirectories(jobsRoot());
    const std::string root = jobsRoot();
    SceUID uid = sceIoDopen(root.c_str());
    if (uid < 0) return 0;
    int purged = 0;
    SceIoDirent ent;
    std::vector<std::string> victims;
    while (sceIoDread(uid, &ent) > 0) {
        if (ent.d_name[0] == '.' || (ent.d_stat.st_mode & SCE_S_IFDIR) == 0) continue;
        victims.push_back(ent.d_name);
    }
    sceIoDclose(uid);

    for (const auto& id : victims) {
        DownloadJob job;
        job.id = id;
        if (!ensureJobDirs(job)) continue;
        const bool hasMeta = loadMetadata(job);
        if (hasMeta && job.state == DownloadState::Completed) continue;
        if (st.exists(job.temporaryPath)) st.removeFile(job.temporaryPath);
        if (st.exists(job.finalPath)) st.removeFile(job.finalPath);
        if (st.exists(job.metadataPath)) st.removeFile(job.metadataPath);
        const std::string jobDir = jobsRoot() + "/" + id;
        SceUID d = sceIoDopen(jobDir.c_str());
        if (d >= 0) {
            SceIoDirent e2;
            while (sceIoDread(d, &e2) > 0) {
                if (e2.d_name[0] == '.') continue;
                st.removeFile(jobDir + "/" + e2.d_name);
            }
            sceIoDclose(d);
        }
        st.removeDirectory(jobDir);
        ++purged;
    }

    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const DownloadJob& j) {
        return j.state != DownloadState::Completed && j.state != DownloadState::Downloading;
    }), jobs_.end());

    if (purged > 0) {
        char m[128];
        sceClibSnprintf(m, sizeof(m), "[DownloadManager] purged %d incomplete job folders", purged);
        sceClibPrintf("%s\n", m);
    }
    return purged;
}

int DownloadManager::recoverJobs() {
    return purgeIncompleteJobs();
}

} // namespace psvitaalive
