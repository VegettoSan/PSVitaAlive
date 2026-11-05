#pragma once

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <cstdint>
#include <string>

namespace psvitaalive {
namespace update {

// Tiny local, per-Title-ID receipt independent from the VPK release receipt.
// Read only on explicit update interaction; never scan during catalog rendering.
class DataRevisionTracker {
    static std::string normalizedTitleId(std::string titleId) {
        for (char& c : titleId) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return titleId;
    }

    static bool validTitleId(const std::string& titleId) {
        if (titleId.size() != 9) return false;
        for (unsigned char c : titleId) {
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
        }
        return true;
    }

    static std::string receiptPath(const std::string& titleId) {
        return "ux0:data/psvitaalive/data_revisions/" + titleId + ".txt";
    }

public:
    static bool read(const std::string& titleId, uint32_t& revision) {
        revision = 0;
        const std::string normalized = normalizedTitleId(titleId);
        if (!validTitleId(normalized)) return false;
        const std::string path = receiptPath(normalized);
        SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
        if (fd < 0) return false;
        char buf[16] = {};
        const int n = sceIoRead(fd, buf, sizeof(buf) - 1);
        sceIoClose(fd);
        if (n <= 0 || n >= static_cast<int>(sizeof(buf))) return false;
        uint32_t value = 0;
        for (int i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(buf[i]);
            if (c == '\n' && i == n - 1) break;
            if (c < '0' || c > '9') return false;
            value = value * 10u + (c - '0');
            if (value > 65535u) return false;
        }
        if (value == 0) return false;
        revision = value;
        return true;
    }

    static bool write(const std::string& titleId, uint32_t revision) {
        const std::string normalized = normalizedTitleId(titleId);
        if (!validTitleId(normalized) || revision == 0 || revision > 65535u) return false;
        sceIoMkdir("ux0:data", 0777);
        sceIoMkdir("ux0:data/psvitaalive", 0777);
        sceIoMkdir("ux0:data/psvitaalive/data_revisions", 0777);
        const std::string path = receiptPath(normalized);
        const std::string tmp = path + ".tmp";
        const std::string data = std::to_string(revision) + "\n";
        SceUID fd = sceIoOpen(tmp.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (fd < 0) return false;
        const int wrote = sceIoWrite(fd, data.data(), static_cast<SceSize>(data.size()));
        sceIoClose(fd);
        if (wrote < 0 || static_cast<size_t>(wrote) != data.size()) {
            sceIoRemove(tmp.c_str());
            return false;
        }
        // Same rename fallback strategy used by the existing VPK receipt.
        sceIoRemove(path.c_str());
        if (sceIoRename(tmp.c_str(), path.c_str()) < 0) {
            sceIoRemove(tmp.c_str());
            return false;
        }
        return true;
    }
};

} // namespace update
} // namespace psvitaalive
