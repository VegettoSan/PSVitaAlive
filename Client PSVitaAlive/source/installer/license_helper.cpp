#include "installer/license_helper.hpp"
#include "storage/storage_manager.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <zlib.h>

#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {

// Preset dictionary used by NoPayStation / PKGj zRIF (1024 bytes).
// Source: PKGj src/zrif.cpp (same public format as NPS TSV).
static const uint8_t kZrifDict[1024] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    48,48,48,48,57,0,0,0,0,0,0,0,0,0,0,0,
    48,48,48,48,54,48,48,48,48,55,48,48,48,48,56,0,
    48,48,48,48,51,48,48,48,48,52,48,48,48,48,53,48,
    95,48,48,45,65,68,68,67,79,78,84,48,48,48,48,50,
    45,80,67,83,71,48,48,48,48,48,48,48,48,48,48,49,
    45,80,67,83,69,48,48,48,45,80,67,83,70,48,48,48,
    45,80,67,83,67,48,48,48,45,80,67,83,68,48,48,48,
    45,80,67,83,65,48,48,48,45,80,67,83,66,48,48,48,
    0,1,0,1,0,1,0,2,239,205,171,137,103,69,35,1,
};

static bool pathExists(const char* path) {
    SceIoStat st{};
    return sceIoGetstat(path, &st) >= 0;
}

static uint32_t base64Decode(const char* in, uint8_t* out, uint32_t outCap) {
    static const int8_t b64d[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    if (!in || !out) return 0;
    size_t len = std::strlen(in);
    while (len > 0 && in[len - 1] == '=') --len;
    uint32_t written = 0;
    size_t i = 0;
    while (i + 4 <= len) {
        const int a = b64d[static_cast<uint8_t>(in[i])];
        const int b = b64d[static_cast<uint8_t>(in[i + 1])];
        const int c = b64d[static_cast<uint8_t>(in[i + 2])];
        const int d = b64d[static_cast<uint8_t>(in[i + 3])];
        if (a < 0 || b < 0 || c < 0 || d < 0) return 0;
        if (written + 3 > outCap) return 0;
        out[written++] = static_cast<uint8_t>((a << 2) | (b >> 4));
        out[written++] = static_cast<uint8_t>((b << 4) | (c >> 2));
        out[written++] = static_cast<uint8_t>((c << 6) | d);
        i += 4;
    }
    const size_t left = len - i;
    if (left == 2) {
        const int a = b64d[static_cast<uint8_t>(in[i])];
        const int b = b64d[static_cast<uint8_t>(in[i + 1])];
        if (a < 0 || b < 0) return 0;
        if (written + 1 > outCap) return 0;
        out[written++] = static_cast<uint8_t>((a << 2) | (b >> 4));
    } else if (left == 3) {
        const int a = b64d[static_cast<uint8_t>(in[i])];
        const int b = b64d[static_cast<uint8_t>(in[i + 1])];
        const int c = b64d[static_cast<uint8_t>(in[i + 2])];
        if (a < 0 || b < 0 || c < 0) return 0;
        if (written + 2 > outCap) return 0;
        out[written++] = static_cast<uint8_t>((a << 2) | (b >> 4));
        out[written++] = static_cast<uint8_t>((b << 4) | (c >> 2));
    } else if (left != 0) {
        return 0;
    }
    return written;
}

} // namespace

bool LicenseHelper::looksLikeRifSize(uint64_t size) {
    return size == 512 || size == 1024 || size == 768;
}

bool LicenseHelper::writeRifBytes(const std::vector<uint8_t>& bytes, const std::string& destPath, std::string& errorOut) {
    errorOut.clear();
    if (bytes.empty()) {
        errorOut = "empty rif bytes";
        return false;
    }
    StorageManager st;
    std::string parent = destPath;
    const auto slash = parent.find_last_of('/');
    if (slash != std::string::npos) {
        parent.resize(slash);
        st.createDirectories(parent);
    }
    SceUID out = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (out < 0) {
        errorOut = "cannot open rif dest";
        return false;
    }
    const int wr = sceIoWrite(out, bytes.data(), bytes.size());
    sceIoClose(out);
    if (wr != static_cast<int>(bytes.size())) {
        errorOut = "rif write short";
        return false;
    }
    return true;
}

bool LicenseHelper::copyRifFile(const std::string& rifPath, const std::string& destPath, std::string& errorOut) {
    errorOut.clear();
    if (rifPath.empty()) {
        errorOut = "empty rif path";
        return false;
    }
    SceIoStat st{};
    if (sceIoGetstat(rifPath.c_str(), &st) < 0) {
        errorOut = "rif source missing";
        return false;
    }
    const int64_t sz = static_cast<int64_t>(st.st_size);
    if (!looksLikeRifSize(static_cast<uint64_t>(sz)) && sz > 0 && sz < 4096) {
        // still allow other small license blobs
        sceClibPrintf("[LicenseHelper] unusual rif size=%lld\n", (long long)sz);
    }
    SceUID in = sceIoOpen(rifPath.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) {
        errorOut = "cannot open rif source";
        return false;
    }
    StorageManager stm;
    std::string parent = destPath;
    const auto slash = parent.find_last_of('/');
    if (slash != std::string::npos) {
        parent.resize(slash);
        stm.createDirectories(parent);
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

bool LicenseHelper::decodeZrif(const std::string& zrif, std::vector<uint8_t>& outBytes, std::string& errorOut) {
    errorOut.clear();
    outBytes.clear();
    if (zrif.empty()) {
        errorOut = "empty zrif";
        return false;
    }

    uint8_t raw[2048];
    const uint32_t rawLen = base64Decode(zrif.c_str(), raw, sizeof(raw));
    if (rawLen < 6) {
        errorOut = "zRIF base64 decode failed or too short";
        return false;
    }

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit(&strm) != Z_OK) {
        errorOut = "inflateInit failed";
        return false;
    }

    std::vector<uint8_t> inflated(2048);
    strm.next_in = raw;
    strm.avail_in = rawLen;
    strm.next_out = inflated.data();
    strm.avail_out = static_cast<uInt>(inflated.size());

    int ret = inflate(&strm, Z_NO_FLUSH);
    if (ret == Z_NEED_DICT) {
        if (inflateSetDictionary(&strm, kZrifDict, sizeof(kZrifDict)) != Z_OK) {
            inflateEnd(&strm);
            errorOut = "inflateSetDictionary failed";
            return false;
        }
        ret = inflate(&strm, Z_FINISH);
    } else if (ret == Z_OK) {
        ret = inflate(&strm, Z_FINISH);
    }

    if (ret != Z_STREAM_END && ret != Z_OK) {
        char buf[96];
        sceClibSnprintf(buf, sizeof(buf), "inflate failed ret=%d msg=%s", ret, strm.msg ? strm.msg : "");
        errorOut = buf;
        inflateEnd(&strm);
        return false;
    }

    const size_t produced = static_cast<size_t>(strm.total_out);
    inflateEnd(&strm);

    if (produced != kRifSize && produced != kRifSizePsm) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "wrong zRIF size %u (want 512 or 1024)", (unsigned)produced);
        errorOut = buf;
        return false;
    }

    outBytes.assign(inflated.begin(), inflated.begin() + static_cast<std::ptrdiff_t>(produced));
    diagnostics::log(std::string("[LicenseHelper] zRIF decoded bytes=") + std::to_string(produced));
    return true;
}

bool LicenseHelper::createPspRif(const std::string& contentId, std::vector<uint8_t>& outBytes, std::string& errorOut) {
    errorOut.clear();
    outBytes.clear();
    if (contentId.empty()) {
        errorOut = "empty content id for PSP RIF";
        return false;
    }
    // Mirror PKGj pkgi_create_psp_rif / SceNpDrmLicense (0x200 bytes).
    std::vector<uint8_t> rif(0x200, 0);
    // account_id at offset 0x08 (after version fields) — little-endian uint64
    // Layout from psp2common/npdrm.h SceNpDrmLicense:
    // 0x00 int16 version, 0x02 version_flags, 0x04 license_type, 0x06 license_flags
    // 0x08 uint64 account_id
    // 0x10 content_id[0x30]
    // ...
    // 0x80 ecdsa_signature[0x28]  (approx — use PKGj offsets carefully)
    //
    // PKGj does:
    //   memset 0
    //   license.account_id = 0x0123456789ABCDEFLL
    //   memset(license.ecdsa_signature, 0xFF, 0x28)
    //   snprintf(license.content_id, ...)
    //   memcpy(rif, &license, PKGI_PSP_RIF_SIZE)
    //
    // We write the same fields into the packed struct layout.
    const uint64_t accountId = 0x0123456789ABCDEFULL;
    std::memcpy(rif.data() + 0x08, &accountId, sizeof(accountId));
    // content_id at 0x10
    const size_t n = contentId.size() < 0x2F ? contentId.size() : 0x2F;
    std::memcpy(rif.data() + 0x10, contentId.data(), n);
    // ecdsa_signature: in SceNpDrmLicense after start_time(8)+expiration(8)+key fields...
    // From struct:
    // 0x10 content_id[0x30] -> 0x40
    // 0x40 key_table[0x10] -> 0x50
    // 0x50 key1[0x10] -> 0x60
    // 0x60 start_time (8) -> 0x68
    // 0x68 expiration (8) -> 0x70
    // 0x70 ecdsa_signature[0x28]
    std::memset(rif.data() + 0x70, 0xFF, 0x28);
    outBytes.swap(rif);
    diagnostics::log(std::string("[LicenseHelper] synthetic PSP RIF content_id=") + contentId);
    return true;
}

bool LicenseHelper::prepareBgdlLicense(
    const std::string& zrifOrEmpty,
    const std::string& existingRifPathOrEmpty,
    std::string& outPath,
    std::string& errorOut
) {
    errorOut.clear();
    outPath.clear();

    StorageManager st;
    st.createDirectories("ux0:bgdl");

    if (!zrifOrEmpty.empty()) {
        std::vector<uint8_t> bytes;
        if (!decodeZrif(zrifOrEmpty, bytes, errorOut)) {
            return false;
        }
        // PKGj writes 512 for Vita game licenses even if buffer is larger.
        if (bytes.size() > kRifSize) {
            // Keep full PSM size when 1024; otherwise trim to 512 for Vita.
            // Heuristic: 1024 stays; else use 512.
        }
        size_t writeSize = bytes.size();
        if (writeSize != kRifSize && writeSize != kRifSizePsm) {
            writeSize = kRifSize;
        }
        std::vector<uint8_t> slice(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(writeSize));
        if (!writeRifBytes(slice, kBgdlTempRif, errorOut)) {
            return false;
        }
        outPath = kBgdlTempRif;
        diagnostics::log(std::string("[LicenseHelper] wrote BGDL license from zRIF -> ") + outPath);
        return true;
    }

    if (!existingRifPathOrEmpty.empty() && pathExists(existingRifPathOrEmpty.c_str())) {
        if (!copyRifFile(existingRifPathOrEmpty, kBgdlTempRif, errorOut)) {
            return false;
        }
        outPath = kBgdlTempRif;
        diagnostics::log(std::string("[LicenseHelper] wrote BGDL license from file -> ") + outPath);
        return true;
    }

    errorOut = "no zRIF or rif file provided";
    return false;
}

} // namespace psvitaalive
