#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive {

/**
 * LicenseHelper — write RIF / work.bin material provided by the environment
 * or catalog. Does NOT generate pirate licenses.
 */
class LicenseHelper {
public:
    /** Copy an existing RIF file to destination (e.g. work.bin path). */
    static bool copyRifFile(const std::string& rifPath, const std::string& destPath, std::string& errorOut);

    /** Write raw RIF bytes to destPath. */
    static bool writeRifBytes(const std::vector<uint8_t>& bytes, const std::string& destPath, std::string& errorOut);

    /** Typical NoNpDrm work.bin size is 512 bytes; accept common sizes. */
    static bool looksLikeRifSize(uint64_t size);
};

} // namespace psvitaalive
