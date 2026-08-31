#include "archive/zip_extractor.hpp"
#include "diagnostic_logger.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <zip.h>

#include <cstdint>
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


namespace {

// libzip's zip_open() on Vita often returns ZIP_ER_INVAL (18) for files >2GB
// (32-bit off_t / fstat). Use a custom source backed by sceIo* 64-bit seeks.
struct VitaZipFileSource {
    std::string path;
    SceUID fd = -1;
    uint64_t size = 0;
    uint64_t pos = 0;
    int lastSysError = 0;
    int lastZipError = ZIP_ER_OK;
};

static zip_int64_t vitaZipSourceCb(void* userdata, void* data, zip_uint64_t len, zip_source_cmd_t cmd) {
    auto* s = static_cast<VitaZipFileSource*>(userdata);
    if (!s) return -1;

    switch (cmd) {
    case ZIP_SOURCE_SUPPORTS:
        return zip_source_make_command_bitmap(
            ZIP_SOURCE_OPEN,
            ZIP_SOURCE_READ,
            ZIP_SOURCE_CLOSE,
            ZIP_SOURCE_STAT,
            ZIP_SOURCE_ERROR,
            ZIP_SOURCE_FREE,
            ZIP_SOURCE_SEEK,
            ZIP_SOURCE_TELL,
            ZIP_SOURCE_SUPPORTS,
            -1);

    case ZIP_SOURCE_OPEN: {
        if (s->fd >= 0) {
            sceIoClose(s->fd);
            s->fd = -1;
        }
        s->fd = sceIoOpen(s->path.c_str(), SCE_O_RDONLY, 0);
        if (s->fd < 0) {
            s->lastSysError = (int)s->fd;
            s->lastZipError = ZIP_ER_OPEN;
            return -1;
        }
        const SceOff end = sceIoLseek(s->fd, 0, SCE_SEEK_END);
        if (end > 0) s->size = (uint64_t)end;
        sceIoLseek(s->fd, 0, SCE_SEEK_SET);
        s->pos = 0;
        return 0;
    }

    case ZIP_SOURCE_READ: {
        if (s->fd < 0 || !data || len == 0) return 0;
        zip_int64_t total = 0;
        uint8_t* out = static_cast<uint8_t*>(data);
        while (len > 0) {
            const SceSize chunk = (len > 512u * 1024u) ? (SceSize)(512u * 1024u) : (SceSize)len;
            const int n = sceIoRead(s->fd, out, chunk);
            if (n < 0) {
                s->lastSysError = n;
                s->lastZipError = ZIP_ER_READ;
                return -1;
            }
            if (n == 0) break;
            total += n;
            out += n;
            len -= (zip_uint64_t)n;
            s->pos += (uint64_t)n;
        }
        return total;
    }

    case ZIP_SOURCE_CLOSE: {
        if (s->fd >= 0) {
            sceIoClose(s->fd);
            s->fd = -1;
        }
        return 0;
    }

    case ZIP_SOURCE_STAT: {
        // libzip docs: return sizeof(zip_stat) on success (not 0).
        if (!data || len < sizeof(zip_stat_t)) return -1;
        zip_stat_t* st = static_cast<zip_stat_t*>(data);
        zip_stat_init(st);
        st->valid = ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE;
        st->size = s->size;
        st->comp_size = s->size;
        return static_cast<zip_int64_t>(sizeof(zip_stat_t));
    }

    case ZIP_SOURCE_SEEK: {
        if (s->fd < 0) {
            s->lastZipError = ZIP_ER_SEEK;
            return -1;
        }
        zip_error_t zerr;
        zip_error_init(&zerr);
        const zip_int64_t target = zip_source_seek_compute_offset(
            s->pos, s->size, data, len, &zerr);
        if (target < 0) {
            s->lastZipError = zip_error_code_zip(&zerr);
            s->lastSysError = zip_error_code_system(&zerr);
            zip_error_fini(&zerr);
            return -1;
        }
        zip_error_fini(&zerr);
        const SceOff got = sceIoLseek(s->fd, (SceOff)target, SCE_SEEK_SET);
        if (got < 0) {
            s->lastSysError = (int)got;
            s->lastZipError = ZIP_ER_SEEK;
            return -1;
        }
        s->pos = (uint64_t)got;
        return 0;
    }

    case ZIP_SOURCE_TELL:
        return (zip_int64_t)s->pos;

    case ZIP_SOURCE_ERROR: {
        if (!data || len < 2 * sizeof(int)) return -1;
        int* codes = static_cast<int*>(data);
        codes[0] = s->lastZipError ? s->lastZipError : ZIP_ER_INTERNAL;
        codes[1] = s->lastSysError;
        return static_cast<zip_int64_t>(2 * sizeof(int));
    }

    case ZIP_SOURCE_FREE:
        if (s->fd >= 0) {
            sceIoClose(s->fd);
            s->fd = -1;
        }
        delete s;
        return 0;

    default:
        return -1;
    }
}


// True if EOCD (or ZIP64 marker) is present near EOF.
static bool zipLooksCompleteOnDisk(const std::string& path, uint64_t fileSize, std::string* detail) {
    if (fileSize < 22) {
        if (detail) *detail = "file too small for ZIP EOCD";
        return false;
    }
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        if (detail) *detail = "cannot open for EOCD check";
        return false;
    }
    unsigned char head[4] = {};
    if (sceIoRead(fd, head, 4) != 4 || head[0] != 0x50 || head[1] != 0x4b ||
        head[2] != 0x03 || head[3] != 0x04) {
        sceIoClose(fd);
        if (detail) *detail = "missing local header magic at start";
        return false;
    }
    const uint64_t window = fileSize < (65536ULL + 22ULL) ? fileSize : (65536ULL + 22ULL);
    if (sceIoLseek(fd, (SceOff)(fileSize - window), SCE_SEEK_SET) < 0) {
        sceIoClose(fd);
        if (detail) *detail = "seek to tail failed";
        return false;
    }
    std::vector<unsigned char> buf(static_cast<size_t>(window));
    size_t got = 0;
    while (got < buf.size()) {
        const int n = sceIoRead(fd, buf.data() + got, (SceSize)(buf.size() - got));
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    sceIoClose(fd);
    if (got < 22) {
        if (detail) *detail = "short read at tail";
        return false;
    }
    bool eocd = false;
    for (size_t i = 0; i + 3 < got; ++i) {
        if (buf[i] == 0x50 && buf[i + 1] == 0x4b) {
            if ((buf[i + 2] == 0x05 && buf[i + 3] == 0x06) ||
                (buf[i + 2] == 0x06 && buf[i + 3] == 0x06) ||
                (buf[i + 2] == 0x06 && buf[i + 3] == 0x07)) {
                eocd = true;
                break;
            }
        }
    }
    if (!eocd) {
        if (detail) *detail = "no EOCD/ZIP64 marker near end (download may be incomplete)";
        return false;
    }
    return true;
}

static uint64_t diskFileSize64(const std::string& path) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    const SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoClose(fd);
    return end > 0 ? (uint64_t)end : 0;
}

static zip_t* vitaZipOpen(const std::string& zipPath, int* outErr, std::string* outDetail) {
    if (outErr) *outErr = 0;
    if (outDetail) outDetail->clear();

    uint64_t fileSize = diskFileSize64(zipPath);
    if (fileSize == 0) {
        SceIoStat st{};
        if (sceIoGetstat(zipPath.c_str(), &st) < 0 || st.st_size <= 0) {
            if (outErr) *outErr = ZIP_ER_NOENT;
            if (outDetail) *outDetail = "file missing or size=0";
            return nullptr;
        }
        fileSize = (uint64_t)st.st_size;
    }

    {
        char logb[192];
        sceClibSnprintf(logb, sizeof(logb),
            "[ZipExtractor] open begin path=%s disk_size=%llu",
            zipPath.c_str(), (unsigned long long)fileSize);
        diagnostics::log(logb);
    }

    std::string eocdDetail;
    if (!zipLooksCompleteOnDisk(zipPath, fileSize, &eocdDetail)) {
#ifdef ZIP_ER_TRUNCATED_ZIP
        if (outErr) *outErr = ZIP_ER_TRUNCATED_ZIP;
#else
        if (outErr) *outErr = 35;
#endif
        if (outDetail) *outDetail = eocdDetail.empty() ? "EOCD check failed" : eocdDetail;
        diagnostics::log(std::string("[ZipExtractor] EOCD pre-check failed: ") + eocdDetail +
                         " size=" + std::to_string(fileSize));
        return nullptr;
    }

    const bool needCustom =
        fileSize >= 0x80000000ULL ||
        zipPath.find(' ') != std::string::npos;

    if (!needCustom) {
        int zerr = 0;
        zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &zerr);
        if (za) {
            diagnostics::log("[ZipExtractor] opened via zip_open (classic)");
            return za;
        }
        if (outErr) *outErr = zerr;
        diagnostics::log(std::string("[ZipExtractor] zip_open failed err=") + std::to_string(zerr) +
                         " — trying sceIo source");
    }

    auto* srcState = new VitaZipFileSource();
    srcState->path = zipPath;
    srcState->size = fileSize;

    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* src = zip_source_function_create(vitaZipSourceCb, srcState, &error);
    if (!src) {
        const int zc = zip_error_code_zip(&error);
        if (outErr) *outErr = zc;
        if (outDetail) {
            const char* es = zip_error_strerror(&error);
            *outDetail = es ? es : "zip_source_function_create failed";
        }
        zip_error_fini(&error);
        delete srcState;
        return nullptr;
    }

    zip_t* za = zip_open_from_source(src, ZIP_RDONLY, &error);
    if (!za) {
        const int zc = zip_error_code_zip(&error);
        if (outErr) *outErr = zc;
        const char* es = zip_error_strerror(&error);
        if (outDetail) *outDetail = es ? es : "zip_open_from_source failed";
        {
            char logb[256];
            sceClibSnprintf(logb, sizeof(logb),
                "[ZipExtractor] zip_open_from_source failed err=%d (%s) disk_size=%llu",
                zc, es ? es : "?", (unsigned long long)fileSize);
            diagnostics::log(logb);
        }
        zip_source_free(src);
        zip_error_fini(&error);
        return nullptr;
    }
    zip_error_fini(&error);

    {
        char logb[192];
        sceClibSnprintf(logb, sizeof(logb),
            "[ZipExtractor] opened via sceIo source disk_size=%llu path=%s",
            (unsigned long long)fileSize, zipPath.c_str());
        diagnostics::log(logb);
    }
    return za;
}

} // namespace

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
    std::string openDetail;
    zip_t* za = vitaZipOpen(zipPath, &zerr, &openDetail);
    if (!za) {
        char buf[320];
        sceClibSnprintf(buf, sizeof(buf),
            "zip_open failed err=%d path=%s detail=%s",
            zerr, zipPath.c_str(), openDetail.empty() ? "(none)" : openDetail.c_str());
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
