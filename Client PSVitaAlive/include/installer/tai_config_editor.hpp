#pragma once

#include <string>

namespace psvitaalive {

/**
 * Append-only taiHEN config.txt helper for Plugin link installs.
 * Resolves the active config the same way as PluginDetector (ux0 preferred, else ur0).
 * Never deletes or rewrites existing plugin lines — only inserts when missing.
 */
class TaiConfigEditor {
public:
    /** Active config path or empty if none found. */
    static std::string resolveActiveConfigPath();

    /**
     * Append `line` at the end of `section` (*KERNEL, *main, *ALL, custom, ...).
     * section "none" or empty → no-op success.
     * If the exact line already exists anywhere in the file, does nothing (success).
     * If the section header is missing, creates it at EOF.
     * Returns false on I/O failure; errorOut optional.
     */
    static bool appendLineToSection(
        const std::string& section,
        const std::string& line,
        std::string* errorOut = nullptr
    );

    /**
     * True if the exact plugin line already appears in the active config.txt
     * (trimmed match). False if no config is found or the line is missing.
     */
    static bool configContainsLine(const std::string& line);
};

} // namespace psvitaalive
