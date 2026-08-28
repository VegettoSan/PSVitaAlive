#pragma once

#include "network/http_client.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace psvitaalive {

enum class DownloadState {
    Queued = 0,
    Preparing,
    Downloading,
    Paused,
    Verifying,
    Ready,
    Failed,
    Cancelled,
    Completed
};

const char* toString(DownloadState s);

struct DownloadJob {
    std::string id;
    std::string url;
    std::string finalPath;
    std::string temporaryPath; // *.part
    std::string metadataPath;
    std::string fileName;

    uint64_t expectedSize = 0;
    uint64_t downloadedSize = 0;
    uint64_t bytesPerSecond = 0;

    DownloadState state = DownloadState::Queued;
    bool resumable = true;
    bool cancelRequested = false;

    int lastHttpStatus = 0;
    std::string lastError;
};

struct DownloadProgressEvent {
    std::string jobId;
    std::string fileName;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    uint64_t bytesPerSecond = 0;
    DownloadState state = DownloadState::Downloading;
    /** Optional UI status line (e.g. "retrying download (2/3)..."). */
    std::string message;
};

using DownloadProgressFn = std::function<void(const DownloadProgressEvent&)>;

/**
 * DownloadManager
 *
 * - One active download at a time.
 * - Jobs stored under ux0:data/psvitaalive/downloads/jobs/<id>/.
 * - Resumable *.part downloads with metadata.
 * - The final payload is temporary installation input and can be reclaimed
 *   by cleanupCompletedJob() after a successful install/extraction.
 */
class DownloadManager {
public:
    explicit DownloadManager(HttpClient& http);

    void setProgressCallback(DownloadProgressFn fn);

    std::string enqueue(const std::string& url, const std::string& finalFileName);
    bool processQueue();
    void cancel(const std::string& jobId);
    int recoverJobs();

    /** Remove final payload, metadata and the empty job directory. */
    bool cleanupCompletedJob(const std::string& jobId);

    /** Delete incomplete/failed/cancelled job folders left on disk (startup + after errors). */
    int purgeIncompleteJobs();

    const std::vector<DownloadJob>& jobs() const { return jobs_; }
    DownloadJob* findJob(const std::string& id);

private:
    HttpClient& http_;
    std::vector<DownloadJob> jobs_;
    DownloadProgressFn onProgress_;
    std::string activeJobId_;

    bool ensureJobDirs(DownloadJob& job);
    bool saveMetadata(const DownloadJob& job) const;
    bool loadMetadata(DownloadJob& job) const;
    bool runJob(DownloadJob& job);

    static std::string makeJobId();
    static std::string jobsRoot();
};

} // namespace psvitaalive
