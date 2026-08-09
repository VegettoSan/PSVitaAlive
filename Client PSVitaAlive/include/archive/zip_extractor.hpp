#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace psvitaalive {

enum class ZipResult {
    Ok = 0,
    OpenFailed,
    InvalidEntry,
    UnsafePath,
    IoError,
    Cancelled,
    UnknownError
};

const char* toString(ZipResult r);

struct ZipProgress {
    uint64_t entriesDone = 0;
    uint64_t entriesTotal = 0;
    uint64_t bytesWritten = 0;
    uint64_t bytesTotal = 0;
    std::string currentEntry;
};

using ZipProgressFn = std::function<void(const ZipProgress&)>;
using ZipCancelFn = std::function<bool()>;

class ZipExtractor {
public:
    ZipResult extract(
        const std::string& zipPath,
        const std::string& destinationDir,
        ZipProgressFn onProgress = nullptr,
        ZipCancelFn shouldCancel = nullptr
    );

    const std::string& lastError() const { return lastError_; }
    static bool isSafeEntryName(const std::string& entryName);
    static bool resolveSafePath(const std::string& destinationDir, const std::string& entryName, std::string& outPath);

private:
    std::string lastError_;
    void setError(const std::string& msg);
};

} // namespace psvitaalive
