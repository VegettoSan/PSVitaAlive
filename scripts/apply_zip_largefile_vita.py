#!/usr/bin/env python3
"""Open large ZIPs (>2GB) on Vita via sceIo 64-bit source — fixes zip_open err=18."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZE = ROOT / "Client PSVitaAlive/source/archive/zip_extractor.cpp"


def main() -> int:
    t = ZE.read_text(encoding="utf-8")
    if "vitaZipOpen" in t:
        print("zip_extractor: already has large-file open")
        return 0

    if "#include <psp2/io/fcntl.h>" not in t:
        t = t.replace(
            '#include "archive/zip_extractor.hpp"',
            '#include "archive/zip_extractor.hpp"\n#include <psp2/io/fcntl.h>\n#include <psp2/io/stat.h>',
            1,
        )

    helper = r'''
namespace {

// libzip's zip_open() on Vita often returns ZIP_ER_INVAL (18) for files >2GB
// (32-bit off_t / fstat). Use a custom source backed by sceIo* 64-bit seeks.
struct VitaZipFileSource {
    std::string path;
    SceUID fd = -1;
    uint64_t size = 0;
    uint64_t pos = 0;
    int lastError = 0;
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
            s->lastError = (int)s->fd;
            return -1;
        }
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
                s->lastError = n;
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
        if (!data || len < sizeof(zip_stat_t)) return -1;
        zip_stat_t* st = static_cast<zip_stat_t*>(data);
        zip_stat_init(st);
        st->valid = ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE;
        st->size = s->size;
        st->comp_size = s->size;
        return 0;
    }

    case ZIP_SOURCE_SEEK: {
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
    }

    case ZIP_SOURCE_TELL:
        return (zip_int64_t)s->pos;

    case ZIP_SOURCE_ERROR:
        return 0;

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

static zip_t* vitaZipOpen(const std::string& zipPath, int* outErr) {
    if (outErr) *outErr = 0;

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
}

} // namespace

'''

    anchor = "ZipResult ZipExtractor::extract("
    if anchor not in t:
        raise SystemExit("extract() not found")
    idx = t.find(anchor)
    t = t[:idx] + helper + t[idx:]

    old_open = """    int zerr = 0;
    zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &zerr);
    if (!za) {
        char buf[128];
        sceClibSnprintf(buf, sizeof(buf), "zip_open failed err=%d path=%s", zerr, zipPath.c_str());
        setError(buf);
        return ZipResult::OpenFailed;
    }
"""
    new_open = """    int zerr = 0;
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
    if old_open not in t:
        raise SystemExit("zip_open block not found for replace")
    t = t.replace(old_open, new_open, 1)

    if "#include <cstdint>" not in t:
        t = t.replace("#include <cstring>", "#include <cstdint>\n#include <cstring>", 1)

    ZE.write_text(t, encoding="utf-8")
    print("OK: vitaZipOpen for >2GB / spaces")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
