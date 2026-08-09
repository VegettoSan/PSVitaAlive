#include "installer/fake_package_builder.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <openssl/sha.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {

constexpr const char* kTemplatePath = "app0:resources/head.bin";

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(p[1] << 8);
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

void writeBytes(uint8_t* dst, const void* src, size_t len) {
    std::memcpy(dst, src, len);
}

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        char buf[128];
        sceClibSnprintf(buf, sizeof(buf), "open failed: %s (0x%08X)", path.c_str(), fd);
        error = buf;
        return false;
    }

    SceIoStat st;
    std::memset(&st, 0, sizeof(st));
    const int statResult = sceIoGetstat(path.c_str(), &st);
    if (statResult < 0 || st.st_size <= 0) {
        sceIoClose(fd);
        error = "invalid or empty file: " + path;
        return false;
    }

    out.resize(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < out.size()) {
        const int r = sceIoRead(fd, out.data() + done, out.size() - done);
        if (r <= 0) {
            sceIoClose(fd);
            error = "read failed: " + path;
            return false;
        }
        done += static_cast<size_t>(r);
    }

    sceIoClose(fd);
    return true;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data, std::string& error) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        char buf[128];
        sceClibSnprintf(buf, sizeof(buf), "create failed: %s (0x%08X)", path.c_str(), fd);
        error = buf;
        return false;
    }

    size_t done = 0;
    while (done < data.size()) {
        const int w = sceIoWrite(fd, data.data() + done, data.size() - done);
        if (w <= 0) {
            sceIoClose(fd);
            error = "write failed: " + path;
            return false;
        }
        done += static_cast<size_t>(w);
    }

    sceIoClose(fd);
    return true;
}

bool readSfoString(const std::vector<uint8_t>& sfo, const char* wantedKey, std::string& value) {
    if (sfo.size() < 0x14 || std::memcmp(sfo.data(), "\0PSF", 4) != 0) {
        return false;
    }

    const uint32_t keyTableOffset = readU32(&sfo[8]);
    const uint32_t dataTableOffset = readU32(&sfo[12]);
    const uint32_t entryCount = readU32(&sfo[16]);

    if (keyTableOffset >= sfo.size() || dataTableOffset >= sfo.size()) {
        return false;
    }
    if (entryCount > (sfo.size() - 0x14) / 16) {
        return false;
    }

    for (uint32_t i = 0; i < entryCount; ++i) {
        const size_t entry = 0x14 + static_cast<size_t>(i) * 16;
        const uint16_t keyOffset = readU16(&sfo[entry]);
        const uint32_t dataLen = readU32(&sfo[entry + 4]);
        const uint32_t dataMaxLen = readU32(&sfo[entry + 8]);
        const uint32_t dataOffset = readU32(&sfo[entry + 12]);

        const size_t keyPos = static_cast<size_t>(keyTableOffset) + keyOffset;
        const size_t dataPos = static_cast<size_t>(dataTableOffset) + dataOffset;
        if (keyPos >= sfo.size() || dataPos >= sfo.size()) {
            return false;
        }
        if (dataLen > sfo.size() - dataPos || dataMaxLen > sfo.size() - dataPos) {
            return false;
        }

        const char* key = reinterpret_cast<const char*>(sfo.data() + keyPos);
        const size_t keyMax = sfo.size() - keyPos;
        size_t keyLen = 0;
        while (keyLen < keyMax && key[keyLen] != '\0') ++keyLen;
        if (keyLen == keyMax) return false;

        if (std::strcmp(key, wantedKey) != 0) continue;

        const uint8_t* data = sfo.data() + dataPos;
        size_t len = dataLen;
        while (len > 0 && data[len - 1] == '\0') --len;
        value.assign(reinterpret_cast<const char*>(data), len);
        return true;
    }

    return false;
}

void fpkgHmac(const uint8_t* data, size_t len, uint8_t out[16]) {
    SHA_CTX ctx;
    uint8_t sha1[20];
    uint8_t buf[64];

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, data, len);
    SHA1_Final(sha1, &ctx);

    std::memset(buf, 0, sizeof(buf));
    std::memcpy(&buf[0], &sha1[4], 8);
    std::memcpy(&buf[8], &sha1[4], 8);
    std::memcpy(&buf[16], &sha1[12], 4);
    buf[20] = sha1[16];
    buf[21] = sha1[1];
    buf[22] = sha1[2];
    buf[23] = sha1[3];
    std::memcpy(&buf[24], &buf[16], 8);

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, buf, sizeof(buf));
    SHA1_Final(sha1, &ctx);
    std::memcpy(out, sha1, 16);
}

bool isValidTitleId(const std::string& titleId) {
    if (titleId.size() != 9) return false;
    for (char c : titleId) {
        const bool upper = (c >= 'A' && c <= 'Z');
        const bool digit = (c >= '0' && c <= '9');
        if (!upper && !digit) return false;
    }
    return true;
}

} // namespace

void FakePackageBuilder::setError(const std::string& message) {
    lastError_ = message;
    sceClibPrintf("[FakePackageBuilder] %s\n", message.c_str());
}

bool FakePackageBuilder::build(const std::string& packageDir) {
    lastError_.clear();

    const std::string paramPath = packageDir + "/sce_sys/param.sfo";
    const std::string packagePath = packageDir + "/sce_sys/package";
    const std::string headPath = packagePath + "/head.bin";

    std::vector<uint8_t> sfo;
    std::string error;
    if (!readFile(paramPath, sfo, error)) {
        setError(error);
        return false;
    }

    std::string titleId;
    if (!readSfoString(sfo, "TITLE_ID", titleId) || !isValidTitleId(titleId)) {
        setError("invalid or missing TITLE_ID in param.sfo");
        return false;
    }

    std::string contentId;
    readSfoString(sfo, "CONTENT_ID", contentId);

    std::vector<uint8_t> head;
    if (!readFile(kTemplatePath, head, error)) {
        setError("cannot load VitaShell head.bin template: " + error);
        return false;
    }

    if (head.size() < 0x100) {
        setError("head.bin template is too small");
        return false;
    }

    char fullTitleId[48];
    std::memset(fullTitleId, 0, sizeof(fullTitleId));
    sceClibSnprintf(fullTitleId, sizeof(fullTitleId), "EP9000-%s_00-0000000000000000", titleId.c_str());
    const std::string effectiveContentId = contentId.empty() ? std::string(fullTitleId) : contentId;
    if (effectiveContentId.size() > 47) {
        setError("CONTENT_ID is too long");
        return false;
    }

    std::memset(&head[0x30], 0, 48);
    writeBytes(&head[0x30], effectiveContentId.c_str(), effectiveContentId.size());

    const uint32_t headerLen = readBE32(&head[0xD0]);
    if (headerLen < 0xD0 || static_cast<size_t>(headerLen) + 16 > head.size()) {
        setError("invalid head.bin header length");
        return false;
    }

    uint8_t hmac[16];
    fpkgHmac(head.data(), headerLen, hmac);
    std::memcpy(&head[headerLen], hmac, 16);

    const uint32_t infoOffset = readBE32(&head[0x8]);
    const uint32_t infoLen = readBE32(&head[0x10]);
    const uint32_t infoHmacOffset = readBE32(&head[0xD4]);
    if (static_cast<size_t>(infoOffset) + infoLen > head.size() ||
        infoLen < 64 ||
        static_cast<size_t>(infoHmacOffset) + 16 > head.size() ||
        static_cast<size_t>(infoOffset) + (infoLen - 64) > head.size()) {
        setError("invalid head.bin package-info offsets");
        return false;
    }

    fpkgHmac(&head[infoOffset], infoLen - 64, hmac);
    std::memcpy(&head[infoHmacOffset], hmac, 16);

    const uint32_t wholeLen = readBE32(&head[0xE8]);
    if (static_cast<size_t>(wholeLen) + 16 > head.size()) {
        setError("invalid head.bin final length");
        return false;
    }

    fpkgHmac(head.data(), wholeLen, hmac);
    std::memcpy(&head[wholeLen], hmac, 16);

    if (sceIoMkdir(packagePath.c_str(), 0777) < 0) {
        SceIoStat st;
        std::memset(&st, 0, sizeof(st));
        if (sceIoGetstat(packagePath.c_str(), &st) < 0 || (st.st_mode & SCE_S_IFDIR) == 0) {
            setError("cannot create sce_sys/package");
            return false;
        }
    }

    if (!writeFile(headPath, head, error)) {
        setError(error);
        return false;
    }

    sceClibPrintf("[FakePackageBuilder] head.bin generated for %s\n", titleId.c_str());
    return true;
}

} // namespace psvitaalive
