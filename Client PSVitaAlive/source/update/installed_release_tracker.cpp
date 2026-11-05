#include "update/installed_release_tracker.hpp"
#include "update/data_revision_tracker.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <string>

namespace psvitaalive {
namespace update {
namespace {

std::string upperTitleId(std::string tid) {
    for (char& c : tid) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return tid;
}

void jsonEscapeAppend(std::string& out, const std::string& in) {
    for (unsigned char c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                sceClibSnprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
}

bool extractStringField(const std::string& json, const char* key, std::string& out) {
    out.clear();
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r'))
        ++p;
    if (p >= json.size() || json[p] != '"') return false;
    ++p;
    std::string val;
    while (p < json.size()) {
        char c = json[p++];
        if (c == '\\' && p < json.size()) {
            char n = json[p++];
            if (n == 'n') val.push_back('\n');
            else if (n == 'r') val.push_back('\r');
            else if (n == 't') val.push_back('\t');
            else val.push_back(n);
            continue;
        }
        if (c == '"') break;
        val.push_back(c);
    }
    out = val;
    return true;
}

bool extractIntField(const std::string& json, const char* key, int& out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size()) return false;
    int sign = 1;
    if (json[p] == '-') { sign = -1; ++p; }
    if (p >= json.size() || json[p] < '0' || json[p] > '9') return false;
    int v = 0;
    while (p < json.size() && json[p] >= '0' && json[p] <= '9') {
        v = v * 10 + (json[p] - '0');
        ++p;
    }
    out = v * sign;
    return true;
}

} // namespace

std::string InstalledReleaseTracker::dataRoot() {
    return "ux0:data/psvitaalive/installed";
}

std::string InstalledReleaseTracker::receiptPath(const std::string& titleId) {
    return dataRoot() + "/" + upperTitleId(titleId) + ".json";
}

bool InstalledReleaseTracker::ensureDir() {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    const int r = sceIoMkdir(dataRoot().c_str(), 0777);
    return r >= 0 || r == static_cast<int>(0x80010011);
}

bool InstalledReleaseTracker::writeReceipt(const InstallReceipt& r) {
    if (r.titleId.empty()) return false;
    if (!ensureDir()) return false;

    const std::string path = receiptPath(r.titleId);
    const std::string tmp = path + ".tmp";

    std::string body;
    body.reserve(512);
    body += "{\n";
    body += "  \"schema_version\": ";
    char num[32];
    sceClibSnprintf(num, sizeof(num), "%d", r.schemaVersion > 1 ? r.schemaVersion : 2);
    body += num;
    body += ",\n  \"app_id\": \"";
    jsonEscapeAppend(body, r.appId);
    body += "\",\n  \"title_id\": \"";
    jsonEscapeAppend(body, upperTitleId(r.titleId));
    body += "\",\n  \"catalog_version\": \"";
    jsonEscapeAppend(body, r.catalogVersion);
    body += "\",\n  \"version_date\": \"";
    jsonEscapeAppend(body, r.versionDate);
    body += "\",\n  \"release_revision\": ";
    sceClibSnprintf(num, sizeof(num), "%d", r.releaseRevision);
    body += num;
    body += ",\n  \"dr\": ";
    sceClibSnprintf(num, sizeof(num), "%u", static_cast<unsigned>(r.dataRevision));
    body += num;
    body += ",\n  \"verification\": {\n";
    body += "    \"path\": \"";
    jsonEscapeAppend(body, r.verifyPath.empty() ? "eboot.bin" : r.verifyPath);
    body += "\",\n    \"algorithm\": \"";
    jsonEscapeAppend(body, r.verifyAlgorithm.empty() ? "sha256" : r.verifyAlgorithm);
    body += "\",\n    \"digest\": \"";
    jsonEscapeAppend(body, r.verifyDigest);
    body += "\"\n  }\n}\n";

    SceUID fd = sceIoOpen(tmp.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    const int w = sceIoWrite(fd, body.data(), static_cast<SceSize>(body.size()));
    sceIoClose(fd);
    if (w < 0 || static_cast<size_t>(w) != body.size()) {
        sceIoRemove(tmp.c_str());
        return false;
    }
    sceIoRemove(path.c_str());
    if (sceIoRename(tmp.c_str(), path.c_str()) < 0) {
        sceIoRemove(tmp.c_str());
        return false;
    }
    // Migration is complete only after the unified JSON has been committed.
    DataRevisionTracker::removeLegacyReceipt(r.titleId);
    return true;
}

bool InstalledReleaseTracker::readReceipt(const std::string& titleId, InstallReceipt& out) {
    out = InstallReceipt{};
    const std::string path = receiptPath(titleId);
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceIoStat st{};
    if (sceIoGetstat(path.c_str(), &st) < 0 || st.st_size <= 0 || st.st_size > 64 * 1024) {
        sceIoClose(fd);
        return false;
    }
    std::string data(static_cast<size_t>(st.st_size), '\0');
    size_t done = 0;
    while (done < data.size()) {
        const int n = sceIoRead(fd, &data[done], data.size() - done);
        if (n <= 0) { sceIoClose(fd); return false; }
        done += static_cast<size_t>(n);
    }
    sceIoClose(fd);

    extractIntField(data, "schema_version", out.schemaVersion);
    extractStringField(data, "app_id", out.appId);
    extractStringField(data, "title_id", out.titleId);
    extractStringField(data, "catalog_version", out.catalogVersion);
    extractStringField(data, "version_date", out.versionDate);
    extractIntField(data, "release_revision", out.releaseRevision);
    int parsedDataRevision = 0;
    if (extractIntField(data, "dr", parsedDataRevision) &&
        parsedDataRevision >= 0 && parsedDataRevision <= 65535)
        out.dataRevision = static_cast<uint32_t>(parsedDataRevision);
    extractStringField(data, "path", out.verifyPath);
    extractStringField(data, "algorithm", out.verifyAlgorithm);
    extractStringField(data, "digest", out.verifyDigest);
    if (out.titleId.empty()) out.titleId = upperTitleId(titleId);
    return !out.catalogVersion.empty() || !out.verifyDigest.empty() || out.dataRevision > 0;
}

uint32_t InstalledReleaseTracker::readDataRevision(const std::string& titleId, const InstallReceipt& receipt) {
    if (receipt.schemaVersion >= 2) return receipt.dataRevision;
    // One-time backward compatibility for the separate .txt receipts emitted
    // by the first dr implementation. Absence is revision 0, not a fatal error.
    uint32_t legacy = 0;
    DataRevisionTracker::read(titleId, legacy);
    return legacy;
}

bool InstalledReleaseTracker::writeDataRevision(const std::string& titleId,
                                                const std::string& appId, uint32_t revision) {
    if (revision == 0 || revision > 65535) return false;
    InstallReceipt r;
    readReceipt(titleId, r); // keep catalog_version, release_revision and verification
    r.titleId = titleId;
    if (r.appId.empty()) r.appId = appId;
    r.schemaVersion = 2;
    r.dataRevision = revision;
    return writeReceipt(r);
}

void InstalledReleaseTracker::removeReceipt(const std::string& titleId) {
    if (titleId.empty()) return;
    sceIoRemove(receiptPath(titleId).c_str());
    DataRevisionTracker::removeLegacyReceipt(titleId);
}

} // namespace update
} // namespace psvitaalive
