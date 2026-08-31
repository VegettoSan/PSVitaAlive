#!/usr/bin/env python3
"""Fix libzip custom source (STAT/SEEK/ERROR), EOCD pre-check, better logs."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZE = ROOT / "Client PSVitaAlive/source/archive/zip_extractor.cpp"


def main() -> int:
    t = ZE.read_text(encoding="utf-8")
    if "zipLooksCompleteOnDisk" in t and "zip_source_seek_compute_offset" in t:
        print("already applied")
        return 0

    if "#include <vector>" not in t:
        t = t.replace("#include <string>", "#include <string>\n#include <vector>", 1)

    t = t.replace(
        """struct VitaZipFileSource {
    std::string path;
    SceUID fd = -1;
    uint64_t size = 0;
    uint64_t pos = 0;
    int lastError = 0;
};""",
        """struct VitaZipFileSource {
    std::string path;
    SceUID fd = -1;
    uint64_t size = 0;
    uint64_t pos = 0;
    int lastSysError = 0;
    int lastZipError = ZIP_ER_OK;
};""",
        1,
    )

    t = t.replace(
        """    case ZIP_SOURCE_OPEN: {
        if (s->fd >= 0) {
            sceIoClose(s->fd);
            s->fd = -1;
        }
        s->fd = sceIoOpen(s->path.c_str(), SCE_O_RDONLY, 0);
        if (s->fd < 0) {
            s->lastError = (int)s->fd;
            return -1;
        }
        s->pos = 0;
        return 0;
    }""",
        """    case ZIP_SOURCE_OPEN: {
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
    }""",
        1,
    )

    t = t.replace(
        """            if (n < 0) {
                s->lastError = n;
                return -1;
            }""",
        """            if (n < 0) {
                s->lastSysError = n;
                s->lastZipError = ZIP_ER_READ;
                return -1;
            }""",
        1,
    )

    t = t.replace(
        """    case ZIP_SOURCE_STAT: {
        if (!data || len < sizeof(zip_stat_t)) return -1;
        zip_stat_t* st = static_cast<zip_stat_t*>(data);
        zip_stat_init(st);
        st->valid = ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE;
        st->size = s->size;
        st->comp_size = s->size;
        return 0;
    }""",
        """    case ZIP_SOURCE_STAT: {
        // libzip docs: return sizeof(zip_stat) on success (not 0).
        if (!data || len < sizeof(zip_stat_t)) return -1;
        zip_stat_t* st = static_cast<zip_stat_t*>(data);
        zip_stat_init(st);
        st->valid = ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE;
        st->size = s->size;
        st->comp_size = s->size;
        return static_cast<zip_int64_t>(sizeof(zip_stat_t));
    }""",
        1,
    )

    t = t.replace(
        """    case ZIP_SOURCE_SEEK: {
        if (!data || len < sizeof(zip_source_args_seek_t)) return -1;
        zip_source_args_seek_t args;
        std::memcpy(&args, data, sizeof(args));
        int64_t target = 0;
        switch (args.whence) {
        case SEEK_SET: target = (int64_t)args.offset; break;
        case SEEK_CUR: target = (int64_t)s->pos + (int64_t)args.offset; break;
        case SEEK_END: target = (int64_t)s->size + (int64_t)args.offset; break;
        default: return -1;
        }
        if (target < 0 || (uint64_t)target > s->size) return -1;
        if (s->fd < 0) return -1;
        const SceOff got = sceIoLseek(s->fd, (SceOff)target, SCE_SEEK_SET);
        if (got < 0) {
            s->lastError = (int)got;
            return -1;
        }
        s->pos = (uint64_t)got;
        return 0;
    }""",
        """    case ZIP_SOURCE_SEEK: {
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
    }""",
        1,
    )

    t = t.replace(
        """    case ZIP_SOURCE_ERROR:
        return 0;""",
        """    case ZIP_SOURCE_ERROR: {
        if (!data || len < 2 * sizeof(int)) return -1;
        int* codes = static_cast<int*>(data);
        codes[0] = s->lastZipError ? s->lastZipError : ZIP_ER_INTERNAL;
        codes[1] = s->lastSysError;
        return static_cast<zip_int64_t>(2 * sizeof(int));
    }""",
        1,
    )

    helper = r'''
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

'''

    if "static zip_t* vitaZipOpen(const std::string& zipPath, int* outErr)" not in t:
        raise SystemExit("vitaZipOpen signature not found")
    t = t.replace(
        "static zip_t* vitaZipOpen(const std::string& zipPath, int* outErr) {",
        helper + "static zip_t* vitaZipOpen(const std::string& zipPath, int* outErr, std::string* outDetail) {",
        1,
    )

    old_body = """    if (outErr) *outErr = 0;

    SceIoStat st{};
    if (sceIoGetstat(zipPath.c_str(), &st) < 0 || st.st_size <= 0) {
        if (outErr) *outErr = ZIP_ER_NOENT;
        return nullptr;
    }
    const uint64_t fileSize = (uint64_t)st.st_size;

    // Prefer custom source for large files (>2GB) or paths with spaces
    // (classic zip_open uses CRT paths that break past 2GiB on Vita).
    const bool needCustom =
        fileSize >= 0x80000000ULL ||
        zipPath.find(' ') != std::string::npos;

    if (!needCustom) {
        int zerr = 0;
        zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &zerr);
        if (za) return za;
        if (outErr) *outErr = zerr;
        // fall through to custom source once
    }

    auto* srcState = new VitaZipFileSource();
    srcState->path = zipPath;
    srcState->size = fileSize;

    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* src = zip_source_function_create(vitaZipSourceCb, srcState, &error);
    if (!src) {
        if (outErr) *outErr = zip_error_code_zip(&error);
        zip_error_fini(&error);
        delete srcState;
        return nullptr;
    }

    zip_t* za = zip_open_from_source(src, ZIP_RDONLY, &error);
    if (!za) {
        if (outErr) *outErr = zip_error_code_zip(&error);
        zip_source_free(src);
        zip_error_fini(&error);
        return nullptr;
    }
    zip_error_fini(&error);

    diagnostics::log(
        std::string("[ZipExtractor] opened via sceIo source size=") +
        std::to_string(fileSize) + " path=" + zipPath);
    return za;
}"""

    new_body = """    if (outErr) *outErr = 0;
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
}"""

    if old_body not in t:
        raise SystemExit("vitaZipOpen body not found for replace")
    t = t.replace(old_body, new_body, 1)

    old_call = """    int zerr = 0;
    zip_t* za = vitaZipOpen(zipPath, &zerr);
    if (!za) {
        char buf[192];
        sceClibSnprintf(buf, sizeof(buf),
            "zip_open failed err=%d path=%s (large ZIPs need 64-bit sceIo source)",
            zerr, zipPath.c_str());
        setError(buf);
        return ZipResult::OpenFailed;
    }
"""
    new_call = """    int zerr = 0;
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
"""
    if old_call not in t:
        raise SystemExit("extract call site not found")
    t = t.replace(old_call, new_call, 1)

    ZE.write_text(t, encoding="utf-8")
    print("OK: libzip STAT/SEEK/ERROR + EOCD + logs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
