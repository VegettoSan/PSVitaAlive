#include "storage/storage_manager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/clib.h>

#include <cstring>
#include <vector>

namespace psvitaalive {

namespace {

bool pathExistsInternal(const std::string& path) {
    SceIoStat stat;
    std::memset(&stat, 0, sizeof(stat));
    return sceIoGetstat(path.c_str(), &stat) >= 0;
}

} // namespace

bool StorageManager::initProjectDirs() {
    bool ok = true;
    ok = createDirectories(BASE_DIR) && ok;
    ok = createDirectories(DOWNLOADS_DIR) && ok;
    ok = createDirectories(JOBS_DIR) && ok;
    ok = createDirectories(CACHE_DIR) && ok;
    ok = createDirectories(TEST_DIR) && ok;
    return ok;
}

bool StorageManager::exists(const std::string& path) const {
    return pathExistsInternal(path);
}

bool StorageManager::isDirectory(const std::string& path) const {
    SceIoStat stat;
    std::memset(&stat, 0, sizeof(stat));
    if (sceIoGetstat(path.c_str(), &stat) < 0) {
        return false;
    }
    return (stat.st_mode & SCE_S_IFDIR) != 0;
}

bool StorageManager::createDirectory(const std::string& path) {
    if (exists(path)) {
        return isDirectory(path);
    }
    int res = sceIoMkdir(path.c_str(), 0777);
    return res >= 0 || exists(path);
}

bool StorageManager::createDirectories(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (exists(path)) {
        return isDirectory(path);
    }

    // Build progressive paths: ux0:data -> ux0:data/psvitaalive -> ...
    std::string current;
    current.reserve(path.size());

    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        current.push_back(c);

        bool atSeparator = (c == '/');
        bool atEnd = (i + 1 == path.size());

        if (atSeparator || atEnd) {
            // Skip empty segments and device roots like "ux0:"
            if (current.size() <= 1) {
                continue;
            }
            // Avoid mkdir on "ux0:" alone
            if (current.find(':') != std::string::npos && current.back() == ':') {
                continue;
            }
            // Trim trailing slash for mkdir
            std::string dir = current;
            if (!dir.empty() && dir.back() == '/') {
                dir.pop_back();
            }
            if (dir.empty()) {
                continue;
            }
            if (!exists(dir)) {
                int res = sceIoMkdir(dir.c_str(), 0777);
                if (res < 0 && !exists(dir)) {
                    return false;
                }
            }
        }
    }
    return exists(path) && isDirectory(path);
}

bool StorageManager::removeFile(const std::string& path) {
    if (!exists(path)) {
        return true;
    }
    return sceIoRemove(path.c_str()) >= 0;
}

bool StorageManager::removeDirectory(const std::string& path) {
    if (!exists(path)) {
        return true;
    }
    return sceIoRmdir(path.c_str()) >= 0;
}

bool StorageManager::rename(const std::string& oldPath, const std::string& newPath) {
    return sceIoRename(oldPath.c_str(), newPath.c_str()) >= 0;
}

int64_t StorageManager::fileSize(const std::string& path) const {
    SceIoStat stat;
    std::memset(&stat, 0, sizeof(stat));
    if (sceIoGetstat(path.c_str(), &stat) < 0) {
        return -1;
    }
    return static_cast<int64_t>(stat.st_size);
}

bool StorageManager::writeFile(const std::string& path, const void* data, size_t size) {
    // Ensure parent directory exists when possible
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        createDirectories(path.substr(0, pos));
    }

    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        int written = sceIoWrite(fd, bytes, remaining);
        if (written <= 0) {
            sceIoClose(fd);
            return false;
        }
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }

    sceIoClose(fd);
    return true;
}

bool StorageManager::writeTextFile(const std::string& path, const std::string& text) {
    return writeFile(path, text.data(), text.size());
}

bool StorageManager::readFile(const std::string& path, std::vector<uint8_t>& out) const {
    out.clear();
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    int64_t size = fileSize(path);
    if (size < 0) {
        sceIoClose(fd);
        return false;
    }

    out.resize(static_cast<size_t>(size));
    if (size == 0) {
        sceIoClose(fd);
        return true;
    }

    size_t remaining = out.size();
    uint8_t* ptr = out.data();
    while (remaining > 0) {
        int rd = sceIoRead(fd, ptr, remaining);
        if (rd <= 0) {
            sceIoClose(fd);
            out.clear();
            return false;
        }
        ptr += rd;
        remaining -= static_cast<size_t>(rd);
    }

    sceIoClose(fd);
    return true;
}

bool StorageManager::readTextFile(const std::string& path, std::string& out) const {
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
    return true;
}

bool StorageManager::appendFile(const std::string& path, const void* data, size_t size) {
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        createDirectories(path.substr(0, pos));
    }

    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0) {
        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        int written = sceIoWrite(fd, bytes, remaining);
        if (written <= 0) {
            sceIoClose(fd);
            return false;
        }
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }

    sceIoClose(fd);
    return true;
}

bool StorageManager::queryUx0Space(uint64_t& freeBytesOut, uint64_t& totalBytesOut) {
    freeBytesOut = 0;
    totalBytesOut = 0;
    struct {
        uint64_t max_size;
        uint64_t free_size;
        uint32_t cluster_size;
        void* unk;
    } info{};
    const int ret = sceIoDevctl("ux0:", 0x3001, nullptr, 0, &info, sizeof(info));
    if (ret < 0) return false;
    freeBytesOut = info.free_size;
    totalBytesOut = info.max_size > 0 ? info.max_size : info.free_size;
    return true;
}

bool StorageManager::hasFreeSpace(uint64_t requiredBytes) const {
    uint64_t freeB = 0, totalB = 0;
    if (!queryUx0Space(freeB, totalB)) return false;
    return freeB >= requiredBytes;
}

} // namespace psvitaalive
