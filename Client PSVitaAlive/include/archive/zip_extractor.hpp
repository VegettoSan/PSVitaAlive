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
        ZipCancelFn shouldCancel = nullptr,
        bool segmentedZip = false,
        const std::string& expectedSha1 = {}
    );

    // Safe accelerated path for Game/Data ZIPs: extract to a unique folder
    // on the same device, verify SHA-1 before commit, then move NEW files.
    // Existing-file collisions and overdeep paths fall back to the proven
    // pre-extraction integrity check + standard extractor.
    ZipResult extractVerifiedDataZip(
        const std::string& zipPath,
        const std::string& destinationDir,
        ZipProgressFn onProgress,
        ZipCancelFn shouldCancel,
        bool segmentedZip,
        const std::string& expectedSha1
    );

    const std::string& lastError() const { return lastError_; }
    /** Staged directory branches were moved; very deep child reads are not verified. */
    bool usedStagedMoves() const { return usedStagedMoves_; }
    static bool isSafeEntryName(const std::string& entryName);
    static bool resolveSafePath(const std::string& destinationDir, const std::string& entryName, std::string& outPath);

private:
    std::string lastError_;
    bool usedStagedMoves_ = false;
    void setError(const std::string& msg);
};

} // namespace psvitaalive
