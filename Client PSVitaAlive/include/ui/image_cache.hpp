#pragma once

#include <psp2/kernel/threadmgr.h>

#include <string>
#include <vector>

namespace psvitaalive {
namespace ui {

class ImageCache {
public:
    ImageCache();
    ~ImageCache();

    bool init();
    void shutdown();

    std::string request(const std::string& url, const std::string& namespaceName);

    // Queue a remote image for background download without requiring it to be
    // visible on screen. Used at startup to warm the complete catalog cache.
    void preload(const std::vector<std::string>& urls, const std::string& namespaceName);

    bool isReady(const std::string& localPath) const;
    bool isFailed(const std::string& localPath) const;

    // Discard queued requests that have not started yet. Kept as an explicit
    // API for callers that intentionally want to cancel work.
    void cancelQueuedRequests();

private:
    struct Job {
        std::string url;
        std::string path;
        int attempt = 0;
    };

    SceUID mutex_ = -1;
    SceUID workerThread_ = -1;
    volatile bool stopping_ = false;
    std::vector<Job> queue_;
    std::vector<std::string> pending_;
    std::vector<std::string> ready_;
    std::vector<std::string> failed_;

    static int workerEntry(SceSize args, void* argp);
    int workerMain();

    std::string makePath(const std::string& url, const std::string& namespaceName) const;
    bool contains(const std::vector<std::string>& values, const std::string& value) const;
    bool ensureDirectory(const std::string& path) const;
    void markReady(const std::string& path);
    void markFailed(const std::string& path);
};

} // namespace ui
} // namespace psvitaalive
