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

    uint64_t expectedSize = 0;
    uint64_t downloadedSize = 0;

    DownloadState state = DownloadState::Queued;
    bool resumable = true;
    bool cancelRequested = false;

    int lastHttpStatus = 0;
    std::string lastError;
};

struct DownloadProgressEvent {
    std::string jobId;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    DownloadState state = DownloadState::Downloading;
};

using DownloadProgressFn = std::function<void(const DownloadProgressEvent&)>;

/**
 * DownloadManager — Phase 3
 *
 * - One active download at a time (simple queue)
 * - Jobs stored under ux0:data/psvitaalive/downloads/jobs/<id>/
 * - payload.part + metadata.json
 * - Resume via HTTP Range when server supports it
 * - Cancel flag checked between chunks (via HttpClient progress)
 */
class DownloadManager {
public:
    explicit DownloadManager(HttpClient& http);

    void setProgressCallback(DownloadProgressFn fn);

    /** Create job directories and metadata. Returns job id. */
    std::string enqueue(const std::string& url, const std::string& finalFileName);

    /** Process queue head until empty or failure that stops the loop. */
    bool processQueue();

    /** Request cancel of current/active job. */
    void cancel(const std::string& jobId);

    /** Load jobs from disk (jobs with .part / metadata). */
    int recoverJobs();

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
