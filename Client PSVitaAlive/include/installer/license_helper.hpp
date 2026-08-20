#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive {

/**
 * LicenseHelper — RIF / work.bin helpers for NoNpDrm + system BGDL.
 * Decodes catalog zRIF strings (NoPayStation / PKGj format).
 * Does NOT invent licenses: only decodes provided zRIF or copies existing files.
 */
class LicenseHelper {
public:
    static constexpr const char* kBgdlTempRif = "ux0:bgdl/temp.dat";
    static constexpr size_t kRifSize = 512;
    static constexpr size_t kRifSizePsm = 1024;

    /** Copy an existing RIF file to destination (e.g. work.bin path). */
    static bool copyRifFile(const std::string& rifPath, const std::string& destPath, std::string& errorOut);

    /** Write raw RIF bytes to destPath. */
    static bool writeRifBytes(const std::vector<uint8_t>& bytes, const std::string& destPath, std::string& errorOut);

    /** Typical NoNpDrm work.bin size is 512 bytes; accept common sizes. */
    static bool looksLikeRifSize(uint64_t size);

    /**
     * Decode a NoPayStation / PKGj zRIF string into raw RIF bytes (512 or 1024).
     * Uses zlib + the public zRIF preset dictionary (same as PKGj).
     */
    static bool decodeZrif(const std::string& zrif, std::vector<uint8_t>& outBytes, std::string& errorOut);

    /**
     * Prepare license file for system BGDL at ux0:bgdl/temp.dat (PKGj path).
     * Accepts either a zRIF string or an existing .rif/.bin path.
     * Returns the path written on success (usually kBgdlTempRif).
     */
    /**
     * PKGj-style synthetic PSP/PS1 license for NoPspEmuDrm BGDL.
     * Does NOT use RAP from TSV — PKGj builds RIF from Content ID:
     * account_id=0x0123456789ABCDEF, ecdsa_signature filled with 0xFF.
     */
    static bool createPspRif(const std::string& contentId, std::vector<uint8_t>& outBytes, std::string& errorOut);

    /** Load one zRIF from catalog sidecar indexes (not held in RAM with catalog items). */
    /** Lookup zRIF by Content ID (primary key in catalog_psvita_games.zrifidx). */
    static bool lookupZrifForContentId(const std::string& contentId, std::string& outZrif);
    /** Legacy: also tries Content ID if the string looks like one; URL keys are no longer in the index. */
    static bool lookupZrifForUrl(const std::string& url, std::string& outZrif);

    static bool prepareBgdlLicense(
        const std::string& zrifOrEmpty,
        const std::string& existingRifPathOrEmpty,
        std::string& outPath,
        std::string& errorOut
    );
};

} // namespace psvitaalive
