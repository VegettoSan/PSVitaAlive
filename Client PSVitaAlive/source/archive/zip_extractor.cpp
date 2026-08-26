#include "archive/zip_extractor.hpp"
#include "diagnostic_logger.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <zip.h>

#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {
constexpr size_t EXTRACT_CHUNK = 64 * 1024;

std::string zipArchiveError(zip_t* za) {
    if (!za) return "null archive";
    zip_error_t* err = zip_get_error(za);
    if (!err) return "unknown archive error";
    const char* s = zip_error_strerror(err);
    return s ? s : "unknown archive error";
}

std::string zipFileError(zip_file_t* zf) {
    if (!zf) return "null file";
    zip_error_t* err = zip_file_get_error(zf);
    if (!err) return "unknown file error";
    const char* s = zip_error_strerror(err);
    return s ? s : "unknown file error";
}

const char* compressionMethodName(zip_uint16_t method) {
    switch (method) {
        case 0: return "store";
        case 8: return "deflate";
        case 9: return "deflate64";
        case 12: return "bzip2";
        case 14: return "lzma";
        case 95: return "xz";
        case 93: return "zstd";
        default: return "other";
    }
}
} // namespace

const char* toString(ZipResult r) {
    switch (r) {
        case ZipResult::Ok: return "Ok";
        case ZipResult::OpenFailed: return "OpenFailed";
        case ZipResult::InvalidEntry: return "InvalidEntry";
        case ZipResult::UnsafePath: return "UnsafePath";
        case ZipResult::IoError: return "IoError";
        case ZipResult::Cancelled: return "Cancelled";
        case ZipResult::UnknownError: return "UnknownError";
        default: return "Unknown";
    }
}

void ZipExtractor::setError(const std::string& msg) {
    lastError_ = msg;
    diagnostics::log(std::string("[ZipExtractor] ") + msg);
}

bool ZipExtractor::isSafeEntryName(const std::string& entryName) {
    if (entryName.empty()) return false;
    if (entryName[0] == '/' || entryName[0] == '\\') return false;
    if (entryName.find(':') != std::string::npos) return false;
    std::string n = entryName;
    for (char& c : n) if (c == '\\') c = '/';
    size_t start = 0;
    while (start <= n.size()) {
        const size_t pos = n.find('/', start);
        const std::string seg = pos == std::string::npos ? n.substr(start) : n.substr(start, pos - start);
        if (seg == "..") return false;
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return true;
}

bool ZipExtractor::resolveSafePath(const std::string& destinationDir, const std::string& entryName, std::string& outPath) {
    if (!isSafeEntryName(entryName)) return false;
    std::string dest = destinationDir;
    while (!dest.empty() && (dest.back() == '/' || dest.back() == '\\')) dest.pop_back();
    std::string name = entryName;
    for (char& c : name) if (c == '\\') c = '/';
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    outPath = dest + "/" + name;
    if (outPath.size() < dest.size() + 1) return false;
    if (outPath.compare(0, dest.size(), dest) != 0) return false;
    if (outPath[dest.size()] != '/') return false;
    if (outPath.find("/../") != std::string::npos) return false;
    if (outPath.size() >= 3 && outPath.compare(outPath.size() - 3, 3, "/..") == 0) return false;
    return true;
}

ZipResult ZipExtractor::extract(
    const std::string& zipPath,
    const std::string& destinationDir,
    ZipProgressFn onProgress,
    ZipCancelFn shouldCancel
) {
    lastError_.clear();
    StorageManager st;

    // VitaDB extracts into an already-installed app folder (ux0:/app/TITLEID). On real
    // hardware createDirectories(ux0:app/...) can fail even when the dir exists and is writable.
    if (!st.isDirectory(destinationDir)) {
        if (!st.createDirectories(destinationDir)) {
            setError(std::string("cannot create destination: ") + destinationDir);
            return ZipResult::IoError;
        }
    }

    int zerr = 0;
    zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &zerr);
    if (!za) {
        char buf[128];
        sceClibSnprintf(buf, sizeof(buf), "zip_open failed err=%d path=%s", zerr, zipPath.c_str());
        setError(buf);
        return ZipResult::OpenFailed;
    }

    const zip_int64_t numEntries = zip_get_num_entries(za, 0);
    if (numEntries < 0) {
        setError(std::string("zip_get_num_entries failed: ") + zipArchiveError(za));
        zip_close(za);
        return ZipResult::OpenFailed;
    }

    ZipProgress prog;
    prog.entriesTotal = static_cast<uint64_t>(numEntries);

    // Pre-scan sizes / methods (helps diagnose large deflate entries on Vita).
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        zip_stat_t zs;
        zip_stat_init(&zs);
        if (zip_stat_index(za, i, 0, &zs) == 0 && zs.name) {
            const size_t len = std::strlen(zs.name);
            const bool isDir = len > 0 && zs.name[len - 1] == '/';
            if (!isDir && zs.size > 0) prog.bytesTotal += static_cast<uint64_t>(zs.size);
            if (!isDir && (zs.valid & ZIP_STAT_COMP_METHOD) && zs.comp_method != 0 && zs.comp_method != 8) {
                char warn[200];
                sceClibSnprintf(
                    warn, sizeof(warn),
                    "entry uses compression method=%u (%s) name=%s — may be unsupported on Vita",
                    static_cast<unsigned>(zs.comp_method),
                    compressionMethodName(static_cast<zip_uint16_t>(zs.comp_method)),
                    zs.name);
                diagnostics::log(std::string("[ZipExtractor] ") + warn);
            }
            if (!isDir && zs.size >= 1024ULL * 1024ULL * 1024ULL) {
                char warn[220];
                sceClibSnprintf(
                    warn, sizeof(warn),
                    "large entry name=%s uncomp=%llu comp_method=%u",
                    zs.name,
                    (unsigned long long)zs.size,
                    (zs.valid & ZIP_STAT_COMP_METHOD) ? (unsigned)zs.comp_method : 0u);
                diagnostics::log(std::string("[ZipExtractor] ") + warn);
            }
        }
    }

    std::vector<char> buffer(EXTRACT_CHUNK);
    ZipResult outcome = ZipResult::Ok;

    for (zip_int64_t i = 0; i < numEntries; ++i) {
        if (shouldCancel && shouldCancel()) {
            setError("cancelled");
            outcome = ZipResult::Cancelled;
            break;
        }

        zip_stat_t zs;
        zip_stat_init(&zs);
        if (zip_stat_index(za, i, 0, &zs) != 0) {
            setError(std::string("zip_stat_index failed: ") + zipArchiveError(za));
            outcome = ZipResult::InvalidEntry;
            break;
        }

        const char* name = zs.name ? zs.name : "";
        prog.currentEntry = name;
        const size_t nameLength = std::strlen(name);
        const bool isDir = nameLength > 0 && name[nameLength - 1] == '/';

        std::string outPath;
        if (!resolveSafePath(destinationDir, name, outPath)) {
            setError(std::string("unsafe path: ") + name);
            outcome = ZipResult::UnsafePath;
            break;
        }

        if (isDir) {
            if (!st.createDirectories(outPath)) {
                setError(std::string("mkdir failed: ") + outPath);
                outcome = ZipResult::IoError;
                break;
            }
            ++prog.entriesDone;
            if (onProgress) onProgress(prog);
            continue;
        }

        const auto slash = outPath.find_last_of('/');
        if (slash != std::string::npos) st.createDirectories(outPath.substr(0, slash));

        zip_file_t* zf = zip_fopen_index(za, i, 0);
        if (!zf) {
            setError(std::string("zip_fopen_index failed: ") + name + " (" + zipArchiveError(za) + ")");
            outcome = ZipResult::InvalidEntry;
            break;
        }

        SceUID fd = sceIoOpen(outPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd < 0) {
            zip_fclose(zf);
            setError(std::string("cannot create file: ") + outPath);
            outcome = ZipResult::IoError;
            break;
        }

        bool fileOk = true;
        uint64_t writtenTotal = 0;
        while (true) {
            if (shouldCancel && shouldCancel()) {
                setError("cancelled");
                outcome = ZipResult::Cancelled;
                fileOk = false;
                break;
            }

            const zip_int64_t n = zip_fread(zf, buffer.data(), buffer.size());
            if (n < 0) {
                const unsigned method = (zs.valid & ZIP_STAT_COMP_METHOD) ? static_cast<unsigned>(zs.comp_method) : 0u;
                char detail[360];
                sceClibSnprintf(
                    detail, sizeof(detail),
                    "zip_fread failed entry=%s method=%u (%s) uncomp=%llu written=%llu libzip=%s — archive may be corrupt, use STORE for huge files, or free RAM and retry",
                    name,
                    method,
                    compressionMethodName(static_cast<zip_uint16_t>(method)),
                    (unsigned long long)zs.size,
                    (unsigned long long)writtenTotal,
                    zipFileError(zf).c_str());
                setError(detail);
                outcome = ZipResult::IoError;
                fileOk = false;
                break;
            }
            if (n == 0) break;

            int written = 0;
            while (written < static_cast<int>(n)) {
                const int w = sceIoWrite(fd, buffer.data() + written, static_cast<int>(n) - written);
                if (w <= 0) {
                    setError(std::string("sceIoWrite failed during extract: ") + outPath);
                    outcome = ZipResult::IoError;
                    fileOk = false;
                    break;
                }
                written += w;
            }
            if (!fileOk) break;
            writtenTotal += static_cast<uint64_t>(n);
            prog.bytesWritten += static_cast<uint64_t>(n);
            if (onProgress) onProgress(prog);
        }

        sceIoClose(fd);
        zip_fclose(zf);

        if (!fileOk) {
            // Best-effort cleanup of partial output
            st.removeFile(outPath);
            break;
        }

        // Optional size check when zip_stat reported a size
        if ((zs.valid & ZIP_STAT_SIZE) && zs.size > 0 && writtenTotal != static_cast<uint64_t>(zs.size)) {
            char detail[240];
            sceClibSnprintf(
                detail, sizeof(detail),
                "size mismatch after extract entry=%s expected=%llu got=%llu",
                name,
                (unsigned long long)zs.size,
                (unsigned long long)writtenTotal);
            setError(detail);
            st.removeFile(outPath);
            outcome = ZipResult::IoError;
            break;
        }

        ++prog.entriesDone;
        if (onProgress) onProgress(prog);
    }

    zip_close(za);
    if (outcome == ZipResult::Ok) {
        diagnostics::log(
            std::string("[ZipExtractor] extracted ") +
            std::to_string(static_cast<unsigned long long>(prog.entriesDone)) +
            " entries, " +
            std::to_string(static_cast<unsigned long long>(prog.bytesWritten)) +
            " bytes -> " + destinationDir);
    }
    return outcome;
}

} // namespace psvitaalive
