#include "storage/StorageManager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

namespace {

const char* ROOT_PATH = "ux0:data/psvitaalive";
const char* TEST_PATH = "ux0:data/psvitaalive/test";

bool createDirectory(const char* path) {
    int result = sceIoMkdir(path, 0777);

    if (result >= 0) {
        return true;
    }

    SceIoStat stat;
    int statResult = sceIoGetstat(path, &stat);

    return statResult >= 0;
}

}

bool StorageManager::initialize() {
    if (!createDirectory(ROOT_PATH)) {
        return false;
    }

    if (!createDirectory(TEST_PATH)) {
        return false;
    }

    return true;
}

bool StorageManager::createTestFile(
    const std::string& path,
    const std::string& content
) {
    SceUID fd = sceIoOpen(
        path.c_str(),
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0777
    );

    if (fd < 0) {
        return false;
    }

    SceSSize written = sceIoWrite(
        fd,
        content.data(),
        content.size()
    );

    sceIoClose(fd);

    return written == static_cast<SceSSize>(content.size());
}

bool StorageManager::readTestFile(
    const std::string& path,
    std::string& content
) {
    SceUID fd = sceIoOpen(
        path.c_str(),
        SCE_O_RDONLY,
        0
    );

    if (fd < 0) {
        return false;
    }

    char buffer[256];

    SceSSize read = sceIoRead(
        fd,
        buffer,
        sizeof(buffer) - 1
    );

    sceIoClose(fd);

    if (read < 0) {
        return false;
    }

    buffer[read] = '\0';
    content.assign(buffer, read);

    return true;
}
