#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive {

/**
 * StorageManager
 * Phase 1 — file system helpers for ux0:data/psvitaalive/
 *
 * Responsibilities:
 * - create directories
 * - check existence
 * - read / write files
 * - remove / rename
 * - free space checks (basic)
 *
 * Does NOT perform network or install logic.
 */
class StorageManager {
public:
    static constexpr const char* BASE_DIR = "ux0:data/psvitaalive";
    static constexpr const char* DOWNLOADS_DIR = "ux0:data/psvitaalive/downloads";
    static constexpr const char* JOBS_DIR = "ux0:data/psvitaalive/downloads/jobs";
    static constexpr const char* CACHE_DIR = "ux0:data/psvitaalive/cache";
    static constexpr const char* TEST_DIR = "ux0:data/psvitaalive/test";

    StorageManager() = default;
    ~StorageManager() = default;

    /** Create base project directories. Returns true if all ok. */
    bool initProjectDirs();

    bool exists(const std::string& path) const;
    bool isDirectory(const std::string& path) const;

    /** Create a single directory (not recursive parents beyond one level helpers). */
    bool createDirectory(const std::string& path);

    /** Create full path recursively (best-effort using successive mkdir). */
    bool createDirectories(const std::string& path);

    bool removeFile(const std::string& path);
    bool removeDirectory(const std::string& path); // non-recursive empty dir

    bool rename(const std::string& oldPath, const std::string& newPath);

    int64_t fileSize(const std::string& path) const;

    /** Write entire buffer to file (overwrite). */
    bool writeFile(const std::string& path, const void* data, size_t size);

    /** Write text convenience. */
    bool writeTextFile(const std::string& path, const std::string& text);

    /** Read entire file into memory. Only for small files. */
    bool readFile(const std::string& path, std::vector<uint8_t>& out) const;

    bool readTextFile(const std::string& path, std::string& out) const;

    /** Append bytes to file (create if missing). */
    bool appendFile(const std::string& path, const void* data, size_t size);

    /**
     * Query ux0 free/total bytes via sceIoDevctl (same method as the UI footer).
     * Returns false if the probe fails.
     */
    static bool queryUx0Space(uint64_t& freeBytesOut, uint64_t& totalBytesOut);

    /** True if ux0 free space is at least requiredBytes (false if probe fails). */
    bool hasFreeSpace(uint64_t requiredBytes) const;
};

} // namespace psvitaalive
