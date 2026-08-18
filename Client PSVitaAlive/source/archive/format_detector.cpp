#include "archive/format_detector.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>

#include <algorithm>
#include <cstring>

namespace psvitaalive {

const char* toString(FileFormat f) {
    switch (f) {
        case FileFormat::Unknown: return "Unknown";
        case FileFormat::Zip: return "Zip";
        case FileFormat::Vpk: return "Vpk";
        case FileFormat::Pkg: return "Pkg";
        case FileFormat::Pbp: return "Pbp";
        case FileFormat::Iso: return "Iso";
        case FileFormat::Cso: return "Cso";
        case FileFormat::Elf: return "Elf";
        case FileFormat::Self: return "Self";
        default: return "Unknown";
    }
}

const char* toString(ContentKind k) {
    switch (k) {
        case ContentKind::Unknown: return "Unknown";
        case ContentKind::Homebrew: return "Homebrew";
        case ContentKind::VitaGame: return "VitaGame";
        case ContentKind::VitaUpdate: return "VitaUpdate";
        case ContentKind::VitaDlc: return "VitaDlc";
        case ContentKind::PspGame: return "PspGame";
        case ContentKind::Ps1Game: return "Ps1Game";
        case ContentKind::Archive: return "Archive";
        case ContentKind::Other: return "Other";
        default: return "Unknown";
    }
}

std::string FormatDetector::extensionOf(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return {};
    std::string ext = name.substr(dot + 1);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

bool FormatDetector::startsWith(const uint8_t* data, size_t size, const char* sig, size_t sigLen) {
    if (!data || size < sigLen) return false;
    return std::memcmp(data, sig, sigLen) == 0;
}

FileFormat FormatDetector::magicDetect(const uint8_t* data, size_t size, std::string& detail) {
    if (!data || size < 4) {
        detail = "too_small";
        return FileFormat::Unknown;
    }

    // ZIP / VPK (VPK is a ZIP container)
    // PK\x03\x04 local file, PK\x05\x06 empty EOCD, PK\x07\x08 spanned
    if (startsWith(data, size, "PK\x03\x04", 4) ||
        startsWith(data, size, "PK\x05\x06", 4) ||
        startsWith(data, size, "PK\x07\x08", 4)) {
        detail = "zip_signature";
        return FileFormat::Zip;
    }

    // PKG — SCE\0 common for PlayStation packages
    if (startsWith(data, size, "\x7fSCE", 4) || startsWith(data, size, "SCE\0", 4)) {
        // Distinguish SELF vs PKG roughly: SELF often has additional structure;
        // many Vita PKG start with specific header. Treat SCE as package-ish.
        detail = "sce_signature";
        return FileFormat::Pkg;
    }

    // ELF
    if (size >= 4 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        detail = "elf_signature";
        return FileFormat::Elf;
    }

    // PSP PBP: "\0PBP"
    if (startsWith(data, size, "\0PBP", 4)) {
        detail = "pbp_signature";
        return FileFormat::Pbp;
    }

    // CSO: "CISO"
    if (startsWith(data, size, "CISO", 4)) {
        detail = "cso_signature";
        return FileFormat::Cso;
    }

    // ISO 9660: "CD001" at offset 0x8001, 0x8801 or 0x9001
    auto hasCd001 = [&](size_t off) -> bool {
        if (size < off + 5) return false;
        return std::memcmp(data + off, "CD001", 5) == 0;
    };
    if (hasCd001(0x8001) || hasCd001(0x8801) || hasCd001(0x9001)) {
        detail = "iso9660_cd001";
        return FileFormat::Iso;
    }

    // SELF (Vita): often starts with SCE\0 and type fields — already caught as Pkg.
    // Some SELF use \x7fSCE
    if (size >= 8 && data[0] == 0x53 && data[1] == 0x43 && data[2] == 0x45) {
        detail = "sce_alt";
        return FileFormat::Self;
    }

    detail = "no_magic";
    return FileFormat::Unknown;
}

FileFormat FormatDetector::extensionDetect(const std::string& ext) {
    if (ext == "zip") return FileFormat::Zip;
    if (ext == "vpk") return FileFormat::Vpk;
    if (ext == "pkg") return FileFormat::Pkg;
    if (ext == "pbp") return FileFormat::Pbp;
    if (ext == "iso") return FileFormat::Iso;
    if (ext == "cso" || ext == "zso") return FileFormat::Cso;
    if (ext == "elf") return FileFormat::Elf;
    if (ext == "self") return FileFormat::Self;
    return FileFormat::Unknown;
}

ContentKind FormatDetector::guessKind(FileFormat fmt, const std::string& ext) {
    switch (fmt) {
        case FileFormat::Vpk:
            return ContentKind::Homebrew;
        case FileFormat::Pkg:
            // Without catalog metadata we cannot know game/update/dlc
            return ContentKind::Unknown;
        case FileFormat::Pbp:
            return ContentKind::PspGame; // may be PS1; refined later
        case FileFormat::Iso:
        case FileFormat::Cso:
            return ContentKind::PspGame;
        case FileFormat::Zip:
            return ContentKind::Archive;
        default:
            if (ext == "vpk") return ContentKind::Homebrew;
            return ContentKind::Unknown;
    }
}


// Scan ZIP local-file headers / plain name bytes for Vita package markers.
// VitaDB get_hb_url.php downloads are ZIP/VPK without a .vpk filename.
static bool zipLooksLikeVpk(const uint8_t* data, size_t size) {
    if (!data || size < 8) return false;
    auto has = [&](const char* s) -> bool {
        const size_t n = std::strlen(s);
        if (n == 0 || size < n) return false;
        for (size_t i = 0; i + n <= size; ++i) {
            if (std::memcmp(data + i, s, n) == 0) return true;
        }
        return false;
    };
    // Typical VPK entry names (stored uncompressed in local headers)
    if (has("eboot.bin")) return true;
    if (has("sce_sys/param.sfo") || has("sce_sys\\param.sfo")) return true;
    if (has("SCE_SYS/PARAM.SFO")) return true;
    if (has("sce_sys/package/head.bin")) return true;
    return false;
}

DetectResult FormatDetector::detectBuffer(
    const uint8_t* data,
    size_t size,
    const std::string& suggestedName
) const {
    DetectResult r;
    r.extension = extensionOf(suggestedName);

    std::string magicDetail;
    FileFormat byMagic = magicDetect(data, size, magicDetail);
    FileFormat byExt = extensionDetect(r.extension);

    r.matchedByMagic = (byMagic != FileFormat::Unknown);
    r.matchedByExtension = (byExt != FileFormat::Unknown);
    r.detail = magicDetail;

    // Prefer magic; refine ZIP+vpk extension as Vpk
    if (byMagic == FileFormat::Zip && byExt == FileFormat::Vpk) {
        r.format = FileFormat::Vpk;
        r.detail = "zip_magic+vpk_ext";
    } else if (byMagic == FileFormat::Zip && zipLooksLikeVpk(data, size)) {
        // Content looks like a Vita package even without .vpk extension (VitaDB, etc.)
        r.format = FileFormat::Vpk;
        r.detail = "zip_magic+vpk_layout";
    } else if (byMagic != FileFormat::Unknown) {
        r.format = byMagic;
    } else {
        r.format = byExt;
        if (r.matchedByExtension) r.detail = "extension_only";
    }

    r.kind = guessKind(r.format, r.extension);
    return r;
}

DetectResult FormatDetector::detectFile(const std::string& path) const {
    DetectResult r;
    r.extension = extensionOf(path);

    StorageManager st;
    if (!st.exists(path)) {
        r.detail = "file_missing";
        r.format = extensionDetect(r.extension);
        r.matchedByExtension = (r.format != FileFormat::Unknown);
        r.kind = guessKind(r.format, r.extension);
        return r;
    }

    // Read first 64 KiB for magic (enough for ISO CD001 offsets up to ~0x9001)
    constexpr size_t kHead = 64 * 1024;
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        r.detail = "open_failed";
        r.format = extensionDetect(r.extension);
        r.matchedByExtension = (r.format != FileFormat::Unknown);
        r.kind = guessKind(r.format, r.extension);
        return r;
    }

    std::vector<uint8_t> buf(kHead);
    int n = sceIoRead(fd, buf.data(), kHead);
    sceIoClose(fd);

    if (n <= 0) {
        r.detail = "read_failed";
        r.format = extensionDetect(r.extension);
        r.matchedByExtension = (r.format != FileFormat::Unknown);
        r.kind = guessKind(r.format, r.extension);
        return r;
    }

    return detectBuffer(buf.data(), static_cast<size_t>(n), path);
}

} // namespace psvitaalive
