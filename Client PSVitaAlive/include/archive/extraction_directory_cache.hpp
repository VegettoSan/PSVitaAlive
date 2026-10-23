#pragma once

// Per-extraction cache of directories already verified or created.
// Bounded by design: avoids unbounded RAM growth on huge ZIP archives.
#include <cstddef>
#include <string>
#include <unordered_set>

namespace psvitaalive {

class ExtractionDirectoryCache {
public:
    static constexpr std::size_t kMaxDirectories = 256;

    ExtractionDirectoryCache() { paths_.reserve(kMaxDirectories); }

    bool contains(const std::string& path) const {
        return paths_.find(path) != paths_.end();
    }

    void remember(const std::string& path) {
        if (path.empty() || contains(path)) return;
        // Clear the cache rather than retaining an unbounded number of
        // long deep paths. A repeated directory is simply checked again.
        if (paths_.size() >= kMaxDirectories) paths_.clear();
        paths_.insert(path);
    }

    std::size_t size() const { return paths_.size(); }

private:
    std::unordered_set<std::string> paths_;
};

} // namespace psvitaalive
