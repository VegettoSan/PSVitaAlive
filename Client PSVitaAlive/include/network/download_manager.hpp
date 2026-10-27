#pragma once

#include "network/http_client.hpp"
#include "installer/install_space_policy.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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
    std::string etag;
    std::string lastModified;
    std::string validatorUrl;

    uint64_t expectedSize = 0;
    uint64_t downloadedSize = 0;
    uint64_t bytesPerSecond = 0;

    DownloadState state = DownloadState::Queued;
    bool resumable = true;
    bool preferSegmentedZip = false; // Opt-in: Game/Data ZIP or VPK direct extraction.
    bool segmentedZip = false; // Verified ZIP/VPK range parts ready for virtual reading.
    bool sourceUnavailable = false; // Failed before bytes or HTTP terminal response.
    bool pausedForConnectivity = false; // Runtime-only; manual pause remains distinct.
    InstallSpaceMode spaceMode = InstallSpaceMode::DirectPackage; // runtime preflight only
    // Shared atomic remains stable when DownloadJob objects are copied/moved.
    std::shared_ptr<std::atomic<bool>> cancelRequested =
        std::make_shared<std::atomic<bool>>(false);
    // Separate from cancel: a paused serial .part is kept for safe Range resume.
    std::shared_ptr<std::atomic<bool>> pauseRequested =
        std::make_shared<std::atomic<bool>>(false);

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
/** Startup scan: completed work / total work, current payload name, deleting phase. */
using StartupCleanupProgressFn = std::function<void(uint64_t, uint64_t, const std::string&, bool)>;

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

    std::string enqueue(const std::string& url, const std::string& finalFileName, uint64_t expectedSizeHint = 0, bool preferSegmentedZip = false, InstallSpaceMode spaceMode = InstallSpaceMode::DirectPackage);
    bool processQueue();
    void cancel(const std::string& jobId);
    bool pause(const std::string& jobId);
    /** Only call after the previous worker has finished. Requeues the SAME job. */
    bool resume(const std::string& jobId);
    int recoverJobs();

    /** Remove final payload, metadata and the empty job directory. */
    bool cleanupCompletedJob(const std::string& jobId);

    /** Delete abandoned jobs at startup; only explicitly kept/legacy ambiguous ZIPs survive. */
    int purgeIncompleteJobs(const StartupCleanupProgressFn& onProgress = {});

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
