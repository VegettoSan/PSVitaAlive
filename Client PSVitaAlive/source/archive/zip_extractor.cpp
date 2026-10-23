#include "archive/zip_extractor.hpp"
#include "archive/extraction_directory_cache.hpp"
#include "archive/segmented_zip_layout.hpp"
#include "diagnostic_logger.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <zip.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {
// A larger streaming buffer reduces sceIoRead/sceIoWrite calls during
// multi-megabyte entries. Still bounded to 256 KiB of extra heap RAM.
constexpr size_t EXTRACT_CHUNK = 256 * 1024;
constexpr uint64_t ZIP_PROGRESS_INTERVAL_US = 250000ULL;
// Let Vita's render thread run between large ux0 write bursts. 1ms per MiB
// adds little overhead while reducing sustained worker/IO scheduling pressure.
constexpr uint64_t ZIP_COOPERATIVE_YIELD_BYTES = 1024ULL * 1024ULL;
constexpr uint64_t ZIP_VERBOSE_ENTRY_MIN_SIZE = 4ULL * 1024ULL * 1024ULL;
constexpr zip_int64_t ZIP_ENTRY_LOG_STEP = 64;

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
struct VitaZipPart {
    std::string path;
    SceUID fd = -1;
    uint64_t start = 0;
    uint64_t size = 0;
    // Physical read cursor for this part. libzip may seek the *virtual*
    // archive many times, but consecutive reads should not seek the Vita
    // file again when the underlying fd is already at the correct offset.
    uint64_t fdPos = 0;
};

struct VitaZipFileSource {
    std::string path;
    bool segmented = false;
    std::vector<VitaZipPart> parts;
    std::vector<ZipRangeSpan> layout;
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
        if (s->segmented) {
            for (VitaZipPart& part : s->parts) {
                if (part.fd >= 0) sceIoClose(part.fd);
                part.fd = sceIoOpen(part.path.c_str(), SCE_O_RDONLY, 0);
                part.fdPos = 0;
                if (part.fd < 0) {
                    s->lastSysError = static_cast<int>(part.fd);
                    s->lastZipError = ZIP_ER_OPEN;
                    for (VitaZipPart& opened : s->parts) {
                        if (opened.fd >= 0) sceIoClose(opened.fd);
                        opened.fd = -1;
                    }
                    return -1;
                }
            }
            s->pos = 0;
            return 0;
        }
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
        if (!data || len == 0) return 0;
        if (s->segmented) {
            zip_int64_t total = 0;
            auto* out = static_cast<uint8_t*>(data);
            while (len > 0 && s->pos < s->size) {
                const size_t partIndex = findZipRange(s->layout, s->pos);
                if (partIndex >= s->parts.size()) {
                    s->lastZipError = ZIP_ER_READ;
                    return -1;
                }
                VitaZipPart* current = &s->parts[partIndex];
                if (current->fd < 0) {
                    s->lastZipError = ZIP_ER_READ;
                    return -1;
                }
                const uint64_t local = s->pos - current->start;
                const uint64_t remaining = current->size - local;
                const SceSize chunk = static_cast<SceSize>(std::min<uint64_t>(
                    std::min<uint64_t>(remaining, len), 512u * 1024u));
                // Keep the physical position separate from virtual seeks:
                // libzip's ZIP_SOURCE_SEEK only changes s->pos. Only issue
                // a syscall when the next read is actually non-contiguous.
                if (current->fdPos != local) {
                    if (sceIoLseek(current->fd, static_cast<SceOff>(local), SCE_SEEK_SET) < 0) {
                        s->lastZipError = ZIP_ER_SEEK;
                        return -1;
                    }
                    current->fdPos = local;
                }
                const int n = sceIoRead(current->fd, out, chunk);
                if (n <= 0) {
                    s->lastSysError = n;
                    s->lastZipError = ZIP_ER_READ;
                    return -1;
                }
                out += n;
                len -= static_cast<zip_uint64_t>(n);
                total += n;
                s->pos += static_cast<uint64_t>(n);
                current->fdPos += static_cast<uint64_t>(n);
            }
            return total;
        }
        if (s->fd < 0) return -1;
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
        for (VitaZipPart& part : s->parts) {
            if (part.fd >= 0) sceIoClose(part.fd);
            part.fd = -1;
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
        if (s->fd < 0 && !s->segmented) {
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
        if (s->segmented) {
            s->pos = static_cast<uint64_t>(target);
            return 0;
        }
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
        for (VitaZipPart& part : s->parts) {
            if (part.fd >= 0) sceIoClose(part.fd);
        }
        delete s;
        return 0;

    default:
        return -1;
    }
}


// True if EOCD (or ZIP64 marker) is present near EOF.
static bool zipLooksCompleteOnDisk(const std::string& path, uint64_t fileSize,
                                   std::string* detail, const std::string& tailPath = {}) {
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
    // A segmented ZIP keeps the central directory/EOCD in its final range.
    // The first range still contains the real local-header magic.
    if (!tailPath.empty()) {
        sceIoClose(fd);
        fd = sceIoOpen(tailPath.c_str(), SCE_O_RDONLY, 0);
        if (fd < 0) {
            if (detail) *detail = "cannot open ZIP tail range";
            return false;
        }
        const SceOff tailSize = sceIoLseek(fd, 0, SCE_SEEK_END);
        if (tailSize <= 0) {
            sceIoClose(fd);
            if (detail) *detail = "invalid ZIP tail range";
            return false;
        }
        fileSize = static_cast<uint64_t>(tailSize);
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


static bool openSegmentLayout(const std::string& base,
                              std::vector<VitaZipPart>& parts,
                              uint64_t& total,
                              std::string* detail) {
    parts.clear();
    total = 0;
    // Only two- and four-range layouts are produced by our downloader.
    // Reject gaps, extra ranges, zero lengths and noncontiguous sizes.
    for (size_t i = 0; i < 4; ++i) {
        const std::string path = i == 0 ? base : base + ".range" + std::to_string(i);
        const SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
        if (fd < 0) {
            if (i == 2 && parts.size() == 2) break;
            if (detail) *detail = "missing ZIP range " + std::to_string(i);
            return false;
        }
        const SceOff len = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoClose(fd);
        if (len <= 0 || static_cast<uint64_t>(len) > UINT64_MAX - total) {
            if (detail) *detail = "invalid segmented ZIP range size";
            return false;
        }
        VitaZipPart part;
        part.path = path;
        part.start = total;
        part.size = static_cast<uint64_t>(len);
        total += part.size;
        parts.push_back(part);
    }
    if (parts.size() != 2 && parts.size() != 4) {
        if (detail) *detail = "expected exactly 2 or 4 ZIP ranges";
        return false;
    }
    if (parts.size() == 2) {
        SceIoStat extra{};
        if (sceIoGetstat((base + ".range3").c_str(), &extra) >= 0) {
            if (detail) *detail = "unexpected fourth ZIP range";
            return false;
        }
    }
    std::vector<uint64_t> sizes;
    sizes.reserve(parts.size());
    for (const VitaZipPart& part : parts) sizes.push_back(part.size);
    std::vector<ZipRangeSpan> layout;
    uint64_t checkedTotal = 0;
    if (!buildZipRangeLayout(sizes, layout, checkedTotal) || checkedTotal != total) {
        if (detail) *detail = "invalid ZIP range offsets or lengths";
        return false;
    }
    for (size_t i = 0; i < parts.size(); ++i) parts[i].start = layout[i].start;
    return true;
}

static zip_t* vitaZipOpen(const std::string& zipPath, int* outErr, std::string* outDetail,
                          bool segmented = false) {
    if (outErr) *outErr = 0;
    if (outDetail) outDetail->clear();

    std::vector<VitaZipPart> segmentParts;
    uint64_t fileSize = 0;
    if (segmented) {
        if (!openSegmentLayout(zipPath, segmentParts, fileSize, outDetail)) {
            if (outErr) *outErr = ZIP_ER_INVAL;
            return nullptr;
        }
    } else {
        fileSize = diskFileSize64(zipPath);
    }
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
    if (!zipLooksCompleteOnDisk(zipPath, fileSize, &eocdDetail,
                                segmented ? segmentParts.back().path : std::string())) {
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
        segmented || fileSize >= 0x80000000ULL ||
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
    srcState->segmented = segmented;
    srcState->parts = std::move(segmentParts);
    if (segmented) {
        std::vector<uint64_t> sizes;
        for (const VitaZipPart& part : srcState->parts) sizes.push_back(part.size);
        uint64_t checkedTotal = 0;
        if (!buildZipRangeLayout(sizes, srcState->layout, checkedTotal) ||
            checkedTotal != srcState->size) {
            if (outErr) *outErr = ZIP_ER_INVAL;
            delete srcState;
            return nullptr;
        }
    }

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
    ZipCancelFn shouldCancel,
    bool segmentedZip
) {
    lastError_.clear();
    usedStagedMoves_ = false;
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
    zip_t* za = vitaZipOpen(zipPath, &zerr, &openDetail, segmentedZip);
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

    // Pre-scan: sum uncompressed sizes, log large/unusual entries, then verify free space.
    uint64_t uncompressedTotal = 0;
    const uint64_t archiveDiskSize = diskFileSize64(zipPath);
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        zip_stat_t zs;
        zip_stat_init(&zs);
        if (zip_stat_index(za, i, 0, &zs) == 0 && zs.name) {
            const size_t len = std::strlen(zs.name);
            const bool isDir = len > 0 && zs.name[len - 1] == '/';
            if (!isDir && (zs.valid & ZIP_STAT_SIZE) && zs.size > 0) {
                const uint64_t entrySize = static_cast<uint64_t>(zs.size);
                if (entrySize > (~0ULL - uncompressedTotal) || entrySize > (~0ULL - prog.bytesTotal)) {
                    setError(std::string("ZIP uncompressed size overflow at entry: ") + zs.name);
                    zip_close(za);
                    return ZipResult::InvalidEntry;
                }
                uncompressedTotal += entrySize;
                prog.bytesTotal += entrySize;
            }
            if (zs.valid & ZIP_STAT_COMP_METHOD) {
                // libzip can report methods that the current build cannot decompress.
                // Fail before creating any output instead of discovering this halfway
                // through a multi-gigabyte extraction.
                if (!zip_compression_method_supported(static_cast<zip_int32_t>(zs.comp_method), 1)) {
                    char err[260];
                    sceClibSnprintf(
                        err, sizeof(err),
                        "unsupported ZIP compression method=%u (%s) entry=%s",
                        static_cast<unsigned>(zs.comp_method),
                        compressionMethodName(static_cast<zip_uint16_t>(zs.comp_method)),
                        zs.name);
                    setError(err);
                    zip_close(za);
                    return ZipResult::InvalidEntry;
                }
                if (zs.comp_method != 0 && zs.comp_method != 8) {
                    char warn[200];
                    sceClibSnprintf(
                        warn, sizeof(warn),
                        "entry uses compression method=%u (%s) name=%s",
                        static_cast<unsigned>(zs.comp_method),
                        compressionMethodName(static_cast<zip_uint16_t>(zs.comp_method)),
                        zs.name);
                    diagnostics::log(std::string("[ZipExtractor] ") + warn);
                }
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

    {
        uint64_t freeB = 0, totalB = 0;
        const bool haveSpace = StorageManager::queryUx0Space(freeB, totalB);
        // Margin: 32 MiB + 5% of uncompressed for FS overhead.
        const uint64_t margin = (32ULL * 1024ULL * 1024ULL) + (uncompressedTotal / 20ULL);
        const uint64_t required = uncompressedTotal > (~0ULL - margin) ? ~0ULL : uncompressedTotal + margin;
        char spaceMsg[320];
        sceClibSnprintf(
            spaceMsg, sizeof(spaceMsg),
            "[ZipExtractor] precheck archive=%llu uncomp=%llu required~=%llu free=%llu",
            (unsigned long long)archiveDiskSize,
            (unsigned long long)uncompressedTotal,
            (unsigned long long)required,
            (unsigned long long)(haveSpace ? freeB : 0ULL));
        diagnostics::log(spaceMsg);
        if (haveSpace && uncompressedTotal > 0 && freeB < required) {
            char err[360];
            sceClibSnprintf(
                err, sizeof(err),
                "not enough free space: archive=%llu uncomp=%llu required~=%llu free=%llu",
                (unsigned long long)archiveDiskSize,
                (unsigned long long)uncompressedTotal,
                (unsigned long long)required,
                (unsigned long long)freeB);
            setError(err);
            zip_close(za);
            return ZipResult::IoError;
        }
    }

    // On a retail Vita sceIoOpen may return 0x80010016 for files at >10
    // path components under the device. The original archive paths must not
    // be shortened or skipped. Instead extract a complete affected directory
    // branch in a shallow staging tree, then rename the WHOLE branch in place.
    // This requires that the target branch be absent or empty. We deliberately
    // do not merge into nonempty game directories or delete other game assets.
    struct StagedBranch {
        std::string destination;
        std::string staging;
        uint64_t files = 0;
    };
    std::vector<std::string> requiredDirs;
    std::vector<std::string> deepBranchPaths;
    requiredDirs.reserve(std::min(static_cast<size_t>(numEntries), size_t(4096)));
    auto pathDepth = [](const std::string& path) -> size_t {
        const size_t device = path.find(':');
        size_t depth = 0;
        bool inSegment = false;
        for (size_t i = device == std::string::npos ? 0 : device + 1;
             i < path.size(); ++i) {
            if (path[i] == '/' || path[i] == '\\') inSegment = false;
            else if (!inSegment) { ++depth; inSegment = true; }
        }
        return depth;
    };
    auto pathPrefixAtDepth = [](const std::string& path, size_t depth) -> std::string {
        const size_t device = path.find(':');
        size_t found = 0;
        bool inSegment = false;
        for (size_t i = device == std::string::npos ? 0 : device + 1;
             i < path.size(); ++i) {
            if (path[i] == '/' || path[i] == '\\') {
                if (inSegment && found == depth) return path.substr(0, i);
                inSegment = false;
            } else if (!inSegment) { ++found; inSegment = true; }
        }
        return found >= depth ? path : std::string();
    };
    auto withinBranch = [](const std::string& path, const std::string& branch) -> bool {
        return path == branch ||
            (path.size() > branch.size() && path.compare(0, branch.size(), branch) == 0 &&
             path[branch.size()] == '/');
    };

    for (zip_int64_t i = 0; i < numEntries; ++i) {
        if (shouldCancel && shouldCancel()) {
            setError("cancelled");
            zip_close(za);
            return ZipResult::Cancelled;
        }
        zip_stat_t zs;
        zip_stat_init(&zs);
        if (zip_stat_index(za, i, 0, &zs) != 0) {
            setError(std::string("ZIP preflight stat failed: ") + zipArchiveError(za));
            zip_close(za);
            return ZipResult::InvalidEntry;
        }
        const std::string name = zs.name ? zs.name : "";
        std::string path;
        if (!resolveSafePath(destinationDir, name, path)) {
            setError("unsafe ZIP entry path: " + name);
            zip_close(za);
            return ZipResult::UnsafePath;
        }
        const bool isDir = !name.empty() && (name.back() == '/' || name.back() == '\\');
        if (isDir) {
            while (!path.empty() && path.back() == '/') path.pop_back();
            requiredDirs.push_back(path);
        } else {
            const auto slash = path.find_last_of('/');
            if (slash != std::string::npos) requiredDirs.push_back(path.substr(0, slash));
        }
        // A directory's own creation at depth 11+ can also fail. Select a
        // directory root at depth 10; its parent remains a normal Vita path.
        if (pathDepth(path) > 10) {
            const std::string branch = pathPrefixAtDepth(path, 10);
            if (branch.empty()) {
                setError("cannot derive safe ZIP staging branch: " + path);
                zip_close(za);
                return ZipResult::IoError;
            }
            deepBranchPaths.push_back(branch);
        }
    }
    std::sort(requiredDirs.begin(), requiredDirs.end());
    requiredDirs.erase(std::unique(requiredDirs.begin(), requiredDirs.end()), requiredDirs.end());
    std::sort(deepBranchPaths.begin(), deepBranchPaths.end());
    deepBranchPaths.erase(std::unique(deepBranchPaths.begin(), deepBranchPaths.end()), deepBranchPaths.end());

    std::vector<StagedBranch> staged;
    std::string stagingBase;
    auto findBranch = [&](const std::string& path) -> const StagedBranch* {
        for (const StagedBranch& branch : staged)
            if (withinBranch(path, branch.destination)) return &branch;
        return nullptr;
    };

    // Stage only on a writable data volume, and always on the SAME partition
    // as the destination. Moving a folder across partitions would copy files
    // instead of using an atomic single-filesystem rename.
    if (!deepBranchPaths.empty()) {
        const size_t colon = destinationDir.find(':');
        const std::string device = colon == std::string::npos
            ? std::string() : destinationDir.substr(0, colon + 1);
        const std::string dataDir = device + "data";
        if ((device != "ux0:" && device != "uma0:") || !st.isDirectory(dataDir)) {
            setError("deep ZIP directory staging needs writable ux0:data or uma0:data on same device");
            zip_close(za);
            return ZipResult::IoError;
        }
        const std::string candidateBase = dataDir + "/PSVA_ZIP_STAGE_" +
            std::to_string(static_cast<unsigned long long>(sceKernelGetProcessTimeWide()));
        for (int n = 0; n < 32; ++n) {
            const std::string candidate = candidateBase + "_" + std::to_string(n);
            if (!st.exists(candidate)) { stagingBase = candidate; break; }
        }
        if (stagingBase.empty() || !st.createDirectories(stagingBase)) {
            setError("cannot create unique ZIP staging directory on target volume");
            zip_close(za);
            return ZipResult::IoError;
        }
        for (size_t n = 0; n < deepBranchPaths.size(); ++n) {
            const std::string& dest = deepBranchPaths[n];
            const size_t slash = dest.find_last_of('/');
            if (slash == std::string::npos) {
                setError("invalid staged destination branch: " + dest);
                zip_close(za);
                return ZipResult::IoError;
            }
            const std::string source = stagingBase + "/b" + std::to_string(n) +
                "/" + dest.substr(slash + 1);
            staged.push_back({dest, source, 0});
        }
        diagnostics::log("[ZipExtractor] directory staging enabled branches=" +
                         std::to_string(staged.size()) + " base=" + stagingBase);
    }

    // Preflight every actual output directory. The staged members are prepared
    // in the temporary tree, never in their deep final paths during extraction.
    for (const std::string& dir : requiredDirs) {
        if (findBranch(dir)) continue;
        if (!st.isDirectory(dir) && !st.createDirectories(dir)) {
            setError("cannot prepare ZIP directory: " + dir +
                     (st.exists(dir) ? " (non-directory path collision)" : " (mkdir failed)"));
            zip_close(za);
            return ZipResult::IoError;
        }
    }
    for (StagedBranch& branch : staged) {
        const auto slash = branch.destination.find_last_of('/');
        const std::string parent = branch.destination.substr(0, slash);
        if (!st.isDirectory(parent) || !st.createDirectories(branch.staging)) {
            setError("cannot prepare ZIP staging branch: " + branch.staging);
            zip_close(za);
            return ZipResult::IoError;
        }
        // A previous failed install may have created an EMPTY destination dir.
        // It can be replaced safely at commit time. Never merge into or
        // remove a directory containing other data, even if ZIP says overwrite.
        if (st.exists(branch.destination)) {
            if (!st.isDirectory(branch.destination)) {
                setError("ZIP branch destination collides with a file: " + branch.destination);
                zip_close(za);
                return ZipResult::IoError;
            }
            const SceUID dh = sceIoDopen(branch.destination.c_str());
            if (dh < 0) {
                setError("cannot check existing ZIP branch safely: " + branch.destination);
                zip_close(za);
                return ZipResult::IoError;
            }
            SceIoDirent ent{};
            int rc = 0;
            bool nonempty = false;
            while ((rc = sceIoDread(dh, &ent)) > 0) {
                if (std::strcmp(ent.d_name, ".") && std::strcmp(ent.d_name, "..")) {
                    nonempty = true;
                    break;
                }
            }
            sceIoDclose(dh);
            if (rc < 0 || nonempty) {
                setError("deep ZIP destination branch is not safely empty; preserved: " +
                         branch.destination + " staging=" + stagingBase);
                zip_close(za);
                return ZipResult::IoError;
            }
        }
    }
    // A physical Vita can reject the deep destination even when the entire
    // branch was successfully moved (the game's later reads may still fail).
    // We intentionally check the root directory move, not open the deepest
    // child; that respects the installer-only contract, but we explicitly log
    // that runtime accessibility is UNVERIFIED.
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        zip_stat_t zs;
        zip_stat_init(&zs);
        if (zip_stat_index(za, i, 0, &zs) != 0) {
            setError("ZIP staging preflight stat failed");
            zip_close(za);
            return ZipResult::InvalidEntry;
        }
        const std::string name = zs.name ? zs.name : "";
        std::string original;
        if (!resolveSafePath(destinationDir, name, original)) {
            setError("unsafe ZIP staging path: " + name);
            zip_close(za);
            return ZipResult::UnsafePath;
        }
        const StagedBranch* branch = findBranch(original);
        if (branch) {
            const std::string rel = original.substr(branch->destination.size());
            if (pathDepth(branch->staging + rel) > 10) {
                setError("ZIP staging cannot safely create overdeep nested path: " +
                         name + " staging=" + stagingBase);
                zip_close(za);
                return ZipResult::IoError;
            }
        }
    }
    diagnostics::log("[ZipExtractor] directory preflight unique=" +
                     std::to_string(requiredDirs.size()) +
                     " staged_branches=" + std::to_string(staged.size()));

    std::vector<char> buffer(EXTRACT_CHUNK);
    ZipResult outcome = ZipResult::Ok;

    // Limit UI handoffs while retaining byte-level progress inside large
    // entries. Cancellation is still checked for every extraction chunk.
    uint64_t lastProgressUs = 0;
    auto reportProgress = [&](bool force = false) {
        if (!onProgress) return;
        const uint64_t now = sceKernelGetProcessTimeWide();
        if (force || lastProgressUs == 0 ||
            now - lastProgressUs >= ZIP_PROGRESS_INTERVAL_US) {
            lastProgressUs = now;
            onProgress(prog);
        }
    };
    reportProgress(true);

    // Bounded directory cache: useful even when small-file entries alternate
    // between parents. Unverified parents still use the original stat/mkdir path.
    ExtractionDirectoryCache ensuredParents;
    std::string lastEnsuredParent;
    uint64_t directoryCacheHits = 0, directoryCacheChecks = 0;
    uint64_t exclusiveCreated = 0, existingFiles = 0, exclusiveFallbacks = 0;
    bool exclusiveCreationSupported = true;
    uint64_t bytesSinceYield = 0;

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
        const std::string originalPath = outPath;
        const StagedBranch* branch = findBranch(originalPath);
        if (branch) outPath = branch->staging + originalPath.substr(branch->destination.size());

        if (isDir) {
            if (outPath == lastEnsuredParent || ensuredParents.contains(outPath)) {
                ++directoryCacheHits;
            } else {
                ++directoryCacheChecks;
                if (!st.createDirectories(outPath) && !st.isDirectory(outPath)) {
                    setError(std::string("mkdir failed: ") + outPath);
                    outcome = ZipResult::IoError;
                    break;
                }
                ensuredParents.remember(outPath);
            }
            lastEnsuredParent = outPath;
            ++prog.entriesDone;
            reportProgress();
            continue;
        }

        const auto slash = outPath.find_last_of('/');
        if (slash != std::string::npos) {
            const std::string parent = outPath.substr(0, slash);
            if (parent == lastEnsuredParent || ensuredParents.contains(parent)) {
                ++directoryCacheHits;
            } else {
                ++directoryCacheChecks;
                if (!st.isDirectory(parent) && !st.createDirectories(parent)) {
                    setError(std::string("mkdir failed: ") + parent);
                    outcome = ZipResult::IoError;
                    break;
                }
                ensuredParents.remember(parent);
            }
            lastEnsuredParent = parent;
        }

        zip_file_t* zf = zip_fopen_index(za, i, 0);
        if (!zf) {
            setError(std::string("zip_fopen_index failed: ") + name + " (" + zipArchiveError(za) + ")");
            outcome = ZipResult::InvalidEntry;
            break;
        }

        // Create new files without a redundant getstat. EXCL must NOT truncate
        // an existing file. If the filesystem rejects EXCL for a missing file,
        // use the original checked path for this and all later entries.
        SceUID fd = -1;
        if (exclusiveCreationSupported)
            fd = sceIoOpen(outPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0777);

        bool replacingExisting = false;
        std::string tempPath, backupPath, writePath = outPath;
        if (fd >= 0) {
            ++exclusiveCreated;
        } else {
            replacingExisting = st.exists(outPath);
            if (replacingExisting && st.isDirectory(outPath)) {
                zip_fclose(zf);
                setError("ZIP file destination is a directory: " + outPath);
                outcome = ZipResult::InvalidEntry;
                break;
            }
            if (!replacingExisting && exclusiveCreationSupported) {
                exclusiveCreationSupported = false;
                ++exclusiveFallbacks;
                diagnostics::log("[ZipExtractor] exclusive-create unsupported/failed for new file; using compatible checked create path");
            }
            if (replacingExisting) {
                ++existingFiles;
                const std::string parent = outPath.substr(0, outPath.find_last_of('/'));
                for (unsigned slot = 0; slot < 32; ++slot) {
                    const std::string suffix = std::to_string(static_cast<unsigned long long>(i)) +
                        "_" + std::to_string(slot);
                    const std::string candidate = parent + "/._psva_" + suffix;
                    const std::string temp = candidate + ".tmp";
                    const std::string backup = candidate + ".bak";
                    if (!st.exists(temp) && !st.exists(backup)) {
                        tempPath = temp;
                        backupPath = backup;
                        break;
                    }
                }
                if (tempPath.empty()) {
                    zip_fclose(zf);
                    setError("cannot reserve safe ZIP replacement paths: " + outPath);
                    outcome = ZipResult::IoError;
                    break;
                }
                writePath = tempPath;
            }
            fd = sceIoOpen(writePath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        }
        if (fd < 0) {
            zip_fclose(zf);
            char err[480];
            sceClibSnprintf(err, sizeof(err),
                            "cannot create file: %s sceIoOpen=0x%08X parent_is_dir=%d",
                            writePath.c_str(), static_cast<unsigned>(fd),
                            slash != std::string::npos && st.isDirectory(outPath.substr(0, slash)) ? 1 : 0);
            setError(err);
            outcome = ZipResult::IoError;
            break;
        }

        const unsigned method = (zs.valid & ZIP_STAT_COMP_METHOD) ? static_cast<unsigned>(zs.comp_method) : 0u;
        bool fileOk = true;
        uint64_t writtenTotal = 0;
        // Per-file session logging competes with extraction I/O for the same
        // ux0 storage. Preserve detailed logs for large entries, unusual
        // compression and one sample per 64 small files.
        const bool traceEntry = numEntries <= 20 ||
            i % ZIP_ENTRY_LOG_STEP == 0 ||
            ((zs.valid & ZIP_STAT_SIZE) && zs.size >= ZIP_VERBOSE_ENTRY_MIN_SIZE) ||
            (method != 0 && method != 8);
        if (traceEntry) {
            char diag[512];
            sceClibSnprintf(
                diag, sizeof(diag),
                "[ZipDiag] entry_begin index=%llu name=%s method=%u method_name=%s comp=%llu uncomp=%llu archive_size=%llu",
                (unsigned long long)i,
                name,
                method,
                compressionMethodName(static_cast<zip_uint16_t>(method)),
                (unsigned long long)zs.comp_size,
                (unsigned long long)zs.size,
                (unsigned long long)archiveDiskSize);
            diagnostics::log(diag);
        }
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
                    "[ZipDiag] entry_read_failed entry=%s method=%u (%s) comp=%llu uncomp=%llu written=%llu libzip_code=%d libzip=%s archive_size=%llu",
                    name,
                    method,
                    compressionMethodName(static_cast<zip_uint16_t>(method)),
                    (unsigned long long)zs.comp_size,
                    (unsigned long long)zs.size,
                    (unsigned long long)writtenTotal,
                    zip_file_get_error(zf) ? zip_error_code_zip(zip_file_get_error(zf)) : -1,
                    zipFileError(zf).c_str(),
                    (unsigned long long)archiveDiskSize);
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
                    uint64_t freeB = 0, totalB = 0;
                    const bool haveSpace = StorageManager::queryUx0Space(freeB, totalB);
                    char err[420];
                    sceClibSnprintf(
                        err, sizeof(err),
                        "sceIoWrite failed path=%s ret=%d tried=%d written_total=%llu free=%llu",
                        outPath.c_str(),
                        w,
                        static_cast<int>(n) - written,
                        (unsigned long long)writtenTotal,
                        (unsigned long long)(haveSpace ? freeB : 0ULL));
                    setError(err);
                    diagnostics::log(std::string("[ZipExtractor] ") + err);
                    outcome = ZipResult::IoError;
                    fileOk = false;
                    break;
                }
                written += w;
            }
            if (!fileOk) break;
            writtenTotal += static_cast<uint64_t>(n);
            prog.bytesWritten += static_cast<uint64_t>(n);
            reportProgress();
            bytesSinceYield += static_cast<uint64_t>(n);
            if (bytesSinceYield >= ZIP_COOPERATIVE_YIELD_BYTES) {
                bytesSinceYield = 0;
                // This worker is not the UI thread. Yield without allocating
                // extra buffers or relaxing any ZIP validation/cancel checks.
                sceKernelDelayThread(1000);
            }
        }

        const int zipCloseResult = zip_fclose(zf);
        const int ioCloseResult = sceIoClose(fd);
        if (fileOk && zipCloseResult != 0) {
            char detail[240];
            sceClibSnprintf(detail, sizeof(detail),
                "zip_fclose failed entry=%s code=%d written=%llu",
                name, zipCloseResult, (unsigned long long)writtenTotal);
            setError(detail);
            outcome = ZipResult::IoError;
            fileOk = false;
        }
        if (fileOk && ioCloseResult < 0) {
            char detail[240];
            sceClibSnprintf(detail, sizeof(detail),
                "sceIoClose failed entry=%s ret=%d written=%llu",
                name, ioCloseResult, (unsigned long long)writtenTotal);
            setError(detail);
            outcome = ZipResult::IoError;
            fileOk = false;
        }

        if (fileOk && traceEntry) {
            char diag[384];
            sceClibSnprintf(
                diag, sizeof(diag),
                "[ZipDiag] entry_read_complete entry=%s method=%u expected=%llu written=%llu",
                name,
                method,
                (unsigned long long)zs.size,
                (unsigned long long)writtenTotal);
            diagnostics::log(diag);
        }

        if (!fileOk) {
            // A failed overwrite removes only the temporary output.
            st.removeFile(writePath);
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
            st.removeFile(writePath);
            outcome = ZipResult::IoError;
            break;
        }

        if (replacingExisting) {
            // CRC, ZIP stream close and expected-size verification all passed.
            if (shouldCancel && shouldCancel()) {
                st.removeFile(tempPath);
                setError("cancelled before ZIP file replacement");
                outcome = ZipResult::Cancelled;
                break;
            }
            const int backedUp = sceIoRename(outPath.c_str(), backupPath.c_str());
            if (backedUp < 0) {
                st.removeFile(tempPath);
                setError("cannot back up existing ZIP destination: " + outPath +
                         " sceIoRename=" + std::to_string(backedUp));
                outcome = ZipResult::IoError;
                break;
            }
            const int promoted = sceIoRename(tempPath.c_str(), outPath.c_str());
            if (promoted < 0) {
                const int restored = sceIoRename(backupPath.c_str(), outPath.c_str());
                st.removeFile(tempPath);
                setError("ZIP replacement rename failed: " + outPath +
                         " commit=" + std::to_string(promoted) +
                         " restore=" + std::to_string(restored) +
                         (restored < 0 ? " (original preserved at " + backupPath + ")" : ""));
                outcome = ZipResult::IoError;
                break;
            }
            if (!st.removeFile(backupPath))
                diagnostics::log("[ZipExtractor] overwrite complete, old backup retained at " + backupPath);
        }

        if (branch) {
            for (StagedBranch& entry : staged)
                if (entry.destination == branch->destination) { ++entry.files; break; }
        }
        ++prog.entriesDone;
        reportProgress();
    }

    const int archiveCloseResult = zip_close(za);
    if (archiveCloseResult != 0) {
        if (outcome == ZipResult::Ok) {
            setError(std::string("zip_close failed: ") + zipArchiveError(za));
            outcome = ZipResult::IoError;
        }
        zip_discard(za);
    }
    // Commit one COMPLETE directory branch at a time, only after libzip
    // reported successful extraction and CRC verification for all entries.
    // Preserve staged contents on any move error for manual recovery.
    if (outcome == ZipResult::Ok) {
        for (const StagedBranch& branch : staged) {
            if (shouldCancel && shouldCancel()) {
                setError("cancelled before ZIP folder move; staged data kept at " + stagingBase);
                outcome = ZipResult::Cancelled;
                break;
            }
            if (st.exists(branch.destination)) {
                const SceUID dh = sceIoDopen(branch.destination.c_str());
                if (dh < 0) {
                    setError("ZIP move: cannot check final directory " + branch.destination +
                             " staged=" + branch.staging);
                    outcome = ZipResult::IoError;
                    break;
                }
                SceIoDirent ent{};
                int rc = 0;
                bool nonempty = false;
                while ((rc = sceIoDread(dh, &ent)) > 0) {
                    if (std::strcmp(ent.d_name, ".") && std::strcmp(ent.d_name, "..")) {
                        nonempty = true;
                        break;
                    }
                }
                sceIoDclose(dh);
                if (rc < 0 || nonempty) {
                    setError("ZIP move: destination now contains files; refusing overwrite " +
                             branch.destination + " staged=" + branch.staging);
                    outcome = ZipResult::IoError;
                    break;
                }
                const int removeEmpty = sceIoRmdir(branch.destination.c_str());
                if (removeEmpty < 0) {
                    setError("ZIP move: cannot remove empty destination folder " +
                             branch.destination + " ret=" + std::to_string(removeEmpty));
                    outcome = ZipResult::IoError;
                    break;
                }
            }
            const int moved = sceIoRename(branch.staging.c_str(), branch.destination.c_str());
            if (moved < 0) {
                char detail[560];
                sceClibSnprintf(detail, sizeof(detail),
                    "ZIP directory relocation failed dest=%s stage=%s sceIoRename=0x%08X",
                    branch.destination.c_str(), branch.staging.c_str(),
                    static_cast<unsigned>(moved));
                setError(detail);
                outcome = ZipResult::IoError;
                break;
            }
            diagnostics::log("[ZipExtractor] staged subtree moved successfully to " +
                             branch.destination + " files=" + std::to_string(branch.files) +
                             " (deep-child accessibility unverified)");
        }
    }
    if (!staged.empty()) {
        if (outcome == ZipResult::Ok) {
            // Only known-empty staging parents are removed. Never recursively
            // delete a directory that could still hold user files after errors.
            for (size_t n = 0; n < staged.size(); ++n)
                st.removeDirectory(stagingBase + "/b" + std::to_string(n));
            st.removeDirectory(stagingBase);
            diagnostics::log("[ZipExtractor] staged folders moved; runtime file access remains unverified");
        } else {
            diagnostics::log("[ZipExtractor] staging kept for investigation: " + stagingBase);
        }
    }
    {
        char perf[320];
        sceClibSnprintf(perf, sizeof(perf),
            "[ZipPerf] entries=%llu dir_cache_hits=%llu dir_checks=%llu "
            "excl_created=%llu existing_replaced=%llu excl_fallbacks=%llu status=%s",
            (unsigned long long)prog.entriesDone,
            (unsigned long long)directoryCacheHits,
            (unsigned long long)directoryCacheChecks,
            (unsigned long long)exclusiveCreated,
            (unsigned long long)existingFiles,
            (unsigned long long)exclusiveFallbacks,
            toString(outcome));
        diagnostics::log(perf);
    }
    if (outcome == ZipResult::Ok) {
        usedStagedMoves_ = !staged.empty();
        prog.currentEntry.clear();
        reportProgress(true);
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
