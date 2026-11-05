#pragma once

#include <cstdint>
#include <string>

namespace psvitaalive {
namespace update {

struct InstallReceipt {
    int schemaVersion = 1;
    std::string appId;
    std::string titleId;
    std::string catalogVersion;
    std::string versionDate;
    int releaseRevision = 0;
    uint32_t dataRevision = 0; // dr: independent of the VPK release version
    std::string verifyPath;
    std::string verifyAlgorithm;
    std::string verifyDigest;
};

class InstalledReleaseTracker {
public:
    static std::string dataRoot();
    static std::string receiptPath(const std::string& titleId);
    static bool writeReceipt(const InstallReceipt& r);
    static bool readReceipt(const std::string& titleId, InstallReceipt& out);
    /** Use dataRevision from schema v2; legacy .txt only for older/no receipts. */
    static uint32_t readDataRevision(const std::string& titleId, const InstallReceipt& receipt);
    /** Update only dr; preserve all VPK version/verification fields. */
    static bool writeDataRevision(const std::string& titleId, const std::string& appId, uint32_t revision);
    static void removeReceipt(const std::string& titleId);
    static bool ensureDir();
};

} // namespace update
} // namespace psvitaalive
