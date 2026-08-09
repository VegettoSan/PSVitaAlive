#include "archive/zip_extractor.hpp"
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
}

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
    sceClibPrintf("[ZipExtractor] %s\n", msg.c_str());
}

bool ZipExtractor::isSafeEntryName(const std::string& entryName) {
    if (entryName.empty()) return false;

    // Absolute paths
    if (entryName[0] == '/' || entryName[0] == '\\') return false;

    // Drive / device style
    if (entryName.find(':') != std::string::npos) return false;

    // Backslashes normalized check — reject raw escapes
    std::string n = entryName;
    for (char& c : n) {
        if (c == '\\') c = '/';
    }

    // Disallow any ".." segment
    size_t start = 0;
    while (start <= n.size()) {
        size_t pos = n.find('/', start);
        std::string seg = (pos == std::string::npos)
            ? n.substr(start)
            : n.substr(start, pos - start);
        if (seg == "..") return false;
        if (pos == std::string::npos) break;
        start = pos + 1;
    }

    return true;
}

bool ZipExtractor::resolveSafePath(
    const std::string& destinationDir,
    const std::string& entryName,
    std::string& outPath
) {
    if (!isSafeEntryName(entryName)) return false;

    std::string dest = destinationDir;
    while (!dest.empty() && (dest.back() == '/' || dest.back() == '\\')) {
        dest.pop_back();
    }

    std::string name = entryName;
    for (char& c : name) {
        if (c == '\\') c = '/';
    }
    while (!name.empty() && name.front() == '/') {
        name.erase(name.begin());
    }

    outPath = dest + "/" + name;

    // Verify prefix: outPath must start with dest + "/"
    if (outPath.size() < dest.size() + 1) return false;
    if (outPath.compare(0, dest.size(), dest) != 0) return false;
    if (outPath[dest.size()] != '/') return false;

    // Second pass: reject residual ".." after join
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

    if (!st.createDirectories(destinationDir)) {
        setError("cannot create destination");
        return ZipResult::IoError;
    }

    int zerr = 0;
    zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &zerr);
    if (!za) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "zip_open failed err=%d", zerr);
        setError(buf);
        return ZipResult::OpenFailed;
    }

    const zip_int64_t numEntries = zip_get_num_entries(za, 0);
    if (numEntries < 0) {
        zip_close(za);
        setError("zip_get_num_entries failed");
        return ZipResult::OpenFailed;
    }

    ZipProgress prog;
    prog.entriesTotal = static_cast<uint64_t>(numEntries);
    prog.entriesDone = 0;
    prog.bytesWritten = 0;

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
            setError("zip_stat_index failed");
            outcome = ZipResult::InvalidEntry;
            break;
        }

        const char* name = zs.name ? zs.name : "";
        prog.currentEntry = name;

        // Directory entries end with /
        const bool isDir = (name[0] != '\0' && name[std::strlen(name) - 1] == '/');

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
            prog.entriesDone++;
            if (onProgress) onProgress(prog);
            continue;
        }

        // Ensure parent directory
        auto slash = outPath.find_last_of('/');
        if (slash != std::string::npos) {
            st.createDirectories(outPath.substr(0, slash));
        }

        zip_file_t* zf = zip_fopen_index(za, i, 0);
        if (!zf) {
            setError(std::string("zip_fopen_index failed: ") + name);
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
        while (true) {
            if (shouldCancel && shouldCancel()) {
                setError("cancelled");
                outcome = ZipResult::Cancelled;
                fileOk = false;
                break;
            }

            zip_int64_t n = zip_fread(zf, buffer.data(), buffer.size());
            if (n < 0) {
                setError("zip_fread failed");
                outcome = ZipResult::IoError;
                fileOk = false;
                break;
            }
            if (n == 0) break;

            int written = 0;
            while (written < static_cast<int>(n)) {
                int w = sceIoWrite(fd, buffer.data() + written, static_cast<int>(n) - written);
                if (w <= 0) {
                    setError("sceIoWrite failed during extract");
                    outcome = ZipResult::IoError;
                    fileOk = false;
                    break;
                }
                written += w;
            }
            if (!fileOk) break;
            prog.bytesWritten += static_cast<uint64_t>(n);
        }

        sceIoClose(fd);
        zip_fclose(zf);

        if (!fileOk) break;

        prog.entriesDone++;
        if (onProgress) onProgress(prog);
    }

    zip_close(za);

    if (outcome == ZipResult::Ok) {
        sceClibPrintf("[ZipExtractor] extracted %llu entries, %llu bytes -> %s\n",
                      (unsigned long long)prog.entriesDone,
                      (unsigned long long)prog.bytesWritten,
                      destinationDir.c_str());
    }
    return outcome;
}

} // namespace psvitaalive
