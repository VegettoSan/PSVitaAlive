#include "installer/license_helper.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

#include <vector>

namespace psvitaalive {

bool LicenseHelper::looksLikeRifSize(uint64_t size) {
    // Common RIF/work.bin sizes observed in the wild.
    return size == 512 || size == 1024 || (size >= 400 && size <= 4096);
}

bool LicenseHelper::writeRifBytes(const std::vector<uint8_t>& bytes, const std::string& destPath, std::string& errorOut) {
    if (bytes.empty() || destPath.empty()) {
        errorOut = "empty rif bytes or path";
        return false;
    }
    // Ensure parent directory exists when possible.
    std::string parent = destPath;
    const auto slash = parent.find_last_of('/');
    if (slash != std::string::npos) {
        parent.resize(slash);
        StorageManager st;
        st.createDirectories(parent);
    }

    SceUID fd = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        errorOut = "cannot open dest for rif write";
        return false;
    }
    const int wr = sceIoWrite(fd, bytes.data(), bytes.size());
    sceIoClose(fd);
    if (wr != static_cast<int>(bytes.size())) {
        errorOut = "short write for rif";
        return false;
    }
    return true;
}

bool LicenseHelper::copyRifFile(const std::string& rifPath, const std::string& destPath, std::string& errorOut) {
    if (rifPath.empty() || destPath.empty()) {
        errorOut = "empty rif/dest path";
        return false;
    }
    StorageManager st;
    if (!st.exists(rifPath)) {
        errorOut = "rif source not found";
        return false;
    }
    const int64_t sz = st.fileSize(rifPath);
    if (sz <= 0) {
        errorOut = "rif source empty";
        return false;
    }
    if (!looksLikeRifSize(static_cast<uint64_t>(sz))) {
        // Still allow, but warn.
        sceClibPrintf("[LicenseHelper] unusual rif size=%lld\n", (long long)sz);
    }

    SceUID in = sceIoOpen(rifPath.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) {
        errorOut = "cannot open rif source";
        return false;
    }

    std::string parent = destPath;
    const auto slash = parent.find_last_of('/');
    if (slash != std::string::npos) {
        parent.resize(slash);
        st.createDirectories(parent);
    }

    SceUID out = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (out < 0) {
        sceIoClose(in);
        errorOut = "cannot open rif dest";
        return false;
    }

    std::vector<char> buf(4096);
    while (true) {
        const int rd = sceIoRead(in, buf.data(), buf.size());
        if (rd < 0) {
            sceIoClose(in);
            sceIoClose(out);
            errorOut = "rif read failed";
            return false;
        }
        if (rd == 0) break;
        if (sceIoWrite(out, buf.data(), rd) != rd) {
            sceIoClose(in);
            sceIoClose(out);
            errorOut = "rif write failed";
            return false;
        }
    }
    sceIoClose(in);
    sceIoClose(out);
    return true;
}

} // namespace psvitaalive
