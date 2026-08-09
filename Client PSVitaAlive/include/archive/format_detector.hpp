#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive {

enum class FileFormat {
    Unknown = 0,
    Zip,
    Vpk,          // ZIP with Vita package layout markers / .vpk intent
    Pkg,          // PlayStation package
    Pbp,          // PSP/PS1 EBOOT
    Iso,
    Cso,
    Elf,
    Self
};

enum class ContentKind {
    Unknown = 0,
    Homebrew,
    VitaGame,
    VitaUpdate,
    VitaDlc,
    PspGame,
    Ps1Game,
    Archive,
    Other
};

const char* toString(FileFormat f);
const char* toString(ContentKind k);

struct DetectResult {
    FileFormat format = FileFormat::Unknown;
    ContentKind kind = ContentKind::Unknown;
    std::string extension;      // lower-case without dot
    bool matchedByMagic = false;
    bool matchedByExtension = false;
    std::string detail;         // free text for logs
};

/**
 * FormatDetector — Phase 5
 *
 * Detects file type using:
 * 1) magic / signature bytes
 * 2) extension (secondary)
 *
 * Does not install or extract.
 */
class FormatDetector {
public:
    DetectResult detectFile(const std::string& path) const;

    DetectResult detectBuffer(
        const uint8_t* data,
        size_t size,
        const std::string& suggestedName = {}
    ) const;

    static std::string extensionOf(const std::string& path);

private:
    static bool startsWith(const uint8_t* data, size_t size, const char* sig, size_t sigLen);
    static FileFormat magicDetect(const uint8_t* data, size_t size, std::string& detail);
    static FileFormat extensionDetect(const std::string& ext);
    static ContentKind guessKind(FileFormat fmt, const std::string& ext);
};

} // namespace psvitaalive
